/*
 *  $Id: cmap_crop.c 24162 2021-09-20 13:49:17Z yeti-dn $
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
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/lawn.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-cmap.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "../process/preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_X,
    PARAM_Y,
    PARAM_WIDTH,
    PARAM_HEIGHT,
    PARAM_KEEP_OFFSETS,
};

typedef struct {
    GwyParams *params;
    GwyLawn *lawn;
    GwyLawn *result;
    GwyDataField *preview;
    /* Cached values for dimensions display. */
    gint xres;
    gint yres;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GtkWidget *view;
    gint last_active;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             crop                (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             execute             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Crop curve map data."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, cmap_crop)

static gboolean
module_register(void)
{
    gwy_curve_map_func_register("cmap_crop",
                                (GwyCurveMapFunc)&crop,
                                N_("/_Basic Operations/_Crop"),
                                GWY_STOCK_CROP,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Crop data"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_curve_map_func_current());
    gwy_param_def_add_int(paramdef, PARAM_X, "x", _("_X"), 0, G_MAXINT, 0);
    gwy_param_def_add_int(paramdef, PARAM_Y, "y", _("_Y"), 0, G_MAXINT, 0);
    gwy_param_def_add_int(paramdef, PARAM_WIDTH, "width", _("_Width"), 0, G_MAXINT, G_MAXINT);
    gwy_param_def_add_int(paramdef, PARAM_HEIGHT, "height", _("_Height"), 0, G_MAXINT, G_MAXINT);
    gwy_param_def_add_boolean(paramdef, PARAM_KEEP_OFFSETS, "keep_offsets", _("Keep lateral offsets"), FALSE);
    return paramdef;
}

static void
crop(GwyContainer *data, GwyRunType runtype)
{
    GQuark quarks[2];
    GwyParams *params;
    ModuleArgs args;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_LAWN, &args.lawn,
                                     GWY_APP_LAWN_KEY, quarks,
                                     GWY_APP_LAWN_ID, &id,
                                     0);
    g_return_if_fail(args.lawn);

    args.xres = gwy_lawn_get_xres(args.lawn);
    args.yres = gwy_lawn_get_yres(args.lawn);
    args.params = params = gwy_params_new_from_settings(define_module_params());
    args.preview = gwy_container_get_object(data, gwy_app_get_lawn_preview_key_for_id(id));
    g_object_ref(args.preview);
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    execute(&args);

    newid = gwy_app_data_browser_add_lawn(args.result, args.preview, data, TRUE);
    g_object_unref(args.preview);
    gwy_app_sync_curve_map_items(data, data, id, newid, FALSE,
                                 GWY_DATA_ITEM_GRADIENT,
                                 GWY_DATA_ITEM_REAL_SQUARE,
                                 0);
    gwy_app_set_lawn_title(data, newid, _("Detail"));
    gwy_app_curve_map_log_add_curve_map(data, id, newid);

end:
    g_object_unref(params);
    GWY_OBJECT_UNREF(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    static const gint ids[] = { PARAM_X, PARAM_Y, PARAM_WIDTH, PARAM_HEIGHT };
    GwyDialogOutcome outcome;
    GwyDataField *previewfield;
    GtkWidget *hbox;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    const gchar *gradient;
    gboolean realsquare;
    guint i;
    gint parid;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();

    previewfield = gwy_data_field_duplicate(args->preview);
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), previewfield);
    g_object_unref(previewfield);
    /* XXX: Manual cross-sync between Lawn and Field. */
    if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), (const guchar**)&gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);
    if (gwy_container_gis_boolean(data, gwy_app_get_lawn_real_square_key_for_id(id), &realsquare))
        gwy_container_set_boolean(gui.data, gwy_app_get_data_real_square_key_for_id(0), realsquare);

    gui.dialog = gwy_dialog_new(_("Crop"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.view = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.view), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("New Dimensions"));
    for (i = 0; i < G_N_ELEMENTS(ids); i++) {
        parid = ids[i];
        gwy_param_table_append_slider(table, parid);
        gwy_param_table_slider_restrict_range(table, parid, i/2, (i % 2 ? args->yres : args->xres) - !(i/2));
        gwy_param_table_slider_set_mapping(table, parid, GWY_SCALE_MAPPING_LINEAR);
        gwy_param_table_slider_add_alt(table, parid);
        if (parid % 2)
            gwy_param_table_alt_set_field_pixel_y(table, parid, args->preview);
        else
            gwy_param_table_alt_set_field_pixel_x(table, parid, args->preview);
    }

    gwy_param_table_append_separator(table);
    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_checkbox(table, PARAM_KEEP_OFFSETS);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

/* Side-effect free version of CLAMP(). */
static gint
clamp_int(gint x, gint lower, gint upper)
{
    return CLAMP(x, lower, upper);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gint x = gwy_params_get_int(params, PARAM_X);
    gint y = gwy_params_get_int(params, PARAM_Y);
    gint width = gwy_params_get_int(params, PARAM_WIDTH);
    gint height = gwy_params_get_int(params, PARAM_HEIGHT);

    if (id < 0 || id == PARAM_WIDTH)
        gwy_param_table_set_int(table, PARAM_X, x = clamp_int(x, 0, args->xres - width));
    if (id < 0 || id == PARAM_HEIGHT)
        gwy_param_table_set_int(table, PARAM_Y, y = clamp_int(y, 0, args->yres - height));
    if (id < 0 || id == PARAM_X)
        gwy_param_table_set_int(table, PARAM_WIDTH, width = clamp_int(width, 1, args->xres - x));
    if (id < 0 || id == PARAM_Y)
        gwy_param_table_set_int(table, PARAM_HEIGHT, height = clamp_int(height, 1, args->yres - y));

    if (id != PARAM_KEEP_OFFSETS)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyLawn *lawn = args->lawn, *result;
    GwyDataField *previewfield;
    gint x = gwy_params_get_int(params, PARAM_X);
    gint y = gwy_params_get_int(params, PARAM_Y);
    gint width = gwy_params_get_int(params, PARAM_WIDTH);
    gint height = gwy_params_get_int(params, PARAM_HEIGHT);
    gboolean keep_offsets = gwy_params_get_boolean(params, PARAM_KEEP_OFFSETS);

    args->result = result = gwy_lawn_new_part(lawn, x, y, width, height, keep_offsets);
    previewfield = gwy_data_field_area_extract(args->preview, x, y, width, height);
    if (keep_offsets) {
        gwy_data_field_set_xoffset(previewfield, gwy_lawn_get_dx(lawn)*x + gwy_lawn_get_xoffset(lawn));
        gwy_data_field_set_yoffset(previewfield, gwy_lawn_get_dy(lawn)*y + gwy_lawn_get_yoffset(lawn));
    }

    /* NB: This changes whether we hold a reference on args->preview (from no to yes). */
    args->preview = previewfield;
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyDataField *field = args->preview;
    GwyDataField *result = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0));
    gint x = gwy_params_get_int(params, PARAM_X);
    gint y = gwy_params_get_int(params, PARAM_Y);
    gint width = gwy_params_get_int(params, PARAM_WIDTH);
    gint height = gwy_params_get_int(params, PARAM_HEIGHT);

    /* Crop just the preview image.  Do not bother with lawn here. */
    gwy_data_field_resample(result, width, height, GWY_INTERPOLATION_NONE);
    gwy_data_field_area_copy(field, result, x, y, width, height, 0, 0);
    gwy_data_field_data_changed(result);
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->view), PREVIEW_SIZE);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gint x = gwy_params_get_int(params, PARAM_X);
    gint y = gwy_params_get_int(params, PARAM_Y);
    gint width = gwy_params_get_int(params, PARAM_WIDTH);
    gint height = gwy_params_get_int(params, PARAM_HEIGHT);

    if (x > args->xres-1 || y > args->yres-1) {
        x = y = 0;
        width = height = G_MAXINT;
    }
    x = MIN(x, args->xres-1);
    y = MIN(y, args->yres-1);
    width = MIN(width, args->xres - x);
    height = MIN(height, args->yres - y);
    gwy_params_set_int(params, PARAM_X, x);
    gwy_params_set_int(params, PARAM_Y, y);
    gwy_params_set_int(params, PARAM_WIDTH, width);
    gwy_params_set_int(params, PARAM_HEIGHT, height);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
