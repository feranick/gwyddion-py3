/*
 *  $Id: resample.c 23850 2021-06-14 15:31:36Z yeti-dn $
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

/* Physical pixel size change with inverse factors compared to resolutions! */
#define MAX_UPSAMPLE 16.0
#define MAX_DOWNSAMPLE 16.0

enum {
    PARAM_DX,
    PARAM_DY,
    PARAM_SQUARE,
    PARAM_INTERP,
    PARAM_TEMPLATE,
    PARAM_MATCH_SIZE,
    INFO_NEWDIM,
};

typedef struct {
    GwyParams *params;
    /* Cached input image properties. */
    GwyDataField *field;
    gdouble orig_dx;
    gdouble orig_dy;
    gint orig_xres;
    gint orig_yres;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwySIValueFormat *vf;
} ModuleGUI;

static gboolean         module_register            (void);
static GwyParamDef*     define_module_params       (void);
static void             resample                   (GwyContainer *data,
                                                    GwyRunType runtype);
static GwyDialogOutcome run_gui                    (ModuleArgs *args);
static void             param_changed              (ModuleGUI *gui,
                                                    gint id);
static void             dialog_response            (ModuleGUI *gui,
                                                    gint response);
static gboolean         template_filter            (GwyContainer *data,
                                                    gint id,
                                                    gpointer user_data);
static void             recalculate_new_resolutions(ModuleArgs *args,
                                                    gint *xres,
                                                    gint *yres);
static void             sanitise_params            (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Resamples data to specified pixel size."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, resample)

static gboolean
module_register(void)
{
    gwy_process_func_register("resample",
                              (GwyProcessFunc)&resample,
                              N_("/_Basic Operations/_Resample..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Resample to pixel size"));

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
    gwy_param_def_add_double(paramdef, PARAM_DX, "dx", _("_X pixel size"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_DY, "dy", _("_Y pixel size"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_boolean(paramdef, PARAM_SQUARE, "square", _("_Square pixels"), TRUE);
    gwy_param_def_add_enum(paramdef, PARAM_INTERP, "interp", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_LINEAR);
    gwy_param_def_add_image_id(paramdef, PARAM_TEMPLATE, "template", _("_Match pixel size"));
    gwy_param_def_add_boolean(paramdef, PARAM_MATCH_SIZE, "match_size", _("_Match pixel size"), FALSE);
    return paramdef;
}

static void
resample(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *fields[3];
    GwyParams *params;
    gint xres, yres, oldid, newid, i;
    ModuleArgs args;
    gdouble xoff, yoff, dx, dy;
    GwyInterpolationType interp;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, fields + 0,
                                     GWY_APP_MASK_FIELD, fields + 1,
                                     GWY_APP_SHOW_FIELD, fields + 2,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(fields[0]);
    args.field = fields[0];
    args.orig_xres = gwy_data_field_get_xres(fields[0]);
    args.orig_yres = gwy_data_field_get_yres(fields[0]);
    args.orig_dx = gwy_data_field_get_dx(fields[0]);
    args.orig_dy = gwy_data_field_get_dy(fields[0]);

    args.params = params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }

    recalculate_new_resolutions(&args, &xres, &yres);
    xoff = gwy_data_field_get_xoffset(fields[0]);
    yoff = gwy_data_field_get_yoffset(fields[0]);
    dx = gwy_params_get_double(params, PARAM_DX);
    dy = gwy_params_get_double(params, PARAM_DY);
    interp = gwy_params_get_enum(params, PARAM_INTERP);
    xoff *= (xres*dx)/(args.orig_xres*args.orig_dx);
    yoff *= (yres*dy)/(args.orig_yres*args.orig_dy);
    for (i = 0; i < 3; i++) {
        if (!fields[i])
            continue;
        fields[i] = gwy_data_field_new_resampled(fields[i], xres, yres, (i == 1 ? GWY_INTERPOLATION_LINEAR : interp));
        gwy_data_field_set_xreal(fields[i], xres*dx);
        gwy_data_field_set_yreal(fields[i], yres*dy);
        gwy_data_field_set_xoffset(fields[i], xoff);
        gwy_data_field_set_yoffset(fields[i], yoff);
        if (i == 1)
            gwy_data_field_threshold(fields[1], 0.5, 0.0, 1.0);
    }

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

    gwy_app_set_data_field_title(data, newid, _("Resampled Data"));
    gwy_app_channel_log_add_proc(data, oldid, newid);

    for (i = 0; i < 3; i++)
        GWY_OBJECT_UNREF(fields[i]);

end:
    g_object_unref(params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    static const int sliders[] = { PARAM_DX, PARAM_DY };
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    gint xypow10, i;
    GwySIUnit *xyunit;

    gwy_clear(&gui, 1);
    gui.args = args;

    xypow10 = 3*GWY_ROUND(0.5*log10(args->orig_dx*args->orig_dy)/3.0);
    xyunit = gwy_data_field_get_si_unit_xy(args->field);
    gui.vf = gwy_si_unit_get_format_for_power10(xyunit, GWY_SI_UNIT_FORMAT_VFMARKUP, xypow10, NULL);

    gui.dialog = gwy_dialog_new(_("Resample"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_image_id(table, PARAM_TEMPLATE);
    gwy_param_table_data_id_set_filter(table, PARAM_TEMPLATE, template_filter, args->field, NULL);
    gwy_param_table_add_enabler(table, PARAM_MATCH_SIZE, PARAM_TEMPLATE);

    for (i = 0; i < (gint)G_N_ELEMENTS(sliders); i++) {
        gint id = sliders[i];
        gwy_param_table_append_slider(table, id);
        gwy_param_table_slider_restrict_range(table, id, args->orig_dx/MAX_UPSAMPLE, args->orig_dx*MAX_DOWNSAMPLE);
        gwy_param_table_slider_set_mapping(table, id, GWY_SCALE_MAPPING_LOG);
        gwy_param_table_set_unitstr(table, id, gui.vf->units);
        gwy_param_table_slider_set_factor(table, id, 1.0/gui.vf->magnitude);
    }

    gwy_param_table_append_checkbox(table, PARAM_SQUARE);
    gwy_param_table_append_combo(table, PARAM_INTERP);
    gwy_param_table_append_info(table, INFO_NEWDIM, _("New dimensions"));
    gwy_param_table_set_unitstr(table, INFO_NEWDIM, _("px"));

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);

    outcome = gwy_dialog_run(dialog);

    gwy_si_unit_value_format_free(gui.vf);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gboolean match_size = gwy_params_get_boolean(params, PARAM_MATCH_SIZE);
    gboolean square = gwy_params_get_boolean(params, PARAM_SQUARE);
    gdouble dx = gwy_params_get_double(params, PARAM_DX);
    gdouble dy = gwy_params_get_double(params, PARAM_DY);
    gboolean has_any = !gwy_params_data_id_is_none(params, PARAM_TEMPLATE);
    gint xres, yres;
    gchar *s;

    if (id < 0)
        gwy_param_table_set_sensitive(table, PARAM_TEMPLATE, has_any);
    if (id < 0 || id == PARAM_MATCH_SIZE) {
        gwy_param_table_set_sensitive(table, PARAM_DX, !match_size);
        gwy_param_table_set_sensitive(table, PARAM_DY, !match_size);
        gwy_param_table_set_sensitive(table, PARAM_SQUARE, !match_size);
    }
    if ((id < 0 || id == PARAM_TEMPLATE || id == PARAM_MATCH_SIZE) && has_any && match_size) {
        GwyDataField *otherfield = gwy_params_get_image(params, PARAM_TEMPLATE);
        gwy_param_table_set_double(table, PARAM_DX, (dx = gwy_data_field_get_dx(otherfield)));
        gwy_param_table_set_double(table, PARAM_DY, (dy = gwy_data_field_get_dy(otherfield)));
        if (dx != dy)
            gwy_param_table_set_boolean(table, PARAM_SQUARE, (square = FALSE));
    }
    if (id == PARAM_SQUARE && square && dx != dy) {
        dx = dy = sqrt(dx*dy);
        gwy_param_table_set_double(table, PARAM_DX, dx);
        gwy_param_table_set_double(table, PARAM_DY, dy);
        /* XXX: We should enforce identical slider ranges when square is enabled. */
    }
    if (id == PARAM_DX && square)
        gwy_param_table_set_double(table, PARAM_DY, (dy = dx));
    if (id == PARAM_DY && square)
        gwy_param_table_set_double(table, PARAM_DX, (dx = dy));

    recalculate_new_resolutions(args, &xres, &yres);
    s = g_strdup_printf(_("%d × %d"), xres, yres);
    gwy_param_table_info_set_valuestr(table, INFO_NEWDIM, s);
    g_free(s);
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;

    if (response == GWY_RESPONSE_RESET) {
        gwy_param_table_set_boolean(gui->table, PARAM_SQUARE, args->orig_dx != args->orig_dy);
        gwy_param_table_set_double(gui->table, PARAM_DX, args->orig_dx);
        gwy_param_table_set_double(gui->table, PARAM_DY, args->orig_dy);
    }
}

static gboolean
template_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *otherfield, *field = (GwyDataField*)user_data;
    gdouble d, dother;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &otherfield))
        return FALSE;
    if (otherfield == field)
        return FALSE;
    if (gwy_data_field_check_compatibility(field, otherfield, GWY_DATA_COMPATIBILITY_LATERAL))
        return FALSE;

    d = gwy_data_field_get_dx(field);
    dother = gwy_data_field_get_dx(otherfield);
    if (dother > d*MAX_DOWNSAMPLE || dother < d/MAX_UPSAMPLE)
        return FALSE;

    d = gwy_data_field_get_dy(field);
    dother = gwy_data_field_get_dy(otherfield);
    if (dother > d*MAX_DOWNSAMPLE || dother < d/MAX_UPSAMPLE)
        return FALSE;

    return TRUE;
}

static void
recalculate_new_resolutions(ModuleArgs *args, gint *xres, gint *yres)
{
    gdouble dx = gwy_params_get_double(args->params, PARAM_DX);
    gdouble dy = gwy_params_get_double(args->params, PARAM_DY);

    *xres = GWY_ROUND(args->orig_xres*args->orig_dx/dx);
    *xres = MAX(*xres, 1);
    *yres = GWY_ROUND(args->orig_yres*args->orig_dy/dy);
    *yres = MAX(*yres, 1);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gboolean match_size = gwy_params_get_boolean(params, PARAM_MATCH_SIZE);
    GwyAppDataId template = gwy_params_get_data_id(params, PARAM_TEMPLATE);
    gboolean is_none = gwy_params_data_id_is_none(params, PARAM_TEMPLATE);
    gboolean square = gwy_params_get_boolean(params, PARAM_SQUARE);
    gdouble dx = gwy_params_get_double(params, PARAM_DX);
    gdouble dy = gwy_params_get_double(params, PARAM_DY);

    if (match_size
        && (is_none || !template_filter(gwy_app_data_browser_get(template.datano), template.id, args->field)))
        gwy_params_set_boolean(params, PARAM_MATCH_SIZE, (match_size = FALSE));

    if (match_size) {
        GwyDataField *otherfield = gwy_params_get_image(params, PARAM_TEMPLATE);
        /* template_filter() only allows template fields which are not too crazy-sized. */
        gwy_params_set_double(params, PARAM_DX, gwy_data_field_get_dx(otherfield));
        gwy_params_set_double(params, PARAM_DY, gwy_data_field_get_dy(otherfield));
    }
    else {
        dx = CLAMP(dx, args->orig_dx/MAX_UPSAMPLE, args->orig_dx*MAX_DOWNSAMPLE);
        gwy_params_set_double(params, PARAM_DX, dx);
        dy = CLAMP(dy, args->orig_dy/MAX_UPSAMPLE, args->orig_dy*MAX_DOWNSAMPLE);
        gwy_params_set_double(params, PARAM_DX, dy);
    }

    if (dx != dy)
        gwy_params_set_boolean(params, PARAM_SQUARE, (square = FALSE));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
