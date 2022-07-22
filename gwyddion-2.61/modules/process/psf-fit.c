/*
 *  $Id: psf-fit.c 24657 2022-03-10 13:16:25Z yeti-dn $
 *  Copyright (C) 2017-2022 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwynlfit.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/simplefft.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/inttrans.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

#define field_convolve_default(field, kernel) \
    gwy_data_field_area_ext_convolve((field), \
                                     0, 0, \
                                     gwy_data_field_get_xres(field), \
                                     gwy_data_field_get_yres(field), \
                                     (field), (kernel), \
                                     GWY_EXTERIOR_BORDER_EXTEND, 0.0, TRUE)

#define DECLARE_PSF(name) \
    static gdouble  psf_##name##_fit_func   (guint i, \
                                             const gdouble *param, \
                                             gpointer user_data, \
                                             gboolean *success); \
    static void     psf_##name##_fit_diff   (guint i, \
                                             const gdouble *param, \
                                             const gboolean *fixed_param, \
                                             GwyNLFitIdxFunc func, \
                                             gpointer user_data, \
                                             gdouble *der, \
                                             gboolean *success); \
    static gboolean psf_##name##_init_params(GwyDataField *model_re, \
                                             GwyDataField *model_im, \
                                             GwyDataField *data_re, \
                                             GwyDataField *data_im, \
                                             GwyDataField *freq_x, \
                                             GwyDataField *freq_y, \
                                             gdouble *params); \
    static void     psf_##name##_fill_psf   (GwyDataField *freq_x, \
                                             GwyDataField *freq_y, \
                                             GwyDataField *buf_re, \
                                             GwyDataField *buf_im, \
                                             GwyDataField *psf, \
                                             GwyDataField *psf_fft, \
                                             const gdouble *param);

/* NB: The values directly index the functions[] array. */
typedef enum {
    PSF_FUNC_GAUSSIAN    = 0,
    PSF_FUNC_AGAUSSIAN   = 1,
    PSF_FUNC_EXPONENTIAL = 2,
    PSF_FUNC_NFUNCTIONS,
} PSFFunctionType;

typedef enum {
    PSF_OUTPUT_PSF       = 0,
    PSF_OUTPUT_CONVOLVED = 1,
    PSF_OUTPUT_DIFFERECE = 2,
} PSFOutputType;

enum {
    PARAM_FUNCTION,
    PARAM_WINDOWING,
    PARAM_AS_INTEGRAL,
    PARAM_OUTPUT_TYPE,
    PARAM_IDEAL,
};

typedef gboolean (*PSFParamInitFunc)(GwyDataField *model_re,
                                     GwyDataField *model_im,
                                     GwyDataField *data_re,
                                     GwyDataField *data_im,
                                     GwyDataField *freq_x,
                                     GwyDataField *freq_y,
                                     gdouble *params);
typedef void (*PSFFillFunc)(GwyDataField *freq_x,
                            GwyDataField *freq_y,
                            GwyDataField *buf_re,
                            GwyDataField *buf_im,
                            GwyDataField *psf,
                            GwyDataField *psf_fft,
                            const gdouble *params);

typedef struct {
    const gchar *name;
    GwyNLFitIdxFunc func;
    GwyNLFitIdxDiffFunc diff;
    PSFParamInitFunc initpar;
    PSFFillFunc fill;
    guint nparams;
} PSFFunction;

typedef struct {
    guint xres;
    guint yres;
    gdouble *xfreq;
    gdouble *yfreq;
    gdouble *model_re;
    gdouble *model_im;
    gdouble *data_re;
    gdouble *data_im;
} PSFEstimateData;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *psf;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register            (void);
static GwyParamDef*     define_module_params       (void);
static void             psf                        (GwyContainer *data,
                                                    GwyRunType runtype);
static void             execute                    (ModuleArgs *args);
static GwyDialogOutcome run_gui                    (ModuleArgs *args);
static void             param_changed              (ModuleGUI *gui,
                                                    gint id);
static gboolean         ideal_image_filter         (GwyContainer *data,
                                                    gint id,
                                                    gpointer user_data);
static gint             create_output_field        (GwyDataField *field,
                                                    GwyContainer *data,
                                                    gint id,
                                                    const gchar *name);
static void             fit_psf                    (GwyDataField *model,
                                                    GwyDataField *data,
                                                    GwyDataField *psf,
                                                    const PSFFunction *func,
                                                    GwyWindowingType windowing);
static void             adjust_tf_to_non_integral  (GwyDataField *psf);
static void             set_transfer_function_units(GwyDataField *ideal,
                                                    GwyDataField *measured,
                                                    GwyDataField *transferfunc);

DECLARE_PSF(gaussian);
DECLARE_PSF(agaussian);
DECLARE_PSF(exponential);

static const PSFFunction functions[] = {
    {
        N_("Gaussian"),
        psf_gaussian_fit_func, psf_gaussian_fit_diff, psf_gaussian_init_params, psf_gaussian_fill_psf,
        2,
    },
    {
        N_("Gaussian (asymmetric)"),
        psf_agaussian_fit_func, psf_agaussian_fit_diff, psf_agaussian_init_params, psf_agaussian_fill_psf,
        3,
    },
    {
        N_("Frequency-space exponential"),
        psf_exponential_fit_func, psf_exponential_fit_diff, psf_exponential_init_params, psf_exponential_fill_psf,
        2,
    },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Transfer function estimation by fitting explicit function form."),
    "Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2017",
};

GWY_MODULE_QUERY2(module_info, psf_fit)

static gboolean
module_register(void)
{
    gwy_process_func_register("psf-fit",
                              (GwyProcessFunc)&psf,
                              N_("/_Statistics/Transfer _Function Fit..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Fit transfer function from known data and image"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum outputs[] = {
        { N_("Transfer function"), (1 << PSF_OUTPUT_PSF),       },
        { N_("Convolved"),         (1 << PSF_OUTPUT_CONVOLVED), },
        { N_("Difference"),        (1 << PSF_OUTPUT_DIFFERECE), },
    };
    static GwyEnum funcs[PSF_FUNC_NFUNCTIONS];
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    gwy_enum_fill_from_struct(funcs, PSF_FUNC_NFUNCTIONS, functions,
                              sizeof(PSFFunction), G_STRUCT_OFFSET(PSFFunction, name), -1);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_FUNCTION, "function", _("_Function type"),
                              funcs, G_N_ELEMENTS(funcs), PSF_FUNC_GAUSSIAN);
    gwy_param_def_add_enum(paramdef, PARAM_WINDOWING, "windowing", NULL, GWY_TYPE_WINDOWING_TYPE, GWY_WINDOWING_WELCH);
    gwy_param_def_add_boolean(paramdef, PARAM_AS_INTEGRAL, "as_integral", "Normalize as _integral", TRUE);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT_TYPE, "output_type", _("Output"),
                               outputs, G_N_ELEMENTS(outputs), (1 << PSF_OUTPUT_PSF));
    gwy_param_def_add_image_id(paramdef, PARAM_IDEAL, "ideal", _("_Ideal response"));
    return paramdef;
}

static void
psf(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GwyDataField *convolved = NULL, *difference, *field, *ideal;
    ModuleArgs args;
    guint output;
    gint id, newid = -1;

    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    args.field = field;
    args.psf = gwy_data_field_new_alike(field, TRUE);
    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    output = gwy_params_get_flags(args.params, PARAM_OUTPUT_TYPE);
    if (!output || !(ideal = gwy_params_get_image(args.params, PARAM_IDEAL)))
        goto end;

    execute(&args);
    if (output & ((1 << PSF_OUTPUT_CONVOLVED) | (1 << PSF_OUTPUT_DIFFERECE))) {
        convolved = gwy_data_field_new_alike(field, FALSE);
        gwy_data_field_copy(ideal, convolved, TRUE);
        gwy_data_field_add(convolved, -gwy_data_field_get_avg(convolved));
        field_convolve_default(convolved, args.psf);
        gwy_data_field_add(convolved, gwy_data_field_get_avg(field));
    }

    if (output & (1 << PSF_OUTPUT_PSF))
        newid = create_output_field(args.psf, data, id, _("Transfer function"));
    if (output & (1 << PSF_OUTPUT_CONVOLVED))
        create_output_field(convolved, data, id, _("Convolved"));
    if (output & (1 << PSF_OUTPUT_DIFFERECE)) {
        difference = gwy_data_field_new_alike(field, FALSE);
        gwy_data_field_subtract_fields(difference, convolved, field);
        create_output_field(difference, data, id, _("Difference"));
        g_object_unref(difference);
    }

    /* Change the normalisation to the discrete (i.e. wrong) one after all calculations are done. */
    if (newid != -1 && !gwy_params_get_boolean(args.params, PARAM_AS_INTEGRAL)) {
        adjust_tf_to_non_integral(args.psf);
        gwy_data_field_data_changed(args.psf);
    }

end:
    g_object_unref(args.params);
    g_object_unref(args.psf);
    GWY_OBJECT_UNREF(convolved);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Fit Transfer Function"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_image_id(table, PARAM_IDEAL);
    gwy_param_table_data_id_set_filter(table, PARAM_IDEAL, ideal_image_filter, args->field, NULL);
    gwy_param_table_append_combo(table, PARAM_FUNCTION);
    gwy_param_table_append_combo(table, PARAM_WINDOWING);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkboxes(table, PARAM_OUTPUT_TYPE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_AS_INTEGRAL);

    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id)
{
    GwyParams *params = gui->args->params;
    guint output = gwy_params_get_flags(params, PARAM_OUTPUT_TYPE);
    gboolean have_ideal = !gwy_params_data_id_is_none(params, PARAM_IDEAL);

    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, output && have_ideal);
}

static gboolean
ideal_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *field = (GwyDataField*)user_data;
    GwyDataField *ideal = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));

    if (ideal == field)
        return FALSE;

    return !gwy_data_field_check_compatibility(ideal, field,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyWindowingType windowing = gwy_params_get_enum(params, PARAM_WINDOWING);
    PSFFunctionType func = gwy_params_get_enum(params, PARAM_FUNCTION);
    GwyDataField *field = args->field, *psf = args->psf;
    GwyDataField *ideal = gwy_params_get_image(params, PARAM_IDEAL);
    gdouble q;

    fit_psf(ideal, field, psf, functions + func, windowing);
    /* See psf.c for normalisation convention. */
    q = sqrt(gwy_data_field_get_xres(field)*gwy_data_field_get_yres(field))
        /(gwy_data_field_get_xreal(field)*gwy_data_field_get_yreal(field));
    gwy_data_field_multiply(psf, q);
}

static gint
create_output_field(GwyDataField *field, GwyContainer *data, gint id, const gchar *name)
{
    gint newid;

    newid = gwy_app_data_browser_add_data_field(field, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    gwy_app_set_data_field_title(data, newid, name);
    gwy_app_channel_log_add_proc(data, id, newid);

    return newid;
}

static gdouble
calculate_root_mean_square_complex(const gdouble *pre, const gdouble *pim,
                                   guint xres, guint yres)
{
    guint k;
    gdouble sum = 0.0;

    for (k = 0; k < xres*yres; k++)
        sum += pre[k]*pre[k] + pim[k]*pim[k];

    return sqrt(sum);
}

static void
precalculate_frequencies(GwyDataField *model, GwyDataField *freq_x, GwyDataField *freq_y)
{
    guint xres = gwy_data_field_get_xres(model), yres = gwy_data_field_get_yres(model);
    gdouble sx = 1.0/gwy_data_field_get_xreal(model), sy = 1.0/gwy_data_field_get_yreal(model);
    gdouble *fx = gwy_data_field_get_data(freq_x);
    gdouble *fy = gwy_data_field_get_data(freq_y);
    gdouble vx, vy;
    guint i, j;

    fx[0] = fy[0] = 0.0;

    for (j = 1; j <= xres/2; j++) {
        vx = j*sx;
        fx[xres-j] = -vx;
        fx[j] = vx;
        fy[j] = fy[xres-j] = 0.0;
    }

    for (i = 1; i <= yres/2; i++) {
        vy = i*sy;
        fx[i*xres] = fx[(yres-i)*xres] = 0.0;
        fy[(yres-i)*xres] = -vy;
        fy[i*xres] = vy;
    }

    for (i = 1; i <= yres/2; i++) {
        vy = i*sy;
        for (j = 1; j <= xres/2; j++) {
            vx = j*sx;
            fx[(yres-i)*xres + xres-j] = -vx;
            fx[i*xres + xres-j] = -vx;
            fx[(yres-i)*xres + j] = vx;
            fx[i*xres + j] = vx;
            fy[(yres-i)*xres + xres-j] = -vy;
            fy[(yres-i)*xres + j] = -vy;
            fy[i*xres + xres-j] = vy;
            fy[i*xres + j] = vy;
        }
    }
}

/* Suppress higher frequencies somewhat; they are lots of them and contain noise.
 * Do not bother with weighting inside the fitting when we can just premultiply model and data to achieve the same
 * effect on cheap. */
static void
weight_fourier_components(GwyDataField *fftfield, GwyDataField *freq_x, GwyDataField *freq_y)
{
    guint xres = gwy_data_field_get_xres(fftfield), yres = gwy_data_field_get_yres(fftfield);
    const gdouble *fx = gwy_data_field_get_data(freq_x);
    const gdouble *fy = gwy_data_field_get_data(freq_y);
    gdouble *d = gwy_data_field_get_data(fftfield);
    guint k = (yres/2)*xres + xres/2;
    gdouble fmax2 = fx[k]*fx[k] + fy[k]*fy[k];
    gdouble factor = 3.0/fmax2;

    d[0] = 0.0;
    for (k = 1; k < xres*yres; k++)
        d[k] /= 1.0 + factor*(fx[k]*fx[k] + fy[k]*fy[k]);
}

#ifdef DEBUG
static void
debug_print_params(const PSFFunction *func, const gdouble *params)
{
    GString *str = g_string_new(NULL);
    guint i;

    for (i = 0; i < func->nparams; i++)
        g_string_append_printf(str, " %g", params[i]);
    gwy_debug("params %s", str->str);
    g_string_free(str, TRUE);
}
#else
#define debug_print_params(func,params) /* */
#endif

static GwyDataField*
prepare_field(GwyDataField *field, GwyWindowingType window)
{
    GwyDataField *wfield;

    wfield = gwy_data_field_duplicate(field);
    gwy_data_field_add(wfield, -gwy_data_field_get_avg(wfield));
    gwy_fft_window_data_field(wfield, GWY_ORIENTATION_HORIZONTAL, window);
    gwy_fft_window_data_field(wfield, GWY_ORIENTATION_VERTICAL, window);

    return wfield;
}

static void
fit_psf(GwyDataField *model, GwyDataField *data, GwyDataField *psf,
        const PSFFunction *func, GwyWindowingType windowing)
{
    PSFEstimateData psfedata;
    guint xres = gwy_data_field_get_xres(model), yres = gwy_data_field_get_yres(model);
    GwyDataField *model_re, *model_im, *data_re, *data_im, *freq_x, *freq_y, *xd, *xm;
    GwyNLFitter *fitter = NULL;
    gdouble rss;
    gdouble *params = g_new(gdouble, func->nparams);

    /* We only need xm and xd at the beginning; reuse them for the freq fields. */
    xm = freq_x = prepare_field(model, windowing);
    xd = freq_y = prepare_field(data, windowing);

    model_re = gwy_data_field_new_alike(xm, FALSE);
    model_im = gwy_data_field_new_alike(xm, FALSE);
    gwy_data_field_2dfft_raw(xm, NULL, model_re, model_im, GWY_TRANSFORM_DIRECTION_FORWARD);

    data_re = gwy_data_field_new_alike(xd, FALSE);
    data_im = gwy_data_field_new_alike(xd, FALSE);
    gwy_data_field_2dfft_raw(xd, NULL, data_re, data_im, GWY_TRANSFORM_DIRECTION_FORWARD);

    precalculate_frequencies(model, freq_x, freq_y);

    if (!func->initpar(model_re, model_im, data_re, data_im, freq_x, freq_y, params)) {
        g_warning("Initial parameter estimation failed.");
        gwy_data_field_clear(psf);
        gwy_data_field_set_val(psf, 0, 0, 1.0);
        goto fail;
    }

    weight_fourier_components(model_re, freq_x, freq_y);
    weight_fourier_components(model_im, freq_x, freq_y);
    weight_fourier_components(data_re, freq_x, freq_y);
    weight_fourier_components(data_im, freq_x, freq_y);

    psfedata.xres = xres;
    psfedata.yres = yres;
    psfedata.model_re = gwy_data_field_get_data(model_re);
    psfedata.model_im = gwy_data_field_get_data(model_im);
    psfedata.data_re = gwy_data_field_get_data(data_re);
    psfedata.data_im = gwy_data_field_get_data(data_im);
    psfedata.xfreq = gwy_data_field_get_data(freq_x);
    psfedata.yfreq = gwy_data_field_get_data(freq_y);

    fitter = gwy_math_nlfit_new_idx(func->func, func->diff);
    rss = gwy_math_nlfit_fit_idx(fitter, 2*xres*yres, func->nparams, params, &psfedata);
    gwy_debug("Fitted rss %g", rss);
    if (!(rss >= 0.0)) {
        g_warning("Initial parameter estimation failed.");
        gwy_data_field_clear(psf);
        gwy_data_field_set_val(psf, 0, 0, 1.0);
        goto fail;
    }
    debug_print_params(func, params);

    /* Use freq_x as a buffer for FFT(psf) */
    func->fill(freq_x, freq_y, data_re, data_im, psf, freq_x, params);

    set_transfer_function_units(model, data, psf);

fail:
    if (fitter)
        gwy_math_nlfit_free(fitter);
    g_free(params);
    g_object_unref(freq_x);
    g_object_unref(freq_y);
    g_object_unref(data_im);
    g_object_unref(data_re);
    g_object_unref(model_re);
    g_object_unref(model_im);
}

static void
set_transfer_function_units(GwyDataField *ideal, GwyDataField *measured, GwyDataField *transferfunc)
{
    GwySIUnit *sunit, *iunit, *tunit, *xyunit;

    xyunit = gwy_data_field_get_si_unit_xy(measured);
    sunit = gwy_data_field_get_si_unit_z(ideal);
    iunit = gwy_data_field_get_si_unit_z(measured);
    tunit = gwy_data_field_get_si_unit_z(transferfunc);
    gwy_si_unit_divide(iunit, sunit, tunit);
    gwy_si_unit_power_multiply(tunit, 1, xyunit, -2, tunit);
}

static void
adjust_tf_to_non_integral(GwyDataField *psf)
{
    GwySIUnit *xyunit, *zunit;

    xyunit = gwy_data_field_get_si_unit_xy(psf);
    zunit = gwy_data_field_get_si_unit_z(psf);
    gwy_si_unit_power_multiply(zunit, 1, xyunit, 2, zunit);
    gwy_data_field_multiply(psf, gwy_data_field_get_dx(psf) * gwy_data_field_get_dy(psf));
}

static gdouble
estimate_width(const gdouble *pre, const gdouble *pim,
               const gdouble *fx, const gdouble *fy,
               guint xres, guint yres)
{
    guint k;
    gdouble sum = 0.0;

    for (k = 0; k < xres*yres; k++)
        sum += (fx[k]*fx[k] + fy[k]*fy[k])*(pre[k]*pre[k] + pim[k]*pim[k]);

    return sqrt(sum);
}

/* We fit G*model on data, so our residuum function is G*model-data. */
static gdouble
psf_gaussian_fit_func(guint i, const gdouble *param, gpointer user_data,
                      gboolean *success)
{
    PSFEstimateData *psfedata = (PSFEstimateData*)user_data;
    guint k = i/2;
    gdouble A = param[0], width = param[1];
    gdouble fx = psfedata->xfreq[k], fy = psfedata->yfreq[k];
    gdouble r2 = (fx*fx + fy*fy)/(width*width);
    gdouble g, m, d;

    if (G_UNLIKELY(width == 0.0)) {
        *success = FALSE;
        return 0.0;
    }

    *success = TRUE;
    g = exp(-r2);
    m = (i % 2) ? psfedata->model_im[k] : psfedata->model_re[k];
    d = (i % 2) ? psfedata->data_im[k] : psfedata->data_re[k];
    return A*g*m - d;
}

static void
psf_gaussian_fit_diff(guint i,
                      const gdouble *param, const gboolean *fixed_param,
                      G_GNUC_UNUSED GwyNLFitIdxFunc func, gpointer user_data,
                      gdouble *der,
                      gboolean *success)
{
    PSFEstimateData *psfedata = (PSFEstimateData*)user_data;
    guint k = i/2;
    gdouble A = param[0], width = param[1];
    gdouble fx = psfedata->xfreq[k], fy = psfedata->yfreq[k];
    gdouble r2 = (fx*fx + fy*fy)/(width*width);
    gdouble g, m;

    if (G_UNLIKELY(width == 0.0)) {
        *success = FALSE;
        return;
    }

    *success = TRUE;
    g = exp(-r2);
    m = (i % 2) ? psfedata->model_im[k] : psfedata->model_re[k];
    der[0] = (fixed_param && fixed_param[0]) ? 0.0 : g*m;
    der[1] = (fixed_param && fixed_param[1]) ? 0.0 : 2.0*A/width*r2*g*m;
}

static gboolean
psf_gaussian_init_params(GwyDataField *model_re, GwyDataField *model_im,
                         GwyDataField *data_re, GwyDataField *data_im,
                         GwyDataField *freq_x, GwyDataField *freq_y,
                         gdouble *params)
{
    guint xres = gwy_data_field_get_xres(model_re), yres = gwy_data_field_get_yres(model_re);
    const gdouble *mre = gwy_data_field_get_data_const(model_re);
    const gdouble *mim = gwy_data_field_get_data_const(model_im);
    const gdouble *dre = gwy_data_field_get_data_const(data_re);
    const gdouble *dim = gwy_data_field_get_data_const(data_im);
    const gdouble *fx = gwy_data_field_get_data_const(freq_x);
    const gdouble *fy = gwy_data_field_get_data_const(freq_y);
    gdouble q_model, q_data, w_model, w_data;

    /* Amplitude. */
    q_model = calculate_root_mean_square_complex(mre, mim, xres, yres);
    q_data = calculate_root_mean_square_complex(dre, dim, xres, yres);
    if (!q_model || !q_data)
        params[0] = 0.0;
    else
        params[0] = q_data/q_model;
    gwy_debug("q_model %g, q_data %g => amplitude %g", q_model, q_data, params[0]);

    /* Width. */
    w_model = estimate_width(mre, mim, fx, fy, xres, yres)/q_model;
    w_data = estimate_width(dre, dim, fx, fy, xres, yres)/q_data;
    params[1] = 0.7*sqrt(fmax(w_model*w_model - w_data*w_data, 0.0)) + 0.3*MIN(w_model, w_data);
    gwy_debug("w_model %g, w_data %g => width %g", w_model, w_data, params[1]);

    return params[0] > 0.0 && params[1] > 0.0;
}

static void
psf_gaussian_fill_psf(GwyDataField *freq_x, GwyDataField *freq_y,
                      G_GNUC_UNUSED GwyDataField *buf_re, GwyDataField *buf_im,
                      GwyDataField *psf, GwyDataField *psf_fft,
                      const gdouble *param)
{
    gdouble A = param[0], w = param[1];
    guint xres = gwy_data_field_get_xres(freq_x), yres = gwy_data_field_get_yres(freq_x);
    const gdouble *fx = gwy_data_field_get_data_const(freq_x);
    const gdouble *fy = gwy_data_field_get_data_const(freq_y);
    gdouble *pf = gwy_data_field_get_data(psf_fft);
    gdouble r2, g;
    guint k;

    for (k = 0; k < xres*yres; k++) {
        r2 = (fx[k]*fx[k] + fy[k]*fy[k])/(w*w);
        g = exp(-r2);
        pf[k] = A*g;
    }

    gwy_data_field_2dfft_raw(psf_fft, NULL, psf, buf_im, GWY_TRANSFORM_DIRECTION_BACKWARD);
    gwy_data_field_2dfft_humanize(psf);
}

static gdouble
psf_agaussian_fit_func(guint i, const gdouble *param, gpointer user_data,
                       gboolean *success)
{
    PSFEstimateData *psfedata = (PSFEstimateData*)user_data;
    guint k = i/2;
    gdouble A = param[0], widthx = param[1], widthy = param[2];
    gdouble fx = psfedata->xfreq[k]/widthx, fy = psfedata->yfreq[k]/widthy;
    gdouble r2 = fx*fx + fy*fy;
    gdouble g, m, d;

    if (G_UNLIKELY(widthx == 0.0 || widthy == 0.0)) {
        *success = FALSE;
        return 0.0;
    }

    *success = TRUE;
    g = exp(-r2);
    m = (i % 2) ? psfedata->model_im[k] : psfedata->model_re[k];
    d = (i % 2) ? psfedata->data_im[k] : psfedata->data_re[k];
    return A*g*m - d;
}

static void
psf_agaussian_fit_diff(guint i,
                       const gdouble *param, const gboolean *fixed_param,
                       G_GNUC_UNUSED GwyNLFitIdxFunc func, gpointer user_data,
                       gdouble *der,
                       gboolean *success)
{
    PSFEstimateData *psfedata = (PSFEstimateData*)user_data;
    guint k = i/2;
    gdouble A = param[0], widthx = param[1], widthy = param[2];
    gdouble fx = psfedata->xfreq[k]/widthx, fy = psfedata->yfreq[k]/widthy;
    gdouble r2 = fx*fx + fy*fy;
    gdouble g, m;

    if (G_UNLIKELY(widthx == 0.0 || widthy == 0.0)) {
        *success = FALSE;
        return;
    }

    *success = TRUE;
    g = exp(-r2);
    m = (i % 2) ? psfedata->model_im[k] : psfedata->model_re[k];
    der[0] = (fixed_param && fixed_param[0]) ? 0.0 : g*m;
    der[1] = (fixed_param && fixed_param[1]) ? 0.0 : 2.0*A/widthx*fx*fx*g*m;
    der[2] = (fixed_param && fixed_param[2]) ? 0.0 : 2.0*A/widthy*fy*fy*g*m;
}

static gboolean
psf_agaussian_init_params(GwyDataField *model_re, GwyDataField *model_im,
                          GwyDataField *data_re, GwyDataField *data_im,
                          GwyDataField *freq_x, GwyDataField *freq_y,
                          gdouble *params)
{
    if (!psf_gaussian_init_params(model_re, model_im, data_re, data_im, freq_x, freq_y, params))
        return FALSE;

    params[2] = params[1];
    return TRUE;
}

static void
psf_agaussian_fill_psf(GwyDataField *freq_x, GwyDataField *freq_y,
                       G_GNUC_UNUSED GwyDataField *buf_re, GwyDataField *buf_im,
                       GwyDataField *psf, GwyDataField *psf_fft,
                       const gdouble *param)
{
    gdouble A = param[0], wx = param[1], wy = param[2];
    guint xres = gwy_data_field_get_xres(freq_x), yres = gwy_data_field_get_yres(freq_x);
    const gdouble *fx = gwy_data_field_get_data_const(freq_x);
    const gdouble *fy = gwy_data_field_get_data_const(freq_y);
    gdouble *pf = gwy_data_field_get_data(psf_fft);
    gdouble r2, g;
    guint k;

    for (k = 0; k < xres*yres; k++) {
        r2 = fx[k]*fx[k]/(wx*wx) + fy[k]*fy[k]/(wy*wy);
        g = exp(-r2);
        pf[k] = A*g;
    }

    gwy_data_field_2dfft_raw(psf_fft, NULL, psf, buf_im, GWY_TRANSFORM_DIRECTION_BACKWARD);
    gwy_data_field_2dfft_humanize(psf);
}

/* We fit G*model on data, so our residuum function is G*model-data. */
static gdouble
psf_exponential_fit_func(guint i, const gdouble *param, gpointer user_data,
                         gboolean *success)
{
    PSFEstimateData *psfedata = (PSFEstimateData*)user_data;
    guint k = i/2;
    gdouble A = param[0], width = param[1];
    gdouble fx = psfedata->xfreq[k], fy = psfedata->yfreq[k];
    gdouble r2 = (fx*fx + fy*fy)/(width*width);
    gdouble g, m, d;

    if (G_UNLIKELY(width == 0.0)) {
        *success = FALSE;
        return 0.0;
    }

    *success = TRUE;
    g = exp(-sqrt(r2));
    m = (i % 2) ? psfedata->model_im[k] : psfedata->model_re[k];
    d = (i % 2) ? psfedata->data_im[k] : psfedata->data_re[k];
    return A*g*m - d;
}

static void
psf_exponential_fit_diff(guint i,
                         const gdouble *param, const gboolean *fixed_param,
                         G_GNUC_UNUSED GwyNLFitIdxFunc func, gpointer user_data,
                         gdouble *der,
                         gboolean *success)
{
    PSFEstimateData *psfedata = (PSFEstimateData*)user_data;
    guint k = i/2;
    gdouble A = param[0], width = param[1];
    gdouble fx = psfedata->xfreq[k], fy = psfedata->yfreq[k];
    gdouble r2 = (fx*fx + fy*fy)/(width*width);
    gdouble g, m;

    if (G_UNLIKELY(width == 0.0)) {
        *success = FALSE;
        return;
    }

    *success = TRUE;
    g = exp(-sqrt(r2));
    m = (i % 2) ? psfedata->model_im[k] : psfedata->model_re[k];
    der[0] = (fixed_param && fixed_param[0]) ? 0.0 : g*m;
    der[1] = (fixed_param && fixed_param[1]) ? 0.0 : 2.0*A/width*r2*g*m;
}

static gboolean
psf_exponential_init_params(GwyDataField *model_re, GwyDataField *model_im,
                            GwyDataField *data_re, GwyDataField *data_im,
                            GwyDataField *freq_x, GwyDataField *freq_y,
                            gdouble *params)
{
    guint xres = gwy_data_field_get_xres(model_re), yres = gwy_data_field_get_yres(model_re);
    const gdouble *mre = gwy_data_field_get_data_const(model_re);
    const gdouble *mim = gwy_data_field_get_data_const(model_im);
    const gdouble *dre = gwy_data_field_get_data_const(data_re);
    const gdouble *dim = gwy_data_field_get_data_const(data_im);
    const gdouble *fx = gwy_data_field_get_data_const(freq_x);
    const gdouble *fy = gwy_data_field_get_data_const(freq_y);
    gdouble q_model, q_data, w_model, w_data;

    /* Amplitude. */
    q_model = calculate_root_mean_square_complex(mre, mim, xres, yres);
    q_data = calculate_root_mean_square_complex(dre, dim, xres, yres);
    if (!q_model || !q_data)
        params[0] = 0.0;
    else
        params[0] = q_data/q_model;
    gwy_debug("q_model %g, q_data %g => amplitude %g", q_model, q_data, params[0]);

    /* Width. */
    w_model = estimate_width(mre, mim, fx, fy, xres, yres)/q_model;
    w_data = estimate_width(dre, dim, fx, fy, xres, yres)/q_data;
    params[1] = 0.7*sqrt(fmax(w_model*w_model - w_data*w_data, 0.0)) + 0.3*MIN(w_model, w_data);
    gwy_debug("w_model %g, w_data %g => width %g", w_model, w_data, params[1]);

    return params[0] > 0.0 && params[1] > 0.0;
}

static void
psf_exponential_fill_psf(GwyDataField *freq_x, GwyDataField *freq_y,
                         G_GNUC_UNUSED GwyDataField *buf_re,
                         GwyDataField *buf_im,
                         GwyDataField *psf, GwyDataField *psf_fft,
                         const gdouble *param)
{
    gdouble A = param[0], w = param[1];
    guint xres = gwy_data_field_get_xres(freq_x), yres = gwy_data_field_get_yres(freq_x);
    const gdouble *fx = gwy_data_field_get_data_const(freq_x);
    const gdouble *fy = gwy_data_field_get_data_const(freq_y);
    gdouble *pf = gwy_data_field_get_data(psf_fft);
    gdouble r2, g;
    guint k;

    for (k = 0; k < xres*yres; k++) {
        r2 = (fx[k]*fx[k] + fy[k]*fy[k])/(w*w);
        g = exp(-sqrt(r2));
        pf[k] = A*g;
    }

    gwy_data_field_2dfft_raw(psf_fft, NULL, psf, buf_im, GWY_TRANSFORM_DIRECTION_BACKWARD);
    gwy_data_field_2dfft_humanize(psf);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
