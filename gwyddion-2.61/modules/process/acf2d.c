/*
 *  $Id: acf2d.c 24669 2022-03-14 16:51:07Z yeti-dn $
 *  Copyright (C) 2007-2022 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/level.h>
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    MIN_RESOLUTION = 4,
    MAX_RESOLUTION = 16384,
};

/* Later update type implies all above. */
typedef enum {
    UPDATE_NOTHING = 0,
    UPDATE_GRAPHS,
    UPDATE_ZOOMED,
    UPDATE_ACF_MASK,  /* == recalculate params */
    UPDATE_ACF,
} UpdateWhat;

typedef enum {
    LEVELLING_NONE       = 0,
    LEVELLING_MEAN_VALUE = 1,
    LEVELLING_MEAN_PLANE = 2,
    LEVELLING_NTYPES,
} LevellingType;

enum {
    PARAM_ZOOM,
    PARAM_LEVEL,
    PARAM_MASKING,
    PARAM_CREATE_IMAGE,
    PARAM_ZOOMED_IMAGE,

    PARAM_FIXRES,
    PARAM_RESOLUTION,
    PARAM_THICKNESS,
    PARAM_SEPARATE,
    PARAM_INTERPOLATION,
    PARAM_TARGET_GRAPH,

    PARAM_THRESHOLD,
    PARAM_MASK_COLOR,
    PARAM_REPORT_STYLE,

    WIDGET_RESULTS,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *acf;
    GwyDataField *acfmask;
    GwySelection *selection;
    GwyGraphModel *gmodel;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GtkWidget *dataview;
    GwyParamTable *table_acf;
    GwyParamTable *table_graph;
    GwyParamTable *table_params;
    GwyDataLine *line;
    GwyContainer *data;
    GwyResults *results;
    UpdateWhat update;
} ModuleGUI;

static gboolean         module_register               (void);
static GwyParamDef*     define_module_params          (void);
static void             acf2d                         (GwyContainer *data,
                                                       GwyRunType runtype);
static void             execute                       (ModuleArgs *args);
static GwyDialogOutcome run_gui                       (ModuleArgs *args,
                                                       GwyContainer *data,
                                                       gint id);
static GwyResults*      create_results                (ModuleArgs *args,
                                                       GwyContainer *data,
                                                       gint id);
static void             param_changed                 (ModuleGUI *gui,
                                                       gint id);
static void             dialog_response               (ModuleGUI *gui,
                                                       gint response);
static void             preview                       (gpointer user_data);
static void             calculate_zoomed_fields       (ModuleGUI *gui);
static GwyDataField*    cut_field_to_zoom             (GwyDataField *field,
                                                       gint zoom);
static void             selection_changed             (ModuleGUI *gui,
                                                       gint hint);
static void             update_sensitivity            (ModuleGUI *gui);
static void             update_curve                  (ModuleGUI *gui,
                                                       gint i);
static void             add_line_selection_from_points(GwyContainer *data,
                                                       GwyDataField *field,
                                                       gint id,
                                                       GwySelection *pointsel);
static void             create_acf_mask               (ModuleArgs *args);
static gboolean         find_decays                   (GwyDataField *mask,
                                                       gdouble *r1,
                                                       gdouble *r2,
                                                       gdouble *phi1,
                                                       gdouble *phi2);

static const gchar *result_values[] = { "Sal", "tau2", "phi1", "phi2", "Str" };

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates and analyzes two-dimensional autocorrelation function."),
    "Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti)",
    "2007",
};

GWY_MODULE_QUERY2(module_info, acf2d)

static gboolean
module_register(void)
{
    gwy_process_func_register("acf2d",
                              (GwyProcessFunc)&acf2d,
                              N_("/_Statistics/2D Auto_correlation..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Calculate 2D autocorrelation function"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum levels[] = {
        { N_("data-adjustment|None"),   LEVELLING_NONE,       },
        { N_("Mean value subtraction"), LEVELLING_MEAN_VALUE, },
        { N_("Plane leveling"),         LEVELLING_MEAN_PLANE, },
    };
    static GwyEnum zooms[5];
    static GwyParamDef *paramdef = NULL;
    guint i;

    if (paramdef)
        return paramdef;

    for (i = 0; i < G_N_ELEMENTS(zooms); i++) {
        zooms[i].value = 1u << i;
        zooms[i].name = g_strdup_printf("%u×", zooms[i].value);
    }

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_ZOOM, "zoom", _("Zoom"), zooms, G_N_ELEMENTS(zooms), 1);
    gwy_param_def_add_gwyenum(paramdef, PARAM_LEVEL, "level", _("Data adjustment"),
                              levels, G_N_ELEMENTS(levels), LEVELLING_MEAN_VALUE);
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_boolean(paramdef, PARAM_CREATE_IMAGE, "create_image", _("Create ACF image"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_ZOOMED_IMAGE, "zoomed_image", _("Only zoomed part"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_FIXRES, "fixres", _("_Fixed resolution"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_RESOLUTION, "resolution", _("_Fixed resolution"),
                          MIN_RESOLUTION, MAX_RESOLUTION, 120);
    gwy_param_def_add_int(paramdef, PARAM_THICKNESS, "thickness", _("_Thickness"), 1, 128, 1);
    gwy_param_def_add_boolean(paramdef, PARAM_SEPARATE, "separate", _("_Separate curves"), FALSE);
    gwy_param_def_add_enum(paramdef, PARAM_INTERPOLATION, "interpolation", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_LINEAR);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD, "threshold", _("_Threshold"), 0.0, 1.0, 0.2);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", _("Save Parameters"),
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON);
    return paramdef;
}

static void
acf2d(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(args.field);

    args.params = gwy_params_new_from_settings(define_module_params());
    args.acf = gwy_data_field_new(17, 17, 1.0, 1.0, TRUE);
    args.acfmask = gwy_data_field_new(17, 17, 1.0, 1.0, TRUE);
    // We need to set the units of args->gmodel immediately for target graph filtering.
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(args.acf), gwy_data_field_get_si_unit_xy(args.field));
    gwy_si_unit_power(gwy_data_field_get_si_unit_z(args.field), 2, gwy_data_field_get_si_unit_z(args.acf));

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        execute(&args);
        create_acf_mask(&args);
    }

    /* Is it reasonable to simply do nothing in non-interactive mode when settings say to not create the ACF image? */
    if (gwy_params_get_boolean(args.params, PARAM_CREATE_IMAGE)) {
        gboolean zoomed_image = gwy_params_get_boolean(args.params, PARAM_ZOOMED_IMAGE);
        guint zoom = (zoomed_image ? gwy_params_get_enum(args.params, PARAM_ZOOM) : 1);
        GwyDataField *zoomed, *zoomedmask;

        zoomed = cut_field_to_zoom(args.acf, zoom);
        zoomedmask = cut_field_to_zoom(args.acfmask, zoom);
        newid = gwy_app_data_browser_add_data_field(zoomed, data, TRUE);
        gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), zoomedmask);
        g_object_unref(zoomed);
        g_object_unref(zoomedmask);

        add_line_selection_from_points(data, zoomed, newid, args.selection);
        gwy_app_set_data_field_title(data, newid, _("2D ACF"));
        gwy_app_channel_log_add_proc(data, oldid, newid);

        gwy_app_sync_data_items(data, data, oldid, newid, FALSE, GWY_DATA_ITEM_MASK_COLOR, 0);
    }
    if (args.gmodel && gwy_graph_model_get_n_curves(args.gmodel)) {
        GwyGraphCurveModel *gcmodel;
        GwyGraphModel *gmodel;
        GwyAppDataId target_graph_id;
        gint i, n;
        gchar *s;

        if (gwy_params_get_boolean(args.params, PARAM_SEPARATE)) {
            n = gwy_graph_model_get_n_curves(args.gmodel);
            for (i = 0; i < n; i++) {
                gmodel = gwy_graph_model_new_alike(args.gmodel);
                gcmodel = gwy_graph_model_get_curve(args.gmodel, i);
                gcmodel = gwy_graph_curve_model_duplicate(gcmodel);
                gwy_graph_model_add_curve(gmodel, gcmodel);
                g_object_unref(gcmodel);
                g_object_get(gcmodel, "description", &s, NULL);
                g_object_set(gmodel, "title", s, NULL);
                g_free(s);
                gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);
                g_object_unref(gmodel);
            }
        }
        else {
            target_graph_id = gwy_params_get_data_id(args.params, PARAM_TARGET_GRAPH);
            gwy_app_add_graph_or_curves(args.gmodel, data, &target_graph_id, 1);
        }
    }

end:
    GWY_OBJECT_UNREF(args.selection);
    GWY_OBJECT_UNREF(args.gmodel);
    g_object_unref(args.acf);
    g_object_unref(args.acfmask);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    ModuleGUI gui;
    GtkWidget *hbox, *vbox, *notebook;
    GwyParamTable *table;
    GwyDialog *dialog;
    GwyGraph *graph;
    GwyDialogOutcome outcome;

    gwy_clear(&gui, 1);
    gui.args = args;
    args->gmodel = gwy_graph_model_new();
    gui.data = gwy_container_new();
    gui.results = create_results(args, data, id);
    gui.line = gwy_data_line_new(1, 1.0, FALSE);
    calculate_zoomed_fields(&gui);

    gui.dialog = gwy_dialog_new(_("Autocorrelation Function"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_CLEAR, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    /***** ACF preview *****/
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    args->selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(gui.dataview), 0, "Point", 12, TRUE);
    g_object_ref(args->selection);
    g_object_set(gwy_data_view_get_top_layer(GWY_DATA_VIEW(gui.dataview)), "draw-as-vector", TRUE, NULL);
    g_signal_connect_swapped(args->selection, "changed", G_CALLBACK(selection_changed), &gui);

    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.dataview), FALSE);

    /***** Graph *****/
    vbox = gwy_vbox_new(0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 4);

    gwy_graph_model_set_units_from_data_field(args->gmodel, args->acf, 1, 0, 0, 1);
    g_object_set(args->gmodel,
                 "title", _("ACF Section"),
                 "axis-label-bottom", "τ",
                 "axis-label-left", "G",
                 NULL);

    graph = GWY_GRAPH(gwy_graph_new(args->gmodel));
    gtk_widget_set_size_request(GTK_WIDGET(graph), 320, 120);
    gwy_graph_set_axis_visible(graph, GTK_POS_LEFT, FALSE);
    gwy_graph_set_axis_visible(graph, GTK_POS_RIGHT, FALSE);
    gwy_graph_set_axis_visible(graph, GTK_POS_TOP, FALSE);
    gwy_graph_set_axis_visible(graph, GTK_POS_BOTTOM, FALSE);
    gwy_graph_enable_user_input(graph, FALSE);
    gwy_graph_area_enable_user_input(GWY_GRAPH_AREA(gwy_graph_get_area(graph)), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(graph), TRUE, TRUE, 0);

    /***** Notebook *****/
    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook, FALSE, FALSE, 0);

    table = gui.table_acf = gwy_param_table_new(args->params);
    gwy_param_table_append_radio_row(table, PARAM_ZOOM);
    gwy_param_table_append_radio(table, PARAM_LEVEL);
    if (args->mask)
        gwy_param_table_append_combo(table, PARAM_MASKING);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_CREATE_IMAGE);
    gwy_param_table_append_checkbox(table, PARAM_ZOOMED_IMAGE);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), gwy_param_table_widget(table), gtk_label_new("ACF"));
    gwy_dialog_add_param_table(dialog, table);

    table = gui.table_graph = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_RESOLUTION);
    gwy_param_table_slider_set_mapping(table, PARAM_RESOLUTION, GWY_SCALE_MAPPING_SQRT);
    gwy_param_table_add_enabler(table, PARAM_FIXRES, PARAM_RESOLUTION);
    gwy_param_table_append_slider(table, PARAM_THICKNESS);
    gwy_param_table_slider_set_mapping(table, PARAM_THICKNESS, GWY_SCALE_MAPPING_SQRT);
    gwy_param_table_append_checkbox(table, PARAM_SEPARATE);
    gwy_param_table_append_combo(table, PARAM_INTERPOLATION);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), gwy_param_table_widget(table), gtk_label_new("Graph"));
    gwy_dialog_add_param_table(dialog, table);

    table = gui.table_params = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_THRESHOLD);
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);
    gwy_param_table_append_resultsv(table, WIDGET_RESULTS, gui.results, result_values, G_N_ELEMENTS(result_values));
    gwy_param_table_append_report(table, PARAM_REPORT_STYLE);
    gwy_param_table_report_set_results(table, PARAM_REPORT_STYLE, gui.results);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), gwy_param_table_widget(table), gtk_label_new("Parameters"));
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(gui.table_acf, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_graph, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_params, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.results);
    g_object_unref(gui.line);

    return outcome;
}

static GwyResults*
create_results(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyResults *results = gwy_results_new();

    gwy_results_add_header(results, N_("Autocorrelation Function"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "image", N_("Image"));
    gwy_results_add_value_yesno(results, "masking", N_("Mask in use"));
    gwy_results_add_separator(results);

    gwy_results_add_value(results, "Sal", N_("Autocorrelation length"),
                          "power-x", 1, "symbol", "S<sub>al</sub>", NULL);
    gwy_results_add_value_x(results, "tau2", N_("Slowest decay length"));
    gwy_results_add_value_angle(results, "phi1", N_("Fastest decay direction"));
    gwy_results_add_value_angle(results, "phi2", N_("Slowest decay direction"));
    gwy_results_add_value(results, "Str", N_("Texture aspect ratio"),
                          "symbol", "S<sub>tr</sub>", NULL);

    gwy_results_set_unit(results, "x", gwy_data_field_get_si_unit_xy(args->field));
    gwy_results_set_unit(results, "y", gwy_data_field_get_si_unit_xy(args->field));
    gwy_results_set_unit(results, "z", gwy_data_field_get_si_unit_z(args->field));
    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_channel(results, "image", data, id);

    return results;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_MASKING || id == PARAM_LEVEL)
        gui->update = MAX(gui->update, UPDATE_ACF);
    if (id < 0 || id == PARAM_THRESHOLD)
        gui->update = MAX(gui->update, UPDATE_ACF_MASK);
    if (id < 0 || id == PARAM_ZOOM)
        gui->update = MAX(gui->update, UPDATE_ZOOMED);
    if (id < 0 || id == PARAM_RESOLUTION || id == PARAM_FIXRES || id == PARAM_INTERPOLATION || id == PARAM_THICKNESS)
        gui->update = MAX(gui->update, UPDATE_GRAPHS);

    if (id < 0 || id == PARAM_SEPARATE) {
        gwy_param_table_set_sensitive(gui->table_graph, PARAM_TARGET_GRAPH,
                                      gwy_params_get_boolean(params, PARAM_SEPARATE));
    }
    if (id < 0 || id == PARAM_CREATE_IMAGE) {
        gwy_param_table_set_sensitive(gui->table_acf, PARAM_ZOOMED_IMAGE,
                                      gwy_params_get_boolean(params, PARAM_CREATE_IMAGE));
        update_sensitivity(gui);
    }

    if (gui->update)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
selection_changed(ModuleGUI *gui, gint hint)
{
    ModuleArgs *args = gui->args;
    gint i, n;

    n = gwy_selection_get_data(args->selection, NULL);
    if (hint < 0) {
        gwy_graph_model_remove_all_curves(args->gmodel);
        for (i = 0; i < n; i++)
            update_curve(gui, i);
    }
    else
        update_curve(gui, hint);
    update_sensitivity(gui);
}

static void
update_sensitivity(ModuleGUI *gui)
{
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK,
                                      gwy_params_get_boolean(gui->args->params, PARAM_CREATE_IMAGE)
                                      || gwy_selection_get_data(gui->args->selection, NULL));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == GWY_RESPONSE_CLEAR)
        gwy_selection_clear(gui->args->selection);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (gui->update >= UPDATE_ACF)
        execute(args);
    if (gui->update >= UPDATE_ACF_MASK) {
        gboolean is_masking = (gwy_params_get_masking(params, PARAM_MASKING, NULL) != GWY_MASK_IGNORE);
        gdouble rmin, rmax, phimin, phimax;

        create_acf_mask(args);
        if (find_decays(args->acfmask, &rmin, &rmax, &phimin, &phimax)) {
            gwy_results_fill_values(gui->results,
                                    "Sal", rmin, "tau2", rmax, "phi1", phimin, "phi2", phimax, "Str", rmin/rmax,
                                    NULL);
        }
        else
            gwy_results_set_nav(gui->results, G_N_ELEMENTS(result_values), result_values);
        gwy_results_fill_values(gui->results, "masking", is_masking, NULL);
        gwy_param_table_results_fill(gui->table_params, WIDGET_RESULTS);
    }
    if (gui->update >= UPDATE_ZOOMED) {
        GwyDataField *acf;
        gdouble xoff, yoff;

        acf = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0));
        xoff = gwy_data_field_get_xoffset(acf);
        yoff = gwy_data_field_get_yoffset(acf);
        calculate_zoomed_fields(gui);
        gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SIZE);
        acf = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0));
        xoff -= gwy_data_field_get_xoffset(acf);
        yoff -= gwy_data_field_get_yoffset(acf);
        if (xoff || yoff) {
            gwy_selection_move(args->selection, xoff, yoff);
            gui->update = UPDATE_NOTHING;
        }
    }
    if (gui->update >= UPDATE_GRAPHS)
        selection_changed(gui, -1);

    gui->update = UPDATE_NOTHING;
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
update_curve(ModuleGUI *gui, gint i)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gboolean fixres = gwy_params_get_boolean(params, PARAM_FIXRES);
    gint resolution = gwy_params_get_int(params, PARAM_RESOLUTION);
    gint thickness = gwy_params_get_int(params, PARAM_THICKNESS);
    GwyInterpolationType interpolation = gwy_params_get_enum(args->params, PARAM_INTERPOLATION);
    GwyDataField *zoomedacf, *acf = args->acf;
    GwyGraphCurveModel *gcmodel;
    gdouble xy[4], xofffull, yofffull;
    gint xl0, yl0, xl1, yl1;
    gint n, lineres;
    gchar *desc;

    if (!gwy_selection_get_object(args->selection, i, xy)) {
        g_return_if_reached();
    }

    zoomedacf = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0));
    xy[0] += gwy_data_field_get_xoffset(zoomedacf);
    xy[1] += gwy_data_field_get_yoffset(zoomedacf);

    xl0 = gwy_data_field_get_xres(acf)/2;
    yl0 = gwy_data_field_get_yres(acf)/2;

    xofffull = gwy_data_field_get_xoffset(acf);
    yofffull = gwy_data_field_get_yoffset(acf);
    xl1 = floor(gwy_data_field_rtoj(acf, xy[0] - xofffull));
    yl1 = floor(gwy_data_field_rtoi(acf, xy[1] - yofffull));

    if (!fixres) {
        lineres = GWY_ROUND(hypot(abs(xl0 - xl1) + 1, abs(yl0 - yl1) + 1));
        lineres = MAX(lineres, MIN_RESOLUTION);
    }
    else
        lineres = resolution;

    gwy_data_field_get_profile(acf, gui->line, xl0, yl0, xl1, yl1, lineres, thickness, interpolation);

    n = gwy_graph_model_get_n_curves(args->gmodel);
    if (i < n)
        gcmodel = gwy_graph_model_get_curve(args->gmodel, i);
    else {
        gcmodel = gwy_graph_curve_model_new();
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(i),
                     NULL);
        gwy_graph_model_add_curve(args->gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

    gwy_graph_curve_model_set_data_from_dataline(gcmodel, gui->line, 0, 0);
    desc = g_strdup_printf(_("ACF %.0f°"), 180.0/G_PI*atan2(-xy[1], xy[0]));
    g_object_set(gcmodel, "description", desc, NULL);
    g_free(desc);
}

static void
calculate_zoomed_fields(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    guint zoom = gwy_params_get_enum(args->params, PARAM_ZOOM);
    GwyDataField *zoomed;

    zoomed = cut_field_to_zoom(args->acf, zoom);
    gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), zoomed);
    gwy_data_field_data_changed(zoomed);
    g_object_unref(zoomed);

    zoomed = cut_field_to_zoom(args->acfmask, zoom);
    gwy_container_set_object(gui->data, gwy_app_get_mask_key_for_id(0), zoomed);
    gwy_data_field_data_changed(zoomed);
    g_object_unref(zoomed);
}

static GwyDataField*
cut_field_to_zoom(GwyDataField *field, gint zoom)
{
    GwyDataField *zoomed;
    guint xres, yres, width, height;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    width = (xres/zoom) | 1;
    height = (yres/zoom) | 1;
    if (width < 17)
        width = MAX(width, MIN(17, xres));
    if (height < 17)
        height = MAX(height, MIN(17, yres));

    if (width >= xres && height >= yres)
        return g_object_ref(field);

    zoomed = gwy_data_field_area_extract(field, (xres - width)/2, (yres - height)/2, width, height);
    gwy_data_field_set_xoffset(zoomed, -0.5*gwy_data_field_get_xreal(zoomed));
    gwy_data_field_set_yoffset(zoomed, -0.5*gwy_data_field_get_yreal(zoomed));

    return zoomed;
}

/* Convert points to lines from the origin, which is assumed to be in the centre. */
static void
add_line_selection_from_points(GwyContainer *data,
                               GwyDataField *field,
                               gint id,
                               GwySelection *pointsel)
{
    GwySelection *linesel;
    gint nsel, i;
    gdouble *seldata;
    gdouble xreal, yreal;
    gchar *key;

    if (!pointsel || !(nsel = gwy_selection_get_data(pointsel, NULL)))
        return;

    linesel = g_object_new(g_type_from_name("GwySelectionLine"), NULL);
    g_return_if_fail(linesel);
    gwy_selection_set_max_objects(linesel, 1024);
    seldata = g_new(gdouble, 4*nsel);
    xreal = gwy_data_field_get_xreal(field);
    yreal = gwy_data_field_get_yreal(field);

    for (i = 0; i < nsel; i++) {
        seldata[4*i + 0] = 0.5*xreal;
        seldata[4*i + 1] = 0.5*yreal;
        gwy_selection_get_object(pointsel, i, seldata + 4*i + 2);
    }

    gwy_selection_set_data(linesel, nsel, seldata);
    g_free(seldata);

    key = g_strdup_printf("/%d/select/line", id);
    gwy_container_set_object_by_name(data, key, linesel);
    g_object_unref(linesel);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    LevellingType level = gwy_params_get_enum(params, PARAM_LEVEL);
    GwyDataField *mask = args->mask, *field = args->field, *acf = args->acf, *acfmask = args->acfmask, *field_for_acf;
    GwyMaskingType masking = gwy_params_get_masking(params, PARAM_MASKING, &mask);
    gint xres = gwy_data_field_get_xres(field);
    gint yres = gwy_data_field_get_yres(field);
    gdouble a, bx, by;

    /* Reuse acfmask for the field because we do not need it for gwy_data_field_area_2dacf_mask().  Reuse acf for the
     * modified mask because we discard it immediately. */
    if (level == LEVELLING_MEAN_VALUE) {
        gwy_data_field_resample(acfmask, xres, yres, GWY_INTERPOLATION_NONE);
        gwy_data_field_copy(field, acfmask, TRUE);
        a = gwy_data_field_area_get_avg_mask(acfmask, mask, masking, 0, 0, xres, yres);
        gwy_data_field_add(acfmask, -a);
        field_for_acf = acfmask;
    }
    else if (level == LEVELLING_MEAN_PLANE) {
        gwy_data_field_resample(acfmask, xres, yres, GWY_INTERPOLATION_NONE);
        gwy_data_field_copy(field, acfmask, TRUE);
        if (mask && masking == GWY_MASK_EXCLUDE) {
            gwy_data_field_resample(acf, xres, yres, GWY_INTERPOLATION_NONE);
            gwy_data_field_copy(mask, acf, FALSE);
            gwy_data_field_area_fit_plane(acfmask, acf, 0, 0, xres, yres, &a, &bx, &by);
        }
        else if (mask && masking == GWY_MASK_INCLUDE)
            gwy_data_field_area_fit_plane(acfmask, mask, 0, 0, xres, yres, &a, &bx, &by);
        else
            gwy_data_field_fit_plane(acfmask, &a, &bx, &by);

        gwy_data_field_plane_level(acfmask, a, bx, by);
        field_for_acf = acfmask;
    }
    else
        field_for_acf = field;

    gwy_data_field_area_2dacf_mask(field_for_acf, acf, mask, masking, 0, 0, xres, yres, 0, 0, NULL);
    create_acf_mask(args);
}

static void
create_acf_mask(ModuleArgs *args)
{
    GwyDataField *acf = args->acf;
    GwyDataField *acfmask = args->acfmask;
    gdouble threshold = gwy_params_get_double(args->params, PARAM_THRESHOLD);
    gint xres = gwy_data_field_get_xres(acf);
    gint yres = gwy_data_field_get_yres(acf);
    gdouble sigma2;

    sigma2 = gwy_data_field_get_val(acf, xres/2, yres/2);
    gwy_data_field_resample(acfmask, xres, yres, GWY_INTERPOLATION_NONE);
    gwy_data_field_copy(acf, acfmask, TRUE);
    gwy_data_field_threshold(acfmask, threshold*sigma2, 0.0, 1.0);
    gwy_data_field_grains_extract_grain(acfmask, xres/2, yres/2);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(acfmask), NULL);
}

/* XXX: This is not very efficient and probably does not do anything useful when the grain is highly non-convex. But
 * what exactly should be done in such case is anyone's guess... */
static gboolean
find_decays(GwyDataField *mask,
            gdouble *r1, gdouble *r2, gdouble *phi1, gdouble *phi2)
{
    const gdouble *data = gwy_data_field_get_data_const(mask);
    guint xres = gwy_data_field_get_xres(mask);
    guint yres = gwy_data_field_get_yres(mask);
    gdouble dx = gwy_data_field_get_dx(mask);
    gdouble dy = gwy_data_field_get_dy(mask);
    gdouble phimin = 0.0, r2min = G_MAXDOUBLE;
    gdouble phimax = 0.0, r2max = 0.0;
    gdouble xc, yc;
    guint i, j, k;

    xc = 0.5*dx*xres;
    yc = 0.5*dy*yres;
    for (i = 0; i <= yres; i++) {
        k = i*xres;
        for (j = 0; j <= xres; j++, k++) {
            /*
             * 1 2
             * 3 4
             */
            guint g0 = i && j && data[k - xres - 1] > 0.0;
            guint g1 = i && j < xres && data[k - xres] > 0.0;
            guint g2 = i < yres && j && data[k - 1] > 0.0;
            guint g3 = i < yres && j < xres && data[k] > 0.0;
            guint g = g0 | (g1 << 1) | (g2 << 2) | (g3 << 3);

            if (g != 0 && g != 15) {
                gdouble x = j*dx - xc, y = i*dy - yc;
                gdouble rr = x*x + y*y;

                if (rr < r2min) {
                    r2min = rr;
                    phimin = atan2(-y, x);
                }
                if (rr > r2max) {
                    r2max = rr;
                    phimax = atan2(-y, x);
                }
            }
        }
    }

    if (r2min == G_MAXDOUBLE) {
        *r1 = *r2 = *phi1 = *phi2 = 0.0;
        return FALSE;
    }

    *r1 = sqrt(r2min);
    *r2 = sqrt(r2max);

    *phi1 = gwy_canonicalize_angle(phimin, FALSE, FALSE);
    *phi2 = gwy_canonicalize_angle(phimax, FALSE, FALSE);

    return TRUE;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
