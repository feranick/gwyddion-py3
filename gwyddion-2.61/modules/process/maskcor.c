/*
 *  $Id: maskcor.c 23727 2021-05-16 21:41:20Z yeti-dn $
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
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/correlation.h>
#include <libprocess/filters.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_INTERACTIVE

enum {
    PARAM_RESULT,
    PARAM_THRESHOLD,
    PARAM_REGCOEFF,
    PARAM_METHOD,
    PARAM_USE_MASK,
    PARAM_PLOT_MASK,
    PARAM_KERNEL,
};

typedef enum {
    MASKCOR_OBJECTS = 0,
    MASKCOR_MAXIMA  = 1,
    MASKCOR_SCORE   = 2,
} MaskcorResult;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             maskcor             (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static gboolean         kernel_filter       (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Searches for a detail in another image using correlation."),
    "Petr Klapetek <klapetek@gwyddion.net>, Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, maskcor)

static gboolean
module_register(void)
{
    gwy_process_func_register("maskcor",
                              (GwyProcessFunc)&maskcor,
                              N_("/M_ultidata/Correlation _Search..."),
                              GWY_STOCK_CORRELATION_MASK,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Search for a detail using correlation"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum methods[] = {
        { N_("Correlation, raw"),           GWY_CORR_SEARCH_COVARIANCE_RAW    },
        { N_("Correlation, leveled"),       GWY_CORR_SEARCH_COVARIANCE        },
        { N_("Correlation score"),          GWY_CORR_SEARCH_COVARIANCE_SCORE  },
        { N_("Height difference, raw"),     GWY_CORR_SEARCH_HEIGHT_DIFF_RAW   },
        { N_("Height difference, leveled"), GWY_CORR_SEARCH_HEIGHT_DIFF       },
        { N_("Height difference score"),    GWY_CORR_SEARCH_HEIGHT_DIFF_SCORE },
    };
    static const GwyEnum results[] = {
        { N_("Objects marked"),     MASKCOR_OBJECTS, },
        { N_("Correlation maxima"), MASKCOR_MAXIMA,  },
        { N_("Correlation score"),  MASKCOR_SCORE,   },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_RESULT, "result", _("Output _type"),
                              results, G_N_ELEMENTS(results), MASKCOR_OBJECTS);
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD, "threshold", _("T_hreshold"), 0.0, 1.0, 0.95);
    gwy_param_def_add_double(paramdef, PARAM_REGCOEFF, "regcoeff", _("_Regularization parameter"), 0.0, 1.0, 0.001);
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("Correlation _method"),
                              methods, G_N_ELEMENTS(methods), GWY_CORR_SEARCH_COVARIANCE_SCORE);
    gwy_param_def_add_boolean(paramdef, PARAM_USE_MASK, "use_mask", _("Use _mask"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_PLOT_MASK, "plot_mask", _("_Plot mask"), TRUE);
    gwy_param_def_add_image_id(paramdef, PARAM_KERNEL, "kernel", _("_Detail to search"));
    return paramdef;
}

static void
maskcor(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    MaskcorResult output;
    ModuleArgs args;
    GQuark mquark;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     0);
    g_return_if_fail(args.field);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());

    outcome = run_gui(&args);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    execute(&args);

    output = gwy_params_get_enum(args.params, PARAM_RESULT);
    if (output == MASKCOR_SCORE) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE, GWY_DATA_ITEM_GRADIENT, 0);
        gwy_app_set_data_field_title(data, newid, _("Correlation score"));
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    else {
        gwy_app_undo_qcheckpointv(data, 1, &mquark);
        gwy_container_set_object(data, mquark, args.result);
        gwy_app_channel_log_add_proc(data, id, id);
    }

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Correlation Search"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_KERNEL);
    gwy_param_table_data_id_set_filter(table, PARAM_KERNEL, kernel_filter, args->field, NULL);

    gwy_param_table_append_header(table, -1, _("Correlation Search"));
    gwy_param_table_append_checkbox(table, PARAM_USE_MASK);
    gwy_param_table_append_combo(table, PARAM_METHOD);
    gwy_param_table_append_slider(table, PARAM_THRESHOLD);
    gwy_param_table_append_slider(table, PARAM_REGCOEFF);

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_combo(table, PARAM_RESULT);
    gwy_param_table_append_checkbox(table, PARAM_PLOT_MASK);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_KERNEL) {
        GwyDataField *mask = NULL, *kernel = gwy_params_get_image(params, PARAM_KERNEL);
        GwyAppDataId dataid = gwy_params_get_data_id(params, PARAM_KERNEL);

        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, !!kernel);
        if (kernel) {
            GwyContainer *data = gwy_app_data_browser_get(dataid.datano);
            gwy_container_gis_object(data, gwy_app_get_mask_key_for_id(dataid.id), &mask);
        }
        gwy_param_table_set_sensitive(table, PARAM_USE_MASK, !!mask && gwy_data_field_get_max(mask) > 0.0);
    }
    if (id < 0 || id == PARAM_RESULT) {
        MaskcorResult output = gwy_params_get_enum(params, PARAM_RESULT);
        gwy_param_table_set_sensitive(table, PARAM_THRESHOLD, output != MASKCOR_SCORE);
        gwy_param_table_set_sensitive(table, PARAM_PLOT_MASK, output != MASKCOR_SCORE);
    }
    if (id < 0 || id == PARAM_METHOD) {
        GwyCorrSearchType method = gwy_params_get_enum(params, PARAM_METHOD);
        gboolean is_score = (method == GWY_CORR_SEARCH_COVARIANCE_SCORE
                             || method == GWY_CORR_SEARCH_HEIGHT_DIFF_SCORE);
        gwy_param_table_set_sensitive(table, PARAM_REGCOEFF, is_score);
    }
}

static gboolean
kernel_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *kernel, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &kernel))
        return FALSE;
    if (gwy_data_field_get_xreal(kernel) <= gwy_data_field_get_xreal(field)/3
        && gwy_data_field_get_yreal(kernel) <= gwy_data_field_get_yreal(field)/3
        && !gwy_data_field_check_compatibility(kernel, field,
                                               GWY_DATA_COMPATIBILITY_LATERAL | GWY_DATA_COMPATIBILITY_MEASURE))
        return TRUE;

    return FALSE;
}

static void
mark_only_maxima(GwyDataField *field, GwyDataField *mask)
{
    gint n, i, j, ngrains;
    gint *grain_maxima, *grains;
    const gdouble *data;
    gdouble *mdata;

    n = gwy_data_field_get_xres(field) * gwy_data_field_get_yres(field);
    data = gwy_data_field_get_data_const(field);
    mdata = gwy_data_field_get_data(mask);

    grains = g_new0(gint, n);
    ngrains = gwy_data_field_number_grains(mask, grains);
    grain_maxima = g_new(gint, ngrains + 1);
    for (i = 1; i <= ngrains; i++)
        grain_maxima[i] = -1;

    /* sum grain sizes and find maxima */
    for (i = 0; i < n; i++) {
        j = grains[i];
        if (j && (grain_maxima[j] < 0.0 || data[grain_maxima[j]] < data[i]))
            grain_maxima[j] = i;
    }
    g_free(grains);

    /* mark only maxima */
    gwy_data_field_clear(mask);
    for (i = 1; i <= ngrains; i++)
        mdata[grain_maxima[i]] = 1;

    g_free(grain_maxima);
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *markfield, *tmp = NULL, *result = args->result, *kernel, *kmask = NULL;
    GwyParams *params = args->params;
    gboolean use_mask = gwy_params_get_boolean(params, PARAM_USE_MASK);
    gboolean plot_mask = gwy_params_get_boolean(params, PARAM_PLOT_MASK);
    gdouble threshold = gwy_params_get_double(params, PARAM_THRESHOLD);
    gdouble regcoeff = gwy_params_get_double(params, PARAM_REGCOEFF);
    GwyCorrSearchType method = gwy_params_get_enum(params, PARAM_METHOD);
    MaskcorResult output = gwy_params_get_enum(params, PARAM_RESULT);
    gdouble min, max;
    gint xres, yres;

    kernel = gwy_params_get_image(params, PARAM_KERNEL);
    if (use_mask)
        kmask = gwy_params_get_mask(params, PARAM_KERNEL);

    gwy_data_field_correlation_search(args->field, kernel, kmask, result,
                                      method, regcoeff, GWY_EXTERIOR_BORDER_EXTEND, 0.0);

    if (output == MASKCOR_SCORE)
        return;

    if (method == GWY_CORR_SEARCH_COVARIANCE_SCORE) {
        /* pass */
    }
    else if (method == GWY_CORR_SEARCH_HEIGHT_DIFF_SCORE) {
        threshold = 2.0*(threshold - 1.0);
    }
    else {
        gwy_data_field_get_min_max(result, &min, &max);
        threshold = max*threshold + min*(1.0 - threshold);
    }

    /* Now it becomes convoluted.  There are the following possible outputs:
     * (a) plain thresholded score
     * (b) single-pixel maxima (in the thresholded score)
     * (c) plain thresholded score dilated by kmask (or rectangle if kmask is not used)
     * (d) single-pixel maxima plain dilated by kmask (or rectangle if kmask is not used)
     * So we interpret OBJECTS as (a) and (c).  The difference is PLOT_MASK.
     * And we interpret MAXIMA as (b) and (d).  Again, the difference is PLOT_MASK.
     *
     * This makes impossible to use kernel mask but plot rectangles.  Do we care?
     */
    markfield = gwy_data_field_duplicate(result);
    gwy_data_field_threshold(markfield, threshold, 0.0, 1.0);
    if (output == MASKCOR_MAXIMA)
        mark_only_maxima(result, markfield);

    if (plot_mask) {
        if (!kmask) {
            kmask = tmp = gwy_data_field_new_alike(kernel, FALSE);
            gwy_data_field_fill(kmask, 1.0);
        }
        xres = gwy_data_field_get_xres(result);
        yres = gwy_data_field_get_yres(result);
        gwy_data_field_area_filter_min_max(markfield, kmask, GWY_MIN_MAX_FILTER_DILATION, 0, 0, xres, yres);
        GWY_OBJECT_UNREF(tmp);
    }

    gwy_data_field_copy(markfield, result, FALSE);
    g_object_unref(markfield);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
