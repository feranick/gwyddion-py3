/*
 *  $Id: scale.c 23854 2021-06-14 16:36:54Z yeti-dn $
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
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_RATIO,
    PARAM_PROPORTIONAL,
    PARAM_ASPECT_RATIO,
    PARAM_INTERPOLATION,
    PARAM_XRES,
    PARAM_YRES,
};

typedef struct {
    GwyParams *params;
    gint orig_xres;
    gint orig_yres;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             scale               (GwyContainer *data,
                                             GwyRunType run);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Scales data by arbitrary factor."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek & Dirk Kähler",
    "2003",
};

GWY_MODULE_QUERY2(module_info, scale)

static gboolean
module_register(void)
{
    gwy_process_func_register("scale",
                              (GwyProcessFunc)&scale,
                              N_("/_Basic Operations/_Scale..."),
                              GWY_STOCK_SCALE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Scale data"));

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
    gwy_param_def_add_double(paramdef, PARAM_RATIO, "ratio", _("Scale by _ratio"), 0.001, 100.0, 1.0);
    gwy_param_def_add_boolean(paramdef, PARAM_PROPORTIONAL, "proportional", _("_Proportional scaling"), TRUE);
    /* We save the aspect ratio in settings, but the user does not control it directly. */
    gwy_param_def_add_double(paramdef, PARAM_ASPECT_RATIO, "aspectratio", NULL, G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    /* The user can control directly the pixel dimensions, but we do not save them.  The default for a different
     * image is the same scaling and aspect ratios, not the same dimensions. */
    gwy_param_def_add_int(paramdef, PARAM_XRES, NULL, _("New _width"), 2, 16384, 256);
    gwy_param_def_add_int(paramdef, PARAM_YRES, NULL, _("New _height"), 2, 16384, 256);
    gwy_param_def_add_enum(paramdef, PARAM_INTERPOLATION, "interp", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_LINEAR);
    return paramdef;
}

static void
scale(GwyContainer *data, GwyRunType run)
{
    GwyDataField *fields[3];
    gint xres, yres, oldid, newid, i;
    gdouble ratio, aspectratio;
    GwyInterpolationType interp;
    ModuleArgs args;
    GwyParams *params;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, fields + 0,
                                     GWY_APP_MASK_FIELD, fields + 1,
                                     GWY_APP_SHOW_FIELD, fields + 2,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(fields[0]);

    args.orig_xres = gwy_data_field_get_xres(fields[0]);
    args.orig_yres = gwy_data_field_get_yres(fields[0]);

    params = args.params = gwy_params_new_from_settings(define_module_params());
    if (gwy_params_get_boolean(params, PARAM_PROPORTIONAL))
        gwy_params_set_double(params, PARAM_ASPECT_RATIO, 1.0);
    ratio = gwy_params_get_double(params, PARAM_RATIO);
    aspectratio = gwy_params_get_double(params, PARAM_ASPECT_RATIO);
    gwy_params_set_int(params, PARAM_XRES, GWY_ROUND(ratio*args.orig_xres));
    gwy_params_set_int(params, PARAM_YRES, GWY_ROUND(aspectratio*ratio*args.orig_yres));

    if (run == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }

    xres = gwy_params_get_int(params, PARAM_XRES);
    yres = gwy_params_get_int(params, PARAM_YRES);
    interp = gwy_params_get_enum(params, PARAM_INTERPOLATION);

    fields[0] = gwy_data_field_new_resampled(fields[0], xres, yres, interp);
    if (fields[1]) {
        fields[1] = gwy_data_field_new_resampled(fields[1], xres, yres, GWY_INTERPOLATION_LINEAR);
        gwy_data_field_threshold(fields[1], 0.5, 0.0, 1.0);
    }
    if (fields[2])
        fields[2] = gwy_data_field_new_resampled(fields[2], xres, yres, interp);

    newid = gwy_app_data_browser_add_data_field(fields[0], data, TRUE);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    if (fields[1])
        gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), fields[1]);
    if (fields[2])
        gwy_container_set_object(data, gwy_app_get_show_key_for_id(newid), fields[2]);

    gwy_app_set_data_field_title(data, newid, _("Scaled Data"));
    gwy_app_channel_log_add_proc(data, oldid, newid);

    for (i = 0; i < 3; i++)
        GWY_OBJECT_UNREF(fields[i]);

end:
    g_object_unref(params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    gdouble minratio, maxratio, step = 1e-4;

    gui.args = args;
    gui.dialog = gwy_dialog_new(gwy_sgettext("verb|Scale"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_slider(table, PARAM_RATIO);
    gwy_param_table_slider_set_mapping(table, PARAM_RATIO, GWY_SCALE_MAPPING_LOG);
    minratio = GWY_ROUND(2.0/MIN(args->orig_xres, args->orig_yres)/step)*step;
    maxratio = 16384.0/MAX(args->orig_xres, args->orig_yres);
    gwy_param_table_slider_restrict_range(table, PARAM_RATIO, minratio, maxratio);
    gwy_param_table_slider_set_digits(table, PARAM_RATIO, 4);
    gwy_param_table_append_checkbox(table, PARAM_PROPORTIONAL);
    gwy_param_table_append_slider(table, PARAM_XRES);
    gwy_param_table_set_unitstr(table, PARAM_XRES, _("px"));
    gwy_param_table_slider_set_mapping(table, PARAM_XRES, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_YRES);
    gwy_param_table_set_unitstr(table, PARAM_YRES, _("px"));
    gwy_param_table_slider_set_mapping(table, PARAM_YRES, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_combo(table, PARAM_INTERPOLATION);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;
    gboolean proportional = gwy_params_get_boolean(params, PARAM_PROPORTIONAL);

    if (id < 0 || id == PARAM_PROPORTIONAL) {
        gwy_param_table_set_sensitive(table, PARAM_RATIO, proportional);
        if (proportional)
            gwy_params_set_double(params, PARAM_ASPECT_RATIO, 1.0);
    }

    if (id < 0 || id == PARAM_RATIO || (id == PARAM_PROPORTIONAL && proportional)) {
        gdouble ratio = gwy_params_get_double(params, PARAM_RATIO);
        gdouble aspectratio = gwy_params_get_double(params, PARAM_ASPECT_RATIO);
        gwy_param_table_set_int(table, PARAM_XRES, GWY_ROUND(ratio*args->orig_xres));
        gwy_param_table_set_int(table, PARAM_YRES, GWY_ROUND(aspectratio*ratio*args->orig_yres));
    }

    if (id == PARAM_XRES || id == PARAM_YRES) {
        gdouble xres = gwy_params_get_int(params, PARAM_XRES);
        gdouble yres = gwy_params_get_int(params, PARAM_YRES);
        if (proportional) {
            gdouble ratio = (id == PARAM_XRES ? xres/args->orig_xres : yres/args->orig_yres);
            gwy_param_table_set_double(table, PARAM_RATIO, ratio);
            ratio = gwy_params_get_double(params, PARAM_RATIO);
            gwy_param_table_set_int(table, PARAM_YRES, GWY_ROUND(ratio*args->orig_yres));
            gwy_param_table_set_int(table, PARAM_XRES, GWY_ROUND(ratio*args->orig_xres));
        }
        else
            gwy_params_set_double(params, PARAM_ASPECT_RATIO, yres/args->orig_yres * args->orig_xres/xres);
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
