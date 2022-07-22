/*
 *  $Id: mask_distribute.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <libgwyddion/gwymacros.h>
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    DISTRIBUTE_WITHIN_FILE  = 0,
    DISTRIBUTE_TO_ALL_FILES = 1,
} MaskDistribMode;

enum {
    PARAM_KEEP_EXISTING,
    PARAM_MODE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *mask;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GArray *undo_quarks;
    GwyContainer *container;
    gint id;
} MaskDistribData;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             mask_distribute     (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             distribute_in_one   (GwyContainer *container,
                                             MaskDistribData *distdata);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Distributes masks to other channels."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, mask_distribute)

static gboolean
module_register(void)
{
    gwy_process_func_register("mask_distribute",
                              (GwyProcessFunc)&mask_distribute,
                              N_("/_Mask/_Distribute..."),
                              GWY_STOCK_MASK_DISTRIBUTE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Distribute mask to other channels"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum modes[] = {
        { N_("Channels within the file"), DISTRIBUTE_WITHIN_FILE,  },
        { N_("Channels in all files"),    DISTRIBUTE_TO_ALL_FILES, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_boolean(paramdef, PARAM_KEEP_EXISTING, "keep_existing", _("Preserve existing masks"), FALSE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", _("Distribute to"),
                              modes, G_N_ELEMENTS(modes), DISTRIBUTE_WITHIN_FILE);
    return paramdef;
}

static void
mask_distribute(GwyContainer *data, GwyRunType runtype)
{
    MaskDistribData distdata;
    ModuleArgs args;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.mask && id >= 0);

    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype != GWY_RUN_IMMEDIATE) {
        GwyDialogOutcome outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }

    distdata.container = data;
    distdata.id = id;
    distdata.args = &args;
    distdata.undo_quarks = g_array_new(FALSE, FALSE, sizeof(GQuark));

    if (gwy_params_get_enum(args.params, PARAM_MODE) == DISTRIBUTE_TO_ALL_FILES)
        gwy_app_data_browser_foreach((GwyAppDataForeachFunc)distribute_in_one, &distdata);
    else
        distribute_in_one(data, &distdata);

    g_array_free(distdata.undo_quarks, TRUE);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Distribute Mask"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_MODE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_KEEP_EXISTING);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(gui.table), FALSE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, gui.table);

    return gwy_dialog_run(dialog);
}

static void
distribute_in_one(GwyContainer *container, MaskDistribData *distdata)
{
    GwyDataField *field, *mask;
    GwyDataCompatibilityFlags flags = GWY_DATA_COMPATIBILITY_RES | GWY_DATA_COMPATIBILITY_REAL;
    gboolean keep_existing = gwy_params_get_boolean(distdata->args->params, PARAM_KEEP_EXISTING);
    GArray *undo_quarks = distdata->undo_quarks;
    GQuark quark;
    gint *ids;
    guint i;

    g_array_set_size(undo_quarks, 0);
    ids = gwy_app_data_browser_get_data_ids(container);

    for (i = 0; ids[i] >= 0; i++) {
        if (container == distdata->container && ids[i] == distdata->id)
            continue;

        quark = gwy_app_get_data_key_for_id(ids[i]);
        field = gwy_container_get_object(container, quark);
        g_return_if_fail(field);

        quark = gwy_app_get_mask_key_for_id(ids[i]);
        if (!gwy_container_gis_object(container, quark, &mask))
            mask = NULL;

        if (mask && keep_existing)
            continue;
        if (gwy_data_field_check_compatibility(distdata->args->mask, field, flags))
            continue;

        g_array_append_val(undo_quarks, quark);
        gwy_app_channel_log_add_proc(container, -1, ids[i]);
    }
    g_free(ids);

    if (!undo_quarks->len)
        return;

    gwy_app_undo_qcheckpointv(container, undo_quarks->len, (GQuark*)undo_quarks->data);
    for (i = 0; i < undo_quarks->len; i++) {
        quark = g_array_index(undo_quarks, GQuark, i);
        mask = gwy_data_field_duplicate(distdata->args->mask);
        gwy_container_set_object(container, quark, mask);
        g_object_unref(mask);
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
