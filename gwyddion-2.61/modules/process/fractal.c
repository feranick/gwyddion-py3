/*
 *  $Id: fractal.c 24450 2021-11-01 14:32:53Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwyresults.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/fractals.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

typedef enum {
    FRACTAL_PARTITIONING  = 0,
    FRACTAL_CUBECOUNTING  = 1,
    FRACTAL_TRIANGULATION = 2,
    FRACTAL_PSDF          = 3,
    FRACTAL_NMETHODS      = 4
} FractalMethodType;

typedef void (*FractalMethodFunc)(GwyDataField *data_field,
                                  GwyDataLine *xresult,
                                  GwyDataLine *yresult,
                                  GwyInterpolationType interpolation);
typedef gdouble (*FractalDimFunc)(GwyDataLine *xresult,
                                  GwyDataLine *yresult,
                                  gdouble *a,
                                  gdouble *b);

enum {
    PARAM_METHOD,
    PARAM_INTERP,
    PARAM_TARGET_GRAPH,
    PARAM_REPORT_STYLE,

    LABEL_FROM,
    LABEL_TO,
    WIDGET_RESULTS,
};

typedef struct {
    const gchar *id;
    const gchar *name;
    const gchar *abscissa;
    const gchar *ordinate;
    FractalMethodFunc calculate;
    FractalDimFunc getdim;
} FractalMethodInfo;

typedef struct {
    gdouble fromto[2];
    gdouble dim;
} FractalMethodComp;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyGraphModel *gmodel;
    FractalMethodComp comp[FRACTAL_NMETHODS];
    GwyDataLine *xline;
    GwyDataLine *yline;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwySelection *selection;
    GwyResults *results;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             fractal             (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static GwyResults*      create_results      (GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static void             graph_selected      (ModuleGUI *gui,
                                             gint hint,
                                             GwySelection *selection);
static gboolean         remove_datapoints   (GwyDataLine *xline,
                                             GwyDataLine *yline,
                                             GwyDataLine *newxline,
                                             GwyDataLine *newyline,
                                             const gdouble *fromto);

/* Indexed directly by FractalMethodType.  Change GUI order using gwy_enum_fill_from_struct() if necessary. */
static const FractalMethodInfo methods[FRACTAL_NMETHODS] = {
    {
        "partitioning", N_("Partitioning"), "log h", "log S",
        gwy_data_field_fractal_partitioning, gwy_data_field_fractal_partitioning_dim,
    },
    {
        "cubecounting", N_("Cube counting"), "log h", "log N",
        gwy_data_field_fractal_cubecounting, gwy_data_field_fractal_cubecounting_dim,
    },
    {
        "triangulation", N_("Triangulation"), "log h", "log A",
        gwy_data_field_fractal_triangulation, gwy_data_field_fractal_triangulation_dim,
    },
    {
        "psdf", N_("Power spectrum"), "log k", "log W",
        gwy_data_field_fractal_psdf, gwy_data_field_fractal_psdf_dim,
    },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates fractal dimension using several methods (partitioning, box counting, triangulation, power "
       "spectrum)."),
    "Jindřich Bilek & Petr Klapetek <klapetek@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek & Jindřich Bílek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, fractal)

static gboolean
module_register(void)
{
    gwy_process_func_register("fractal",
                              (GwyProcessFunc)&fractal,
                              N_("/_Statistics/_Fractal Dimension..."),
                              GWY_STOCK_FRACTAL_MEASURE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Calculate fractal dimension"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyEnum *methodenum = NULL;
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    methodenum = gwy_enum_fill_from_struct(NULL, FRACTAL_NMETHODS, methods, sizeof(FractalMethodInfo),
                                           G_STRUCT_OFFSET(FractalMethodInfo, name), -1);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "out", _("_Method"),
                              methodenum, FRACTAL_NMETHODS, FRACTAL_PARTITIONING);
    gwy_param_def_add_enum(paramdef, PARAM_INTERP, "interp", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_LINEAR);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", _("Save Fractal Dimension"),
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_TABSEP);
    return paramdef;
}

static void
fractal(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    GwyAppDataId target_graph_id;
    ModuleArgs args;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    args.params = gwy_params_new_from_settings(define_module_params());
    args.gmodel = gwy_graph_model_new();
    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    target_graph_id = gwy_params_get_data_id(args.params, PARAM_TARGET_GRAPH);
    gwy_app_add_graph_or_curves(args.gmodel, data, &target_graph_id, 2);

end:
    GWY_OBJECT_UNREF(args.xline);
    GWY_OBJECT_UNREF(args.yline);
    g_object_unref(args.params);
    g_object_unref(args.gmodel);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *graph;
    GwyDialog *dialog;
    GwyParamTable *table;
    GwyGraphCurveModel *gcmodel;
    ModuleGUI gui;
    GwyDialogOutcome outcome;
    const gchar *values[FRACTAL_NMETHODS];
    guint i;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.results = create_results(data, id);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_POINTS, NULL);
    gwy_graph_model_add_curve(args->gmodel, gcmodel);
    g_object_unref(gcmodel);

    for (i = 0; i < FRACTAL_NMETHODS; i++)
        values[i] = methods[i].id;

    gui.dialog = gwy_dialog_new(_("Fractal Dimension"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_METHOD);
    gwy_param_table_append_combo(table, PARAM_INTERP);
    gwy_param_table_append_header(table, -1, _("Fit Area"));
    gwy_param_table_append_info(table, LABEL_FROM, _("From:"));
    gwy_param_table_append_info(table, LABEL_TO, _("To:"));
    gwy_param_table_append_header(table, -1, _("Result"));
    gwy_param_table_append_resultsv(table, WIDGET_RESULTS, gui.results, values, FRACTAL_NMETHODS);
    gwy_param_table_append_report(table, PARAM_REPORT_STYLE);
    gwy_param_table_report_set_results(table, PARAM_REPORT_STYLE, gui.results);
    gwy_param_table_append_separator(table);
    /* XXX: This is strange because we can calculate results using all methods but still may only create one graph. */
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    graph = gwy_graph_new(args->gmodel);
    gtk_widget_set_size_request(graph, 480, 300);
    gtk_box_pack_end(GTK_BOX(hbox), graph, TRUE, TRUE, 0);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gwy_graph_set_status(GWY_GRAPH(graph), GWY_GRAPH_STATUS_XSEL);
    gui.selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(graph))),
                                                 GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(gui.selection, 1);

    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(graph_selected), &gui);
    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    graph_selected(&gui, 0, gui.selection);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.results);

    return outcome;
}

static GwyResults*
create_results(GwyContainer *data, gint id)
{
    GwyResults *results = gwy_results_new();
    guint i;

    gwy_results_add_header(results, N_("Fractal Dimension"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "image", N_("Image"));
    gwy_results_add_separator(results);
    for (i = 0; i < FRACTAL_NMETHODS; i++)
        gwy_results_add_value_plain(results, methods[i].id, gwy_sgettext(methods[i].name));

    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_channel(results, "image", data, id);

    return results;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_METHOD) {
        FractalMethodType method = gwy_params_get_enum(params, PARAM_METHOD);

        if (args->comp[method].fromto[0] == args->comp[method].fromto[1])
            gwy_selection_clear(gui->selection);
        else
            gwy_selection_set_data(gui->selection, 1, args->comp[method].fromto);
    }
    if (id < 0 || id == PARAM_METHOD || id == PARAM_INTERP) {
        /* These only change when the method or interpolation changes.  But, cruciually, not when the graph
         * selection changes. */
        GWY_OBJECT_UNREF(args->xline);
        GWY_OBJECT_UNREF(args->yline);
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
    }
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    FractalMethodType method = gwy_params_get_enum(args->params, PARAM_METHOD);

    if (execute(gui->args)) {
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
        gwy_results_fill_values(gui->results, methods[method].id, args->comp[method].dim, NULL);
    }
    else {
        gwy_results_set_na(gui->results, methods[method].id, NULL);
    }
    gwy_param_table_results_fill(gui->table, WIDGET_RESULTS);
}

static gboolean
execute(ModuleArgs *args)
{
    GwyDataLine *yfit, *xnline, *ynline;
    FractalMethodType method = gwy_params_get_enum(args->params, PARAM_METHOD);
    GwyInterpolationType interpolation = gwy_params_get_enum(args->params, PARAM_INTERP);
    FractalMethodComp *comp = args->comp + method;
    GwyGraphModel *gmodel = args->gmodel;
    GwyGraphCurveModel *gcmodel;
    gdouble a, b;
    gdouble *xdata, *ydata;
    gboolean line_ok;
    gint i, res;

    xnline = gwy_data_line_new(1, 1.0, FALSE);
    ynline = gwy_data_line_new(1, 1.0, FALSE);

    if (!args->xline) {
        args->xline = gwy_data_line_new(1, 1.0, FALSE);
        args->yline = gwy_data_line_new(1, 1.0, FALSE);
        methods[method].calculate(args->field, args->xline, args->yline, interpolation);
    }
    if ((line_ok = remove_datapoints(args->xline, args->yline, xnline, ynline, comp->fromto)))
        comp->dim = methods[method].getdim(xnline, ynline, &a, &b);

    g_object_set(gmodel,
                 "title", gwy_sgettext(methods[method].name),
                 "axis-label-bottom", methods[method].abscissa,
                 "axis-label-left", methods[method].ordinate,
                 NULL);

    gcmodel = gwy_graph_model_get_curve(gmodel, 0);
    g_object_set(gcmodel, "description", gwy_sgettext(methods[method].name), NULL);
    gwy_graph_curve_model_set_data(gcmodel,
                                   gwy_data_line_get_data_const(args->xline),
                                   gwy_data_line_get_data_const(args->yline),
                                   gwy_data_line_get_res(args->xline));

    res = gwy_data_line_get_res(xnline);
    if (line_ok) {
        yfit = gwy_data_line_new(res, res, FALSE);
        xdata = gwy_data_line_get_data(xnline);
        ydata = gwy_data_line_get_data(yfit);
        for (i = 0; i < res; i++)
            ydata[i] = xdata[i]*a + b;

        if (gwy_graph_model_get_n_curves(gmodel) == 2)
            gcmodel = gwy_graph_model_get_curve(gmodel, 1);
        else {
            gcmodel = gwy_graph_curve_model_new();
            g_object_set(gcmodel,
                         "mode", GWY_GRAPH_CURVE_LINE,
                         "description", _("Linear fit"),
                         NULL);
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);
        }
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, res);
        g_object_unref(yfit);
    }
    else if (gwy_graph_model_get_n_curves(gmodel) == 2)
        gwy_graph_model_remove_curve(gmodel, 1);

    g_object_unref(xnline);
    g_object_unref(ynline);

    return line_ok;
}

static void
graph_selected(ModuleGUI *gui, G_GNUC_UNUSED gint hint, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    FractalMethodType method = gwy_params_get_enum(args->params, PARAM_METHOD);
    FractalMethodComp *comp = args->comp + method;
    gchar buffer[24];
    gdouble sel[2];
    gint nsel;

    if ((nsel = gwy_selection_get_data(selection, NULL)))
        gwy_selection_get_object(selection, 0, sel);

    if (!nsel || sel[0] == sel[1]) {
        gwy_clear(sel, 2);
        nsel = 0;
    }
    else if (sel[0] > sel[1])
        GWY_SWAP(gdouble, sel[0], sel[1]);

    gwy_assign(comp->fromto, sel, 2);
    if (nsel) {
        g_snprintf(buffer, sizeof(buffer), "%.2f", comp->fromto[0]);
        gwy_param_table_info_set_valuestr(gui->table, LABEL_FROM, buffer);
        g_snprintf(buffer, sizeof(buffer), "%.2f", comp->fromto[1]);
        gwy_param_table_info_set_valuestr(gui->table, LABEL_TO, buffer);
    }
    else {
        gwy_param_table_info_set_valuestr(gui->table, LABEL_FROM, _("minimum"));
        gwy_param_table_info_set_valuestr(gui->table, LABEL_TO, _("maximum"));
    }

    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

/* Remove datapoints that are below or above selection. New data are in newxline and newyline and can be directly used
 * for fitting and fractal dimension evaluation. */
static gboolean
remove_datapoints(GwyDataLine *xline, GwyDataLine *yline,
                  GwyDataLine *newxline, GwyDataLine *newyline,
                  const gdouble *fromto)
{
    const gdouble *xdata, *ydata;
    gdouble *newxdata, *newydata;
    gint i, j, res;

    res = gwy_data_line_get_res(xline);
    g_assert(res == gwy_data_line_get_res(yline));
    gwy_data_line_resample(newxline, res, GWY_INTERPOLATION_NONE);
    gwy_data_line_resample(newyline, res, GWY_INTERPOLATION_NONE);
    if (fromto[0] == fromto[1]) {
        gwy_data_line_copy(xline, newxline);
        gwy_data_line_copy(yline, newyline);
        return res >= 2;
    }

    j = 0;
    xdata = gwy_data_line_get_data_const(xline);
    ydata = gwy_data_line_get_data_const(yline);
    newxdata = gwy_data_line_get_data(newxline);
    newydata = gwy_data_line_get_data(newyline);
    for (i = 0; i < res; i++) {
        if (xdata[i] >= fromto[0] && xdata[i] <= fromto[1]) {
            newxdata[j] = xdata[i];
            newydata[j] = ydata[i];
            j++;
        }
    }
    if (j < 2)
        return FALSE;

    gwy_data_line_resize(newxline, 0, j);
    gwy_data_line_resize(newyline, 0, j);

    return TRUE;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
