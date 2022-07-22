/*
 *  $Id: grain_wshed.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_INVERTED,
    PARAM_LOCATE_STEPS,
    PARAM_LOCATE_THRESH,
    PARAM_LOCATE_DROPSIZE,
    PARAM_WSHED_STEPS,
    PARAM_WSHED_DROPSIZE,
    PARAM_COMBINE_TYPE,
    PARAM_COMBINE,
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
static void             grain_wshed         (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *wait_window);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Marks grains by watershed algorithm."),
    "Petr Klapetek <petr@klapetek.cz>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, grain_wshed)

static gboolean
module_register(void)
{
    gwy_process_func_register("grain_wshed",
                              (GwyProcessFunc)&grain_wshed,
                              N_("/_Grains/Mark by _Watershed..."),
                              GWY_STOCK_GRAINS_WATER,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Mark grains by watershed"));

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
    gwy_param_def_add_boolean(paramdef, PARAM_INVERTED, "inverted", _("_Invert height"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_LOCATE_STEPS, "locate_steps", _("_Number of steps"), 1, 200, 10);
    gwy_param_def_add_int(paramdef, PARAM_LOCATE_THRESH, "locate_thresh", _("T_hreshold"), 0, 200, 10);
    gwy_param_def_add_double(paramdef, PARAM_LOCATE_DROPSIZE, "locate_dropsize", _("_Drop size"), 0.0001, 1.0, 0.1);
    gwy_param_def_add_int(paramdef, PARAM_WSHED_STEPS, "wshed_steps", _("Num_ber of steps"), 1, 2000, 20);
    gwy_param_def_add_double(paramdef, PARAM_WSHED_DROPSIZE, "wshed_dropsize", _("Dr_op size"), 0.0001, 1.0, 0.1);
    gwy_param_def_add_enum(paramdef, PARAM_COMBINE_TYPE, "combine_type", NULL, GWY_TYPE_MERGE_TYPE, GWY_MERGE_UNION);
    gwy_param_def_add_boolean(paramdef, PARAM_COMBINE, "combine", NULL, FALSE);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
grain_wshed(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GQuark mquark;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field && mquark);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
            goto end;
    }

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    if (gwy_data_field_get_max(args.result) > 0.0)
        gwy_container_set_object(data, mquark, args.result);
    else
        gwy_container_remove(data, mquark);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview;
    GwyDialogOutcome outcome;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    gchar *s;

    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/0/mask", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Mark Grains by Watershed"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Grain Location"));
    gwy_param_table_append_slider(table, PARAM_LOCATE_STEPS);
    gwy_param_table_append_slider(table, PARAM_LOCATE_DROPSIZE);
    gwy_param_table_slider_set_factor(table, PARAM_LOCATE_DROPSIZE, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_LOCATE_DROPSIZE, "%");
    gwy_param_table_append_slider(table, PARAM_LOCATE_THRESH);
    s = g_strconcat(_("px"), "<sup>2</sup>", NULL);
    gwy_param_table_set_unitstr(table, PARAM_LOCATE_THRESH, s);
    g_free(s);

    gwy_param_table_append_header(table, -1, _("Segmentation"));
    gwy_param_table_append_slider(table, PARAM_WSHED_STEPS);
    gwy_param_table_append_slider(table, PARAM_WSHED_DROPSIZE);
    gwy_param_table_slider_set_factor(table, PARAM_WSHED_DROPSIZE, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_WSHED_DROPSIZE, "%");

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);
    gwy_param_table_append_checkbox(table, PARAM_INVERTED);
    if (args->mask) {
        gwy_param_table_append_radio_buttons(table, PARAM_COMBINE_TYPE, NULL);
        gwy_param_table_add_enabler(table, PARAM_COMBINE, PARAM_COMBINE_TYPE);
    }

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id != PARAM_MASK_COLOR)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    if (execute(args, GTK_WINDOW(gui->dialog))) {
        gwy_data_field_data_changed(args->result);
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    }
    else
        gwy_data_field_clear(args->result);
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gboolean combine = gwy_params_get_boolean(params, PARAM_COMBINE);
    GwyMergeType combine_type = gwy_params_get_enum(params, PARAM_COMBINE_TYPE);
    gint locate_steps = gwy_params_get_int(params, PARAM_LOCATE_STEPS);
    gint locate_thresh = gwy_params_get_int(params, PARAM_LOCATE_THRESH);
    gdouble locate_dropsize = gwy_params_get_double(params, PARAM_LOCATE_DROPSIZE);
    gint wshed_steps = gwy_params_get_int(params, PARAM_WSHED_STEPS);
    gdouble wshed_dropsize = gwy_params_get_double(params, PARAM_WSHED_DROPSIZE);
    gboolean inverted = gwy_params_get_boolean(params, PARAM_INVERTED);
    GwyDataField *newmask, *mask = args->mask;
    gdouble max, min, q;
    GwyComputationState *state;
    GwyWatershedStateType oldstate = -1;
    gboolean ok;

    max = gwy_data_field_get_max(args->field);
    min = gwy_data_field_get_min(args->field);
    q = (max - min)/5000.0 * 100.0;

    newmask = gwy_data_field_new_alike(args->result, FALSE);
    state = gwy_data_field_grains_watershed_init(args->field, newmask,
                                                 locate_steps, locate_thresh, locate_dropsize*q,
                                                 wshed_steps, wshed_dropsize*q,
                                                 FALSE, inverted);
    gwy_app_wait_start(wait_window, _("Initializing..."));

    do {
        gwy_data_field_grains_watershed_iteration(state);
        if (oldstate != state->state) {
            if (state->state == GWY_WATERSHED_STATE_MIN)
                ok = gwy_app_wait_set_message(_("Finding minima..."));
            else if (state->state == GWY_WATERSHED_STATE_LOCATE)
                ok = gwy_app_wait_set_message(_("Locating..."));
            else if (state->state == GWY_WATERSHED_STATE_WATERSHED)
                ok = gwy_app_wait_set_message(_("Simulating watershed..."));
            else if (state->state == GWY_WATERSHED_STATE_MARK)
                ok = gwy_app_wait_set_message(_("Marking boundaries..."));
            oldstate = state->state;
            if (!ok)
                break;
        }
        if (!gwy_app_wait_set_fraction(state->fraction))
            break;
    } while (state->state != GWY_WATERSHED_STATE_FINISHED);
    ok = (state->state == GWY_WATERSHED_STATE_FINISHED);

    gwy_app_wait_finish();
    gwy_data_field_grains_watershed_finalize(state);

    if (ok) {
        if (mask && combine) {
            if (combine_type == GWY_MERGE_UNION)
                gwy_data_field_grains_add(newmask, mask);
            else if (combine_type == GWY_MERGE_INTERSECTION)
                gwy_data_field_grains_intersect(newmask, mask);
        }
        gwy_data_field_threshold(newmask, 0.5, 0.0, 1.0);
        gwy_data_field_copy(newmask, args->result, FALSE);
    }
    g_object_unref(newmask);

    return ok;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
