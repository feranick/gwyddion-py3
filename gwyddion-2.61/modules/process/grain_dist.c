/*
 *  $Id: grain_dist.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek, Sven Neumann.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, neumann@jpk.com.
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

/* FIXME: Is there any sane way to add support for target graphs here when we can create multiple graphs of
 * incompatible quantities? */

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/grains.h>
#include <libprocess/linestats.h>
#include <libgwydgets/gwygrainvaluemenu.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define RUN_MODES (GWY_RUN_INTERACTIVE | GWY_RUN_IMMEDIATE)

typedef enum {
    MODE_GRAPH,
    MODE_RAW
} GrainDistMode;

enum {
    PARAM_FIXRES,
    PARAM_MODE,
    PARAM_RESOLUTION,
    PARAM_ADD_COMMENT,
    PARAM_SELECTED,
    PARAM_EXPANDED,
};

typedef struct {
    GwyGrainQuantity quantity;
    const gchar *label;
    const gchar *symbol;
    const gchar *identifier;
    const gchar *gtitle;
    const gchar *cdesc;
} QuantityInfo;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    /* Cached precalculated data. */
    gboolean units_equal;
    gint *grains;
    guint ngrains;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    guint nvalues;
    GwyGrainValue **gvalues;
    GwyDataLine **rawvalues;
    gboolean add_comment;
} GrainDistExportData;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyGraphModel *gmodel;
    GtkWidget *values;
} ModuleGUI;

static gboolean         module_register            (void);
static GwyParamDef*     define_module_params       (void);
static void             grain_dist                 (GwyContainer *data,
                                                    GwyRunType runtype);
static void             execute                    (ModuleArgs *args,
                                                    GwyContainer *data);
static GwyDialogOutcome run_gui                    (ModuleArgs *args);
static void             param_changed              (ModuleGUI *gui,
                                                    gint id);
static void             dialog_response            (ModuleGUI *gui,
                                                    gint response);
static void             preview                    (gpointer user_data);
static void             selected_changed           (ModuleGUI *gui);
static void             row_expanded_collapsed     (ModuleGUI *gui);
static void             add_one_distribution       (GwyGraphModel *gmodel,
                                                    GrainDistExportData *expdata,
                                                    guint i);
static gchar*           grain_dist_export_create   (gpointer user_data,
                                                    gssize *data_len);
static gchar*           rectify_grain_quantity_list(const gchar *s);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Evaluates distribution of grains (continuous parts of mask)."),
    "Petr Klapetek <petr@klapetek.cz>, Sven Neumann <neumann@jpk.com>, Yeti <yeti@gwyddion.net>",
    "5.0",
    "David Nečas (Yeti) & Petr Klapetek & Sven Neumann",
    "2003",
};

GWY_MODULE_QUERY2(module_info, grain_dist)

static gboolean
module_register(void)
{
    gwy_process_func_register("grain_dist",
                              (GwyProcessFunc)&grain_dist,
                              N_("/_Grains/_Distributions..."),
                              GWY_STOCK_GRAINS_GRAPH,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Distributions of various grain characteristics"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum modes[] = {
        { N_("_Export raw data"), MODE_RAW,   },
        { N_("Plot _graphs"),     MODE_GRAPH, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_boolean(paramdef, PARAM_FIXRES, "fixres", _("_Fixed resolution"), FALSE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", NULL, modes, G_N_ELEMENTS(modes), MODE_GRAPH);
    gwy_param_def_add_int(paramdef, PARAM_RESOLUTION, "resolution", _("_Fixed resolution"), 4, 1024, 120);
    gwy_param_def_add_boolean(paramdef, PARAM_ADD_COMMENT, "add_comment", _("Add _informational comment header"),
                              FALSE);
    gwy_param_def_add_string(paramdef, PARAM_SELECTED, "selected", NULL,
                             GWY_PARAM_STRING_NULL_IS_EMPTY, rectify_grain_quantity_list, "Equivalent disc radius");
    gwy_param_def_add_int(paramdef, PARAM_EXPANDED, "expanded", NULL, 0, G_MAXINT, 0);
    return paramdef;
}

static void
grain_dist(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     0);
    g_return_if_fail(args.field && args.mask);

    args.units_equal = gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(args.field),
                                         gwy_data_field_get_si_unit_z(args.field));
    args.grains = g_new0(gint, gwy_data_field_get_xres(args.mask) * gwy_data_field_get_yres(args.mask));
    args.ngrains = gwy_data_field_number_grains(args.mask, args.grains);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    execute(&args, data);

end:
    g_free(args.grains);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GtkWidget *scwin, *hbox, *vbox, *graph;
    GwyParamTable *table;
    GwyDialog *dialog;
    GtkTreeView *treeview;
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GwyDialogOutcome outcome;
    gchar **selected_quantities;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Grain Distributions"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_CLEAR, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    gtk_window_set_default_size(GTK_WINDOW(dialog), -1, 520);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    gui.gmodel = gwy_graph_model_new();
    graph = gwy_graph_new(gui.gmodel);
    gtk_widget_set_size_request(graph, 360, -1);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 4);

    vbox = gwy_vbox_new(2);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 4);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    gui.values = gwy_grain_value_tree_view_new(FALSE, "name", "enabled", NULL);
    treeview = GTK_TREE_VIEW(gui.values);
    model = gtk_tree_view_get_model(treeview);
    gtk_tree_view_set_headers_visible(treeview, FALSE);
    selection = gtk_tree_view_get_selection(treeview);
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    gwy_grain_value_tree_view_set_same_units(treeview, args->units_equal);
    gwy_grain_value_tree_view_set_expanded_groups(treeview, gwy_params_get_int(args->params, PARAM_EXPANDED));
    selected_quantities = g_strsplit(gwy_params_get_string(args->params, PARAM_SELECTED), "\n", 0);
    gwy_grain_value_tree_view_set_enabled(treeview, selected_quantities);
    g_strfreev(selected_quantities);
    gtk_container_add(GTK_CONTAINER(scwin), gui.values);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio_item(table, PARAM_MODE, MODE_RAW);
    gwy_param_table_append_checkbox(table, PARAM_ADD_COMMENT);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_MODE, MODE_GRAPH);
    gwy_param_table_append_slider(table, PARAM_RESOLUTION);
    gwy_param_table_add_enabler(table, PARAM_FIXRES, PARAM_RESOLUTION);

    gtk_box_pack_start(GTK_BOX(vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    g_signal_connect_swapped(selection, "changed", G_CALLBACK(preview), &gui);
    g_signal_connect_swapped(model, "row-changed", G_CALLBACK(selected_changed), &gui);
    /* FIXME: We would like some helper for this. */
    g_signal_connect_swapped(treeview, "row-expanded", G_CALLBACK(row_expanded_collapsed), &gui);
    g_signal_connect_swapped(treeview, "row-collapsed", G_CALLBACK(row_expanded_collapsed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.gmodel);

    return outcome;
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == GWY_RESPONSE_CLEAR) {
        GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(gui->values));
        g_signal_handlers_block_by_func(model, selected_changed, gui);
        gwy_grain_value_tree_view_set_enabled(GTK_TREE_VIEW(gui->values), NULL);
        g_signal_handlers_unblock_by_func(model, selected_changed, gui);
        selected_changed(gui);
    }
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_MODE) {
        GrainDistMode mode = gwy_params_get_enum(params, PARAM_MODE);
        gwy_param_table_set_sensitive(table, PARAM_ADD_COMMENT, mode == MODE_RAW);
        gwy_param_table_set_sensitive(table, PARAM_RESOLUTION, mode == MODE_GRAPH);
    }

    if (id < 0 || id == PARAM_SELECTED) {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK,
                                          strlen(gwy_params_get_string(params, PARAM_SELECTED)));
    }

    if (id == PARAM_RESOLUTION || id == PARAM_FIXRES)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
selected_changed(ModuleGUI *gui)
{
    const gchar **selected_rows;
    gchar *s;

    selected_rows = gwy_grain_value_tree_view_get_enabled(GTK_TREE_VIEW(gui->values));
    s = g_strjoinv("\n", (gchar**)selected_rows);
    g_free(selected_rows);
    gwy_params_set_string(gui->args->params, PARAM_SELECTED, s);
    g_free(s);
    gwy_param_table_param_changed(gui->table, PARAM_SELECTED);
}

static void
row_expanded_collapsed(ModuleGUI *gui)
{
    guint expanded = gwy_grain_value_tree_view_get_expanded_groups(GTK_TREE_VIEW(gui->values));
    gwy_params_set_int(gui->args->params, PARAM_EXPANDED, expanded);
    gwy_param_table_param_changed(gui->table, PARAM_EXPANDED);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->values));
    GtkTreeModel *model;
    GrainDistExportData expdata;
    GwyGrainValue *gvalue;
    GwyDataLine *dline;
    GtkTreeIter iter;
    gdouble *d;

    gwy_graph_model_remove_all_curves(gui->gmodel);
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 0, &gvalue, -1);
    expdata.args = args;
    expdata.nvalues = 1;
    expdata.gvalues = &gvalue;
    dline = gwy_data_line_new(args->ngrains+1, 1.0, FALSE);
    d = gwy_data_line_get_data(dline);
    expdata.rawvalues = &dline;
    gwy_grain_values_calculate(1, expdata.gvalues, &d, args->field, args->ngrains, args->grains);
    add_one_distribution(gui->gmodel, &expdata, 0);
    g_object_unref(dline);
}

static void
add_one_distribution(GwyGraphModel *gmodel, GrainDistExportData *expdata, guint i)
{
    GwyParams *params = expdata->args->params;
    GwyDataField *field = expdata->args->field;
    GwyGraphCurveModel *cmodel;
    GwyDataLine *dline, *distribution;
    GwyGrainValue *gvalue;
    const gchar *name;
    gint res;

    dline = expdata->rawvalues[i];
    gvalue = expdata->gvalues[i];
    gwy_si_unit_power_multiply(gwy_data_field_get_si_unit_xy(field), gwy_grain_value_get_power_xy(gvalue),
                               gwy_data_field_get_si_unit_z(field), gwy_grain_value_get_power_z(gvalue),
                               gwy_data_line_get_si_unit_y(dline));
    /* Get rid of the zeroth bogus item corresponding to no grain. */
    gwy_data_line_resize(dline, 1, gwy_data_line_get_res(dline));
    res = gwy_params_get_boolean(params, PARAM_FIXRES) ? gwy_params_get_int(params, PARAM_RESOLUTION) : 0;
    distribution = gwy_data_line_new(res ? res : 1, 1.0, FALSE);
    gwy_data_line_distribution(dline, distribution, 0.0, 0.0, FALSE, res);
    /* Make the values centered in bins.  Changing gwy_data_line_distribution() to do that itself would be
     * incompatible and of course changing gwy_graph_curve_model_set_data_from_dataline() is impossible. */
    gwy_data_line_set_offset(distribution, distribution->off + 0.5*distribution->real/distribution->res);

    cmodel = gwy_graph_curve_model_new();
    gwy_graph_model_add_curve(gmodel, cmodel);
    g_object_unref(cmodel);

    name = gettext(gwy_resource_get_name(GWY_RESOURCE(gvalue)));
    g_object_set(gmodel,
                 "title", name,
                 "axis-label-left", _("count"),
                 "axis-label-bottom", gwy_grain_value_get_symbol_markup(gvalue),
                 NULL);
    gwy_graph_model_set_units_from_data_line(gmodel, distribution);
    g_object_set(cmodel, "description", name, NULL);
    gwy_graph_curve_model_set_data_from_dataline(cmodel, distribution, 0, 0);
    g_object_unref(distribution);
}

static void
execute(ModuleArgs *args, GwyContainer *data)
{
    GrainDistMode mode = gwy_params_get_enum(args->params, PARAM_MODE);
    GrainDistExportData expdata;
    GwyGrainValue *gvalue;
    GwyDataLine *dline;
    gchar **selected_quantities;
    guint i, nvalues;
    gdouble **results;

    expdata.args = args;
    selected_quantities = g_strsplit(gwy_params_get_string(args->params, PARAM_SELECTED), "\n", 0);
    nvalues = g_strv_length(selected_quantities);
    expdata.gvalues = g_new(GwyGrainValue*, nvalues);
    expdata.rawvalues = g_new(GwyDataLine*, nvalues);
    expdata.add_comment = gwy_params_get_boolean(args->params, PARAM_ADD_COMMENT);
    results = g_new(gdouble*, nvalues);
    for (nvalues = i = 0; selected_quantities[i]; i++) {
        gvalue = gwy_grain_values_get_grain_value(selected_quantities[nvalues]);
        if (!gvalue)
            continue;
        if (!args->units_equal && (gwy_grain_value_get_flags(gvalue) & GWY_GRAIN_VALUE_SAME_UNITS))
            continue;
        expdata.gvalues[nvalues] = gvalue;
        dline = gwy_data_line_new(args->ngrains+1, 1.0, FALSE);
        expdata.rawvalues[nvalues] = dline;
        results[nvalues] = gwy_data_line_get_data(dline);
        nvalues++;
    }
    expdata.nvalues = nvalues;
    g_strfreev(selected_quantities);

    gwy_grain_values_calculate(nvalues, expdata.gvalues, results, args->field, args->ngrains, args->grains);
    g_free(results);

    if (mode == MODE_GRAPH) {
        for (i = 0; i < expdata.nvalues; i++) {
            GwyGraphModel *gmodel = gwy_graph_model_new();

            add_one_distribution(gmodel, &expdata, i);
            gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);
            g_object_unref(gmodel);
        }
    }
    else if (mode == MODE_RAW) {
        gwy_save_auxiliary_with_callback(_("Export Raw Grain Values"), NULL,
                                         grain_dist_export_create, (GwySaveAuxiliaryDestroy)g_free, &expdata);
    }
    else {
        g_assert_not_reached();
    }

    for (i = 0; i < expdata.nvalues; i++)
        g_object_unref(expdata.rawvalues[i]);
    g_free(expdata.rawvalues);
    g_free(expdata.gvalues);
}

static gchar*
grain_dist_export_create(gpointer user_data, gssize *data_len)
{
    const GrainDistExportData *expdata = (const GrainDistExportData*)user_data;
    GString *report;
    gchar buffer[32];
    gint gno;
    gchar *retval;
    guint i, ngrains = 0;
    gdouble val;

    if (expdata->nvalues)
        ngrains = gwy_data_line_get_res(expdata->rawvalues[0]) - 1;

    report = g_string_sized_new(12*ngrains*expdata->nvalues);

    if (expdata->add_comment) {
        g_string_append_c(report, '#');
        for (i = 0; i < expdata->nvalues; i++) {
            g_string_append_c(report, '\t');
            g_string_append(report, gwy_grain_value_get_symbol(expdata->gvalues[i]));
        }
        g_string_append_c(report, '\n');
    }

    for (gno = 1; gno <= ngrains; gno++) {
        for (i = 0; i < expdata->nvalues; i++) {
            val = gwy_data_line_get_val(expdata->rawvalues[i], gno);
            g_ascii_formatd(buffer, sizeof(buffer), "%g", val);
            g_string_append(report, buffer);
            g_string_append_c(report, i == expdata->nvalues-1 ? '\n' : '\t');
        }
    }

    retval = report->str;
    g_string_free(report, FALSE);
    *data_len = -1;

    return retval;
}

static gchar*
rectify_grain_quantity_list(const gchar *s)
{
    GwyInventory *inventory = gwy_grain_values();
    gchar **values = g_strsplit(s, "\n", 0);
    gchar *rectified;
    guint i, j;

    if (!values)
        return NULL;

    for (i = j = 0; values[i]; i++) {
        if (gwy_inventory_get_item(inventory, values[i]))
            values[j++] = values[i];
        else
            GWY_FREE(values[i]);
    }
    rectified = g_strjoinv("\n", values);
    g_strfreev(values);

    return rectified;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
