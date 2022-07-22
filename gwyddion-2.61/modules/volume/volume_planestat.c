/*
 *  $Id: volume_planestat.c 22340 2019-07-25 10:23:59Z yeti-dn $
 *  Copyright (C) 2015-2019 David Necas (Yeti).
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
#include <libgwyddion/gwythreads.h>
#include <libprocess/brick.h>
#include <libprocess/stats.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define ENTROPY_NORMAL 1.41893853320467274178l

#define LINE_STAT_RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
};

enum {
    RESPONSE_RESET   = 1,
    RESPONSE_PREVIEW = 2,
};

typedef enum {
    GWY_PLANE_STAT_MEAN        = 0,
    GWY_PLANE_STAT_RMS         = 1,
    GWY_PLANE_STAT_MIN         = 2,
    GWY_PLANE_STAT_MAX         = 3,
    GWY_PLANE_STAT_RANGE       = 4,
    GWY_PLANE_STAT_SKEW        = 5,
    GWY_PLANE_STAT_KURTOSIS    = 6,
    GWY_PLANE_STAT_SA          = 7,
    GWY_PLANE_STAT_MEDIAN      = 8,
    GWY_PLANE_STAT_VARIATION   = 9,
    GWY_PLANE_STAT_ENTROPY     = 10,
    GWY_PLANE_STAT_ENTROPY_DEF = 11,
    GWY_PLANE_STAT_NQUANTITIES,
} PlaneStatQuantity;

typedef enum {
    GWY_PLANE_STAT_LATERAL_EQUAL = (1 << 0),
    GWY_PLANE_STAT_ALL_EQUAL     = (1 << 1),
    GWY_PLANE_STAT_FLAGS_ALL     = (1 << 2) - 1,
} PlaneStatFlags;

typedef gdouble (*PlaneStatFunc)(GwyDataField *datafield);

typedef struct {
    PlaneStatQuantity quantity;
    guint flags;
    PlaneStatFunc func;
    const gchar *name;
    const gchar *symbol;
    gint powerx;
    gint powery;
    gint powerw;
} PlaneStatQuantInfo;

typedef struct {
    PlaneStatQuantity quantity;
    gint col;
    gint row;
    gint width;
    gint height;
    gint level;
    gboolean update;
    GwyAppDataId target_graph;
    /* Dynamic state. */
    gboolean lateral_equal;
    gboolean all_equal;
    GwyBrick *brick;
    GwyDataLine *calibration;
} PlaneStatArgs;

typedef struct {
    PlaneStatArgs *args;
    GwyContainer *mydata;
    GtkWidget *dialog;
    GtkWidget *view;
    GwyPixmapLayer *player;
    GwyVectorLayer *vlayer;
    GtkWidget *graph;
    GtkWidget *quantity;
    GtkWidget *target_graph;
    GtkWidget *update;
    GtkObject *col;
    GtkObject *row;
    GtkObject *width;
    GtkObject *height;
    GtkWidget *col_real;
    GtkWidget *row_real;
    GtkWidget *width_real;
    GtkWidget *height_real;
    GtkWidget *current_value;
    GwySIValueFormat *xvf;
    GwySIValueFormat *yvf;
    GwySIValueFormat *vf;
    guint sid;
    gboolean in_update;
} PlaneStatControls;

typedef struct {
    GwyBrick *brick;
    const gdouble *db;
    GwyDataLine *dline;
    gdouble *buf;
    guint npts;
    guint npixels;
    guint k;
} PlaneStatIter;

static gboolean       module_register             (void);
static void           plane_stat                  (GwyContainer *data,
                                                   GwyRunType run);
static gboolean       plane_stat_dialog           (PlaneStatArgs *args,
                                                   GwyContainer *data,
                                                   gint id);
static void           plane_stat_do               (PlaneStatArgs *args,
                                                   GwyContainer *data);
static void           plane_stat_reset            (PlaneStatControls *controls);
static GtkWidget*     construct_selection         (PlaneStatControls *controls);
static GtkWidget*     construct_quantities        (PlaneStatControls *controls);
static GwyGraphModel* create_graph_model          (const PlaneStatArgs *args);
static void           rectangle_selection_changed (PlaneStatControls *controls,
                                                   gint id,
                                                   GwySelection *selection);
static void           graph_selection_changed     (PlaneStatControls *controls,
                                                   gint id,
                                                   GwySelection *selection);
static void           quantity_changed            (GtkComboBox *combo,
                                                   PlaneStatControls *controls);
static void           update_current_value        (PlaneStatControls *controls);
static void           update_changed              (PlaneStatControls *controls,
                                                   GtkToggleButton *check);
static void           col_changed                 (GtkObject *adj,
                                                   PlaneStatControls *controls);
static void           row_changed                 (GtkObject *adj,
                                                   PlaneStatControls *controls);
static void           width_changed               (GtkObject *adj,
                                                   PlaneStatControls *controls);
static void           height_changed              (GtkObject *adj,
                                                   PlaneStatControls *controls);
static void           update_graph_model_ordinate (const PlaneStatArgs *args,
                                                   GwyGraphModel *gmodel);
static void           update_target_graphs        (PlaneStatControls *controls);
static gboolean       filter_target_graphs        (GwyContainer *data,
                                                   gint id,
                                                   gpointer user_data);
static void           target_graph_changed        (PlaneStatControls *controls);
static void           update_rectangle_real_size  (PlaneStatControls *controls,
                                                   const gchar *which);
static void           invalidate                  (PlaneStatControls *controls);
static gboolean       recalculate                 (gpointer user_data);
static void           update_rectangular_selection(PlaneStatControls *controls);
static void           extract_summary_graph       (const PlaneStatArgs *args,
                                                   GwyGraphModel *gmodel);
static gdouble        get_plane_range             (GwyDataField *dfield);
static gdouble        get_plane_Sa                (GwyDataField *dfield);
static gdouble        get_plane_median            (GwyDataField *dfield);
static gdouble        get_plane_skew              (GwyDataField *dfield);
static gdouble        get_plane_kurtosis          (GwyDataField *dfield);
static gdouble        get_plane_entropy_deficit   (GwyDataField *dfield);
static void           plane_stat_sanitize_args    (PlaneStatArgs *args);
static void           plane_stat_load_args        (GwyContainer *container,
                                                   PlaneStatArgs *args);
static void           plane_stat_save_args        (GwyContainer *container,
                                                   PlaneStatArgs *args);
static const PlaneStatQuantInfo* get_quantity_info(PlaneStatQuantity quantity);

static const PlaneStatQuantInfo quantities[] =  {
    {
        GWY_PLANE_STAT_MEAN, 0,
        gwy_data_field_get_avg,
        N_("Mean"), "μ",
        0, 0, 1,
    },
    {
        GWY_PLANE_STAT_RMS, 0,
        gwy_data_field_get_rms,
        N_("RMS"), "σ",
        0, 0, 1,
    },
    {
        GWY_PLANE_STAT_MIN, 0,
        gwy_data_field_get_min,
        N_("Minimum"), "v<sub>min</sub>",
        0, 0, 1,
    },
    {
        GWY_PLANE_STAT_MAX, 0,
        gwy_data_field_get_max,
        N_("Maximum"), "v<sub>min</sub>",
        0, 0, 1,
    },
    {
        GWY_PLANE_STAT_RANGE, 0,
        get_plane_range,
        N_("Range"), "R",
        0, 0, 1,
    },
    {
        GWY_PLANE_STAT_SKEW, 0,
        get_plane_skew,
        N_("Skew"), "γ",
        0, 0, 0,
    },
    {
        GWY_PLANE_STAT_KURTOSIS, 0,
        get_plane_kurtosis,
        N_("Excess kurtosis"), "κ",
        0, 0, 0,
    },
    {
        GWY_PLANE_STAT_SA, 0,
        get_plane_Sa,
        N_("Mean roughness"), "Sa",
        0, 0, 1,
    },
    {
        GWY_PLANE_STAT_MEDIAN, 0,
        get_plane_median,
        N_("Median"), "m",
        0, 0, 1,
    },
    {
        GWY_PLANE_STAT_VARIATION, GWY_PLANE_STAT_LATERAL_EQUAL,
        gwy_data_field_get_variation,
        N_("Variation"), "var",
        1, 0, 1,
    },
    {
        GWY_PLANE_STAT_ENTROPY, 0,
        gwy_data_field_get_entropy,
        N_("Entropy"), "H",
        0, 0, 0,
    },
    {
        GWY_PLANE_STAT_ENTROPY_DEF, 0,
        get_plane_entropy_deficit,
        N_("Entropy deficit"), "H<sub>def</sub>",
        0, 0, 0,
    },
};

static const PlaneStatArgs plane_stat_defaults = {
    GWY_PLANE_STAT_MEAN,
    -1, -1, -1, -1, -1,
    TRUE,
    GWY_APP_DATA_ID_NONE,
    /* Dynamic state. */
    TRUE, TRUE, NULL, NULL,
};

static GwyAppDataId target_graph_id = GWY_APP_DATA_ID_NONE;

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Summarizes volume data planes to a graph."),
    "Yeti <yeti@gwyddion.net>",
    "1.4",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, volume_planestat)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_planestat",
                             (GwyVolumeFunc)&plane_stat,
                             N_("/Summarize P_lanes..."),
                             GWY_STOCK_VOLUME_PLANE_STATS,
                             LINE_STAT_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Summarize planes"));

    return TRUE;
}

static void
plane_stat(GwyContainer *data, GwyRunType run)
{
    PlaneStatArgs args;
    const PlaneStatQuantInfo *info;
    GwyBrick *brick = NULL;
    GwySIUnit *xunit, *yunit, *wunit;
    gint xres, yres, zres;
    gint id;

    g_return_if_fail(run & LINE_STAT_RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerRectangle"));

    plane_stat_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    args.brick = brick;

    args.calibration = gwy_brick_get_zcalibration(brick);
    if (args.calibration
        && (gwy_brick_get_zres(brick)
            != gwy_data_line_get_res(args.calibration)))
        args.calibration = NULL;

    xunit = gwy_brick_get_si_unit_x(brick);
    yunit = gwy_brick_get_si_unit_y(brick);
    args.lateral_equal = gwy_si_unit_equal(xunit, yunit);
    wunit = gwy_brick_get_si_unit_w(brick);
    args.all_equal = args.lateral_equal && gwy_si_unit_equal(wunit, xunit);

    info = get_quantity_info(args.quantity);
    if (!args.all_equal && (info->flags & GWY_PLANE_STAT_ALL_EQUAL)) {
        args.quantity = GWY_PLANE_STAT_MEAN;
    }
    else if (!args.lateral_equal
             && (info->flags & GWY_PLANE_STAT_LATERAL_EQUAL)) {
        args.quantity = GWY_PLANE_STAT_MEAN;
    }

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);

    if (args.col < 0 || args.col + 4 > xres)
        args.col = 0;
    if (args.col < 0 || args.row + 4 > yres)
        args.row = 0;
    if (args.width < 0 || args.col + args.width > xres)
        args.width = xres - args.col;
    if (args.height < 0 || args.row + args.height > yres)
        args.height = yres - args.row;
    if (args.level < 0 || args.level >= zres)
        args.level = zres/2;

    if (plane_stat_dialog(&args, data, id))
        plane_stat_do(&args, data);

    plane_stat_save_args(gwy_app_settings_get(), &args);
}

static gboolean
plane_stat_dialog(PlaneStatArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *dialog, *table, *hbox, *label, *area;
    GwyDataChooser *chooser;
    PlaneStatControls controls;
    GwyDataField *dfield;
    GwyGraphModel *gmodel;
    gint response, row;
    GwyPixmapLayer *layer;
    GwyVectorLayer *vlayer = NULL;
    GwySelection *selection, *gselection;
    GwyBrick *brick;
    const guchar *gradient;
    GQuark quark;
    gdouble z;

    gwy_clear(&controls, 1);
    controls.args = args;

    brick = args->brick;
    controls.xvf = gwy_brick_get_value_format_x(brick,
                                                GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                NULL);
    controls.yvf = gwy_brick_get_value_format_y(brick,
                                                GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                NULL);

    dialog = gtk_dialog_new_with_buttons(_("Summarize Volume Planes"),
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
    quark = gwy_app_get_brick_preview_key_for_id(id);
    dfield = gwy_container_get_object(data, quark);
    /* We replace it with a slice later. */
    dfield = gwy_data_field_duplicate(dfield);
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

    controls.vlayer
        = vlayer = g_object_new(g_type_from_name("GwyLayerRectangle"), NULL);
    gwy_vector_layer_set_selection_key(vlayer, "/0/select/rectangle");
    gwy_data_view_set_top_layer(GWY_DATA_VIEW(controls.view), vlayer);
    selection = gwy_vector_layer_ensure_selection(vlayer);
    gwy_selection_set_max_objects(selection, 1);
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(rectangle_selection_changed),
                             &controls);

    gmodel = create_graph_model(args);
    g_object_set(gmodel, "label-visible", FALSE, NULL);  /* Only here. */
    controls.graph = gwy_graph_new(gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(controls.graph), FALSE);
    g_object_unref(gmodel);
    gtk_widget_set_size_request(controls.graph, 4*PREVIEW_SIZE/3, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.graph, TRUE, TRUE, 0);

    area = gwy_graph_get_area(GWY_GRAPH(controls.graph));
    gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XLINES);
    gselection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area),
                                              GWY_GRAPH_STATUS_XLINES);
    gwy_selection_set_max_objects(gselection, 1);
    g_signal_connect_swapped(gselection, "changed",
                             G_CALLBACK(graph_selection_changed), &controls);

    hbox = gtk_hbox_new(FALSE, 24);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 4);

    table = construct_selection(&controls);
    gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 0);

    table = gtk_table_new(4, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 0);
    row = 0;

    label = gtk_label_new_with_mnemonic(_("_Quantity:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    controls.quantity = construct_quantities(&controls);
    gtk_table_attach(GTK_TABLE(table), controls.quantity,
                     1, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.quantity);
    row++;

    controls.current_value = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.current_value), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.current_value,
                     1, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    label = gtk_label_new_with_mnemonic(_("Target _graph:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    controls.target_graph = gwy_data_chooser_new_graphs();
    chooser = GWY_DATA_CHOOSER(controls.target_graph);
    gwy_data_chooser_set_none(chooser, _("New graph"));
    gwy_data_chooser_set_active(chooser, NULL, -1);
    update_graph_model_ordinate(args, gmodel);
    gwy_data_chooser_set_filter(chooser, filter_target_graphs, &controls, NULL);
    gwy_data_chooser_set_active_id(chooser, &args->target_graph);
    gwy_data_chooser_get_active_id(chooser, &args->target_graph);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.target_graph);
    gtk_table_attach(GTK_TABLE(table), controls.target_graph,
                     1, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.target_graph, "changed",
                             G_CALLBACK(target_graph_changed), &controls);
    row++;

    controls.update = gtk_check_button_new_with_mnemonic(_("I_nstant updates"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.update),
                                 args->update);
    gtk_table_attach(GTK_TABLE(table), controls.update,
                     0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.update, "toggled",
                             G_CALLBACK(update_changed), &controls);
    row++;

    update_rectangle_real_size(&controls, NULL);
    update_rectangular_selection(&controls);
    z = gwy_brick_ktor_cal(brick, args->level);
    gwy_selection_set_data(gselection, 1, &z);
    quantity_changed(GTK_COMBO_BOX(controls.quantity), &controls);
    gtk_widget_show_all(dialog);

    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            goto finalize;
            break;

            case GTK_RESPONSE_OK:
            break;

            case RESPONSE_RESET:
            plane_stat_reset(&controls);
            break;

            case RESPONSE_PREVIEW:
            invalidate(&controls);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);

finalize:
    if (controls.sid)
        g_source_remove(controls.sid);
    g_object_unref(controls.mydata);
    gwy_si_unit_value_format_free(controls.xvf);
    gwy_si_unit_value_format_free(controls.yvf);
    if (controls.vf)
        gwy_si_unit_value_format_free(controls.vf);

    return response == GTK_RESPONSE_OK;
}

static GtkWidget*
construct_selection(PlaneStatControls *controls)
{
    GtkWidget *label, *spin;
    GtkTable *table;
    PlaneStatArgs *args = controls->args;
    GwyBrick *brick = args->brick;
    gint xres, yres;

    table = GTK_TABLE(gtk_table_new(6, 4, FALSE));
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_col_spacings(table, 8);
    gtk_table_set_row_spacings(table, 2);

    label = gwy_label_new_header(_("Origin"));
    gtk_table_attach(table, label, 0, 2, 0, 1, GTK_FILL, 0, 0, 0);

    label = gtk_label_new("X");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = controls->col_real = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(label), 12);
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(table, label, 1, 2, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("px"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 3, 4, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new("Y");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = controls->row_real = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(label), 12);
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(table, label, 1, 2, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("px"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 3, 4, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gwy_label_new_header(_("Size"));
    gtk_table_attach(table, label, 0, 1, 3, 4, GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Width"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 4, 5, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = controls->width_real = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(label), 12);
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(table, label, 1, 2, 4, 5, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("px"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 3, 4, 4, 5, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Height"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 5, 6, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = controls->height_real = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(label), 12);
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(table, label, 1, 2, 5, 6, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("px"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 3, 4, 5, 6, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);

    controls->col = gtk_adjustment_new(args->col, 0, xres, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls->col), 0, 0);
    gtk_entry_set_width_chars(GTK_ENTRY(spin), 4);
    gtk_table_attach(table, spin, 2, 3, 1, 2, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls->col, "value-changed",
                     G_CALLBACK(col_changed), controls);

    controls->row = gtk_adjustment_new(args->row, 0, xres, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls->row), 0, 0);
    gtk_entry_set_width_chars(GTK_ENTRY(spin), 4);
    gtk_table_attach(table, spin, 2, 3, 2, 3, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls->row, "value-changed",
                     G_CALLBACK(row_changed), controls);

    controls->width = gtk_adjustment_new(args->width, 0, yres, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls->width), 0, 0);
    gtk_entry_set_width_chars(GTK_ENTRY(spin), 4);
    gtk_table_attach(table, spin, 2, 3, 4, 5, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls->width, "value-changed",
                     G_CALLBACK(width_changed), controls);

    controls->height = gtk_adjustment_new(args->height, 0, yres, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls->height), 0, 0);
    gtk_entry_set_width_chars(GTK_ENTRY(spin), 4);
    gtk_table_attach(table, spin, 2, 3, 5, 6, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls->height, "value-changed",
                     G_CALLBACK(height_changed), controls);

    return GTK_WIDGET(table);
}

static GtkWidget*
construct_quantities(PlaneStatControls *controls)
{
    PlaneStatArgs *args = controls->args;
    GtkWidget *combo;
    GwyEnum *genum;
    guint i, n;

    genum = g_new(GwyEnum, G_N_ELEMENTS(quantities));
    for (i = n = 0; i < G_N_ELEMENTS(quantities); i++) {
        const PlaneStatQuantInfo *info = quantities + i;
        if (!args->lateral_equal
            && (info->flags & GWY_PLANE_STAT_LATERAL_EQUAL))
            continue;
        if (!args->all_equal && (info->flags & GWY_PLANE_STAT_ALL_EQUAL))
            continue;

        genum[n].name = info->name;
        genum[n].value = info->quantity;
        n++;
    }

    combo = gwy_enum_combo_box_new(genum, n,
                                   G_CALLBACK(quantity_changed), controls,
                                   args->quantity, TRUE);
    return combo;
}

static GwyGraphModel*
create_graph_model(const PlaneStatArgs *args)
{
    GwyBrick *brick = args->brick;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwySIUnit *siunitz;

    if (args->calibration)
        siunitz = gwy_data_line_get_si_unit_y(args->calibration);
    else
        siunitz = gwy_brick_get_si_unit_z(brick);

    gmodel = gwy_graph_model_new();
    g_object_set(gmodel,
                 "si-unit-x", siunitz,
                 "axis-label-bottom", "z",
                 NULL);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 NULL);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);

    return gmodel;
}

static void
rectangle_selection_changed(PlaneStatControls *controls,
                            G_GNUC_UNUSED gint id,
                            GwySelection *selection)
{
    PlaneStatArgs *args = controls->args;
    GwyBrick *brick = args->brick;
    gint xres, yres, newcol, newrow, newwidth, newheight;
    gdouble xy[4];

    if (controls->in_update)
        return;

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);

    newwidth = newheight = 0;
    if (gwy_selection_get_object(selection, 0, xy)) {
        if (xy[0] > xy[2])
            GWY_SWAP(gdouble, xy[0], xy[2]);
        if (xy[1] > xy[3])
            GWY_SWAP(gdouble, xy[1], xy[3]);

        newcol = CLAMP(gwy_brick_rtoi(brick, xy[0]), 0, xres-1);
        newrow = CLAMP(gwy_brick_rtoi(brick, xy[1]), 0, yres-1);
        newwidth = CLAMP(gwy_brick_rtoi(brick, xy[2])+1, 0, xres) - newcol;
        newheight = CLAMP(gwy_brick_rtoi(brick, xy[3])+1, 0, yres) - newrow;
        gwy_debug("new %d×%d at %d,%d", newwidth, newheight, newcol, newrow);
    }
    if (newwidth < 4 || newheight < 4) {
        newcol = newrow = 0;
        newwidth = xres;
        newheight = yres;
        gwy_debug("newfix %d×%d at %d,%d", newwidth, newheight, newcol, newrow);
    }

    controls->in_update = TRUE;
    /* NB: This does not change any default -1 to meaningful selection because
     * -1 was already outside of the adjustment range. */
    if (newcol != args->col)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->col), newcol);
    if (newrow != args->row)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->row), newrow);
    if (newwidth != args->width)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->width), newwidth);
    if (newheight != args->height)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->height), newheight);
    controls->in_update = FALSE;
}

static void
graph_selection_changed(PlaneStatControls *controls,
                        G_GNUC_UNUSED gint id,
                        GwySelection *selection)
{
    PlaneStatArgs *args = controls->args;
    GwyBrick *brick = args->brick;
    GwyDataField *dfield;
    gint zres;
    gdouble z;

    if (!gwy_selection_get_object(selection, 0, &z))
        return;

    zres = gwy_brick_get_zres(brick);
    args->level = CLAMP(gwy_brick_rtok_cal(brick, z), 0, zres-1);
    dfield = gwy_container_get_object_by_name(controls->mydata, "/0/data");
    gwy_brick_extract_xy_plane(brick, dfield, args->level);
    gwy_data_field_data_changed(dfield);
    update_current_value(controls);
}

static void
quantity_changed(GtkComboBox *combo, PlaneStatControls *controls)
{
    PlaneStatArgs *args = controls->args;
    GwyGraphModel *gmodel;

    args->quantity = gwy_enum_combo_box_get_active(combo);
    gmodel = gwy_graph_get_model(GWY_GRAPH(controls->graph));
    /* This sets the units so that we can do update_target_graphs(). */
    update_graph_model_ordinate(args, gmodel);
    update_target_graphs(controls);
    invalidate(controls);
}

static void
update_current_value(PlaneStatControls *controls)
{
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwySIValueFormat *vf;
    gint n, zlevel;
    GwySIUnit *unit;
    gdouble v;
    gchar *s;

    gmodel = gwy_graph_get_model(GWY_GRAPH(controls->graph));
    if (!gwy_graph_model_get_n_curves(gmodel))
        return;

    gcmodel = gwy_graph_model_get_curve(gmodel, 0);
    zlevel = controls->args->level;
    n = gwy_graph_curve_model_get_ndata(gcmodel);
    if (CLAMP(zlevel, 0, n-1) != zlevel)
        return;

    v = gwy_graph_curve_model_get_ydata(gcmodel)[zlevel];

    g_object_get(gmodel, "si-unit-y", &unit, NULL);
    controls->vf = gwy_si_unit_get_format_with_digits(unit,
                                                      GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                      v, 3,
                                                      controls->vf);
    g_object_unref(unit);
    vf = controls->vf;

    s = g_strdup_printf("%.*f%s%s",
                        vf->precision, v/vf->magnitude,
                        *vf->units ? " " : "", vf->units);
    gtk_label_set_markup(GTK_LABEL(controls->current_value), s);
    g_free(s);
}

static void
update_changed(PlaneStatControls *controls, GtkToggleButton *check)
{
    PlaneStatArgs *args = controls->args;

    args->update = gtk_toggle_button_get_active(check);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      RESPONSE_PREVIEW, !args->update);
    if (args->update)
        invalidate(controls);
}

static void
col_changed(GtkObject *adj, PlaneStatControls *controls)
{
    PlaneStatArgs *args = controls->args;
    gint res, m;

    args->col = gwy_adjustment_get_int(GTK_ADJUSTMENT(adj));
    res = gwy_brick_get_xres(args->brick);
    m = res - args->col;
    if (args->width > m)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->width), m);
    g_object_set(controls->width, "upper", 1.0*m, NULL);
    update_rectangle_real_size(controls, "col");
    update_rectangular_selection(controls);
    if (args->update)
        invalidate(controls);
}

static void
row_changed(GtkObject *adj, PlaneStatControls *controls)
{
    PlaneStatArgs *args = controls->args;
    gint res, m;

    args->row = gwy_adjustment_get_int(GTK_ADJUSTMENT(adj));
    res = gwy_brick_get_xres(args->brick);
    m = res - args->row;
    if (args->height > m)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->height), m);
    g_object_set(controls->height, "upper", 1.0*m, NULL);
    update_rectangle_real_size(controls, "row");
    update_rectangular_selection(controls);
    if (args->update)
        invalidate(controls);
}

static void
width_changed(GtkObject *adj, PlaneStatControls *controls)
{
    PlaneStatArgs *args = controls->args;

    args->width = gwy_adjustment_get_int(GTK_ADJUSTMENT(adj));
    update_rectangle_real_size(controls, "width");
    update_rectangular_selection(controls);
    if (args->update)
        invalidate(controls);
}

static void
height_changed(GtkObject *adj, PlaneStatControls *controls)
{
    PlaneStatArgs *args = controls->args;

    args->height = gwy_adjustment_get_int(GTK_ADJUSTMENT(adj));
    update_rectangle_real_size(controls, "height");
    update_rectangular_selection(controls);
    if (args->update)
        invalidate(controls);
}

static void
update_rectangle_real_size(PlaneStatControls *controls, const gchar *which)
{
    PlaneStatArgs *args = controls->args;
    GwySIValueFormat *xvf = controls->xvf, *yvf = controls->yvf;
    gdouble v;
    gchar *s;

    if (!which || gwy_strequal(which, "col")) {
        v = gwy_brick_jtor(args->brick, args->col);
        s = g_strdup_printf("%.*f%s%s",
                            xvf->precision, v/xvf->magnitude,
                            xvf->units ? " " : "", xvf->units);
        gtk_label_set_markup(GTK_LABEL(controls->col_real), s);
        g_free(s);
    }
    if (!which || gwy_strequal(which, "row")) {
        v = gwy_brick_jtor(args->brick, args->row);
        s = g_strdup_printf("%.*f%s%s",
                            yvf->precision, v/yvf->magnitude,
                            yvf->units ? " " : "", yvf->units);
        gtk_label_set_markup(GTK_LABEL(controls->row_real), s);
        g_free(s);
    }
    if (!which || gwy_strequal(which, "width")) {
        v = gwy_brick_jtor(args->brick, args->width);
        s = g_strdup_printf("%.*f%s%s",
                            xvf->precision, v/xvf->magnitude,
                            xvf->units ? " " : "", xvf->units);
        gtk_label_set_markup(GTK_LABEL(controls->width_real), s);
        g_free(s);
    }
    if (!which || gwy_strequal(which, "height")) {
        v = gwy_brick_jtor(args->brick, args->height);
        s = g_strdup_printf("%.*f%s%s",
                            yvf->precision, v/yvf->magnitude,
                            yvf->units ? " " : "", yvf->units);
        gtk_label_set_markup(GTK_LABEL(controls->height_real), s);
        g_free(s);
    }
}

static void
update_graph_model_ordinate(const PlaneStatArgs *args,
                            GwyGraphModel *gmodel)
{
    GwyBrick *brick = args->brick;
    GwySIUnit *xunit, *yunit, *wunit, *unit;
    const PlaneStatQuantInfo *info;

    info = get_quantity_info(args->quantity);

    xunit = gwy_brick_get_si_unit_x(brick);
    yunit = gwy_brick_get_si_unit_y(brick);
    wunit = gwy_brick_get_si_unit_w(brick);

    unit = gwy_si_unit_new(NULL);
    gwy_si_unit_power_multiply(xunit, info->powerx, yunit, info->powery, unit);
    gwy_si_unit_power_multiply(unit, 1, wunit, info->powerw, unit);

    g_object_set(gmodel,
                 "axis-label-left", info->symbol,
                 "si-unit-y", unit,
                 NULL);
    g_object_unref(unit);
}

static void
update_target_graphs(PlaneStatControls *controls)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(controls->target_graph);
    gwy_data_chooser_refilter(chooser);
}

static gboolean
filter_target_graphs(GwyContainer *data, gint id, gpointer user_data)
{
    PlaneStatControls *controls = (PlaneStatControls*)user_data;
    GwyGraphModel *gmodel, *targetgmodel;
    GQuark quark = gwy_app_get_graph_key_for_id(id);

    gmodel = gwy_graph_get_model(GWY_GRAPH(controls->graph));
    g_return_val_if_fail(GWY_IS_GRAPH_MODEL(gmodel), FALSE);
    return (gwy_container_gis_object(data, quark, (GObject**)&targetgmodel)
            && gwy_graph_model_units_are_compatible(gmodel, targetgmodel));
}

static void
target_graph_changed(PlaneStatControls *controls)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(controls->target_graph);
    gwy_data_chooser_get_active_id(chooser, &controls->args->target_graph);
}

static void
invalidate(PlaneStatControls *controls)
{
    if (controls->sid)
        return;

    controls->sid = g_idle_add(recalculate, controls);
}

static gboolean
recalculate(gpointer user_data)
{
    PlaneStatControls *controls = (PlaneStatControls*)user_data;
    GwyGraphModel *gmodel;

    gwy_app_wait_cursor_start(GTK_WINDOW(controls->dialog));
    gmodel = gwy_graph_get_model(GWY_GRAPH(controls->graph));
    extract_summary_graph(controls->args, gmodel);
    update_current_value(controls);
    gwy_app_wait_cursor_finish(GTK_WINDOW(controls->dialog));
    controls->sid = 0;
    return FALSE;
}

static void
update_rectangular_selection(PlaneStatControls *controls)
{
    PlaneStatArgs *args = controls->args;
    GwySelection *selection;
    GwyBrick *brick = args->brick;
    gdouble xy[4];

    if (controls->in_update)
        return;

    controls->in_update = TRUE;

    selection = gwy_vector_layer_ensure_selection(controls->vlayer);
    if (args->width && args->height) {
        xy[0] = gwy_brick_jtor(brick, args->col + 0.5);
        xy[1] = gwy_brick_itor(brick, args->row + 0.5);
        xy[2] = gwy_brick_jtor(brick, args->col + args->width - 0.5);
        xy[3] = gwy_brick_itor(brick, args->row + args->height - 0.5);

        gwy_selection_set_data(selection, 1, xy);
    }
    else
        gwy_selection_clear(selection);

    controls->in_update = FALSE;
}

static void
extract_summary_graph(const PlaneStatArgs *args, GwyGraphModel *gmodel)
{
    PlaneStatQuantity quantity = args->quantity;
    const PlaneStatQuantInfo *info;
    GwyBrick *brick = args->brick;
    GwyGraphCurveModel *gcmodel;
    gdouble zreal, zoff;
    gint xres, yres, zres, col, row, w, h;
    gdouble *xdata, *ydata;
    PlaneStatFunc func;

    info = get_quantity_info(quantity);
    func = info->func;

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);
    zreal = gwy_brick_get_zreal(brick);
    zoff = gwy_brick_get_zoffset(brick);

    if (args->calibration) {
        xdata = g_memdup(gwy_data_line_get_data(args->calibration),
                         zres*sizeof(gdouble));
    }
    else {
        gint k;

        xdata = g_new(gdouble, zres);
        for (k = 0; k < zres; k++)
            xdata[k] = (k + 0.5)*zreal/zres + zoff;
    }
    ydata = g_new(gdouble, zres);

    col = args->col;
    row = args->row;
    w = args->width;
    h = args->height;
    gwy_debug("selected %dx%d at (%d,%d)", w, h, col, row);
    if (w < 4 || h < 4 || col < 0 || row < 0) {
        col = row = 0;
        w = xres;
        h = yres;
        gwy_debug("fixed to %dx%d at (%d,%d)", w, h, col, row);
    }

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(brick,ydata,zres,w,h,col,row,func)
#endif
    {
        GwyDataField *dfield = gwy_data_field_new(w, h, 1.0*w, 1.0*h, FALSE);
        gint kfrom = gwy_omp_chunk_start(zres), kto = gwy_omp_chunk_end(zres);
        gint k;

        for (k = kfrom; k < kto; k++) {
            gwy_brick_extract_plane(brick, dfield,
                                    col, row, k, w, h, -1, FALSE);
            ydata[k] = func(dfield);
        }
        g_object_unref(dfield);
    }

    gcmodel = gwy_graph_model_get_curve(gmodel, 0);
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, zres);
    g_object_set(gcmodel, "description", _(info->name), NULL);

    g_free(ydata);
    g_free(xdata);
}

static const PlaneStatQuantInfo*
get_quantity_info(PlaneStatQuantity quantity)
{
    const PlaneStatQuantInfo *info = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS(quantities); i++) {
        info = quantities + i;
        if (info->quantity == quantity)
            return info;
    }
    g_assert_not_reached();
    return NULL;
}

static void
plane_stat_reset(PlaneStatControls *controls)
{
    GwySelection *selection;

    selection = gwy_vector_layer_ensure_selection(controls->vlayer);
    gwy_selection_clear(selection);

    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->quantity),
                                  plane_stat_defaults.quantity);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->update),
                                 plane_stat_defaults.update);
}

static void
plane_stat_do(PlaneStatArgs *args, GwyContainer *data)
{
    GwyGraphModel *gmodel;

    gmodel = create_graph_model(args);
    update_graph_model_ordinate(args, gmodel);
    extract_summary_graph(args, gmodel);
    gwy_app_add_graph_or_curves(gmodel, data, &args->target_graph, 1);
    g_object_unref(gmodel);
}

static gdouble
get_plane_range(GwyDataField *dfield)
{
    gdouble min, max;

    gwy_data_field_get_min_max(dfield, &min, &max);
    return max - min;
}

static gdouble
get_plane_Sa(GwyDataField *dfield)
{
    gdouble Sa;

    gwy_data_field_get_stats(dfield, NULL, &Sa, NULL, NULL, NULL);

    return Sa;
}

static gdouble
get_plane_median(GwyDataField *dfield)
{
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);

    /* Reshuffle the data because the field is just a scratch buffer anyway. */
    return gwy_math_median(xres*yres, gwy_data_field_get_data(dfield));
}

static gdouble
get_plane_skew(GwyDataField *dfield)
{
    gdouble rms, skew;

    gwy_data_field_get_stats(dfield, NULL, NULL, &rms, &skew, NULL);

    return rms > 0.0 ? skew : 0.0;
}

static gdouble
get_plane_kurtosis(GwyDataField *dfield)
{
    gdouble rms, kurtosis;

    gwy_data_field_get_stats(dfield, NULL, NULL, &rms, NULL, &kurtosis);

    return rms > 0.0 ? kurtosis : 0.0;
}

static gdouble
get_plane_entropy_deficit(GwyDataField *dfield)
{
    gdouble rms, H;

    H = gwy_data_field_get_entropy(dfield);
    rms = gwy_data_field_get_rms(dfield);

    if (rms > 0.0 && H < 0.1*G_MAXDOUBLE)
        return (ENTROPY_NORMAL + log(rms) - H);
    return 0.0;
}

static const gchar col_key[]      = "/module/volume_plane_stat/col";
static const gchar height_key[]   = "/module/volume_plane_stat/height";
static const gchar level_key[]    = "/module/volume_plane_stat/level";
static const gchar quantity_key[] = "/module/volume_plane_stat/quantity";
static const gchar row_key[]      = "/module/volume_plane_stat/row";
static const gchar update_key[]   = "/module/volume_plane_stat/update";
static const gchar width_key[]    = "/module/volume_plane_stat/width";

static void
plane_stat_sanitize_args(PlaneStatArgs *args)
{
    /* Positions are validated against the brick. */
    args->quantity = CLAMP(args->quantity, 0, GWY_PLANE_STAT_NQUANTITIES-1);
    args->update = !!args->update;
    gwy_app_data_id_verify_graph(&args->target_graph);
}

static void
plane_stat_load_args(GwyContainer *container,
                     PlaneStatArgs *args)
{
    *args = plane_stat_defaults;

    gwy_container_gis_enum_by_name(container, quantity_key, &args->quantity);
    gwy_container_gis_int32_by_name(container, col_key, &args->col);
    gwy_container_gis_int32_by_name(container, row_key, &args->row);
    gwy_container_gis_int32_by_name(container, width_key, &args->width);
    gwy_container_gis_int32_by_name(container, height_key, &args->height);
    gwy_container_gis_int32_by_name(container, level_key, &args->level);
    gwy_container_gis_boolean_by_name(container, update_key, &args->update);
    args->target_graph = target_graph_id;
    plane_stat_sanitize_args(args);
}

static void
plane_stat_save_args(GwyContainer *container,
                     PlaneStatArgs *args)
{
    target_graph_id = args->target_graph;
    gwy_container_set_enum_by_name(container, quantity_key, args->quantity);
    gwy_container_set_int32_by_name(container, col_key, args->col);
    gwy_container_set_int32_by_name(container, row_key, args->row);
    gwy_container_set_int32_by_name(container, width_key, args->width);
    gwy_container_set_int32_by_name(container, height_key, args->height);
    gwy_container_set_int32_by_name(container, level_key, args->level);
    gwy_container_set_boolean_by_name(container, update_key, args->update);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
