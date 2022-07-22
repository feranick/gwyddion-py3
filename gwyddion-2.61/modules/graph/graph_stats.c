/*
 *  $Id: graph_stats.c 24195 2021-09-24 13:28:21Z yeti-dn $
 *  Copyright (C) 2016-2021 David Necas (Yeti).
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

enum {
    PARAM_CURVE,
    PARAM_RANGE_FROM,
    PARAM_RANGE_TO,
    PARAM_REPORT_STYLE,
    LABEL_NPOINTS,
    WIDGET_RESULTS_SIMPLE,
    WIDGET_RESULTS_INTEGRAL,
};

typedef struct {
    GwyParams *params;
    GwyGraphModel *gmodel;
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
    GwyGraphModel *gmodel;
    GwySelection *xsel;
    GwySIValueFormat *xvf;
} ModuleGUI;

static gboolean     module_register     (void);
static GwyParamDef* define_module_params(void);
static void         graph_stats         (GwyGraph *graph);
static void         run_gui             (ModuleArgs *args,
                                         GwyContainer *data);
static gint         execute             (ModuleArgs *args,
                                         GwyResults *results);
static void         param_changed       (ModuleGUI *gui,
                                         gint id);
static void         preview             (gpointer user_data);
static GtkWidget*   create_rangebox     (gpointer user_data);
static GwyResults*  create_results      (GwyContainer *data,
                                         GwyGraphModel *gmodel,
                                         GwySIUnit *xunit,
                                         GwySIUnit *yunit);
static void         graph_selected      (GwySelection* selection,
                                         gint i,
                                         ModuleGUI *gui);
static void         update_range_entries(ModuleGUI *gui,
                                         gdouble xfrom,
                                         gdouble xto);


static const gchar* results_simple[] = { "min", "max", "avg", "median", "ra", "rms", "skew", "kurtosis", };
static const gchar* results_integral[] = {
    "projlen", "length", "variation", "integralavg", "integral", "integralp", "integraln", "integral2",
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates simple graph curve statistics."),
    "Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti)",
    "2017",
};

GWY_MODULE_QUERY2(module_info, graph_stats)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_stats",
                            (GwyGraphFunc)&graph_stats,
                            N_("/_Statistics/_Statistical Quantities..."),
                            GWY_STOCK_GRAPH_STATISTICS,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Calculate graph curve statistics"));

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
    gwy_param_def_add_graph_curve(paramdef, PARAM_CURVE, "curve", NULL);
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", _("Save Parameters"),
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON);
    /* Foreign, not saved to settings. */
    gwy_param_def_add_double(paramdef, PARAM_RANGE_FROM, NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_RANGE_TO, NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    return paramdef;
}

static void
graph_stats(GwyGraph *graph)
{
    GwyContainer *data;
    ModuleArgs args;

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data, 0);
    gwy_clear(&args, 1);
    args.params = gwy_params_new_from_settings(define_module_params());
    args.gmodel = gwy_graph_get_model(graph);
    gwy_graph_model_get_x_range(args.gmodel, &args.xmin, &args.xmax);
    gwy_params_set_double(args.params, PARAM_RANGE_FROM, args.xmin);
    gwy_params_set_double(args.params, PARAM_RANGE_TO, args.xmax);
    run_gui(&args, data);
    gwy_params_save_to_settings(args.params);
    g_object_unref(args.params);
}

static void
run_gui(ModuleArgs *args, GwyContainer *data)
{
    GtkWidget *hbox, *graph;
    GwyDialog *dialog;
    GwyParamTable *table;
    gdouble xrange;
    GwySIUnit *xunit, *yunit;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.gmodel = gwy_graph_model_new_alike(args->gmodel);
    g_object_get(args->gmodel, "si-unit-x", &xunit, "si-unit-y", &yunit, NULL);
    xrange = MAX(fabs(args->xmin), fabs(args->xmax));
    gui.xvf = gwy_si_unit_get_format_with_digits(xunit, GWY_SI_UNIT_FORMAT_VFMARKUP, xrange, 3, NULL);
    gui.results = create_results(data, args->gmodel, xunit, yunit);

    gui.dialog = gwy_dialog_new(_("Statistical Quantities"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, FALSE, FALSE, 0);

    graph = gwy_graph_new(gui.gmodel);
    gtk_widget_set_size_request(graph, 480, 360);
    gtk_box_pack_end(GTK_BOX(hbox), graph, TRUE, TRUE, 0);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gwy_graph_set_status(GWY_GRAPH(graph), GWY_GRAPH_STATUS_XSEL);
    gui.xsel = gwy_graph_area_get_selection(GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(graph))),
                                            GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(gui.xsel, 1);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_graph_curve(table, PARAM_CURVE, args->gmodel);
    /* Simply pass one of the range parameters – it only serves as a unique id here. */
    gwy_param_table_append_foreign(table, PARAM_RANGE_FROM, create_rangebox, &gui, NULL);
    gwy_param_table_append_info(table, LABEL_NPOINTS, _("Number of points"));

    gwy_param_table_append_header(table, -1, _("Simple Parameters"));
    gwy_param_table_append_resultsv(table, WIDGET_RESULTS_SIMPLE, gui.results,
                                    results_simple, G_N_ELEMENTS(results_simple));
    gwy_param_table_append_header(table, -1, _("Integrals"));
    gwy_param_table_append_resultsv(table, WIDGET_RESULTS_INTEGRAL, gui.results,
                                    results_integral, G_N_ELEMENTS(results_integral));
    gwy_param_table_append_report(table, PARAM_REPORT_STYLE);
    gwy_param_table_report_set_results(table, PARAM_REPORT_STYLE, gui.results);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect(gui.xsel, "changed", G_CALLBACK(graph_selected), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    update_range_entries(&gui, args->xmin, args->xmax);
    gwy_dialog_run(dialog);

    g_object_unref(gui.gmodel);
    g_object_unref(gui.results);
    g_object_unref(xunit);
    g_object_unref(yunit);
    gwy_si_unit_value_format_free(gui.xvf);
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
    ModuleArgs *args = gui->args;

    if (id < 0 || id == PARAM_CURVE) {
        gint curve = gwy_params_get_int(args->params, PARAM_CURVE);
        gwy_graph_model_remove_all_curves(gui->gmodel);
        gwy_graph_model_add_curve(gui->gmodel, gwy_graph_model_get_curve(args->gmodel, curve));
    }
    if (id < 0 || id == PARAM_CURVE || id == PARAM_RANGE_FROM || id == PARAM_RANGE_TO)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static GwyResults*
create_results(GwyContainer *data, GwyGraphModel *gmodel,
               GwySIUnit *xunit, GwySIUnit *yunit)
{
    GwyResults *results;

    results = gwy_results_new();
    gwy_results_add_header(results, N_("Graph Statistics"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "graph", N_("Graph"));
    gwy_results_add_value_str(results, "curve", N_("Curve"));
    gwy_results_add_format(results, "range", N_("Range"), TRUE,
    /* TRANSLATORS: %{from}v and %{to}v are ids, do NOT translate them. */
                           N_("%{from}v to %{to}v"),
                           "power-x", 1,
                           NULL);
    gwy_results_add_value_int(results, "npts", N_("Number of points"));

    gwy_results_add_separator(results);
    gwy_results_add_header(results, _("Simple Parameters"));
    gwy_results_add_value_z(results, "min", N_("Minimum"));
    gwy_results_add_value_z(results, "max", N_("Maximum"));
    gwy_results_add_value_z(results, "avg", N_("Mean value"));
    gwy_results_add_value_z(results, "median", N_("Median"));
    gwy_results_add_value_z(results, "ra", N_("Ra"));
    gwy_results_add_value_z(results, "rms", N_("Rms (Rq)"));
    gwy_results_add_value_plain(results, "skew", N_("Skew"));
    gwy_results_add_value_plain(results, "kurtosis", N_("Excess kurtosis"));

    gwy_results_add_separator(results);
    gwy_results_add_header(results, _("Integrals"));
    gwy_results_add_value_x(results, "projlen", N_("Projected length"));
    gwy_results_add_value_x(results, "length", N_("Developed length"));
    gwy_results_add_value_z(results, "variation", N_("Variation"));
    gwy_results_add_value_z(results, "integralavg", N_("Mean value"));
    gwy_results_add_value(results, "integral", N_("Area under curve"),
                          "type", GWY_RESULTS_VALUE_FLOAT,
                          "power-x", 1, "power-z", 1,
                          NULL);
    gwy_results_add_value(results, "integralp", N_("Positive area"),
                          "type", GWY_RESULTS_VALUE_FLOAT,
                          "power-x", 1, "power-z", 1,
                          NULL);
    gwy_results_add_value(results, "integraln", N_("Negative area"),
                          "type", GWY_RESULTS_VALUE_FLOAT,
                          "power-x", 1, "power-z", 1,
                          NULL);
    gwy_results_add_value_z(results, "integral2", N_("Root mean square"));

    gwy_results_set_unit(results, "x", xunit);
    gwy_results_set_unit(results, "z", yunit);

    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_graph(results, "graph", gmodel);

    return results;
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    gchar buffer[16];
    gint npts;

    npts = execute(gui->args, gui->results);
    gwy_param_table_results_fill(gui->table, WIDGET_RESULTS_SIMPLE);
    gwy_param_table_results_fill(gui->table, WIDGET_RESULTS_INTEGRAL);
    gwy_param_table_set_sensitive(gui->table, PARAM_REPORT_STYLE, npts > 0);
    g_snprintf(buffer, sizeof(buffer), "%u", npts);
    gwy_param_table_info_set_valuestr(gui->table, LABEL_NPOINTS, buffer);
}

static gint
execute(ModuleArgs *args, GwyResults *results)
{
    GwyParams *params = args->params;
    gdouble from = gwy_params_get_double(params, PARAM_RANGE_FROM);
    gdouble to = gwy_params_get_double(params, PARAM_RANGE_TO);
    gint curve = gwy_params_get_int(params, PARAM_CURVE);
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(args->gmodel, curve);
    GwyDataLine *dline;
    const gdouble *xdata, *ydata;
    guint ndata, i, pos, npts;
    gdouble min, max, projlen, length, variation, integralp, integraln, integral, integral2;
    GwySIUnit *xunit, *yunit;

    xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    ndata = gwy_graph_curve_model_get_ndata(gcmodel);
    gwy_results_fill_graph_curve(results, "curve", gcmodel);
    gwy_results_set_nav(results, G_N_ELEMENTS(results_simple), results_simple);
    gwy_results_set_nav(results, G_N_ELEMENTS(results_integral), results_integral);

    for (pos = 0; pos < ndata && xdata[pos] < from; pos++)
        pos++;
    for (npts = ndata; npts && xdata[npts-1] > to; npts--)
        npts--;

    if (npts <= pos)
        return 0;

    npts -= pos;
    gwy_results_fill_values(results, "npts", npts, NULL);
    gwy_results_fill_format(results, "range", "from", from, "to", to, NULL);

    /* Calculate simple quantities only depending on the value distribution using DataLine methods. */
    dline = gwy_data_line_new(npts, 1.0, FALSE);
    gwy_assign(gwy_data_line_get_data(dline), ydata + pos, npts);
    gwy_data_line_get_min_max(dline, &min, &max);
    gwy_results_fill_values(results,
                            "min", min, "max", max,
                            "avg", gwy_data_line_get_avg(dline),
                            "median", gwy_data_line_get_median(dline),
                            NULL);
    if (npts > 1) {
        gwy_results_fill_values(results,
                                "rms", gwy_data_line_get_rms(dline),
                                "ra", gwy_data_line_get_ra(dline),
                                "skew", gwy_data_line_get_skew(dline),
                                "kurtosis", gwy_data_line_get_kurtosis(dline),
                                NULL);
    }
    g_object_unref(dline);
    if (npts < 2)
        return npts;

    projlen = xdata[pos + npts-1] - xdata[pos];
    integralp = integraln = integral = integral2 = length = variation = 0.0;
    for (i = 0; i < npts-1; i++) {
        gdouble y1 = ydata[pos + i], y2 = ydata[pos + i+1];
        gdouble dx = xdata[pos + i+1] - xdata[pos + i];
        gdouble x, dpos = 0.0, dneg = 0.0, d2 = 0.0;

        length += sqrt((y2 - y1)*(y2 - y1) + dx*dx);
        variation += fabs(y2 - y1);
        if (dx <= 0.0)
            continue;

        if (y1 >= 0.0 && y2 >= 0.0) {
            dpos = (y1 + y2)*dx;
            d2 = (y1*y1 + y2*y2)*dx;
        }
        else if (y1 <= 0.0 && y2 <= 0.0) {
            dneg = (y1 + y2)*dx;
            d2 = (y1*y1 + y2*y2)*dx;
        }
        else if (y1 > 0.0 && y2 < 0.0) {
            x = y1/(y1 - y2)*dx;
            dpos = y1*x;
            dneg = y2*(dx - x);
            d2 = (y1*y1*x + y2*y2*(dx - x));
        }
        else if (y1 < 0.0 && y2 > 0.0) {
            x = y2/(y2 - y1)*dx;
            dpos = y2*x;
            dneg = y1*(dx - x);
            d2 = (y1*y1*(dx - x) + y2*y2*x);
        }
        else {
            g_warning("Impossible curve value signs.");
            continue;
        }
        integralp += dpos;
        integraln += dneg;
        integral += dpos + dneg;
        integral2 += d2;
    }

    integral *= 0.5;
    integralp *= 0.5;
    integraln *= 0.5;
    integral2 *= 0.5;
    gwy_results_fill_values(results,
                            "projlen", projlen,
                            "variation", variation,
                            "integralp", integralp,
                            "integraln", integraln,
                            "integral", integral,
                            "integralavg", integral/projlen,
                            "integral2", sqrt(integral2/projlen),
                            NULL);

    g_object_get(args->gmodel, "si-unit-x", &xunit, "si-unit-y", &yunit, NULL);
    if (gwy_si_unit_equal(xunit, yunit))
        gwy_results_fill_values(results, "length", length, NULL);
    g_object_unref(xunit);
    g_object_unref(yunit);

    return npts;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
