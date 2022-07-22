/*
 *  $Id: grain_mark.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/grains.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_HEIGHT,
    PARAM_IS_HEIGHT,
    PARAM_SLOPE,
    PARAM_IS_SLOPE,
    PARAM_LAP,
    PARAM_IS_LAP,
    PARAM_INVERTED,
    PARAM_MERGE_TYPE,
    PARAM_COMBINE_TYPE,
    PARAM_COMBINE,
    PARAM_UPDATE,
    PARAM_MASK_COLOR,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             grain_mark          (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Marks grains by thresholding (height, slope, curvature)."),
    "Petr Klapetek <petr@klapetek.cz>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY2(module_info, grain_mark)

static gboolean
module_register(void)
{
    gwy_process_func_register("grain_mark",
                              (GwyProcessFunc)&grain_mark,
                              N_("/_Grains/_Mark by Threshold..."),
                              GWY_STOCK_GRAINS,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Mark grains by threshold"));

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
    gwy_param_def_add_percentage(paramdef, PARAM_HEIGHT, "height", _("_Height"), 0.5);
    gwy_param_def_add_boolean(paramdef, PARAM_IS_HEIGHT, "isheight", NULL, TRUE);
    gwy_param_def_add_percentage(paramdef, PARAM_SLOPE, "slope", _("_Slope"), 0.5);
    gwy_param_def_add_boolean(paramdef, PARAM_IS_SLOPE, "isslope", NULL, FALSE);
    gwy_param_def_add_percentage(paramdef, PARAM_LAP, "lap", _("_Curvature"), 0.5);
    gwy_param_def_add_boolean(paramdef, PARAM_IS_LAP, "islap", NULL, FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_INVERTED, "inverted", _("_Invert height"), FALSE);
    gwy_param_def_add_enum(paramdef, PARAM_MERGE_TYPE, "merge_type", _("Criteria combination"),
                           GWY_TYPE_MERGE_TYPE, GWY_MERGE_UNION);
    gwy_param_def_add_enum(paramdef, PARAM_COMBINE_TYPE, "combine_type", NULL, GWY_TYPE_MERGE_TYPE, GWY_MERGE_UNION);
    gwy_param_def_add_boolean(paramdef, PARAM_COMBINE, "combine", NULL, FALSE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
grain_mark(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GQuark mquark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     0);
    g_return_if_fail(args.field && mquark);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    if (gwy_data_field_get_max(args.result) > 0.0)
        gwy_container_set_object(data, mquark, args.result);
    else
        gwy_container_remove(data, mquark);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();

    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/0/mask", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Mark Grains by Threshold"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Threshold by"));
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_add_enabler(table, PARAM_IS_HEIGHT, PARAM_HEIGHT);
    gwy_param_table_slider_add_alt(table, PARAM_HEIGHT);
    gwy_param_table_alt_set_field_frac_z(table, PARAM_HEIGHT, args->field);
    gwy_param_table_append_slider(table, PARAM_SLOPE);
    gwy_param_table_add_enabler(table, PARAM_IS_SLOPE, PARAM_SLOPE);
    gwy_param_table_append_slider(table, PARAM_LAP);
    gwy_param_table_add_enabler(table, PARAM_IS_LAP, PARAM_LAP);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_INVERTED);
    gwy_param_table_append_radio_buttons(table, PARAM_MERGE_TYPE, NULL);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);
    if (args->mask) {
        gwy_param_table_append_radio_buttons(table, PARAM_COMBINE_TYPE, NULL);
        gwy_param_table_add_enabler(table, PARAM_COMBINE, PARAM_COMBINE_TYPE);
    }
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id != PARAM_MASK_COLOR && id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    execute(args);
    gwy_data_field_data_changed(args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
combine_masks(GwyDataField *result, GwyDataField *operand, GwyMergeType merge_type)
{
    if (merge_type == GWY_MERGE_UNION)
        gwy_data_field_grains_add(result, operand);
    else if (merge_type == GWY_MERGE_INTERSECTION)
        gwy_data_field_grains_intersect(result, operand);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble height_perc = gwy_params_get_double(params, PARAM_HEIGHT)*100.0;
    gdouble slope_perc = gwy_params_get_double(params, PARAM_SLOPE)*100.0;
    gdouble curvature_perc = gwy_params_get_double(params, PARAM_LAP)*100.0;
    gboolean inverted = gwy_params_get_boolean(params, PARAM_INVERTED);
    GwyMergeType merge_type = gwy_params_get_enum(params, PARAM_MERGE_TYPE);
    GwyMergeType combine_type = gwy_params_get_enum(params, PARAM_COMBINE_TYPE);
    GwyDataField *field = args->field, *mask = args->mask, *result = args->result, *tmp = NULL;
    gboolean field_been_set = FALSE;

    if (gwy_params_get_boolean(params, PARAM_IS_HEIGHT)) {
        gwy_data_field_grains_mark_height(field, result, height_perc, inverted);
        field_been_set = TRUE;
    }
    if (gwy_params_get_boolean(params, PARAM_IS_SLOPE)) {
        if (field_been_set) {
            tmp = gwy_data_field_new_alike(result, FALSE);
            gwy_data_field_grains_mark_slope(field, tmp, slope_perc, FALSE);
            combine_masks(result, tmp, merge_type);
        }
        else
            gwy_data_field_grains_mark_slope(field, result, slope_perc, FALSE);
        field_been_set = TRUE;
    }
    if (gwy_params_get_boolean(params, PARAM_IS_LAP)) {
        if (field_been_set) {
            if (!tmp)
                tmp = gwy_data_field_new_alike(result, FALSE);
            gwy_data_field_grains_mark_curvature(field, tmp, curvature_perc, FALSE);
            combine_masks(result, tmp, merge_type);
        }
        else
            gwy_data_field_grains_mark_curvature(field, result, curvature_perc, FALSE);
        field_been_set = TRUE;
    }
    if (!field_been_set)
        gwy_data_field_clear(result);
    if (mask && gwy_params_get_boolean(params, PARAM_COMBINE))
        combine_masks(result, mask, combine_type);

    GWY_OBJECT_UNREF(tmp);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
