/*
 *  $Id: volume_lawnize.c 24290 2021-10-07 15:12:30Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/brick.h>
#include <libprocess/lawn.h>
#include <libprocess/arithmetic.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    NOTHER_BRICK = 5,
};

enum {
    PARAM_ZCAL,
    PARAM_ALL,
    PARAM_ADD1,
    PARAM_ADD2,
    PARAM_ADD3,
    PARAM_ADD4,
    PARAM_ADD5,
    PARAM_ADD1_ENABLED,
    PARAM_ADD2_ENABLED,
    PARAM_ADD3_ENABLED,
    PARAM_ADD4_ENABLED,
    PARAM_ADD5_ENABLED,
};

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyLawn *result;
    /* Cached input brick info. */
    GwyDataLine *calibration;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             lawnize             (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static gboolean         other_brick_filter  (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Creates curve map data from volume data."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, volume_lawnize)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_lawnize",
                             (GwyVolumeFunc)&lawnize,
                             N_("/Convert to _Curve Map..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Convert to curve map"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;
    static gchar **paramnames = NULL;
    gint i;

    if (paramdef)
        return paramdef;

    paramnames = g_new(gchar*, 2*NOTHER_BRICK);
    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_boolean(paramdef, PARAM_ZCAL, "zcal", _("_Z calibration"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_ALL, "all", _("_All compatible data"), TRUE);
    for (i = 0; i < NOTHER_BRICK; i++) {
        paramnames[2*i] = g_strdup_printf("add%d", i);
        gwy_param_def_add_volume_id(paramdef, PARAM_ADD1 + i, paramnames[2*i], NULL);
        paramnames[2*i + 1] = g_strdup_printf("add%d_enabled", i);
        gwy_param_def_add_boolean(paramdef, PARAM_ADD1_ENABLED + i, paramnames[2*i + 1], NULL, FALSE);
    }
    return paramdef;
}

static void
lawnize(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyBrick *brick = NULL;
    GwyDataField *preview;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    args.result = NULL;
    args.brick = brick;
    args.calibration = gwy_brick_get_zcalibration(brick);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    execute(&args, data, oldid);

    preview = gwy_container_get_object(data, gwy_app_get_brick_preview_key_for_id(oldid));
    preview = gwy_data_field_duplicate(preview);
    newid = gwy_app_data_browser_add_lawn(args.result, preview, data, TRUE);
    g_object_unref(preview);
    gwy_app_curve_map_log_add(data, -1, newid, "volume::volume_lawnize", NULL);

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    gint i;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Convert to Curve Map"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_message(table, -1, _("Combine with other data:"));
    gwy_param_table_append_checkbox(table, PARAM_ZCAL);
    gwy_param_table_set_sensitive(table, PARAM_ZCAL, !!args->calibration);
    gwy_param_table_append_checkbox(table, PARAM_ALL);
    for (i = 0; i < NOTHER_BRICK; i++) {
        gwy_param_table_append_volume_id(table, PARAM_ADD1 + i);
        gwy_param_table_data_id_set_filter(table, PARAM_ADD1 + i, other_brick_filter, args->brick, NULL);
        gwy_param_table_add_enabler(table, PARAM_ADD1_ENABLED + i, PARAM_ADD1 + i);
    }
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 0);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;
    gint i;

    if (id < 0 || id == PARAM_ALL) {
        gboolean is_not_all = !gwy_params_get_boolean(params, PARAM_ALL);
        for (i = 0; i < NOTHER_BRICK; i++)
            gwy_param_table_set_sensitive(table, PARAM_ADD1 + i, is_not_all);
    }
}

typedef struct {
    GwyContainer *container;
    gint id;
    GwyBrick *brick;
    const gdouble *data;
} OtherData;

static GArray*
gather_other_bricks(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyParams *params = args->params;
    GArray *otherdata = g_array_new(FALSE, FALSE, sizeof(OtherData));
    OtherData item;
    GwyAppDataId dataid;
    gint i;

    item.container = data;
    item.id = id;
    item.brick = args->brick;
    g_array_append_val(otherdata, item);

    for (i = 0; i < NOTHER_BRICK; i++) {
        if (!gwy_params_get_boolean(params, PARAM_ADD1_ENABLED + i))
            continue;

        item.brick = gwy_params_get_volume(params, PARAM_ADD1 + i);
        dataid = gwy_params_get_data_id(params, PARAM_ADD1 + i);
        item.container = gwy_app_data_browser_get(dataid.datano);
        item.id = dataid.id;
        g_array_append_val(otherdata, item);
    }

    return otherdata;
}

static GArray*
gather_bricks_in_file(ModuleArgs *args, GwyContainer *data)
{
    GArray *otherdata = g_array_new(FALSE, FALSE, sizeof(OtherData));
    GwyBrick *brick = args->brick;
    OtherData item;
    gint *ids;
    gint i;

    ids = gwy_app_data_browser_get_volume_ids(data);
    for (i = 0; ids[i] >= 0; i++) {
        if (!other_brick_filter(data, ids[i], brick))
            continue;

        item.brick = gwy_container_get_object(data, gwy_app_get_brick_key_for_id(ids[i]));
        item.container = data;
        item.id = ids[i];
        g_array_append_val(otherdata, item);
    }
    g_free(ids);

    return otherdata;
}

static void
execute(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyParams *params = args->params;
    GwyBrick *brick = args->brick;
    GwyDataLine *calibration = gwy_params_get_boolean(params, PARAM_ZCAL) ? args->calibration : NULL;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick), zres = gwy_brick_get_zres(brick);
    const gdouble *caldata;
    gdouble *curvedata;
    GwyLawn *lawn;
    gint i, j, k, m, n, nbricks, ncurves;
    GArray *allbricks_array;
    OtherData *allbricks;
    gchar *s;

    if (gwy_params_get_boolean(params, PARAM_ALL))
        allbricks_array = gather_bricks_in_file(args, data);
    else
        allbricks_array = gather_other_bricks(args, data, id);
    allbricks = &g_array_index(allbricks_array, OtherData, 0);

    nbricks = allbricks_array->len;
    ncurves = nbricks + !!calibration;
    caldata = calibration ? gwy_data_line_get_data(calibration) : NULL;
    for (m = 0; m < nbricks; m++)
        allbricks[m].data = gwy_brick_get_data_const(allbricks[m].brick);

    curvedata = g_new(gdouble, zres*ncurves);
    args->result = lawn = gwy_lawn_new(xres, yres, gwy_brick_get_xreal(brick), gwy_brick_get_yreal(brick), ncurves, 0);
    gwy_lawn_set_xoffset(lawn, gwy_brick_get_xoffset(brick));
    gwy_lawn_set_yoffset(lawn, gwy_brick_get_yoffset(brick));
    /* FIXME: Absolutely bonkers memory access pattern. */
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            n = 0;
            if (caldata) {
                gwy_assign(curvedata + n, caldata, zres);
                n += zres;
            }
            for (m = 0; m < nbricks; m++) {
                for (k = 0; k < zres; k++)
                    curvedata[n++] = allbricks[m].data[i*xres + j + xres*yres*k];
            }
            gwy_lawn_set_curves(lawn, j, i, zres, curvedata, NULL);
        }
    }

    gwy_si_unit_assign(gwy_lawn_get_si_unit_xy(lawn), gwy_brick_get_si_unit_x(brick));
    n = 0;

    if (calibration) {
        gwy_si_unit_assign(gwy_lawn_get_si_unit_curve(lawn, n), gwy_data_line_get_si_unit_y(calibration));
        gwy_lawn_set_curve_label(lawn, n, _("Z calibration"));
        n++;
    }

    for (m = 0; m < nbricks; m++) {
        gwy_si_unit_assign(gwy_lawn_get_si_unit_curve(lawn, n), gwy_brick_get_si_unit_w(allbricks[m].brick));
        s = gwy_app_get_brick_title(allbricks[m].container, allbricks[m].id);
        gwy_lawn_set_curve_label(lawn, n, s);
        g_free(s);
        n++;
    }

    g_array_free(allbricks_array, TRUE);
    g_free(curvedata);
}

static gboolean
other_brick_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyBrick *otherbrick, *brick = (GwyBrick*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_brick_key_for_id(id), &otherbrick))
        return FALSE;
    /* Ignore zcal, we take it from the master brick. */
    return !gwy_brick_check_compatibility(brick, otherbrick,
                                          GWY_DATA_COMPATIBILITY_RES
                                          | GWY_DATA_COMPATIBILITY_REAL
                                          | GWY_DATA_COMPATIBILITY_LATERAL);
}
/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
