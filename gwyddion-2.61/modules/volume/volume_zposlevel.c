/*
 *  $Id: volume_zposlevel.c 22619 2019-10-29 16:47:55Z yeti-dn $
 *  Copyright (C) 2019 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <libgwyddion/gwythreads.h>
#include <libprocess/brick.h>
#include <libprocess/arithmetic.h>
#include <libprocess/linestats.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define ZPOSLEVEL_RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
    /* 16 is good for current processors; increasing it to 32 might not
     * hurt in the future. */
    BLOCK_SIZE = 16,
};

enum {
    RESPONSE_RESET   = 1,
    RESPONSE_PREVIEW = 2,
};

#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
typedef enum {
    SHOW_IMAGE   = 0,
    SHOW_RESULT = 1,
    NSHOWS
} ZposlevelShow;
#endif

typedef struct {
#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
    ZposlevelShow show_type;
#endif
    gint x;
    gint y;
    gint z;
    gboolean update;
    /* Dynamic state. */
    GwyBrick *brick;
    GwyDataLine *calibration;
    gdouble value;
} ZposlevelArgs;

typedef struct {
    ZposlevelArgs *args;
    GwyContainer *mydata;
    GwyDataField *image;
    GtkWidget *dialog;
    GtkWidget *view;
    GwyPixmapLayer *player;
    GwyVectorLayer *vlayer;
    GtkWidget *graph;
    GtkWidget *update;
#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
    GSList *show_type;
#endif
    GtkWidget *z;
    GtkWidget *wlabel;
    GwySIValueFormat *zvf;
    GwySIValueFormat *vf;
} ZposlevelControls;


static gboolean module_register        (void);
static void     zposlevel              (GwyContainer *data,
                                        GwyRunType run);
static gboolean zposlevel_dialog       (ZposlevelArgs *args,
                                        GwyContainer *data,
                                        gint id);
static void     zposlevel_do           (ZposlevelArgs *args,
                                        GwyContainer *data,
                                        gint id);
static void     zposlevel_reset        (ZposlevelControls *controls);
static void     point_selection_changed(ZposlevelControls *controls,
                                        gint id,
                                        GwySelection *selection);
static void     graph_selection_changed(ZposlevelControls *controls,
                                        gint id,
                                        GwySelection *selection);
static void     range_changed          (GtkWidget *entry,
                                        ZposlevelControls *controls);
#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
static void     show_type_changed      (GtkToggleButton *button,
                                        ZposlevelControls *controls);
#endif
static void     update_changed         (ZposlevelControls *controls,
                                        GtkToggleButton *check);
static void     shift_to_zero          (ZposlevelArgs *args);
static void     extract_graph_curve    (const ZposlevelArgs *args,
                                        GwyGraphCurveModel *gcmodel);
static void     extract_gmodel         (const ZposlevelArgs *args,
                                        GwyGraphModel *gmodel);
static void     zposlevel_sanitize_args(ZposlevelArgs *args);
static void     zposlevel_load_args    (GwyContainer *container,
                                        ZposlevelArgs *args);
static void     zposlevel_save_args    (GwyContainer *container,
                                        ZposlevelArgs *args);

static const ZposlevelArgs zposlevel_defaults = {
#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
    SHOW_IMAGE,
#endif
    -1, -1, -1,
    FALSE,
    /* Dynamic state. */
    NULL, NULL, 0.0,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Shifts values in z curves to be zero at defined position."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek",
    "2019",
};

GWY_MODULE_QUERY2(module_info, volume_zposlevel)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_zposlevel",
                             (GwyVolumeFunc)&zposlevel,
                             N_("/S_hift Value To Zero..."),
                             NULL,
                             ZPOSLEVEL_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Shift value at some z plane to zero"));

    return TRUE;
}

static void
zposlevel(GwyContainer *data, GwyRunType run)
{
    ZposlevelArgs args;
    GwyBrick *brick = NULL;
    gint id;

    g_return_if_fail(run & ZPOSLEVEL_RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    zposlevel_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));

    brick = gwy_brick_duplicate(brick);
    args.brick = brick;

    args.calibration = gwy_brick_get_zcalibration(brick);
    if (args.calibration
        && (gwy_brick_get_zres(brick)
            != gwy_data_line_get_res(args.calibration)))
        args.calibration = NULL;


    if (CLAMP(args.x, 0, brick->xres-1) != args.x)
        args.x = brick->xres/2;
    if (CLAMP(args.y, 0, brick->yres-1) != args.y)
        args.y = brick->yres/2;
    if (CLAMP(args.z, 0, brick->zres-1) != args.z)
        args.z = 0;

    if (zposlevel_dialog(&args, data, id)) {
        zposlevel_do(&args, data, id);
    }
    g_object_unref(brick);

    zposlevel_save_args(gwy_app_settings_get(), &args);
}

static gboolean
zposlevel_dialog(ZposlevelArgs *args, GwyContainer *data, gint id)
{
#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
    static const GwyEnum show_types[] = {
        { N_("_Data"),   SHOW_IMAGE,  },
        { N_("_Result"), SHOW_RESULT, },
    };
#endif

    GtkWidget *dialog, *table, *hbox, *label, *area;
    ZposlevelControls controls;
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
    gdouble zmax, z;

    gwy_clear(&controls, 1);
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

    controls.vf = gwy_si_unit_get_format_with_digits(gwy_brick_get_si_unit_w(brick),
                                                      GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                      gwy_brick_get_max(brick)
                                                        - gwy_brick_get_min(brick),
                                                      5, /* 5 digits */
                                                      NULL);

    dialog = gtk_dialog_new_with_buttons(_("Summarize Volume Profiles"),
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
    gwy_brick_extract_xy_plane(args->brick, dfield, 0);
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
    gwy_graph_model_add_curve(gmodel, gcmodel);
    extract_graph_curve(args, gcmodel);
    g_object_unref(gcmodel);

    controls.graph = gwy_graph_new(gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(controls.graph), FALSE);
    g_object_unref(gmodel);
    gtk_widget_set_size_request(controls.graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.graph, TRUE, TRUE, 0);

    area = gwy_graph_get_area(GWY_GRAPH(controls.graph));
    gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XLINES);
    selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area),
                                             GWY_GRAPH_STATUS_XLINES);
    gwy_selection_set_max_objects(selection, 1);
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(graph_selection_changed), &controls);

    hbox = gtk_hbox_new(FALSE, 24);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 4);

    table = gtk_table_new(2, 3, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 0);
    row = 0;


    label = gtk_label_new_with_mnemonic(_("_Z value:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls.z = gtk_entry_new();
    g_object_set_data(G_OBJECT(controls.z), "id", (gpointer)"value");
    gtk_entry_set_width_chars(GTK_ENTRY(controls.z), 8);
    gtk_table_attach(GTK_TABLE(table), controls.z,
                     1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(controls.z, "activate",
                     G_CALLBACK(range_changed), &controls);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.z);
    gwy_widget_set_activate_on_unfocus(controls.z, TRUE);

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), controls.zvf->units);
    gtk_table_attach(GTK_TABLE(table), label,
                     2, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    row++;

    label = gtk_label_new(_("Constant value:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls.wlabel = gtk_label_new("");
    gtk_table_attach(GTK_TABLE(table), controls.wlabel,
                     1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), controls.vf->units);
    gtk_table_attach(GTK_TABLE(table), label,
                     2, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);


    row++;

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

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    label = gtk_label_new(_("Output type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
    controls.show_type
        = gwy_radio_buttons_create(show_types, G_N_ELEMENTS(show_types),
                                   G_CALLBACK(show_type_changed), &controls,
                                   args->show_type);
    row = gwy_radio_buttons_attach_to_table(controls.show_type,
                                            GTK_TABLE(table), 2, row);
#endif

    selection = gwy_vector_layer_ensure_selection(vlayer);
    xy[0] = gwy_brick_itor(brick, args->x);
    xy[1] = gwy_brick_jtor(brick, args->y);
    gwy_selection_set_object(selection, 0, xy);

    selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area),
                                             GWY_GRAPH_STATUS_XLINES);
    if (args->z > 0 || args->z < brick->zres-1) {
        z = gwy_brick_ktor_cal(brick, args->z);
        gwy_selection_set_object(selection, 0, &z);
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
            zposlevel_reset(&controls);
            break;

            case RESPONSE_PREVIEW:
            shift_to_zero(args);
            gwy_brick_extract_xy_plane(args->brick, dfield, 0);
            gwy_data_field_data_changed(dfield);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);
    g_object_unref(controls.mydata);
    gwy_si_unit_value_format_free(controls.zvf);
    if (controls.vf)
        gwy_si_unit_value_format_free(controls.vf);

    return TRUE;
}

static void
point_selection_changed(ZposlevelControls *controls,
                        G_GNUC_UNUSED gint id,
                        GwySelection *selection)
{
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    ZposlevelArgs *args = controls->args;
    GwyBrick *brick = args->brick;
    gdouble xy[2];

    if (!gwy_selection_get_object(selection, 0, xy))
        return;

    args->x = CLAMP(gwy_brick_rtoi(brick, xy[0]), 0, brick->xres-1);
    args->y = CLAMP(gwy_brick_rtoj(brick, xy[1]), 0, brick->yres-1);

    gmodel = gwy_graph_get_model(GWY_GRAPH(controls->graph));
    gcmodel = gwy_graph_model_get_curve(gmodel, 0);
    extract_graph_curve(args, gcmodel);
}

static void
graph_selection_changed(ZposlevelControls *controls,
                        G_GNUC_UNUSED gint id,
                        GwySelection *selection)
{
    ZposlevelArgs *args = controls->args;
    GwyBrick *brick = args->brick;
    GwySIValueFormat *zvf = controls->zvf;
    GwySIValueFormat *vf = controls->vf;
    gchar buf[32];
    gdouble z[2];
    gdouble value;

    if (!gwy_selection_get_object(selection, 0, z)) {
        args->z = 0.0;
    }
    else {
        args->z = GWY_ROUND(gwy_brick_rtok_cal(brick, z[0]));
        args->z = CLAMP(args->z, 0, brick->zres-1);
    }

    value = gwy_brick_get_val(brick, args->x, args->y, args->z);

    //printf("gs: real %g   pixel  %d   back to real %g   value %g\n", z[0], args->z, gwy_brick_ktor_cal(brick, z[0]), gwy_brick_get_val(brick,args->x, args->y, args->z));

    g_snprintf(buf, sizeof(buf), "%.*f",
               zvf->precision,
               z[0]/zvf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->z), buf);

    g_snprintf(buf, sizeof(buf), "%.*f",
               vf->precision,
               value/vf->magnitude);
    gtk_label_set_text(GTK_LABEL(controls->wlabel), buf);

    args->value = value;

    if (args->update) {
        shift_to_zero(controls->args);
        gwy_brick_extract_xy_plane(args->brick, controls->image, 0);
        gwy_data_field_data_changed(controls->image);
    }
}

static void
range_changed(GtkWidget *entry,
              ZposlevelControls *controls)
{
    ZposlevelArgs *args = controls->args;
    GwySelection *selection;
    GtkWidget *area;
    gdouble z = g_strtod(gtk_entry_get_text(GTK_ENTRY(entry)), NULL);
    gdouble xy[2];

    z *= controls->zvf->magnitude;
    area = gwy_graph_get_area(GWY_GRAPH(controls->graph));
    selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area),
                                             GWY_GRAPH_STATUS_XLINES);


    if (!gwy_selection_get_object(selection, 0, xy)) {
        xy[0] = gwy_brick_rtok_cal(args->brick, 0.0);
    }
    else xy[0] = z;

    gwy_selection_set_object(selection, 0, xy);
}

#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
static void
show_type_changed(GtkToggleButton *button, ZposlevelControls *controls)
{
    ZposlevelArgs *args = controls->args;

    if (!gtk_toggle_button_get_active(button))
        return;

    args->show_type = gwy_radio_buttons_get_current(controls->show_type);

    if (args->update) {
        shift_to_zero(controls->args);
        gwy_brick_extract_xy_plane(args->brick, controls->image, 0);
        gwy_data_field_data_changed(controls->image);
    }

}
#endif

static void
update_changed(ZposlevelControls *controls, GtkToggleButton *check)
{
    ZposlevelArgs *args = controls->args;

    args->update = gtk_toggle_button_get_active(check);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      RESPONSE_PREVIEW, !args->update);
    if (args->update) {
        shift_to_zero(controls->args);
        gwy_brick_extract_xy_plane(args->brick, controls->image, 0);
        gwy_data_field_data_changed(controls->image);
    }
}


static void
shift_to_zero(ZposlevelArgs *args)
{
    GwyDataField *shifts;

    shifts = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    gwy_brick_extract_xy_plane(args->brick, shifts, args->z);
    gwy_data_field_multiply(shifts, -1.0);
    gwy_brick_add_to_xy_planes(args->brick, shifts);
    g_object_unref(shifts);
}

static void
extract_graph_curve(const ZposlevelArgs *args,
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
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, line,
                                                     0, 0);
    }
    g_object_unref(line);
}

static void
extract_gmodel(const ZposlevelArgs *args, GwyGraphModel *gmodel)
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
zposlevel_reset(ZposlevelControls *controls)
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
                                             GWY_GRAPH_STATUS_XLINES);
    gwy_selection_clear(selection);
}

static void
zposlevel_do(ZposlevelArgs *args,
             GwyContainer *data,
             gint id)
{
    gchar title[100];
    gint newid;

    shift_to_zero(args);

    newid = gwy_app_data_browser_add_brick(args->brick, NULL, data, TRUE);

    g_snprintf(title, sizeof(title), _("Shifted to zero for z level = %d"), args->z);
    gwy_app_set_brick_title(data, newid, title);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                0);

    gwy_app_volume_log_add_volume(data, id, newid);

}

#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
static const gchar show_type_key[]  = "/module/volume_zposlevel/show_type";
#endif
static const gchar update_key[]     = "/module/volume_zposlevel/update";
static const gchar xpos_key[]       = "/module/volume_zposlevel/xpos";
static const gchar ypos_key[]       = "/module/volume_zposlevel/ypos";
static const gchar zpos_key[]       = "/module/volume_zposlevel/zpos";

static void
zposlevel_sanitize_args(ZposlevelArgs *args)
{
    /* Positions are validated against the brick. */
#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
    args->show_type = MIN(args->show_type, NSHOWS-1);
#endif
    args->update = !!args->update;
}

static void
zposlevel_load_args(GwyContainer *container,
                    ZposlevelArgs *args)
{
    *args = zposlevel_defaults;

#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
    gwy_container_gis_enum_by_name(container, show_type_key, &args->show_type);
#endif
    gwy_container_gis_int32_by_name(container, xpos_key, &args->x);
    gwy_container_gis_int32_by_name(container, ypos_key, &args->y);
    gwy_container_gis_int32_by_name(container, zpos_key, &args->z);
    gwy_container_gis_boolean_by_name(container, update_key, &args->update);
    zposlevel_sanitize_args(args);
}

static void
zposlevel_save_args(GwyContainer *container,
                    ZposlevelArgs *args)
{
#ifdef I_HAVE_ACTUALLY_IMPLEMENTED_ZPOSLEVEL_SHOW
    gwy_container_set_enum_by_name(container, show_type_key, args->show_type);
#endif
    gwy_container_set_int32_by_name(container, xpos_key, args->x);
    gwy_container_set_int32_by_name(container, ypos_key, args->y);
    gwy_container_set_int32_by_name(container, zpos_key, args->z);
    gwy_container_set_boolean_by_name(container, update_key, args->update);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
