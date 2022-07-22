/*
 *  $Id: volume_rephase.c 22619 2019-10-29 16:47:55Z yeti-dn $
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
#include <libprocess/brick.h>
#include <libprocess/gwyprocess.h>
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

#define REPHASE_RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
};

enum {
    RESPONSE_RESET = 1,
};

typedef struct {
    gint x;
    gint y;
    gint z;
} RephasePos;

typedef struct {
    RephasePos currpos;
    GwyAppDataId object;
    gboolean right;
    gboolean invert;
    /* Dynamic state. */
    GwyBrick *brick;
    GwyBrick *second_brick;
} RephaseArgs;

typedef struct {
    RephaseArgs *args;
    GwyContainer *mydata;
    GwyDataField *image;
    GtkWidget *dialog;
    GtkWidget *view;
    GwyPixmapLayer *player;
    GwyVectorLayer *vlayer;
    GtkWidget *graph;
    GtkWidget *right;
    GtkWidget *invert;
    GtkObject *xpos;
    GtkObject *ypos;
    GtkObject *zpos;
    GwySIValueFormat *xvf;
    GwySIValueFormat *yvf;
    GwySIValueFormat *zvf;
    GtkWidget *xposreal;
    GtkWidget *yposreal;
    GtkWidget *zposreal;
    GtkWidget *data;
    GwyNullStore *store;
    gboolean in_update;
    gint current_object;
} RephaseControls;

static gboolean module_register            (void);
static void     rephase                      (GwyContainer *data,
                                            GwyRunType run);
static gboolean rephase_dialog               (RephaseArgs *args,
                                            GwyContainer *data,
                                            gint id);
static void     rephase_do                   (RephaseArgs *args,
                                            GwyContainer *data,
                                            gint id);
static void     rephase_reset                (RephaseControls *controls);
static void     point_selection_changed    (RephaseControls *controls,
                                            gint id,
                                            GwySelection *selection);
static void     plane_selection_changed    (RephaseControls *controls,
                                            gint id,
                                            GwySelection *selection);
static void     rephase_data_chosen        (GwyDataChooser *chooser,
                                            RephaseControls *controls);
static void     xpos_changed               (RephaseControls *controls,
                                            GtkAdjustment *adj);
static void     ypos_changed               (RephaseControls *controls,
                                            GtkAdjustment *adj);
static void     zpos_changed               (RephaseControls *controls,
                                            GtkAdjustment *adj);
static void     reduce_selection           (RephaseControls *controls);
static void     update_position            (RephaseControls *controls,
                                            const RephasePos *pos);
static void     update_labels              (RephaseControls *controls);
static void     right_changed              (RephaseControls *controls,
                                            GtkToggleButton *check);
static void     invert_changed             (RephaseControls *controls,
                                            GtkToggleButton *check);
static void     extract_image_plane        (const RephaseArgs *args,
                                            GwyDataField *dfield);
static void     extract_graph_curve        (const RephaseArgs *args,
                                            GwyGraphCurveModel *gcmodel);
static void     extract_gmodel             (const RephaseArgs *args,
                                            GwyGraphModel *gmodel);
static void     rephase_sanitize_args        (RephaseArgs *args);
static void     rephase_load_args            (GwyContainer *container,
                                            RephaseArgs *args);
static void     rephase_save_args            (GwyContainer *container,
                                            RephaseArgs *args);

static const RephasePos nullpos = { -1, -1, -1 };

static const RephaseArgs rephase_defaults = {
    { -1, -1, -1 },
    GWY_APP_DATA_ID_NONE,
    TRUE, TRUE,
    /* Dynamic state. */
    NULL, NULL,
};


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Swaps phase in continuous data based on user's selection"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek",
    "2019",
};

GWY_MODULE_QUERY2(module_info, volume_rephase)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_rephase",
                             (GwyVolumeFunc)&rephase,
                             N_("/_Adjust Phase..."),
                             NULL,
                             REPHASE_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Change phase in continuous data"));

    return TRUE;
}

static void
rephase(GwyContainer *data, GwyRunType run)
{
    RephaseArgs args;
    GwyBrick *brick = NULL;
    gint id;

    g_return_if_fail(run & REPHASE_RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    rephase_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    args.brick = brick;

    if (CLAMP(args.currpos.x, 0, brick->xres-1) != args.currpos.x)
        args.currpos.x = brick->xres/2;
    if (CLAMP(args.currpos.y, 0, brick->yres-1) != args.currpos.y)
        args.currpos.y = brick->yres/2;
    if (CLAMP(args.currpos.z, 0, brick->zres-1) != args.currpos.z)
        args.currpos.z = brick->zres/2;

    if (rephase_dialog(&args, data, id))
        rephase_do(&args, data, id);

    rephase_save_args(gwy_app_settings_get(), &args);
}

static gboolean
rephase_dialog(RephaseArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *dialog, *table, *hbox, *label, *area, *chooser;
    RephaseControls controls;
    GwyBrick *brick = args->brick;
    GwyDataField *dfield;
    GwyDataLine *calibration;
    GwyGraphCurveModel *gcmodel;
    GwyGraphModel *gmodel;
    GwySIUnit *siunitz;
    gdouble zmax;
    gint response, row;
    GwyPixmapLayer *layer;
    GwyVectorLayer *vlayer = NULL;
    GwySelection *selection;
    RephasePos pos;
    const guchar *gradient;
    GQuark quark;

    controls.args = args;
    controls.in_update = TRUE;
    controls.current_object = 0;

    dialog = gtk_dialog_new_with_buttons(_("Adjust Phase in Volume Data"),
                                         NULL, 0,
                                         _("_Reset"), RESPONSE_RESET,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_volume_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    controls.mydata = gwy_container_new();
    controls.image = dfield = gwy_data_field_new(1, 1, 1.0, 1.0, TRUE);
    extract_image_plane(args, dfield);
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
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(point_selection_changed), &controls);

    gmodel = gwy_graph_model_new();
    g_object_set(gmodel, "label-visible", FALSE, NULL);
    gcmodel = gwy_graph_curve_model_new();
    gwy_graph_model_add_curve(gmodel, gcmodel);
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
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(plane_selection_changed), &controls);

    hbox = gtk_hbox_new(FALSE, 24);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 4);


    table = gtk_table_new(4, 5, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_table_set_col_spacing(GTK_TABLE(table), 2, 12);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 0);
    row = 0;

    label = gtk_label_new_with_mnemonic("Related dataset:");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row+1,
                         GTK_FILL, 0, 0, 0);


    chooser = gwy_data_chooser_new_volumes();
    gwy_data_chooser_set_active_id(GWY_DATA_CHOOSER(chooser),
                                       &(args->object));
    g_signal_connect(chooser, "changed",
                         G_CALLBACK(rephase_data_chosen), &controls);
    gtk_table_attach(GTK_TABLE(table), chooser, 1, 2, row, row+1,
                         GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), chooser);
    controls.data = chooser;

    row++;

    label = gwy_label_new_header(_("Positions"));
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.xpos = gtk_adjustment_new(args->currpos.x, 0.0, brick->xres-1.0,
                                       1.0, 10.0, 0);
    gwy_table_attach_adjbar(table, row, _("_X:"), _("px"),
                            controls.xpos, GWY_HSCALE_LINEAR | GWY_HSCALE_SNAP);
    gtk_widget_set_size_request(gwy_table_hscale_get_scale(controls.xpos),
                                96, -1);
    g_signal_connect_swapped(controls.xpos, "value-changed",
                             G_CALLBACK(xpos_changed), &controls);

    controls.xposreal = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.xposreal), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.xposreal,
                     3, 4, row, row+1, GTK_FILL, 0, 0, 0);
    controls.xvf = gwy_brick_get_value_format_x(brick,
                                                GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                NULL);
    label = gtk_label_new(controls.xvf->units);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     4, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.ypos = gtk_adjustment_new(args->currpos.y, 0.0, brick->yres-1.0,
                                       1.0, 10.0, 0);
    gwy_table_attach_adjbar(table, row, _("_Y:"), _("px"),
                            controls.ypos, GWY_HSCALE_LINEAR | GWY_HSCALE_SNAP);
    gtk_widget_set_size_request(gwy_table_hscale_get_scale(controls.xpos),
                                96, -1);
    g_signal_connect_swapped(controls.ypos, "value-changed",
                             G_CALLBACK(ypos_changed), &controls);

    controls.yposreal = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.yposreal), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.yposreal,
                     3, 4, row, row+1, GTK_FILL, 0, 0, 0);
    controls.yvf = gwy_brick_get_value_format_y(brick,
                                                GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                NULL);
    label = gtk_label_new(controls.yvf->units);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     4, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.zpos = gtk_adjustment_new(args->currpos.z, 0.0, 2*brick->zres-1.0,
                                       1.0, 10.0, 0);
    gwy_table_attach_adjbar(table, row, _("_Z:"), _("px"),
                            controls.zpos, GWY_HSCALE_LINEAR | GWY_HSCALE_SNAP);
    gtk_widget_set_size_request(gwy_table_hscale_get_scale(controls.xpos),
                                96, -1);
    g_signal_connect_swapped(controls.zpos, "value-changed",
                             G_CALLBACK(zpos_changed), &controls);

    controls.zposreal = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.zposreal), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.zposreal,
                     3, 4, row, row+1, GTK_FILL, 0, 0, 0);
    if ((calibration = gwy_brick_get_zcalibration(brick))) {
        siunitz = gwy_data_line_get_si_unit_y(calibration);
        zmax = gwy_data_line_get_max(calibration);
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
    label = gtk_label_new(controls.zvf->units);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     4, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.right = gtk_check_button_new_with_mnemonic(_("Place second curve to the _right"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.right),
                                 args->right);
    gtk_table_attach(GTK_TABLE(table), controls.right,
                     0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.right, "toggled",
                             G_CALLBACK(right_changed), &controls);
    row++;

    controls.invert = gtk_check_button_new_with_mnemonic(_("_Invert second curve"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.invert),
                                 args->invert);
    gtk_table_attach(GTK_TABLE(table), controls.invert,
                     0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.invert, "toggled",
                             G_CALLBACK(invert_changed), &controls);
    row++;

    //label = gtk_label_new(NULL);
    //gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

    pos = args->currpos;
    args->currpos = nullpos;
    rephase_data_chosen(GWY_DATA_CHOOSER(chooser), &controls);

    update_position(&controls, &pos);

    controls.in_update = FALSE;

    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            g_object_unref(controls.mydata);
            gwy_si_unit_value_format_free(controls.xvf);
            gwy_si_unit_value_format_free(controls.yvf);
            gwy_si_unit_value_format_free(controls.zvf);
            return FALSE;
            break;

            case GTK_RESPONSE_OK:
            break;

            case RESPONSE_RESET:
            rephase_reset(&controls);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);
    g_object_unref(controls.mydata);
    gwy_si_unit_value_format_free(controls.xvf);
    gwy_si_unit_value_format_free(controls.yvf);
    gwy_si_unit_value_format_free(controls.zvf);

    return TRUE;
}

static void
rephase_reset(RephaseControls *controls)
{
    RephaseArgs *args = controls->args;
    GwyBrick *brick = args->brick;

    args->currpos.x = brick->xres/2;
    args->currpos.y = brick->yres/2;
    args->currpos.z = brick->zres/2;
    reduce_selection(controls);
}

static void
right_changed(RephaseControls *controls, GtkToggleButton *check)
{
    RephaseArgs *args = controls->args;

    args->right = gtk_toggle_button_get_active(check);
    update_position(controls, &(controls->args->currpos));
}
static void
invert_changed(RephaseControls *controls, GtkToggleButton *check)
{
    RephaseArgs *args = controls->args;

    args->invert = gtk_toggle_button_get_active(check);
    update_position(controls, &(controls->args->currpos));
}

static void
point_selection_changed(RephaseControls *controls,
                        gint id,
                        GwySelection *selection)
{
    RephaseArgs *args = controls->args;
    RephasePos pos = args->currpos;
    gdouble xy[2];
    gint i, j;

    gwy_debug("%d (%d)", controls->in_update, id);
    if (controls->in_update)
        return;

    /* What should we do here?  Hope we always get another update with a
     * specific id afterwards. */
    if (id < 0)
        return;

    if (!gwy_selection_get_object(selection, id, xy))
        return;

    controls->current_object = id;

    j = CLAMP(gwy_data_field_rtoj(controls->image, xy[0]),
              0, controls->image->xres-1);
    i = CLAMP(gwy_data_field_rtoi(controls->image, xy[1]),
              0, controls->image->yres-1);

    pos.x = j;
    pos.y = i;

    controls->in_update = TRUE;
    update_position(controls, &pos);
    controls->in_update = FALSE;
}


static void
plane_selection_changed(RephaseControls *controls,
                        gint id,
                        GwySelection *selection)
{
    RephaseArgs *args = controls->args;
    RephasePos pos = args->currpos;
    GwyBrick *brick = args->brick;
    gdouble r;

    gwy_debug("%d (%d)", controls->in_update, id);
    if (controls->in_update)
        return;

    /* What should we do here?  Hope we always get another update with a
     * specific id afterwards. */
    if (id < 0)
        return;

    if (!gwy_selection_get_object(selection, id, &r))
        return;

    pos.z = CLAMP(r, 0, 2*brick->zres-1);

    controls->in_update = TRUE;
    update_position(controls, &pos);
    extract_image_plane(args, controls->image);
    controls->in_update = FALSE;
}

static void
rephase_data_chosen(GwyDataChooser *chooser,
                    RephaseControls *controls)
{
    RephaseArgs *args;
    GwyContainer *data;
    GQuark quark;

    args = controls->args;
    gwy_data_chooser_get_active_id(chooser, &(args->object));

    data = gwy_app_data_browser_get(args->object.datano);
    g_return_if_fail(data);
    quark = gwy_app_get_brick_key_for_id(args->object.id);
    args->second_brick = GWY_BRICK(gwy_container_get_object(data, quark));

    if (controls->in_update == FALSE) {
        controls->in_update = TRUE;
        update_position(controls, &(controls->args->currpos));
        extract_image_plane(args, controls->image);
        controls->in_update = FALSE;
    }
}

static void
xpos_changed(RephaseControls *controls, GtkAdjustment *adj)
{
    RephasePos pos = controls->args->currpos;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    pos.x = gwy_adjustment_get_int(adj);
    update_position(controls, &pos);
    controls->in_update = FALSE;
}

static void
ypos_changed(RephaseControls *controls, GtkAdjustment *adj)
{
    RephasePos pos = controls->args->currpos;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    pos.y = gwy_adjustment_get_int(adj);
    update_position(controls, &pos);
    controls->in_update = FALSE;
}

static void
zpos_changed(RephaseControls *controls, GtkAdjustment *adj)
{
    RephasePos pos = controls->args->currpos;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    pos.z = gwy_adjustment_get_int(adj);
    update_position(controls, &pos);
    controls->in_update = FALSE;
}

static void
reduce_selection(RephaseControls *controls)
{
    RephasePos pos = controls->args->currpos;
    GwySelection *selection;
    GtkWidget *area;
    gdouble xyz[2] = { 0.0, 0.0 };

    g_assert(!controls->in_update);

    controls->in_update = TRUE;
    selection = gwy_vector_layer_ensure_selection(controls->vlayer);
    gwy_selection_set_data(selection, 1, xyz);

    area = gwy_graph_get_area(GWY_GRAPH(controls->graph));
    selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area),
                                             GWY_GRAPH_STATUS_XLINES);
    gwy_selection_set_data(selection, 1, xyz);

    controls->args->currpos = nullpos;
    update_position(controls, &pos);
    controls->in_update = FALSE;
}

/*
 * All signal handlers must
 * - do nothing in update
 * - calculate the integer coordinate
 * - enter in-update
 * - call this function
 * - leave in-update
 * This way there are no circular dependencies, we always completely update
 * anything that has changed here.
 */
static void
update_position(RephaseControls *controls,
                const RephasePos *pos)
{
    RephaseArgs *args = controls->args;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwySelection *selection;
    GwyBrick *brick = args->brick;
    gdouble xy[2];
    gint id;

    if (!controls->in_update)
        return;

    xy[0] = gwy_brick_itor(brick, pos->x);
    xy[1] = gwy_brick_jtor(brick, pos->y);

    args->currpos = *pos;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->xpos), pos->x);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->ypos), pos->y);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->zpos), pos->z);

    update_labels(controls);

    id = controls->current_object;

    selection = gwy_vector_layer_ensure_selection(controls->vlayer);
    gwy_selection_set_object(selection, id, xy);

    gmodel = gwy_graph_get_model(GWY_GRAPH(controls->graph));
    extract_gmodel(args, gmodel);

    gcmodel = gwy_graph_model_get_curve(gmodel, 0);
    extract_graph_curve(args, gcmodel); //first part
}

static void
update_labels(RephaseControls *controls)
{
    RephaseArgs *args = controls->args;
    GwyBrick *brick = args->brick;
    gdouble x, y, z;
    gchar buf[64];

    x = gwy_brick_itor(brick, args->currpos.x);
    g_snprintf(buf, sizeof(buf), "%.*f",
               controls->xvf->precision, x/controls->xvf->magnitude);
    gtk_label_set_markup(GTK_LABEL(controls->xposreal), buf);

    y = gwy_brick_jtor(brick, args->currpos.y);
    g_snprintf(buf, sizeof(buf), "%.*f",
               controls->xvf->precision, y/controls->yvf->magnitude);
    gtk_label_set_markup(GTK_LABEL(controls->yposreal), buf);

    z = gwy_brick_ktor_cal(brick, args->currpos.z);
    g_snprintf(buf, sizeof(buf), "%.*f",
               controls->zvf->precision, z/controls->zvf->magnitude);
    gtk_label_set_markup(GTK_LABEL(controls->zposreal), buf);
}

/* gets the two brick values for lev, corrected for shift and
 * direction/inversion. lev should go from 0 to zres */
static gboolean
get_shifted_values(GwyBrick *b1, GwyBrick *b2,
                   gint col, gint row, gint lev,
                   gint shift, gint right, gint invert,
                   gdouble *val1, gdouble *val2)
{
    const gdouble *b1data = b1->data, *b2data = b2->data;
    gint xres = b1->xres, yres = b1->yres, zres = b1->zres;
    gint pos;

    if (right) {
        pos = lev + shift;
        if (pos < zres)
            *val1 = b1data[col + xres*row  + xres*yres*pos];
        else {
            if (invert) {
                pos = 2*zres - pos - 1;
                *val1 = b2data[col + xres*row  + xres*yres*pos];
            }
            else {
                /* ??? ??? ??? ??? ??? val1 is unset. */
            }
        }

        pos = lev + shift + zres;
        if (pos < 2*zres) {
            pos = 2*zres - pos - 1;
            *val2 = b2data[col + xres*row  + xres*yres*pos];
        }
        else {
            pos -= 2*zres;
            *val2 = b1data[col + xres*row  + xres*yres*pos];
        }
    }
    else {
        *val1 = 3;
        *val2 = 4;
    }
    return TRUE;

}

static void
rephase_do(RephaseArgs *args, GwyContainer *data, gint id)
{
    GwyBrick *result1, *result2;
    gint newid;
    GwyBrick *brick = args->brick;
    GwyBrick *second_brick = args->second_brick;
    gint xres = brick->xres, yres = brick->yres, zres = brick->zres;
    gint col, row, lev;
    gdouble *r1data, *r2data;
    gdouble val1, val2, lastval1, lastval2;

    result1 = gwy_brick_new_alike(args->brick, TRUE);
    result2 = gwy_brick_new_alike(args->brick, TRUE);

    r1data = gwy_brick_get_data(result1);
    r2data = gwy_brick_get_data(result2);

    for (col = 0; col < xres; col++) {
        for (row = 0; row < yres; row++) {
            for (lev = 0; lev < zres; lev++) {
                 if (get_shifted_values(brick, second_brick,
                                        col, row, lev,
                                        args->currpos.z, args->right,
                                        args->invert,
                                        &val1, &val2)) {
                     r1data[col + xres*row  + xres*yres*lev] = val1;
                     r2data[col + xres*row  + xres*yres*lev] = val2;
                     lastval1 = val1;
                     lastval2 = val2;
                 }
                 else {
                     r1data[col + xres*row  + xres*yres*lev] = lastval1;
                     r2data[col + xres*row  + xres*yres*lev] = lastval2;
                 }
            }
        }
    }
    gwy_brick_data_changed(result1);
    gwy_brick_data_changed(result2);

    newid = gwy_app_data_browser_add_brick(result1, NULL, data, TRUE);

    gwy_app_set_brick_title(data, newid, _("Phase adjusted result A"));
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                0);

    gwy_app_volume_log_add_volume(data, -1, newid);

    newid = gwy_app_data_browser_add_brick(result2, NULL, data, TRUE);

    gwy_app_set_brick_title(data, newid, _("Phase adjusted result B"));
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                0);

    gwy_app_volume_log_add_volume(data, -1, newid);

    g_object_unref(result1);
    g_object_unref(result2);
}


static void
extract_image_plane(const RephaseArgs *args, GwyDataField *dfield)
{
    gint zres = gwy_brick_get_zres(args->brick);
    gint z = args->currpos.z;

    if (args->right) {
       if (z < zres)
           gwy_brick_extract_xy_plane(args->brick, dfield, z);
       else if (args->second_brick) {
           if (args->invert)
               gwy_brick_extract_xy_plane(args->brick, dfield, 2*zres - z - 1);
           else
               gwy_brick_extract_xy_plane(args->brick, dfield, z - zres);
       }
    }

    gwy_data_field_data_changed(dfield);
}

static void
extract_graph_curve(const RephaseArgs *args,
                    GwyGraphCurveModel *gcmodel)
{
    GwyDataLine *line = gwy_data_line_new(1, 1.0, FALSE);
    GwyDataLine *second_line = gwy_data_line_new(1, 1.0, FALSE);
    GwyDataLine *merged_line;
    GwyBrick *brick = args->brick;
    GwyBrick *second_brick = args->second_brick;
    const RephasePos *pos = &args->currpos;
    gchar *desc;
    gint i;
    gdouble *data, *data1, *data2;

    gwy_debug("(%d, %d, %d)", pos->x, pos->y, pos->z);

    gwy_brick_extract_line(brick, line,
                           pos->x, pos->y, 0,
                           pos->x, pos->y, brick->zres,
                           FALSE);

    gwy_brick_extract_line(second_brick, second_line,
                           pos->x, pos->y, 0,
                           pos->x, pos->y, brick->zres,
                           FALSE);

    merged_line = gwy_data_line_new(2*brick->zres, 2*brick->zres, FALSE);
    data = gwy_data_line_get_data(merged_line);
    data1 = gwy_data_line_get_data(line);
    data2 = gwy_data_line_get_data(second_line);

    for (i = 0; i < brick->zres; i++) {
        if (args->right) {
           data[i] = data1[i];
           if (args->invert)
              data[i+brick->zres] = data2[brick->zres-i-1];
           else
               data[i+brick->zres] = data2[i];
        }
        else {
           data[i] = data2[i];
           if (args->invert)
              data[i+brick->zres] = data1[brick->zres-i-1];
           else
               data[i+brick->zres] = data1[i];
        }
    }

    desc = g_strdup_printf(_("Merged graph at x: %d y: %d"), pos->x, pos->y);
    g_object_set(gcmodel,
                 "description", desc,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 NULL);
    g_free(desc);

    gwy_graph_curve_model_set_data_from_dataline(gcmodel, merged_line, 0, 0);

    g_object_unref(line);
    g_object_unref(second_line);
    g_object_unref(merged_line);
}

static void
extract_gmodel(const RephaseArgs *args, GwyGraphModel *gmodel)
{
    GwyBrick *brick = args->brick;
    GwyDataLine *calibration = NULL;
    const gchar *xlabel, *ylabel, *gtitle;
    GwySIUnit *xunit = NULL, *yunit;

    gtitle = _("Volume Z graphs");
    xlabel = "z";
    ylabel = "w";

    calibration = gwy_brick_get_zcalibration(brick);
    if (calibration)
         xunit = gwy_data_line_get_si_unit_y(calibration);
    else
         xunit = gwy_brick_get_si_unit_z(brick);
    xunit = gwy_si_unit_duplicate(xunit);
    yunit = gwy_si_unit_duplicate(gwy_brick_get_si_unit_w(brick));

    g_object_set(gmodel,
                 "title", gtitle,
                 "si-unit-x", xunit,
                 "si-unit-y", yunit,
                 "axis-label-bottom", xlabel,
                 "axis-label-left", ylabel,
                 NULL);
    g_object_unref(xunit);
    g_object_unref(yunit);
}

static const gchar xpos_key[]        = "/module/volume_rephase/xpos";
static const gchar ypos_key[]        = "/module/volume_rephase/ypos";
static const gchar zpos_key[]        = "/module/volume_rephase/zpos";
static const gchar right_key[]       = "/module/volume_rephase/right";
static const gchar invert_key[]      = "/module/volume_rephase/invert";


static void
rephase_sanitize_args(RephaseArgs *args)
{
    /* Positions are validated against the brick. */
    args->invert = !!args->invert;
    args->right = !!args->right;
}

static void
rephase_load_args(GwyContainer *container,
                RephaseArgs *args)
{
    *args = rephase_defaults;

    gwy_container_gis_int32_by_name(container, xpos_key, &args->currpos.x);
    gwy_container_gis_int32_by_name(container, ypos_key, &args->currpos.y);
    gwy_container_gis_int32_by_name(container, zpos_key, &args->currpos.z);
    gwy_container_gis_boolean_by_name(container, right_key, &args->right);
    gwy_container_gis_boolean_by_name(container, invert_key, &args->invert);


    rephase_sanitize_args(args);
}

static void
rephase_save_args(GwyContainer *container,
                RephaseArgs *args)
{
    gwy_container_set_int32_by_name(container, xpos_key, args->currpos.x);
    gwy_container_set_int32_by_name(container, ypos_key, args->currpos.y);
    gwy_container_set_int32_by_name(container, zpos_key, args->currpos.z);

    gwy_container_set_boolean_by_name(container, right_key, args->right);
    gwy_container_set_boolean_by_name(container, invert_key, args->invert);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
