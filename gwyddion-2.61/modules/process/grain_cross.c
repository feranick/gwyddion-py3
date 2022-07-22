/*
 *  $Id: grain_cross.c 24650 2022-03-08 14:34:03Z yeti-dn $
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
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/arithmetic.h>
#include <libprocess/grains.h>
#include <libgwydgets/gwygrainvaluemenu.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_ABSCISSA,
    PARAM_ABSCISSA_EXPANDED,
    PARAM_ORDINATE,
    PARAM_ORDINATE_EXPANDED,
    PARAM_DIFFERENT_ORDINATE,
    PARAM_OTHER_IMAGE,
    PARAM_TARGET_GRAPH,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyGraphModel *gmodel;
    /* Cached input data properties. */
    guint ngrains;
    gint *grains;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GtkTreeView *abscissa;
    GtkTreeView *ordinate;
} ModuleGUI;

static gboolean         module_register      (void);
static GwyParamDef*     define_module_params (void);
static void             grain_cross          (GwyContainer *data,
                                              GwyRunType runtype);
static void             execute              (ModuleArgs *args);
static GwyDialogOutcome run_gui              (ModuleArgs *args);
static void             param_changed        (ModuleGUI *gui,
                                              gint id);
static void             preview              (gpointer user_data);
static void             axis_quantity_changed(ModuleGUI *gui);
static gboolean         other_image_filter   (GwyContainer *data,
                                              gint id,
                                              gpointer user_data);
static GwyDataField*    get_ordinate_field   (ModuleArgs *args);
static GtkTreeView*     attach_axis_list     (GtkWidget *table,
                                              const gchar *name,
                                              gint column,
                                              gint idvalue,
                                              gint idexpanded,
                                              GwyDataField *field,
                                              ModuleGUI *gui);
static void             set_graph_model_units(ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Plots one grain quantity as a function of another."),
    "Yeti <yeti@gwyddion.net>",
    "4.0",
    "David Nečas",
    "2007",
};

GWY_MODULE_QUERY2(module_info, grain_cross)

static gboolean
module_register(void)
{
    gwy_process_func_register("grain_cross",
                              (GwyProcessFunc)&grain_cross,
                              N_("/_Grains/_Correlate..."),
                              GWY_STOCK_GRAIN_CORRELATION,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Correlate grain characteristics"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_resource(paramdef, PARAM_ABSCISSA, "abscissa", _("_Abscissa"),
                               gwy_grain_values(), "Equivalent disc radius");
    gwy_param_def_add_grain_groups(paramdef, PARAM_ABSCISSA_EXPANDED, "abscissa_expanded", NULL,
                                   1 << GWY_GRAIN_VALUE_GROUP_AREA);
    gwy_param_def_add_resource(paramdef, PARAM_ORDINATE, "ordinate", _("O_rdinate"),
                               gwy_grain_values(), "Projected boundary length");
    gwy_param_def_add_grain_groups(paramdef, PARAM_ORDINATE_EXPANDED, "ordinate_expanded", NULL,
                                   1 << GWY_GRAIN_VALUE_GROUP_BOUNDARY);
    gwy_param_def_add_boolean(paramdef, PARAM_DIFFERENT_ORDINATE, "different_ordinate",
                              _("Ordinate data calculated from different image"), FALSE);
    gwy_param_def_add_image_id(paramdef, PARAM_OTHER_IMAGE, "other_image", _("Ordinate _image"));
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    return paramdef;
}

static gboolean
check_same_units(GwyParams *params, gint idvalue, gint idexpanded,
                 GwyDataField *field, GwyContainer *data, gint id,
                 GwyRunType runtype)
{
    GwyGrainValue *gvalue = GWY_GRAIN_VALUE(gwy_params_get_resource(params, idvalue));
    GwyGrainValueFlags flags = gwy_grain_value_get_flags(gvalue);

    if (!(flags & GWY_GRAIN_VALUE_SAME_UNITS))
        return TRUE;
    if (gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(field), gwy_data_field_get_si_unit_z(field)))
        return TRUE;

    /* Non-interactively, we complain.  But interactively we just reset to valid values. */
    if (runtype == GWY_RUN_IMMEDIATE) {
        gwy_require_image_same_units(field, data, id, _("Grain Correlations"));
        return FALSE;
    }

    gwy_params_reset(params, idvalue);
    gwy_params_reset(params, idexpanded);
    return TRUE;
}

static void
grain_cross(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GwyAppDataId target_graph_id;
    ModuleArgs args;
    GwyDataField *ordfield;
    GwyParams *params;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field && args.mask);
    args.params = params = gwy_params_new_from_settings(define_module_params());
    if (gwy_params_data_id_is_none(params, PARAM_OTHER_IMAGE))
        gwy_params_set_boolean(params, PARAM_DIFFERENT_ORDINATE, FALSE);
    ordfield = get_ordinate_field(&args);
    if (!check_same_units(params, PARAM_ABSCISSA, PARAM_ABSCISSA_EXPANDED, args.field, data, id, runtype)
        || !check_same_units(params, PARAM_ORDINATE, PARAM_ORDINATE_EXPANDED, ordfield, data, id, runtype))
        goto end;

    args.gmodel = gwy_graph_model_new();
    set_graph_model_units(&args);
    args.grains = g_new0(gint, gwy_data_field_get_xres(args.mask)*gwy_data_field_get_yres(args.mask));
    args.ngrains = gwy_data_field_number_grains(args.mask, args.grains);
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    target_graph_id = gwy_params_get_data_id(params, PARAM_TARGET_GRAPH);
    gwy_app_add_graph_or_curves(args.gmodel, data, &target_graph_id, 1);

end:
    g_free(args.grains);
    GWY_OBJECT_UNREF(args.gmodel);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GtkWidget *gtktable, *graph;
    GwyParamTable *table;

    gui.args = args;
    gui.dialog = gwy_dialog_new(_("Grain Correlations"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 860, 520);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK, !!args->ngrains);

    gtktable = gtk_table_new(3, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(gtktable), 2);
    gtk_table_set_col_spacings(GTK_TABLE(gtktable), 6);
    gtk_container_set_border_width(GTK_CONTAINER(gtktable), 4);
    gwy_dialog_add_content(dialog, gtktable, TRUE, TRUE, 0);

    graph = gwy_graph_new(args->gmodel);
    gtk_widget_set_size_request(graph, PREVIEW_SMALL_SIZE, -1);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_table_attach(GTK_TABLE(gtktable), graph, 0, 1, 0, 3, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    gui.abscissa = attach_axis_list(gtktable, _("_Abscissa"), 1, PARAM_ABSCISSA, PARAM_ABSCISSA_EXPANDED,
                                    args->field, &gui);
    gui.ordinate = attach_axis_list(gtktable, _("O_rdinate"), 2, PARAM_ORDINATE, PARAM_ORDINATE_EXPANDED,
                                    get_ordinate_field(args), &gui);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_checkbox(table, PARAM_DIFFERENT_ORDINATE);
    gwy_param_table_append_image_id(table, PARAM_OTHER_IMAGE);
    gwy_param_table_data_id_set_filter(table, PARAM_OTHER_IMAGE, other_image_filter, args->field, NULL);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);

    gtk_table_attach(GTK_TABLE(gtktable), gwy_param_table_widget(table), 1, 3, 2, 3, GTK_FILL, 0, 0, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gtk_tree_view_get_selection(gui.abscissa), "changed",
                             G_CALLBACK(axis_quantity_changed), &gui);
    g_signal_connect_swapped(gtk_tree_view_get_selection(gui.ordinate), "changed",
                             G_CALLBACK(axis_quantity_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    return gwy_dialog_run(dialog);
}

static void
axis_quantity_changed(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyGrainValue *gvalue;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean ok = !!args->ngrains;

    gwy_params_set_flags(params, PARAM_ABSCISSA_EXPANDED, gwy_grain_value_tree_view_get_expanded_groups(gui->abscissa));
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(gui->abscissa), &model, &iter)) {
        gtk_tree_model_get(model, &iter, GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue, -1);
        gwy_params_set_resource(params, PARAM_ABSCISSA, gwy_resource_get_name(GWY_RESOURCE(gvalue)));
    }
    else
        ok = FALSE;

    gwy_params_set_flags(params, PARAM_ORDINATE_EXPANDED, gwy_grain_value_tree_view_get_expanded_groups(gui->ordinate));
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(gui->ordinate), &model, &iter)) {
        gtk_tree_model_get(model, &iter, GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue, -1);
        gwy_params_set_resource(params, PARAM_ORDINATE, gwy_resource_get_name(GWY_RESOURCE(gvalue)));
    }
    else
        ok = FALSE;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, ok);
    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_DIFFERENT_ORDINATE || id == PARAM_OTHER_IMAGE) {
        GwyGrainValue *gvalue = GWY_GRAIN_VALUE(gwy_params_get_resource(params, PARAM_ORDINATE));
        GwyGrainValueFlags flags = gwy_grain_value_get_flags(gvalue);
        GwyDataField *ordfield = get_ordinate_field(args);
        gboolean same_units = gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(ordfield),
                                                gwy_data_field_get_si_unit_z(ordfield));
        /* XXX: The grain value tree view is not smart enough to move the selection away from a disabled item
         * – basically it does not know where.  And GtkTreeView does not allow selecting something else if an
         * insensitive item is currently selected.  So we have to fix it manually. */
        if ((flags & GWY_GRAIN_VALUE_SAME_UNITS) && !same_units) {
            gwy_params_reset(params, PARAM_ORDINATE);
            gvalue = GWY_GRAIN_VALUE(gwy_params_get_resource(params, PARAM_ORDINATE));
            gwy_grain_value_tree_view_select(gui->ordinate, gvalue);
        }
        gwy_grain_value_tree_view_set_same_units(gui->ordinate, same_units);
    }
    if (id < 0 || id == PARAM_DIFFERENT_ORDINATE) {
        gboolean different_ordinate = gwy_params_get_boolean(params, PARAM_DIFFERENT_ORDINATE);
        gwy_param_table_set_sensitive(gui->table, PARAM_OTHER_IMAGE, different_ordinate);
    }

    if (id != PARAM_TARGET_GRAPH)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_param_table_data_id_refilter(gui->table, PARAM_TARGET_GRAPH);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static gboolean
other_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *field = (GwyDataField*)user_data;
    GwyDataField *otherfield = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));

    /* Do not reject the field itself.  This ensures the chooser is non-empty. */
    return !gwy_data_field_check_compatibility(field, otherfield,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL);
}

static GwyDataField*
get_ordinate_field(ModuleArgs *args)
{
    gboolean different_ordinate = gwy_params_get_boolean(args->params, PARAM_DIFFERENT_ORDINATE);
    return different_ordinate ? gwy_params_get_image(args->params, PARAM_OTHER_IMAGE) : args->field;
}

static GtkTreeView*
attach_axis_list(GtkWidget *table, const gchar *name,
                 gint column, gint idvalue, gint idexpanded,
                 GwyDataField *field, ModuleGUI *gui)
{
    guint expanded = gwy_params_get_flags(gui->args->params, idexpanded);
    GwyGrainValue *gvalue = GWY_GRAIN_VALUE(gwy_params_get_resource(gui->args->params, idvalue));
    GtkWidget *label, *widget, *scwin;
    GtkTreeView *list;
    gboolean same_units;

    label = gtk_label_new_with_mnemonic(name);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, column, column+1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_table_attach(GTK_TABLE(table), scwin, column, column+1, 1, 2,
                     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

    widget = gwy_grain_value_tree_view_new(FALSE, "name", NULL);
    list = GTK_TREE_VIEW(widget);
    gtk_tree_view_set_headers_visible(list, FALSE);
    gwy_grain_value_tree_view_set_expanded_groups(list, expanded);
    same_units = gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(field), gwy_data_field_get_si_unit_z(field));
    gwy_grain_value_tree_view_set_same_units(list, same_units);
    gwy_grain_value_tree_view_select(list, gvalue);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), widget);
    gtk_container_add(GTK_CONTAINER(scwin), widget);

    return list;
}

static int
compare_doubles(const void *a, const void *b)
{
    gdouble da = *(gdouble*)a;
    gdouble db = *(gdouble*)b;

    if (da < db)
        return -1;
    if (db < da)
        return 1;
    return 0.0;
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyGraphModel *gmodel = args->gmodel;
    GwyDataField *absfield = args->field, *ordfield = get_ordinate_field(args);
    GwyGraphCurveModel *cmodel;
    GwyGrainValue *gvalues[2];
    gdouble *xdata, *ydata, *bothdata, *rdata[2];
    const gchar *title;
    gint i, ngrains = args->ngrains;

    gvalues[0] = GWY_GRAIN_VALUE(gwy_params_get_resource(params, PARAM_ABSCISSA));
    gvalues[1] = GWY_GRAIN_VALUE(gwy_params_get_resource(params, PARAM_ORDINATE));

    bothdata = g_new(gdouble, 4*ngrains + 2);
    rdata[0] = xdata = bothdata + 2*ngrains;
    rdata[1] = ydata = bothdata + 3*ngrains + 1;

    if (ordfield != absfield) {
        gwy_grain_values_calculate(1, gvalues + 0, rdata + 0, absfield, ngrains, args->grains);
        gwy_grain_values_calculate(1, gvalues + 1, rdata + 1, ordfield, ngrains, args->grains);
    }
    else
        gwy_grain_values_calculate(2, gvalues, rdata, absfield, ngrains, args->grains);

    for (i = 0; i < ngrains; i++) {
        bothdata[2*i + 0] = xdata[i+1];
        bothdata[2*i + 1] = ydata[i+1];
    }
    qsort(bothdata, ngrains, 2*sizeof(gdouble), compare_doubles);
    for (i = 0; i < ngrains; i++) {
        xdata[i] = bothdata[2*i + 0];
        ydata[i] = bothdata[2*i + 1];
    }

    cmodel = gwy_graph_curve_model_new();
    title = gwy_resource_get_name(GWY_RESOURCE(gvalues[1]));
    g_object_set(cmodel,
                 "description", title,
                 "mode", GWY_GRAPH_CURVE_POINTS,
                 NULL);
    gwy_graph_curve_model_set_data(cmodel, xdata, ydata, ngrains);
    g_free(bothdata);

    gwy_graph_model_remove_all_curves(gmodel);
    gwy_graph_model_add_curve(gmodel, cmodel);
    g_object_unref(cmodel);

    g_object_set(gmodel,
                 "title", title,
                 "axis-label-left", gwy_grain_value_get_symbol_markup(gvalues[1]),
                 "axis-label-bottom", gwy_grain_value_get_symbol_markup(gvalues[0]),
                 NULL);
    set_graph_model_units(args);
}

static void
set_graph_model_units(ModuleArgs *args)
{
    GwyDataField *absfield = args->field, *ordfield = get_ordinate_field(args);
    GwyGrainValue *gvalues[2];
    GwySIUnit *unit = gwy_si_unit_new(NULL);

    gvalues[0] = GWY_GRAIN_VALUE(gwy_params_get_resource(args->params, PARAM_ABSCISSA));
    gvalues[1] = GWY_GRAIN_VALUE(gwy_params_get_resource(args->params, PARAM_ORDINATE));

    gwy_si_unit_power_multiply(gwy_data_field_get_si_unit_xy(absfield), gwy_grain_value_get_power_xy(gvalues[0]),
                               gwy_data_field_get_si_unit_z(absfield), gwy_grain_value_get_power_z(gvalues[0]),
                               unit);
    g_object_set(args->gmodel, "si-unit-x", unit, NULL);
    gwy_si_unit_power_multiply(gwy_data_field_get_si_unit_xy(ordfield), gwy_grain_value_get_power_xy(gvalues[1]),
                               gwy_data_field_get_si_unit_z(ordfield), gwy_grain_value_get_power_z(gvalues[1]),
                               unit);
    g_object_set(args->gmodel, "si-unit-y", unit, NULL);
    g_object_unref(unit);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
