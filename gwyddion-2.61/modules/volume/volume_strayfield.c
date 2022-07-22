/*
 *  $Id: volume_strayfield.c 22157 2019-06-10 13:03:11Z klapetek $
 *  Copyright (C) 2015-2018 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/brick.h>
#include <libprocess/arithmetic.h>
#include <libprocess/linestats.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/mfm.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define STRAYFIELD_RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
};

enum {
    RESPONSE_RESET   = 1,
    RESPONSE_PREVIEW = 2,
};

typedef enum {
    GWY_STRAYFIELD_SINGLE   = 0,
    GWY_STRAYFIELD_PLANEDIFF = 1,
    GWY_STRAYFIELD_ZSHIFT = 2,
    NQUANTITIES
} StrayfieldQuantity;

typedef gdouble (*StrayfieldFunc)(GwyDataLine *dataline);

typedef struct {
    StrayfieldQuantity quantity;
    gint x;
    gint y;
    gint zfrom;
    gint zto;
    gboolean update;
    gboolean computed;
    GwyBrick *strayfield;
    GwyBrick *brick;
    GwyDataLine *calibration;
} StrayfieldArgs;

typedef struct {
    StrayfieldArgs *args;
    GwyContainer *mydata;
    GwyDataField *image;
    GtkWidget *dialog;
    GtkWidget *view;
    GwyPixmapLayer *player;
    GwyVectorLayer *vlayer;
    GtkWidget *graph;
    GtkWidget *quantity;
    GtkWidget *update;
    GtkWidget *zfrom;
    GtkWidget *zto;
    GwySIValueFormat *zvf;
} StrayfieldControls;


static gboolean module_register        (void);
static void     strayfield              (GwyContainer *data,
                                        GwyRunType run);
static gboolean strayfield_dialog       (StrayfieldArgs *args,
                                        GwyContainer *data,
                                        gint id);
static void     strayfield_reset        (StrayfieldControls *controls);
static void     point_selection_changed(StrayfieldControls *controls,
                                        gint id,
                                        GwySelection *selection);
static void     graph_selection_changed(StrayfieldControls *controls,
                                        gint id,
                                        GwySelection *selection);
static void     graph_selection_finished(StrayfieldControls *controls,
                                        GwySelection *selection);
static void     quantity_changed       (GtkComboBox *combo,
                                        StrayfieldControls *controls);
static void     range_changed          (GtkWidget *entry,
                                        StrayfieldControls *controls);
static void     update_changed         (StrayfieldControls *controls,
                                        GtkToggleButton *check);
static void     extract_image          (const StrayfieldArgs *args, 
                                        GwyDataField *dfield);

static void     extract_results        (StrayfieldControls *controls,
                                        GwyGraphModel *gmodel);
static void     extract_graph_curve    (const StrayfieldArgs *args,
                                        GwyGraphCurveModel *gcmodel);
static void     extract_gmodel         (const StrayfieldArgs *args,
                                        GwyGraphModel *gmodel);
static void     strayfield_sanitize_args(StrayfieldArgs *args);
static void     strayfield_load_args    (GwyContainer *container,
                                        StrayfieldArgs *args);
static void     strayfield_save_args    (GwyContainer *container,
                                        StrayfieldArgs *args);

static void     graph_selection_update  (StrayfieldControls *controls);

static const GwyEnum quantities[] =  {
    { N_("Single value evolution"),     GWY_STRAYFIELD_SINGLE,     },
    { N_("Plane variance"),             GWY_STRAYFIELD_PLANEDIFF,  },
 //   { N_("Z shift difference"),         GWY_STRAYFIELD_ZSHIFT,     },
};

static const StrayfieldArgs strayfield_defaults = {
    GWY_STRAYFIELD_SINGLE,
    -1, -1, -1, -1,
    TRUE, FALSE,
    NULL, NULL, NULL,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Checks the stray field dependence consistency."),
    "Petr Klapetek <pklapetek@gwyddion.net>",
    "1.1",
    "Petr Klapetek, Robb Puttock & David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, volume_strayfield)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_strayfield",
                             (GwyVolumeFunc)&strayfield,
                             N_("/_Stray Field Consistency..."),
                             NULL,
                             STRAYFIELD_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Summarize profiles"));

    return TRUE;
}

static void
strayfield(GwyContainer *data, GwyRunType run)
{
    StrayfieldArgs args;
    GwyBrick *brick = NULL;
//    GwySIUnit *zunit, *wunit;
    gint id;

    g_return_if_fail(run & STRAYFIELD_RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    strayfield_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    args.brick = brick;
    args.strayfield = NULL;
    args.computed = FALSE;

    args.calibration = gwy_brick_get_zcalibration(brick);
    if (args.calibration
        && (gwy_brick_get_zres(brick)
            != gwy_data_line_get_res(args.calibration)))
        args.calibration = NULL;

    /*
    wunit = gwy_brick_get_si_unit_w(brick);
    if (args.calibration)
        zunit = gwy_data_line_get_si_unit_y(args.calibration);
    else
        zunit = gwy_brick_get_si_unit_z(brick);
*/

    if (CLAMP(args.x, 0, brick->xres-1) != args.x)
        args.x = brick->xres/2;
    if (CLAMP(args.y, 0, brick->yres-1) != args.y)
        args.y = brick->yres/2;
    if (CLAMP(args.zfrom, 0, brick->zres-1) != args.zfrom)
        args.zfrom = 0;
    if (CLAMP(args.zto, 0, brick->zres-1) != args.zto)
        args.zto = brick->zres;

    strayfield_dialog(&args, data, id);
    strayfield_save_args(gwy_app_settings_get(), &args);
}

static gboolean
strayfield_dialog(StrayfieldArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *dialog, *table, *hbox, *label, *area, *hbox2;
    StrayfieldControls controls;
    GwyDataField *dfield;
    GwyGraphCurveModel *gcmodel;
    GwyGraphModel *gmodel;
    GwySIUnit *siunitz;
    gint response, row;
    GwyPixmapLayer *layer;
    GwyVectorLayer *vlayer = NULL;
    GwySelection *selection;
    GwyBrick *brick;
    const guchar *gradient;
    GQuark quark;
    gdouble xy[2];
    gdouble zmax;
    guint nquantities;

    controls.args = args;

    brick = args->brick;
    if (args->calibration) {
        siunitz = gwy_data_line_get_si_unit_y(args->calibration);
        zmax = gwy_data_line_get_max(args->calibration);
    }
    else {
        siunitz = gwy_brick_get_si_unit_z(brick);
        zmax = gwy_brick_get_zreal(brick);
    }
    controls.zvf = gwy_si_unit_get_format_with_digits(siunitz,
                                                      GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                      zmax,
                                                      5, /* 5 digits */
                                                      NULL);

    dialog = gtk_dialog_new_with_buttons(_("Stray field consistency check"),
                                         NULL, 0, NULL);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                                 gwy_stock_like_button_new(_("_Update"),
                                                           GTK_STOCK_EXECUTE),
                                 RESPONSE_PREVIEW);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),
                                      RESPONSE_PREVIEW, !args->update);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Reset"), RESPONSE_RESET);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_volume_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    controls.mydata = gwy_container_new();
    controls.image = dfield = gwy_data_field_new(1, 1, 1.0, 1.0, TRUE);
    extract_image(args, dfield);
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    g_object_unref(dfield);

    quark = gwy_app_get_brick_palette_key_for_id(id);
    if (gwy_container_gis_string(data, quark, &gradient)) {
        gwy_container_set_const_string_by_name(controls.mydata,
                                               "/0/base/palette", gradient);
    }

    controls.view = gwy_data_view_new(controls.mydata);
    controls.player = layer = gwy_layer_basic_new();
    g_object_set(layer,
                 "data-key", "/0/data",
                 "gradient-key", "/0/base/palette",
                 NULL);
    gwy_data_view_set_data_prefix(GWY_DATA_VIEW(controls.view), "/0/data");
    gwy_data_view_set_base_layer(GWY_DATA_VIEW(controls.view), layer);
    gwy_set_data_preview_size(GWY_DATA_VIEW(controls.view), PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.view, FALSE, FALSE, 0);

    controls.vlayer = vlayer = g_object_new(g_type_from_name("GwyLayerPoint"),
                                            NULL);
    gwy_vector_layer_set_selection_key(vlayer, "/0/select/pointer");
    gwy_data_view_set_top_layer(GWY_DATA_VIEW(controls.view), vlayer);
    selection = gwy_vector_layer_ensure_selection(vlayer);
    gwy_selection_set_max_objects(selection, 1);
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(point_selection_changed), &controls);

    gmodel = gwy_graph_model_new();
    g_object_set(gmodel, "label-visible", FALSE, NULL);
    extract_gmodel(args, gmodel);
    gcmodel = gwy_graph_curve_model_new();
    gwy_graph_model_add_curve(gmodel, gcmodel); //use always up to 2 curves
    gcmodel = gwy_graph_curve_model_new();
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);

    controls.graph = gwy_graph_new(gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(controls.graph), FALSE);
    g_object_unref(gmodel);
    gtk_widget_set_size_request(controls.graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.graph, TRUE, TRUE, 0);

    area = gwy_graph_get_area(GWY_GRAPH(controls.graph));
    gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area),
                                             GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(selection, 1);
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(graph_selection_changed), &controls);
    g_signal_connect_swapped(selection, "finished",
                             G_CALLBACK(graph_selection_finished), &controls);


    hbox = gtk_hbox_new(FALSE, 24);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 4);

    table = gtk_table_new(2, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 0);
    row = 0;

    label = gtk_label_new_with_mnemonic(_("_Quantity:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    nquantities = G_N_ELEMENTS(quantities);
    controls.quantity
        = gwy_enum_combo_box_new(quantities, nquantities,
                                 G_CALLBACK(quantity_changed), &controls,
                                 args->quantity, TRUE);
    gtk_table_attach(GTK_TABLE(table), controls.quantity,
                     1, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.quantity);
    row++;

    hbox2 = gtk_hbox_new(FALSE, 6);
    gtk_table_attach(GTK_TABLE(table), hbox2,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new(_("Range:"));
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, FALSE, 0);

    controls.zfrom = gtk_entry_new();
    g_object_set_data(G_OBJECT(controls.zfrom), "id", (gpointer)"from");
    gtk_entry_set_width_chars(GTK_ENTRY(controls.zfrom), 8);
    gtk_box_pack_start(GTK_BOX(hbox2), controls.zfrom, FALSE, FALSE, 0);
    g_signal_connect(controls.zfrom, "activate",
                     G_CALLBACK(range_changed), &controls);
    gwy_widget_set_activate_on_unfocus(controls.zfrom, TRUE);

    label = gtk_label_new(gwy_sgettext("range|to"));
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, FALSE, 0);

    controls.zto = gtk_entry_new();
    g_object_set_data(G_OBJECT(controls.zto), "id", (gpointer)"to");
    gtk_entry_set_width_chars(GTK_ENTRY(controls.zto), 8);
    gtk_box_pack_start(GTK_BOX(hbox2), controls.zto, FALSE, FALSE, 0);
    g_signal_connect(controls.zto, "activate",
                     G_CALLBACK(range_changed), &controls);
    gwy_widget_set_activate_on_unfocus(controls.zto, TRUE);

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), controls.zvf->units);
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, FALSE, 0);

    table = gtk_table_new(4, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 0);
    row = 0;

    controls.update = gtk_check_button_new_with_mnemonic(_("I_nstant updates"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.update),
                                 args->update);
    gtk_table_attach(GTK_TABLE(table), controls.update,
                     0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.update, "toggled",
                             G_CALLBACK(update_changed), &controls);
    row++;

    selection = gwy_vector_layer_ensure_selection(vlayer);
    xy[0] = gwy_brick_itor(brick, args->x);
    xy[1] = gwy_brick_jtor(brick, args->y);
    gwy_selection_set_object(selection, 0, xy);

    selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area),
                                             GWY_GRAPH_STATUS_XSEL);
    if (args->zfrom > 0 || args->zto < brick->zres-1) {
        xy[0] = gwy_brick_ktor_cal(brick, args->zfrom);
        xy[1] = gwy_brick_ktor_cal(brick, args->zto);
        gwy_selection_set_object(selection, 0, xy);
    }
    else
        gwy_selection_clear(selection);

    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            g_object_unref(controls.mydata);
            gwy_si_unit_value_format_free(controls.zvf);
            return FALSE;
            break;

            case GTK_RESPONSE_OK:
            break;

            case RESPONSE_RESET:
            strayfield_reset(&controls);
            break;

            case RESPONSE_PREVIEW:
            graph_selection_update(&controls);
            extract_results(&controls,
                            gwy_graph_get_model(GWY_GRAPH(controls.graph)));
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);
    g_object_unref(controls.mydata);
    gwy_si_unit_value_format_free(controls.zvf);

    return TRUE;
}

static void
graph_selection_update(StrayfieldControls *controls)
{
    GtkWidget *area = gwy_graph_get_area(GWY_GRAPH(controls->graph));
    StrayfieldArgs *args = controls->args;

    if (args->quantity == GWY_STRAYFIELD_SINGLE)
    {
       gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL); //here also selection should be restored
    }
    else
        gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_PLAIN);

}

static void
point_selection_changed(StrayfieldControls *controls,
                        G_GNUC_UNUSED gint id,
                        GwySelection *selection)
{
    StrayfieldArgs *args = controls->args;
    GwyBrick *brick = args->brick;
    gdouble xy[2];

    if (!gwy_selection_get_object(selection, 0, xy))
        return;

    args->x = CLAMP(gwy_brick_rtoi(brick, xy[0]), 0, brick->xres-1);
    args->y = CLAMP(gwy_brick_rtoj(brick, xy[1]), 0, brick->yres-1);

    if (args->update) {
        extract_results(controls, gwy_graph_get_model(GWY_GRAPH(controls->graph)));
    }
}

static void
graph_selection_changed(StrayfieldControls *controls,
                        G_GNUC_UNUSED gint id,
                        GwySelection *selection)
{
    StrayfieldArgs *args = controls->args;
    GwyBrick *brick = args->brick;
    GwySIValueFormat *zvf = controls->zvf;
    gchar buf[32];
    gdouble z[2];

    if (!gwy_selection_get_object(selection, 0, z)) {
        args->zfrom = args->zto = -1;
    }
    else {
        args->zfrom = CLAMP(gwy_brick_rtok_cal(brick, z[0])+0.49, 0, brick->zres);
        args->zto = CLAMP(gwy_brick_rtok_cal(brick, z[1])+0.5, 0, brick->zres);
        if (args->zto < args->zfrom)
            GWY_SWAP(gint, args->zfrom, args->zto);
        if (args->zto - args->zfrom < 2)
            args->zfrom = args->zto = -1;
    }

    g_snprintf(buf, sizeof(buf), "%.*f",
               zvf->precision,
               gwy_brick_ktor_cal(brick, z[0])/zvf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->zfrom), buf);

    g_snprintf(buf, sizeof(buf), "%.*f",
               zvf->precision,
               gwy_brick_ktor_cal(brick, z[1])/zvf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->zto), buf);

    extract_image(controls->args, controls->image);

    args->computed = FALSE;
}

static void
graph_selection_finished(StrayfieldControls *controls,
                        G_GNUC_UNUSED GwySelection *selection)
{
    if (controls->args->update) { 
        extract_results(controls, gwy_graph_get_model(GWY_GRAPH(controls->graph)));
    }
}

static void
quantity_changed(GtkComboBox *combo, StrayfieldControls *controls)
{
    StrayfieldArgs *args = controls->args;

    args->quantity = gwy_enum_combo_box_get_active(combo);

    if (args->update) {
        graph_selection_update(controls);
        extract_results(controls, gwy_graph_get_model(GWY_GRAPH(controls->graph)));
    }
}

static void
range_changed(GtkWidget *entry,
              StrayfieldControls *controls)
{
    StrayfieldArgs *args = controls->args;
    GwySelection *selection;
    GtkWidget *area;
    const gchar *id = g_object_get_data(G_OBJECT(entry), "id");
    gdouble z = g_strtod(gtk_entry_get_text(GTK_ENTRY(entry)), NULL);
    gdouble xy[2];

    z *= controls->zvf->magnitude;
    area = gwy_graph_get_area(GWY_GRAPH(controls->graph));
    selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area),
                                             GWY_GRAPH_STATUS_XSEL);
    if (!gwy_selection_get_object(selection, 0, xy)) {
        xy[0] = gwy_brick_rtok_cal(args->brick, 0.0);
        xy[1] = gwy_brick_rtok_cal(args->brick, args->brick->zres-1);
    }
    if (gwy_strequal(id, "from"))
        xy[0] = z;
    else
        xy[1] = z;

    gwy_selection_set_object(selection, 0, xy);
}

static void
update_changed(StrayfieldControls *controls, GtkToggleButton *check)
{
    StrayfieldArgs *args = controls->args;

    args->update = gtk_toggle_button_get_active(check);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      RESPONSE_PREVIEW, !args->update);
    if (args->update) {
        graph_selection_update(controls);
        extract_results(controls, gwy_graph_get_model(GWY_GRAPH(controls->graph)));
    }
}

/*show the zfrom image only*/

static void
extract_image(const StrayfieldArgs *args, GwyDataField *dfield)
{
    GwyBrick *brick = args->brick;
    gint zfrom = args->zfrom;

    if (zfrom == -1)
        zfrom = 0;
    gwy_brick_extract_xy_plane(brick, dfield, zfrom);
    gwy_data_field_data_changed(dfield);
}

static gdouble
get_brick_mutual_rms(GwyBrick *brick, gint from, gint to)
{
    gint xres = gwy_brick_get_xres(brick);
    gint yres = gwy_brick_get_yres(brick);
    gint row, col;
    gdouble d, sum = 0;
    const gdouble *p;

    p = gwy_brick_get_data(brick);

    for (row = 0; row < yres; row++) {
        for (col = 0; col < xres; col++) {
            d = p[col + xres*(row + yres*from)] - p[col + xres*(row + yres*to)];
            sum += d*d;
        }
    }

    return sqrt(sum)/(xres*yres);
}

static gboolean
compute_strayfield_brick(GwyBrick *brick, GwyBrick *result,
                         gint zfrom, gint zto, GtkWidget *dialog)
{
    GwyDataField *shiftedfield;
    GwyDataField *basefield;
    gint level;
    gint xres = gwy_brick_get_xres(brick);
    gint yres = gwy_brick_get_yres(brick);
    gdouble dz = gwy_brick_get_dz(brick);

    if (zfrom >= zto)
        return FALSE;

    if (dialog) {
        gwy_app_wait_start(GTK_WINDOW(dialog),
                           _("Building stray field dependence..."));
    }

    basefield = gwy_data_field_new(xres, yres,
                                   gwy_brick_get_xreal(brick),
                                   gwy_brick_get_yreal(brick),
                                   FALSE);
    gwy_brick_extract_xy_plane(brick, basefield, zfrom);
    shiftedfield = gwy_data_field_new_alike(basefield, FALSE);
    gwy_brick_clear(result);

    for (level = zfrom; level<zto; level++) {
         gwy_data_field_mfm_shift_z(basefield, shiftedfield,
                                    -(level - zfrom)*dz);
         gwy_brick_set_xy_plane(result, shiftedfield, level);
         if (dialog
             && !gwy_app_wait_set_fraction(((gdouble)(level-zfrom))/(zto-zfrom))) {
            g_object_unref(basefield);
            g_object_unref(shiftedfield);
            gwy_app_wait_finish();
            return FALSE;
         }
    }
    if (dialog)
        gwy_app_wait_finish();

    g_object_unref(basefield);
    g_object_unref(shiftedfield);

    return TRUE;
}

static void
extract_results(StrayfieldControls *controls, GwyGraphModel *gmodel)
{
    StrayfieldArgs *args = controls->args;
    GwyGraphCurveModel *gcmodel, *gcmodel2;
    GwyBrick *brick = args->brick;
    gdouble *xdata;
    gdouble *ydata;
    gint level, ndata;

    gdouble dz;
 
    gcmodel = gwy_graph_model_get_curve(gmodel, 0);
    gcmodel2 = gwy_graph_model_get_curve(gmodel, 1);

    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
    g_object_set(gcmodel2, "mode", GWY_GRAPH_CURVE_LINE, NULL);

    if (!args->strayfield)
        args->strayfield = gwy_brick_new_alike(brick, FALSE);

    if (!args->computed) {
        if (!compute_strayfield_brick(brick, args->strayfield,
                                      args->zfrom, args->zto,
                                      controls->dialog)) {
            g_object_unref(gcmodel);
            g_object_unref(gcmodel2);
            return;
        }
        args->computed = TRUE;
    }

    if (args->quantity == GWY_STRAYFIELD_SINGLE) {
       extract_graph_curve(args, gcmodel);

       ndata = args->zto-args->zfrom;
       xdata = g_new(gdouble, ndata);
       ydata = g_new(gdouble, ndata);

       dz = gwy_brick_get_dz(brick);

       if (ndata>1) {

          for (level = args->zfrom; level<args->zto; level++) {
               xdata[level-args->zfrom] = level*dz;
               ydata[level-args->zfrom] = gwy_brick_get_val(args->strayfield, args->x, args->y, level);

         }
         gwy_graph_curve_model_set_data(gcmodel2, xdata, ydata, ndata);
       }
    }
    if (args->quantity == GWY_STRAYFIELD_PLANEDIFF) {
       ndata = args->zto-args->zfrom;
       xdata = g_new(gdouble, ndata);
       ydata = g_new(gdouble, ndata);

       dz = gwy_brick_get_dz(brick);

       xdata[0] = 0;
       ydata[0] = 0;

       if (ndata>1) {

          for (level = args->zfrom+1; level<args->zto; level++) {
 
               xdata[level-args->zfrom] = (level-args->zfrom)*dz;
               ydata[level-args->zfrom] = get_brick_mutual_rms(args->strayfield, args->zfrom, level);

          }
          gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ndata);
          gwy_graph_curve_model_set_data(gcmodel2, xdata, ydata, 0); //FIXME remove curve somehow else
       }

    }
    if (args->quantity==GWY_STRAYFIELD_ZSHIFT) {


    }
}

static void
extract_graph_curve(const StrayfieldArgs *args,
                    GwyGraphCurveModel *gcmodel)
{
    GwyDataLine *line = gwy_data_line_new(1, 1.0, FALSE);
    GwyBrick *brick = args->brick;
    gdouble *xdata, *ydata;
    guint n;

    gwy_brick_extract_line(brick, line,
                           args->x, args->y, 0,
                           args->x, args->y, brick->zres,
                           FALSE);
    gwy_data_line_set_offset(line, brick->zoff);
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);

    if (args->calibration) {
        xdata = gwy_data_line_get_data(args->calibration);
        ydata = gwy_data_line_get_data(line);
        n = MIN(gwy_data_line_get_res(args->calibration),
                gwy_data_line_get_res(line));
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, n);
    }
    else {
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, line, 0, 0);
    }
    g_object_unref(line);
}

static void
extract_gmodel(const StrayfieldArgs *args, GwyGraphModel *gmodel)
{
    GwyBrick *brick = args->brick;
    GwySIUnit *xunit = NULL, *yunit;

    if (args->calibration)
        xunit = gwy_data_line_get_si_unit_y(args->calibration);
    else
        xunit = gwy_brick_get_si_unit_z(brick);
    xunit = gwy_si_unit_duplicate(xunit);
    yunit = gwy_si_unit_duplicate(gwy_brick_get_si_unit_w(brick));

    g_object_set(gmodel,
                 "si-unit-x", xunit,
                 "si-unit-y", yunit,
                 NULL);
    g_object_unref(xunit);
    g_object_unref(yunit);
}

static void
strayfield_reset(StrayfieldControls *controls)
{
    GwyBrick *brick = controls->args->brick;
    GtkWidget *area;
    GwySelection *selection;
    gdouble xy[2];

    xy[0] = 0.5*brick->xreal;
    xy[1] = 0.5*brick->yreal;
    selection = gwy_vector_layer_ensure_selection(controls->vlayer);
    gwy_selection_set_object(selection, 0, xy);

    area = gwy_graph_get_area(GWY_GRAPH(controls->graph));
    selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area),
                                             GWY_GRAPH_STATUS_XSEL);
    gwy_selection_clear(selection);
}

static const gchar quantity_key[] = "/module/volume_strayfield/quantity";
static const gchar update_key[]   = "/module/volume_strayfield/update";
static const gchar xpos_key[]     = "/module/volume_strayfield/xpos";
static const gchar ypos_key[]     = "/module/volume_strayfield/ypos";
static const gchar zfrom_key[]    = "/module/volume_strayfield/zfrom";
static const gchar zto_key[]      = "/module/volume_strayfield/zto";

static void
strayfield_sanitize_args(StrayfieldArgs *args)
{
    args->quantity = MIN(args->quantity, NQUANTITIES-1);
    args->update = !!args->update;
}

static void
strayfield_load_args(GwyContainer *container,
                    StrayfieldArgs *args)
{
    *args = strayfield_defaults;

    gwy_container_gis_enum_by_name(container, quantity_key, &args->quantity);
    gwy_container_gis_int32_by_name(container, xpos_key, &args->x);
    gwy_container_gis_int32_by_name(container, ypos_key, &args->y);
    gwy_container_gis_int32_by_name(container, zfrom_key, &args->zfrom);
    gwy_container_gis_int32_by_name(container, zto_key, &args->zto);
    gwy_container_gis_boolean_by_name(container, update_key, &args->update);
    strayfield_sanitize_args(args);
}

static void
strayfield_save_args(GwyContainer *container,
                    StrayfieldArgs *args)
{
    gwy_container_set_enum_by_name(container, quantity_key, args->quantity);
    gwy_container_set_int32_by_name(container, xpos_key, args->x);
    gwy_container_set_int32_by_name(container, ypos_key, args->y);
    gwy_container_set_int32_by_name(container, zfrom_key, args->zfrom);
    gwy_container_set_int32_by_name(container, zto_key, args->zto);
    gwy_container_set_boolean_by_name(container, update_key, args->update);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
