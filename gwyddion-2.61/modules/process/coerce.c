/*
 *  $Id: coerce.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2016-2021 David Necas (Yeti).
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/arithmetic.h>
#include <libprocess/filters.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    COERCE_DISTRIBUTION_DATA        = 0,
    COERCE_DISTRIBUTION_UNIFORM     = 1,
    COERCE_DISTRIBUTION_GAUSSIAN    = 2,
    COERCE_DISTRIBUTION_LEVELS      = 3,
    COERCE_DISTRIBUTION_SKEW_NORMAL = 4,
} CoerceDistributionType;

typedef enum {
    COERCE_PROCESSING_FIELD   = 0,
    COERCE_PROCESSING_ROWS    = 1,
} CoerceProcessingType;

typedef enum {
    COERCE_LEVELS_UNIFORM  = 0,
    COERCE_LEVELS_EQUIAREA = 1,
} CoerceLevelsType;

enum {
    PARAM_DISTRIBUTION,
    PARAM_LEVEL_TYPE,
    PARAM_NLEVELS,
    PARAM_PROCESSING,
    PARAM_SKEW,
    PARAM_UPDATE,
    PARAM_TEMPLATE,
};

typedef struct {
    gdouble z;
    guint k;
} ValuePos;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             coerce                  (GwyContainer *data,
                                                 GwyRunType runtype);
static void             execute                 (ModuleArgs *args);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             preview                 (gpointer user_data);
static gboolean         template_filter         (GwyContainer *data,
                                                 gint id,
                                                 gpointer user_data);
static void             coerce_do_field         (GwyDataField *field,
                                                 GwyDataField *result,
                                                 GwyParams *params);
static void             coerce_do_field_levels  (GwyDataField *field,
                                                 GwyDataField *result,
                                                 GwyParams *params);
static void             coerce_do_rows          (GwyDataField *field,
                                                 GwyDataField *result,
                                                 GwyParams *params);
static void             build_values_levels     (const ValuePos *vpos,
                                                 gdouble *z,
                                                 guint n,
                                                 guint nlevels);
static void             build_values_uniform    (gdouble *z,
                                                 guint n,
                                                 gdouble min,
                                                 gdouble max);
static void             build_values_gaussian   (gdouble *z,
                                                 guint n,
                                                 gdouble mean,
                                                 gdouble rms);
static void             build_values_skew_normal(gdouble *z,
                                                 guint n,
                                                 gdouble mean,
                                                 gdouble rms,
                                                 gdouble skew);
static void             build_values_from_data  (gdouble *z,
                                                 guint n,
                                                 const gdouble *data,
                                                 guint ndata);
static void             sanitise_params         (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Transforms surfaces to have prescribed statistical properties."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, coerce)

static gboolean
module_register(void)
{
    gwy_process_func_register("coerce",
                              (GwyProcessFunc)&coerce,
                              N_("/_Distortion/Co_erce..."),
                              GWY_STOCK_ENFORCE_DISTRIBUTION,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              _("Enforce prescribed statistical properties"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum distributions[] = {
        { N_("distribution|Uniform"),     COERCE_DISTRIBUTION_UNIFORM,     },
        { N_("distribution|Gaussian"),    COERCE_DISTRIBUTION_GAUSSIAN,    },
        { N_("distribution|Skew-normal"), COERCE_DISTRIBUTION_SKEW_NORMAL, },
        { N_("As another data"),          COERCE_DISTRIBUTION_DATA,        },
        { N_("Discrete levels"),          COERCE_DISTRIBUTION_LEVELS,      },
    };
    static const GwyEnum processings[] = {
        { N_("Entire image"),         COERCE_PROCESSING_FIELD, },
        { N_("By row (identically)"), COERCE_PROCESSING_ROWS,  },
    };
    static const GwyEnum leveltypes[] = {
        { N_("distribution|Uniform"), COERCE_LEVELS_UNIFORM,  },
        { N_("Same area"),            COERCE_LEVELS_EQUIAREA, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISTRIBUTION, "distribution", _("Coerce value distribution to"),
                              distributions, G_N_ELEMENTS(distributions), COERCE_DISTRIBUTION_UNIFORM);
    gwy_param_def_add_gwyenum(paramdef, PARAM_LEVEL_TYPE, "level_type", _("Level _type"),
                              leveltypes, G_N_ELEMENTS(leveltypes), COERCE_LEVELS_EQUIAREA);
    gwy_param_def_add_int(paramdef, PARAM_NLEVELS, "nlevels", _("Number of _levels"), 2, 16384, 4);
    gwy_param_def_add_gwyenum(paramdef, PARAM_PROCESSING, "processing", _("Data processing"),
                              processings, G_N_ELEMENTS(processings), COERCE_PROCESSING_FIELD);
    gwy_param_def_add_double(paramdef, PARAM_SKEW, "skew", _("_Skew"), -0.0, 1.0, 0.5);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_image_id(paramdef, PARAM_TEMPLATE, "template", _("_Template"));
    return paramdef;
}

static void
coerce(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.result = gwy_data_field_new_alike(args.field, FALSE);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome == GWY_DIALOG_PROCEED)
        execute(&args);

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_app_set_data_field_title(data, newid, _("Coerced"));
    gwy_app_channel_log_add_proc(data, id, newid);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
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

    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Coerce Statistics"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_radio_header(table, PARAM_DISTRIBUTION);
    gwy_param_table_append_radio_item(table, PARAM_DISTRIBUTION, COERCE_DISTRIBUTION_UNIFORM);
    gwy_param_table_append_radio_item(table, PARAM_DISTRIBUTION, COERCE_DISTRIBUTION_GAUSSIAN);

    gwy_param_table_append_radio_item(table, PARAM_DISTRIBUTION, COERCE_DISTRIBUTION_SKEW_NORMAL);
    gwy_param_table_append_slider(table, PARAM_SKEW);
    gwy_param_table_slider_set_mapping(table, PARAM_SKEW, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_slider_set_steps(table, PARAM_SKEW, 0.001, 0.1);

    gwy_param_table_append_radio_item(table, PARAM_DISTRIBUTION, COERCE_DISTRIBUTION_DATA);
    gwy_param_table_append_image_id(table, PARAM_TEMPLATE);
    gwy_param_table_data_id_set_filter(table, PARAM_TEMPLATE, template_filter, args->field, NULL);

    gwy_param_table_append_radio_item(table, PARAM_DISTRIBUTION, COERCE_DISTRIBUTION_LEVELS);
    gwy_param_table_append_combo(table, PARAM_LEVEL_TYPE);
    gwy_param_table_append_slider(table, PARAM_NLEVELS);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_radio(table, PARAM_PROCESSING);
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
    GwyParamTable *table = gui->table;
    GwyParams *params = gui->args->params;
    CoerceDistributionType distribution = gwy_params_get_enum(params, PARAM_DISTRIBUTION);
    gboolean has_template = !gwy_params_data_id_is_none(params, PARAM_TEMPLATE);

    if (id < 0 || id == PARAM_TEMPLATE)
        gwy_param_table_radio_set_sensitive(table, PARAM_DISTRIBUTION, COERCE_DISTRIBUTION_DATA, has_template);
    if (id < 0 || id == PARAM_DISTRIBUTION) {
        gwy_param_table_set_sensitive(table, PARAM_TEMPLATE, (distribution == COERCE_DISTRIBUTION_DATA));
        gwy_param_table_set_sensitive(table, PARAM_LEVEL_TYPE, (distribution == COERCE_DISTRIBUTION_LEVELS));
        gwy_param_table_set_sensitive(table, PARAM_NLEVELS, (distribution == COERCE_DISTRIBUTION_LEVELS));
        gwy_param_table_set_sensitive(table, PARAM_SKEW, (distribution == COERCE_DISTRIBUTION_SKEW_NORMAL));
    }
    if (id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static gboolean
template_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *template, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &template))
        return FALSE;
    if (template == field)
        return FALSE;

    return !gwy_data_field_check_compatibility(field, template, GWY_DATA_COMPATIBILITY_VALUE);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    CoerceProcessingType processing = gwy_params_get_enum(args->params, PARAM_PROCESSING);

    if (processing == COERCE_PROCESSING_FIELD)
        coerce_do_field(args->field, args->result, args->params);
    else if (processing == COERCE_PROCESSING_ROWS)
        coerce_do_rows(args->field, args->result, args->params);
    else {
        g_assert_not_reached();
    }
}

static int
compare_double(const void *a, const void *b)
{
    const gdouble *da = (const gdouble*)a;
    const gdouble *db = (const gdouble*)b;

    if (*da < *db)
        return -1;
    if (*da > *db)
        return 1;
    return 0;
}

static void
coerce_do_field(GwyDataField *field, GwyDataField *result,
                GwyParams *params)
{
    guint n = gwy_data_field_get_xres(field)*gwy_data_field_get_yres(field);
    const gdouble *d = gwy_data_field_get_data_const(field);
    CoerceDistributionType distribution = gwy_params_get_enum(params, PARAM_DISTRIBUTION);
    CoerceLevelsType level_type = gwy_params_get_enum(params, PARAM_LEVEL_TYPE);
    ValuePos *vpos;
    gdouble *z, *dr;
    guint k;

    if (distribution == COERCE_DISTRIBUTION_LEVELS && level_type == COERCE_LEVELS_UNIFORM) {
        coerce_do_field_levels(field, result, params);
        return;
    }

    vpos = g_new(ValuePos, n);
    z = g_new(gdouble, n);
    for (k = 0; k < n; k++) {
        vpos[k].z = d[k];
        vpos[k].k = k;
    }
    qsort(vpos, n, sizeof(ValuePos), compare_double);

    if (distribution == COERCE_DISTRIBUTION_DATA) {
        GwyDataField *src = gwy_params_get_image(params, PARAM_TEMPLATE);
        guint nsrc = gwy_data_field_get_xres(src)*gwy_data_field_get_yres(src);
        build_values_from_data(z, n, gwy_data_field_get_data_const(src), nsrc);
    }
    else if (distribution == COERCE_DISTRIBUTION_LEVELS) {
        build_values_levels(vpos, z, n, gwy_params_get_int(params, PARAM_NLEVELS));
    }
    else if (distribution == COERCE_DISTRIBUTION_UNIFORM) {
        gdouble min, max;
        gwy_data_field_get_min_max(field, &min, &max);
        build_values_uniform(z, n, min, max);
    }
    else if (distribution == COERCE_DISTRIBUTION_GAUSSIAN) {
        gdouble avg, rms;
        avg = gwy_data_field_get_avg(field);
        rms = gwy_data_field_get_rms(field);
        build_values_gaussian(z, n, avg, rms);
    }
    else if (distribution == COERCE_DISTRIBUTION_SKEW_NORMAL) {
        gdouble avg, rms;
        avg = gwy_data_field_get_avg(field);
        rms = gwy_data_field_get_rms(field);
        build_values_skew_normal(z, n, avg, rms, gwy_params_get_double(params, PARAM_SKEW));
    }
    else {
        g_return_if_reached();
    }

    dr = gwy_data_field_get_data(result);
    for (k = 0; k < n; k++)
        dr[vpos[k].k] = z[k];

    g_free(z);
    g_free(vpos);
}

static void
coerce_do_field_levels(GwyDataField *field, GwyDataField *result,
                       GwyParams *params)
{
    guint n = gwy_data_field_get_xres(field)*gwy_data_field_get_yres(field);
    const gdouble *d = gwy_data_field_get_data_const(field);
    gdouble *dr = gwy_data_field_get_data(result);
    gdouble min, max, q;
    gint k, nlevels = gwy_params_get_int(params, PARAM_NLEVELS);

    gwy_data_field_get_min_max(field, &min, &max);
    if (max <= min) {
        gwy_data_field_fill(result, 0.5*(min + max));
        return;
    }

    q = (max - min)/nlevels;
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(k) \
            shared(d,dr,n,nlevels,q,min)
#endif
    for (k = 0; k < n; k++) {
        gint v = (gint)floor((d[k] - min)/q);
        v = CLAMP(v, 0, nlevels-1);
        dr[k] = (v + 0.5)*q + min;
    }
}

static void
coerce_do_rows(GwyDataField *field, GwyDataField *result,
               GwyParams *params)
{
    guint xres = gwy_data_field_get_xres(field),
          yres = gwy_data_field_get_yres(field);
    const gdouble *d = gwy_data_field_get_data_const(field);
    CoerceDistributionType distribution = gwy_params_get_enum(params, PARAM_DISTRIBUTION);
    ValuePos *vpos;
    gdouble *z, *dr;
    guint i, j;

    /* It is not completely clear what we should do in the case of row-wise processing but this at least ensures that
     * the levels are the same in the entire field but individual rows are transformed individualy. */
    if (distribution == COERCE_DISTRIBUTION_LEVELS) {
        GwyDataField *tmp = gwy_data_field_duplicate(field);
        gdouble min, max;
        gwy_data_field_get_min_max(field, &min, &max);
        for (i = 0; i < yres; i++)
            gwy_data_field_area_renormalize(tmp, 0, i, xres, 1, max-min, min);
        coerce_do_field(tmp, result, params);
        g_object_unref(tmp);
        return;
    }

    vpos = g_new(ValuePos, xres);
    z = g_new(gdouble, xres);

    if (distribution == COERCE_DISTRIBUTION_DATA) {
        GwyDataField *src = gwy_params_get_image(params, PARAM_TEMPLATE);
        guint nsrc = gwy_data_field_get_xres(src)*gwy_data_field_get_yres(src);
        build_values_from_data(z, xres, gwy_data_field_get_data_const(src), nsrc);
    }
    else if (distribution == COERCE_DISTRIBUTION_UNIFORM) {
        gdouble min, max;
        gwy_data_field_get_min_max(field, &min, &max);
        build_values_uniform(z, xres, min, max);
    }
    else if (distribution == COERCE_DISTRIBUTION_GAUSSIAN) {
        gdouble avg, rms;
        avg = gwy_data_field_get_avg(field);
        rms = gwy_data_field_get_rms(field);
        build_values_gaussian(z, xres, avg, rms);
    }
    else if (distribution == COERCE_DISTRIBUTION_SKEW_NORMAL) {
        gdouble avg, rms;
        avg = gwy_data_field_get_avg(field);
        rms = gwy_data_field_get_rms(field);
        build_values_skew_normal(z, xres, avg, rms, gwy_params_get_double(params, PARAM_SKEW));
    }
    else {
        g_return_if_reached();
    }

    dr = gwy_data_field_get_data(result);
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            vpos[j].z = d[i*xres + j];
            vpos[j].k = j;
        }
        qsort(vpos, xres, sizeof(ValuePos), compare_double);
        for (j = 0; j < xres; j++)
            dr[i*xres + vpos[j].k] = z[j];
    }

    g_free(z);
    g_free(vpos);
}

static void
build_values_levels(const ValuePos *vpos, gdouble *z, guint n, guint nlevels)
{
    guint i, j, blockstart, counter;
    gdouble v;

    if (nlevels >= n) {
        for (i = 0; i < n; i++)
            z[i] = vpos[i].z;
        return;
    }

    blockstart = 0;
    counter = nlevels/2;
    for (i = 0; i < n; i++) {
        counter += nlevels;
        if (counter >= n) {
            v = 0.0;
            for (j = blockstart; j <= i; j++)
                v += vpos[j].z;

            v /= (i+1 - blockstart);
            for (j = blockstart; j <= i; j++)
                z[j] = v;

            counter -= n;
            blockstart = i+1;
        }
    }
}

static void
build_values_uniform(gdouble *z, guint n, gdouble min, gdouble max)
{
    gdouble x;
    guint i;

    for (i = 0; i < n; i++) {
        x = i/(n - 1.0);
        z[i] = min + x*(max - min);
    }
}

static gdouble
gwy_inverf(gdouble y)
{
    /* Coefficients in rational approximations. */
    static const gdouble a[4] = {
        0.886226899, -1.645349621, 0.914624893, -0.140543331
    };
    static const gdouble b[4] = {
        -2.118377725, 1.442710462, -0.329097515, 0.012229801
    };
    static const gdouble c[4] = {
        -1.970840454, -1.624906493, 3.429567803, 1.641345311
    };
    static const gdouble d[2] = {
        3.543889200, 1.637067800
    };
    const gdouble y0 = 0.7;

    gdouble x, z;

    if (y <= -1.0)
       return -G_MAXDOUBLE;
    if (y >= 1.0)
       return G_MAXDOUBLE;

    if (y < -y0) {
        z = sqrt(-log(0.5*(1.0 + y)));
        x = -(((c[3]*z + c[2])*z + c[1])*z + c[0])/((d[1]*z + d[0])*z + 1.0);
    }
    else if (y > y0) {
        z = sqrt(-log(0.5*(1.0 - y)));
        x = (((c[3]*z + c[2])*z + c[1])*z + c[0])/((d[1]*z + d[0])*z + 1.0);
    }
    else {
        z = y*y;
        x = y*(((a[3]*z + a[2])*z + a[1])*z + a[0])/((((b[3]*z + b[3])*z + b[1])*z + b[0])*z + 1.0);
    }

    /* Three steps of Newton method correction to full accuracy. */
    x -= (erf(x) - y)/(2.0/GWY_SQRT_PI*exp(-x*x));
    x -= (erf(x) - y)/(2.0/GWY_SQRT_PI*exp(-x*x));
    x -= (erf(x) - y)/(2.0/GWY_SQRT_PI*exp(-x*x));

    return x;
}

static void
build_values_gaussian(gdouble *z, guint n, gdouble mean, gdouble rms)
{
    gdouble x;
    guint i;

    rms *= sqrt(2.0);
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i,x) \
            shared(z,n,mean,rms)
#endif
    for (i = 0; i < n; i++) {
        x = (2.0*i + 1.0)/n - 1.0;
        z[i] = mean + rms*gwy_inverf(x);
    }
}

static void
build_values_from_data(gdouble *z, guint n, const gdouble *data, guint ndata)
{
    gdouble *sorted;
    guint i;

    if (n == ndata) {
        gwy_assign(z, data, n);
        gwy_math_sort(n, z);
        return;
    }

    if (ndata < 2) {
        for (i = 0; i < n; i++)
            z[i] = data[0];
        return;
    }

    sorted = g_memdup(data, ndata*sizeof(gdouble));
    gwy_math_sort(ndata, sorted);

    if (n < 3) {
        if (n == 1)
            z[0] = sorted[ndata/2];
        else if (n == 2) {
            z[0] = sorted[0];
            z[1] = sorted[ndata-1];
        }
        g_free(sorted);
        return;
    }

    for (i = 0; i < n; i++) {
        gdouble x = (ndata - 1.0)*i/(n - 1.0);
        gint j = (gint)floor(x);

        if (G_UNLIKELY(j >= ndata-1)) {
            j = ndata-2;
            x = 1.0;
        }
        else
            x -= j;

        z[i] = sorted[j]*(1.0 - x) + sorted[j+1]*x;
    }

    g_free(sorted);
}

static void
build_values_from_pdf(gdouble *z, guint n, gdouble a, gdouble b, gdouble *pdf, guint npdf)
{
    gdouble s, h, y, xi;
    guint i, j;

    /* Transform pdf to normalised cumulative distribution function. */
    pdf[0] = 0.0;  /* We cannot sample to the left anyway. */
    for (i = 1; i < npdf; i++)
        pdf[i] += pdf[i-1];

    s = pdf[npdf-1];
    for (i = 0; i < npdf; i++)
        pdf[i] /= s;

    /* Create values by linearly sampling the CDF bins. */
    h = (b - a)/(npdf - 1.0);
    j = 0;
    for (i = 0; i < n; i++) {
        y = (i + 0.5)/n;
        while (j < npdf && y > pdf[j])
            j++;
        if (G_UNLIKELY(j == 0)) {
            z[i] = a;
            continue;
        }
        if (G_UNLIKELY(j == npdf)) {
            z[i] = b;
            continue;
        }
        /* Now we have j-1..j interval in which to interpolate. */
        xi = (y - pdf[j-1])/(pdf[j] - pdf[j-1]);
        z[i] = a + h*(j + xi - 1.0);
    }
}

static gdouble
solve_alpha_for_skew(gdouble skew)
{
    gdouble a = (4.0 - G_PI)/2.0;
    gdouble a3 = cbrt(a);
    gdouble s3 = cbrt(skew);
    gdouble u = (a*a3*s3*s3 - a3*a3*skew*s3 + skew*skew)/(a*a + skew*skew);
    gdouble alpha = sqrt(0.5*G_PI*u/(2.0 - G_PI*u));

    return alpha;
}

static void
build_values_skew_normal(gdouble *z, guint n,
                         gdouble mean, gdouble rms, gdouble skew)
{
    guint i, npdf = 2*n;
    gdouble *pdf = g_new(gdouble, npdf);
    gdouble x, s, m, alpha;

    if (fabs(skew) > 0.995)
        alpha = 100.0;
    else
        alpha = solve_alpha_for_skew(skew);

    if (skew < 0.0)
        alpha = -alpha;

    s = 0.0;
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            reduction(+:s) \
            private(i,x) \
            shared(pdf,npdf,alpha)
#endif
    for (i = 0; i < npdf; i++) {
        x = 20.0*i/(npdf - 1.0) - 10.0;
        pdf[i] = exp(-0.5*x*x) * (erf(alpha*x) + 1.0);
        s += pdf[i];
    }

    /* Ensure given mean and rms: This is actually generic and we assume
     * interval [-1,1]. */
    m = 0.0;
    for (i = 0; i < npdf; i++) {
        x = 2.0*i/(npdf - 1.0) - 1.0;
        pdf[i] /= s;
        m += x*pdf[i];
    }
    s = 0.0;
    for (i = 0; i < npdf; i++) {
        x = 2.0*i/(npdf - 1.0) - 1.0 - m;
        s += x*x*pdf[i];
    }
    s = sqrt(s);

    build_values_from_pdf(z, n, -(1.0 + m)/s*rms + mean, (1.0 - m)/s*rms + mean, pdf, npdf);
    g_free(pdf);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    CoerceDistributionType distribution = gwy_params_get_enum(params, PARAM_DISTRIBUTION);
    GwyAppDataId template = gwy_params_get_data_id(params, PARAM_TEMPLATE);
    gboolean is_none = gwy_params_data_id_is_none(params, PARAM_TEMPLATE);

    if (distribution == COERCE_DISTRIBUTION_DATA
        && (is_none || !template_filter(gwy_app_data_browser_get(template.datano), template.id, args->field)))
        gwy_params_reset(params, PARAM_DISTRIBUTION);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
