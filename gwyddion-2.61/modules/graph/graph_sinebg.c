/*
 *  $Id: graph_sinebg.c 24487 2021-11-07 21:05:57Z yeti-dn $
 *  Copyright (C) 2019-2021 David Necas (Yeti).
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
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>


typedef enum {
    OUTPUT_DATA_FIT   = 0,
    OUTPUT_LEVELLED  = 1,
    OUTPUT_NTYPES
} OutputType;


enum {
    PARAM_CURVE,
    PARAM_RANGE_FROM,
    PARAM_RANGE_TO,
    PARAM_ALL,
    PARAM_OUTPUT_TYPE,
    PARAM_TARGET_GRAPH,
    WIDGET_RESULTS,
};

typedef struct {
    GwyParams *params;
    GwyGraphModel *gmodel;
    GwyGraphModel *result;
    gdouble xmin;
    gdouble xmax;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyResults *results;
    GwyParamTable *table;
    GtkWidget *xfrom;
    GtkWidget *xto;
    GwySelection *xsel;
    GwySIValueFormat *xvf;
} ModuleGUI;

static gboolean         module_register              (void);
static GwyParamDef*     define_module_params         (void);
static void             graph_sinebg                 (GwyGraph *graph);
static GwyDialogOutcome run_gui                      (ModuleArgs *args,
                                                      GwyContainer *data);
static void             execute                      (ModuleArgs *args,
                                                      GwyResults *results);
static void             param_changed                (ModuleGUI *gui,
                                                      gint id);
static GwyResults*      create_results               (GwyGraphModel *gmodel,
                                                      GwySIUnit *xunit,
                                                      GwySIUnit *yunit);

static void             preview                      (gpointer user_data);
static void             graph_selected               (GwySelection* selection,
                                                      gint i,
                                                      ModuleGUI *gui);
static void             update_range_entries         (ModuleGUI *gui,
                                                      gdouble xfrom,
                                                      gdouble xto);
static GtkWidget*       create_rangebox              (gpointer user_data);


static const gchar* fitresults[] = { "period", "amplitude", "yoffset", };

static const GwyEnum output_types[] =  {
    { N_("Data + fit"),      OUTPUT_DATA_FIT,   },
    { N_("Leveled data"),    OUTPUT_LEVELLED,   },
};



static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Remove sine background"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2021",
};

GWY_MODULE_QUERY2(module_info, graph_sinebg)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_sinebg",
                            (GwyGraphFunc)&graph_sinebg,
                            N_("/_Force Distance/_Remove Sine Background..."),
                            NULL,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Remove interference effects from FZ curve"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_graph_func_current());
    gwy_param_def_add_graph_curve(paramdef, PARAM_CURVE, "curve", _("Curve to fit"));
    gwy_param_def_add_boolean(paramdef, PARAM_ALL, "all", _("_All curves"), FALSE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT_TYPE, "output_type", _("_Output"),
                              output_types, G_N_ELEMENTS(output_types), OUTPUT_LEVELLED);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);

    /* Foreign, not saved to settings. */
    gwy_param_def_add_double(paramdef, PARAM_RANGE_FROM, NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_RANGE_TO, NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);


    return paramdef;
}

static void
graph_sinebg(GwyGraph *graph)
{
    GwyDialogOutcome outcome;
    GwyContainer *data;
    ModuleArgs args;

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data, 0);
    gwy_clear(&args, 1);
    args.params = gwy_params_new_from_settings(define_module_params());
    args.gmodel = gwy_graph_get_model(graph);
    gwy_graph_model_get_x_range(args.gmodel, &args.xmin, &args.xmax);
    gwy_params_set_double(args.params, PARAM_RANGE_FROM, args.xmin);
    gwy_params_set_double(args.params, PARAM_RANGE_TO, args.xmax);

    args.result = gwy_graph_model_new_alike(args.gmodel);

    outcome = run_gui(&args, data);
    gwy_params_save_to_settings(args.params);

    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args, NULL);

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *graph;
    GwyDialog *dialog;
    GwyParamTable *table;
    GwyAppDataId target_graph_id;
    gdouble xrange;
    GwySIUnit *xunit, *yunit;
    ModuleGUI gui;

    /* This is to get the target graph filter right. */
    execute(args, NULL);

    gwy_clear(&gui, 1);
    gui.args = args;
    g_object_set(args->result, "label-visible", FALSE, NULL);

    g_object_get(args->gmodel, "si-unit-x", &xunit, "si-unit-y", &yunit, NULL);
    xrange = MAX(fabs(args->xmin), fabs(args->xmax));
    gui.xvf = gwy_si_unit_get_format_with_digits(xunit, GWY_SI_UNIT_FORMAT_VFMARKUP, xrange, 3, NULL);
    gui.results = create_results(args->result, xunit, yunit);

    gui.dialog = gwy_dialog_new(_("Remove Sine Background"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    gwy_dialog_have_result(dialog);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, FALSE, FALSE, 0);

    graph = gwy_graph_new(args->result);
    gtk_widget_set_size_request(graph, 480, 300);
    gtk_box_pack_end(GTK_BOX(hbox), graph, TRUE, TRUE, 0);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gwy_graph_set_status(GWY_GRAPH(graph), GWY_GRAPH_STATUS_XSEL);
    gui.xsel = gwy_graph_area_get_selection(GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(graph))),
                                            GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(gui.xsel, 1);


    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_graph_curve(table, PARAM_CURVE, args->gmodel);
    gwy_param_table_append_checkbox(table, PARAM_ALL);
    gwy_param_table_append_foreign(table, PARAM_RANGE_FROM, create_rangebox, &gui, NULL);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->result);
    gwy_param_table_append_combo(table, PARAM_OUTPUT_TYPE);
    gwy_param_table_append_header(table, -1, _("Fit results"));
    gwy_param_table_append_resultsv(table, WIDGET_RESULTS, gui.results,
                                    fitresults, G_N_ELEMENTS(fitresults));

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, TRUE, 0);

    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect(gui.xsel, "changed", G_CALLBACK(graph_selected), &gui);

    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    outcome = gwy_dialog_run(dialog);

    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(args, gui.results);

    g_object_set(args->result, "label-visible", TRUE, NULL);

    target_graph_id = gwy_params_get_data_id(args->params, PARAM_TARGET_GRAPH);
    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data, 0);
    gwy_app_add_graph_or_curves(args->result, data, &target_graph_id, 1);

end:
    g_object_unref(gui.results);
    g_object_unref(xunit);
    g_object_unref(yunit);
    gwy_si_unit_value_format_free(gui.xvf);


    return outcome;
}

static void
update_range_entries(ModuleGUI *gui, gdouble xfrom, gdouble xto)
{
    GwySIValueFormat *xvf = gui->xvf;
    gdouble power10 = pow10(xvf->precision);
    gchar buffer[24];

    g_snprintf(buffer, sizeof(buffer), "%.*f", xvf->precision, floor(xfrom*power10/xvf->magnitude)/power10);
    gtk_entry_set_text(GTK_ENTRY(gui->xfrom), buffer);
    g_snprintf(buffer, sizeof(buffer), "%.*f", xvf->precision, ceil(xto*power10/xvf->magnitude)/power10);
    gtk_entry_set_text(GTK_ENTRY(gui->xto), buffer);
}


static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    //GwyParamTable *table = gui->table;

    /* //we want to be able to choose the fitted curve
    if (id < 0 || id == PARAM_ALL) {
        gboolean all_curves = gwy_params_get_boolean(params, PARAM_ALL);
        gwy_param_table_set_sensitive(table, PARAM_CURVE, !all_curves);
    }
    */

    if (id < 0 || id == PARAM_CURVE || id == PARAM_OUTPUT_TYPE || id == PARAM_ALL) {
        gint curve = gwy_params_get_int(params, PARAM_CURVE);
        gwy_graph_model_remove_all_curves(gui->args->result);
        gwy_graph_model_add_curve(gui->args->result, gwy_graph_model_get_curve(gui->args->gmodel, curve));
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));

    }

}

static void
limit_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gdouble range[2];

    range[0] = gwy_params_get_double(args->params, PARAM_RANGE_FROM);
    range[1] = gwy_params_get_double(args->params, PARAM_RANGE_TO);
    if (range[0] <= args->xmin && range[1] >= args->xmax)
        gwy_selection_clear(gui->xsel);
    else
        gwy_selection_set_object(gui->xsel, 0, range);
}

static void
range_changed(GtkWidget *entry, ModuleGUI *gui)
{
    gdouble newval;
    gint id;

    newval = g_strtod(gtk_entry_get_text(GTK_ENTRY(entry)), NULL);
    newval *= gui->xvf->magnitude;
    id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "id"));
    if (gwy_params_set_double(gui->args->params, id, newval)) {
        limit_selection(gui);
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
    }
}

static GtkWidget*
create_range_entry(ModuleGUI *gui, GtkBox *rangebox, gint id)
{
    GtkWidget *entry;

    entry = gtk_entry_new();
    g_object_set_data(G_OBJECT(entry), "id", GINT_TO_POINTER(id));
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    gtk_box_pack_start(GTK_BOX(rangebox), entry, FALSE, FALSE, 0);
    g_signal_connect(entry, "activate", G_CALLBACK(range_changed), gui);
    gwy_widget_set_activate_on_unfocus(entry, TRUE);

    return entry;
}

static GtkWidget*
create_rangebox(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GtkWidget *rangebox, *label;

    rangebox = gwy_hbox_new(6);

    gtk_box_pack_start(GTK_BOX(rangebox), gtk_label_new(_("Range:")), FALSE, FALSE, 0);
    gui->xfrom = create_range_entry(gui, GTK_BOX(rangebox), PARAM_RANGE_FROM);
    gtk_box_pack_start(GTK_BOX(rangebox), gtk_label_new(gwy_sgettext("range|to")), FALSE, FALSE, 0);
    gui->xto = create_range_entry(gui, GTK_BOX(rangebox), PARAM_RANGE_TO);

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), gui->xvf->units);
    gtk_box_pack_start(GTK_BOX(rangebox), label, FALSE, FALSE, 0);

    return rangebox;
}

static void
graph_selected(GwySelection* selection, gint i, ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gdouble range[2];
    gdouble xfrom, xto;
    gboolean have_range = TRUE;

    g_return_if_fail(i <= 0);

    if (gwy_selection_get_data(selection, NULL) <= 0)
        have_range = FALSE;
    else {
        gwy_selection_get_object(selection, 0, range);
        if (range[0] == range[1])
            have_range = FALSE;
    }
    if (have_range) {
        xfrom = MIN(range[0], range[1]);
        xto = MAX(range[0], range[1]);
    }
    else {
        xfrom = args->xmin;
        xto = args->xmax;
    }

    update_range_entries(gui, xfrom, xto);
    if (gwy_params_set_double(args->params, PARAM_RANGE_FROM, xfrom))
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
    if (gwy_params_set_double(args->params, PARAM_RANGE_TO, xto))
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}


static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args, gui->results);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    gwy_param_table_results_fill(gui->table, WIDGET_RESULTS);

}

static gdouble
func_sine(gdouble x,
                 G_GNUC_UNUSED gint n_param,
                 const gdouble *param,
                 G_GNUC_UNUSED gpointer user_data,
                 gboolean *fres)
{
    *fres = TRUE;
    return param[0]*sin(param[1]*x+param[2])+param[3];
}


static void
execute(ModuleArgs *args, GwyResults *results)
{
    GwyParams *params = args->params;
    GwyGraphModel *gmodel = args->gmodel, *result = args->result;
    gboolean all_curves = gwy_params_get_boolean(params, PARAM_ALL);
    OutputType output_type = gwy_params_get_enum(params, PARAM_OUTPUT_TYPE);

    gint curve = gwy_params_get_int(params, PARAM_CURVE);
    gint ifrom = (all_curves ? 0 : curve);
    gint ito = (all_curves ? gwy_graph_model_get_n_curves(gmodel) : curve+1);
    gdouble from = gwy_params_get_double(params, PARAM_RANGE_FROM);
    gdouble to = gwy_params_get_double(params, PARAM_RANGE_TO);

    const gdouble *xdata, *ydata;
    gdouble *nxdata, *nydata;
    gint ndata;
    GwyGraphCurveModel *gcmodel, *ngcmodel;
    gint i, j, n, fitstart;
    GwyNLFitter *fitter;
    gdouble param[4];
    gboolean fix[4], fres;
    gdouble xmin, xmax, ymin, ymax, allxmin, allxmax;

    gwy_graph_model_remove_all_curves(result);

    gcmodel = gwy_graph_model_get_curve(gmodel, curve);
    xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    ndata = gwy_graph_curve_model_get_ndata(gcmodel);

    n = 0;
    fitstart = -1;
    xmin = ymin = G_MAXDOUBLE;
    xmax = ymax = -G_MAXDOUBLE;
    for (i=0; i<ndata; i++) {
        if (xdata[i]>=from && xdata[i]<to)
        {
            if (xdata[i]<xmin) xmin = xdata[i];
            if (ydata[i]<ymin) ymin = ydata[i];

            if (xdata[i]>xmax) xmax = xdata[i];
            if (ydata[i]>ymax) ymax = ydata[i];

            if (fitstart<0) fitstart = i;
            n++;
        }
    }
    if (fitstart<1) fitstart = 0;

    fitter = gwy_math_nlfit_new((GwyNLFitFunc)func_sine, gwy_math_nlfit_diff);
    fix[0] = 0;
    fix[1] = 0;
    fix[2] = 0;
    fix[3] = 0;

    param[0] = (ymax-ymin)/2.0; //amplitude
    param[1] = 2*M_PI/(xmax-xmin); //frequency
    param[2] = 0; //phase
    param[3] = (ymax+ymin)/2.0; //offset

    gwy_math_nlfit_fit_full(fitter, n, xdata + fitstart, ydata + fitstart, NULL, 4, param, fix, NULL, NULL);

    if (results)
       gwy_results_fill_values(results,
                            "period", param[1],
                            "amplitude", param[0],
                            "yoffset", param[3],
                            NULL);

    allxmin = G_MAXDOUBLE;
    allxmax = -G_MAXDOUBLE;

    for (i = ifrom; i < ito; i++) {
        gcmodel = gwy_graph_model_get_curve(gmodel, i);

        ngcmodel = gwy_graph_curve_model_duplicate(gcmodel);

        xdata = gwy_graph_curve_model_get_xdata(gcmodel);
        ydata = gwy_graph_curve_model_get_ydata(gcmodel);
        ndata = gwy_graph_curve_model_get_ndata(gcmodel);

        nxdata = g_new(gdouble, ndata);
        nydata = g_new(gdouble, ndata);

        if (output_type == OUTPUT_DATA_FIT)
        {
           for (j=0; j<ndata; j++) {
              nxdata[j] = xdata[j];
              nydata[j] = ydata[j];

              if (xdata[j]<allxmin) allxmin = xdata[j];
              if (xdata[j]>allxmax) allxmax = xdata[j];

           }

        }
        else {
           for (j=0; j<ndata; j++) {
              nxdata[j] = xdata[j];
              nydata[j] = ydata[j] - func_sine(xdata[j], 4, param, NULL, &fres);
           }
        }

        gwy_graph_curve_model_set_data(ngcmodel, nxdata, nydata, ndata);
        g_free(nxdata);
        g_free(nydata);

        g_object_set(ngcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);

        if (all_curves) {
            g_object_set(ngcmodel,
                         "color", gwy_graph_get_preset_color(i),
                         NULL);
        }
        else
            g_object_set(ngcmodel, "description", g_strdup(_("FD curve")), NULL);
        gwy_graph_model_add_curve(result, ngcmodel);

        g_object_unref(ngcmodel);
    }

    if (output_type == OUTPUT_DATA_FIT) {
        ngcmodel = gwy_graph_curve_model_new_alike(gcmodel);

        ndata = 100;
        nxdata = g_new(gdouble, ndata);
        nydata = g_new(gdouble, ndata);

        for (j=0; j<ndata; j++) {
           nxdata[j] = allxmin + j*(allxmax - allxmin)/(gdouble)ndata;
           nydata[j] = func_sine(nxdata[j], 4, param, NULL, &fres);
        }

        gwy_graph_curve_model_set_data(ngcmodel, nxdata, nydata, ndata);
        g_free(nxdata);
        g_free(nydata);

        g_object_set(ngcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);

        g_object_set(ngcmodel, "description", g_strdup(_("fit")), NULL);
        gwy_graph_model_add_curve(result, ngcmodel);

        g_object_unref(ngcmodel);
    }

    gwy_math_nlfit_free(fitter);


}

static GwyResults*
create_results(GwyGraphModel *gmodel,
               GwySIUnit *xunit, GwySIUnit *yunit)
{
    GwyResults *results;

    results = gwy_results_new();
    gwy_results_add_header(results, N_("Fit results"));
    gwy_results_add_value_x(results, "period", N_("Period"));
    gwy_results_add_value_z(results, "amplitude", N_("Amplitude"));
    gwy_results_add_value_z(results, "yoffset", N_("Y offset"));

    gwy_results_set_unit(results, "x", xunit);
    gwy_results_set_unit(results, "z", yunit);

    gwy_results_fill_graph(results, "graph", gmodel);

    return results;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
