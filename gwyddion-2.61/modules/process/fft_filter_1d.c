/*
 *  $Id: fft_filter_1d.c 23854 2021-06-14 16:36:54Z yeti-dn $
 *  Copyright (C) 2004-2021 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/inttrans.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/correct.h>
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

typedef enum {
    SUPPRESS_NULL         = 0,
    SUPPRESS_NEIGBOURHOOD = 1
} GwyFFTFilt1DSuppressType;

typedef enum {
    OUTPUT_MARKED   = 0,
    OUTPUT_UNMARKED = 1
} GwyFFTFilt1DOutputType;

enum {
    PARAM_SUPPRESS,
    PARAM_OUTPUT,
    PARAM_DIRECTION,
    PARAM_INTERPOLATION,
    PARAM_UPDATE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    GwyDataLine *modulus;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GwyGraphModel *gmodel;
    GwySelection *selection;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             fftf_1d             (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             dialog_response     (ModuleGUI *gui,
                                             gint response);
static void             preview             (gpointer user_data);
static void             graph_selected      (ModuleGUI *gui);
static void             ensure_modulus      (ModuleArgs *args);
static void             plot_modulus        (ModuleGUI *gui);
static GwyDataLine*     calculate_weights   (ModuleArgs *args,
                                             GwySelection *selection);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("FFT filtering"),
    "Petr Klapetek <petr@klapetek.cz>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, fft_filter_1d)

static gboolean
module_register(void)
{
    gwy_process_func_register("fft_filter_1d",
                              (GwyProcessFunc)&fftf_1d,
                              N_("/_Correct Data/1D _FFT Filtering..."),
                              GWY_STOCK_FFT_FILTER_1D,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("1D FFT Filtering"));
    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum outputs[] = {
        { N_("Marked"),    OUTPUT_MARKED,    },
        { N_("Unmarked"),  OUTPUT_UNMARKED,  },
    };
    static const GwyEnum suppresses[] = {
        { N_("Null"),      SUPPRESS_NULL,         },
        { N_("Suppress"),  SUPPRESS_NEIGBOURHOOD, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_SUPPRESS, "suppress", _("_Suppress type"),
                              suppresses, G_N_ELEMENTS(suppresses), SUPPRESS_NEIGBOURHOOD);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT, "output", _("_Filter type"),
                              outputs, G_N_ELEMENTS(outputs), OUTPUT_UNMARKED);
    gwy_param_def_add_enum(paramdef, PARAM_DIRECTION, "direction", NULL, GWY_TYPE_ORIENTATION,
                           GWY_ORIENTATION_HORIZONTAL);
    gwy_param_def_add_enum(paramdef, PARAM_INTERPOLATION, "interpolation", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_LINEAR);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, FALSE);
    return paramdef;
}

static void
fftf_1d(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyDialogOutcome outcome;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(args.field);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    args.modulus = NULL;
    args.params = gwy_params_new_from_settings(define_module_params());

    outcome = run_gui(&args, data, oldid);
    gwy_params_save_to_settings(args.params);
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        goto end;

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            0);
    gwy_app_set_data_field_title(data, newid, _("1D FFT Filtered Data"));
    gwy_app_channel_log_add_proc(data, oldid, newid);

end:
    GWY_OBJECT_UNREF(args.modulus);
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *align, *dataview;
    GwyGraph *graph;
    GwyDialog *dialog;
    GwyParamTable *table;
    GwyDialogOutcome outcome;
    ModuleGUI gui;
    GwySelection *selection;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_container_set_object_by_name(gui.data, "/1/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 1, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("1D FFT filter"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_CLEAR, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SMALL_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), dataview, FALSE, FALSE, 4);

    dataview = gwy_create_preview(gui.data, 1, PREVIEW_SMALL_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), dataview, FALSE, FALSE, 4);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    gui.gmodel = gwy_graph_model_new();
    graph = GWY_GRAPH(gwy_graph_new(gui.gmodel));
    gwy_graph_set_status(graph, GWY_GRAPH_STATUS_XSEL);
    gtk_widget_set_size_request(GTK_WIDGET(graph), -1, PREVIEW_HALF_SIZE);
    gwy_graph_enable_user_input(graph, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(graph), TRUE, TRUE, 4);

    selection = gui.selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(gwy_graph_get_area(graph)),
                                                             GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(selection, 20);
    g_signal_connect_swapped(selection, "changed", G_CALLBACK(graph_selected), &gui);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_DIRECTION);
    gwy_param_table_append_combo(table, PARAM_SUPPRESS);
    gwy_param_table_append_combo(table, PARAM_OUTPUT);
    gwy_param_table_append_combo(table, PARAM_INTERPOLATION);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(align), gwy_param_table_widget(table));

    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK, FALSE);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.gmodel);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table;
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_SUPPRESS) {
        GwyFFTFilt1DSuppressType suppres = gwy_params_get_enum(params, PARAM_SUPPRESS);
        GwyFFTFilt1DOutputType output = gwy_params_get_enum(params, PARAM_OUTPUT);
        if (suppres == SUPPRESS_NEIGBOURHOOD && output == OUTPUT_MARKED)
            gwy_param_table_set_enum(table, PARAM_OUTPUT, (output = OUTPUT_UNMARKED));
        gwy_param_table_set_sensitive(table, PARAM_OUTPUT, suppres == SUPPRESS_NULL);
    }
    if (id < 0 || id == PARAM_DIRECTION) {
        GWY_OBJECT_UNREF(gui->args->modulus);
        gwy_selection_clear(gui->selection);
        ensure_modulus(gui->args);
        plot_modulus(gui);
    }
    if (id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == GWY_RESPONSE_CLEAR)
        gwy_selection_clear(gui->selection);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyOrientation direction = gwy_params_get_enum(args->params, PARAM_DIRECTION);
    GwyInterpolationType interpolation = gwy_params_get_enum(args->params, PARAM_INTERPOLATION);
    GwyDataLine *weights;

    weights = calculate_weights(args, gui->selection);
    gwy_data_field_fft_filter_1d(args->field, args->result, weights, direction, interpolation);
    gwy_data_field_data_changed(args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
graph_selected(ModuleGUI *gui)
{
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK,
                                      gwy_selection_get_data(gui->selection, NULL));
    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
ensure_modulus(ModuleArgs *args)
{
    GwyOrientation direction = gwy_params_get_enum(args->params, PARAM_DIRECTION);
    GwyDataLine *modulus;
    gint i, res;
    gdouble m;
    gdouble *d;

    if (args->modulus)
        return;

    modulus = args->modulus = gwy_data_line_new(1, 1.0, FALSE);
    /* There is no interpolation.  Pass anything. */
    gwy_data_field_psdf(args->field, modulus, direction, GWY_INTERPOLATION_LINEAR, GWY_WINDOWING_RECT, -1);
    m = gwy_data_line_get_max(modulus);
    if (!m)
        m = 1.0;
    d = gwy_data_line_get_data(modulus);
    res = gwy_data_line_get_res(modulus);
    for (i = 0; i < res; i++)
        d[i] = (d[i] > 0.0 ? sqrt(d[i]/m) : 0.0);
}

static void
plot_modulus(ModuleGUI *gui)
{
    GwyGraphCurveModel *cmodel;
    GwyDataLine *modulus = gui->args->modulus;

    gwy_graph_model_remove_all_curves(gui->gmodel);

    cmodel = gwy_graph_curve_model_new();
    gwy_graph_curve_model_set_data_from_dataline(cmodel, modulus, 0, 0);
    g_object_set(cmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "description", _("FFT Modulus"),
                 NULL);
    g_object_set(gui->gmodel,
                 "si-unit-x", gwy_data_line_get_si_unit_x(modulus),
                 "axis-label-bottom", "k",
                 "axis-label-left", "",
                 NULL);

    gwy_graph_model_add_curve(gui->gmodel, cmodel);
    g_object_unref(cmodel);
}

static GwyDataLine*
calculate_weights(ModuleArgs *args, GwySelection *selection)
{
    GwyFFTFilt1DSuppressType suppres = gwy_params_get_enum(args->params, PARAM_SUPPRESS);
    GwyFFTFilt1DOutputType output = gwy_params_get_enum(args->params, PARAM_OUTPUT);
    GwyDataLine *weights, *modulus = args->modulus;
    gint res, k, nsel;
    gdouble sel[2];
    gint fill_from, fill_to;

    res = gwy_data_line_get_res(modulus);
    weights = gwy_data_line_new_alike(modulus, TRUE);

    nsel = gwy_selection_get_data(selection, NULL);
    for (k = 0; k < nsel; k++) {
        gwy_selection_get_object(selection, k, sel);
        if (sel[1] < sel[0])
            GWY_SWAP(gdouble, sel[0], sel[1]);
        fill_from = MAX(0, gwy_data_line_rtoi(weights, sel[0]));
        fill_from = MIN(res, fill_from);
        fill_to = MIN(res, gwy_data_line_rtoi(weights, sel[1]));
        gwy_data_line_part_fill(weights, fill_from, fill_to, 1.0);
    }

    /* For Suppress, interpolate PSDF linearly between endpoints.  Since we pass weights to the filter, not PSDF
     * itself, we have to divide to get the weight. */
    if (suppres == SUPPRESS_NEIGBOURHOOD) {
        GwyDataLine *buf = gwy_data_line_duplicate(modulus);
        gdouble *b, *m, *w;

        gwy_data_line_correct_laplace(buf, weights);
        b = gwy_data_line_get_data(buf);
        m = gwy_data_line_get_data(modulus);
        w = gwy_data_line_get_data(weights);
        for (k = 0; k < res; k++)
            w[k] = (m[k] > 0.0 ? fmin(b[k]/m[k], 1.0) : 0.0);
        g_object_unref(buf);
    }
    else if (output == OUTPUT_UNMARKED) {
        gdouble *w = gwy_data_line_get_data(weights);
        for (k = 0; k < res; k++)
            w[k] = 1.0 - w[k];
    }

    return weights;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
