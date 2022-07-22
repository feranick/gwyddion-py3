/*
 *  $Id: cpde_synth.c 23788 2021-05-26 12:14:14Z yeti-dn $
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define DECLARE_PRESET(name) \
    static gboolean (cpde_##name)(ModuleArgs *args, gdouble *domain, GTimer *timer, gdouble preview_time)

typedef enum {
    CPDE_TURING_PATTERN = 0,
} CpdeSynthPresetType;

enum {
    PARAM_PRESET,
    PARAM_NITERS,
    PARAM_HEIGHT,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_ANIMATED,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

    PARAM_TURING_SIZE,
    PARAM_TURING_CHAOS,

    PARAM_DIMS0
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached input image parameters. */
    gdouble zscale;  /* Negative value means there is no input image. */
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_dimensions;
    GwyParamTable *table_generator;
    GwyContainer *data;
    GwyDataField *template_;
} ModuleGUI;

typedef gboolean (*CpdeSynthPresetFunc)(ModuleArgs *args,
                                        gdouble *domain,
                                        GTimer *timer,
                                        gdouble preview_time);

typedef struct {
    const gchar *name;
    CpdeSynthPresetFunc func;
    gint domain_size;
} CpdeSynthPreset;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             cpde_synth          (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *wait_window);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static GtkWidget*       dimensions_tab_new  (ModuleGUI *gui);
static GtkWidget*       generator_tab_new   (ModuleGUI *gui);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             dialog_response     (ModuleGUI *gui,
                                             gint response);
static void             preview             (gpointer user_data);

DECLARE_PRESET(turing_pattern);

/* NB: The order of these must match the enums.  See obj_synth.c for how to reorder it in the GUI. */
static const CpdeSynthPreset presets[] = {
    { N_("Turing pattern"), &cpde_turing_pattern, 5, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates images by assorted coupled partial differential equation models."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, cpde_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("cpde_synth",
                              (GwyProcessFunc)&cpde_synth,
                              N_("/S_ynthetic/Coupled PD_Es..."),
                              GWY_STOCK_SYNTHETIC_TURING_PATTERN,
                              RUN_MODES,
                              0,
                              N_("Generate image by coupled PDEs"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyEnum *patterns = NULL;
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    patterns = gwy_enum_fill_from_struct(NULL, G_N_ELEMENTS(presets), presets, sizeof(CpdeSynthPreset),
                                         G_STRUCT_OFFSET(CpdeSynthPreset, name), -1);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_PRESET, "preset", _("_Pattern"),
                              patterns, G_N_ELEMENTS(presets), CPDE_TURING_PATTERN);
    gwy_param_def_add_int(paramdef, PARAM_NITERS, "niters", _("_Number of iterations"), 1, 1000000, 10000);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height scale"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_ANIMATED, "animated", _("Progressive preview"), TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);

    gwy_param_def_add_double(paramdef, PARAM_TURING_SIZE, "turing/size", _("Si_ze"), 2.2, 100.0, 8.0);
    gwy_param_def_add_double(paramdef, PARAM_TURING_CHAOS, "turing/chaos", _("Degree of _chaos"), 0.0, 1.0, 0.25);

    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
cpde_synth(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GwyDataField *field;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    args.field = field;
    args.zscale = field ? gwy_data_field_get_rms(field) : -1.0;

    args.params = gwy_params_new_from_settings(define_module_params());
    gwy_synth_sanitise_params(args.params, PARAM_DIMS0, field);
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    args.result = gwy_synth_make_result_data_field((args.field = field), args.params, FALSE);
    if (gwy_params_get_boolean(args.params, PARAM_ANIMATED))
        gwy_app_wait_preview_data_field(args.result, data, id);
    if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
        goto end;
    gwy_synth_add_result_to_file(args.result, data, id, args.params);

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GtkWidget *hbox, *dataview;
    GtkNotebook *notebook;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.template_ = args->field;

    if (gui.template_)
        args->field = gwy_synth_make_preview_data_field(gui.template_, PREVIEW_SIZE);
    else
        args->field = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE, PREVIEW_SIZE, PREVIEW_SIZE, TRUE);
    args->result = gwy_synth_make_result_data_field(args->field, args->params, TRUE);

    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->result);
    if (gui.template_)
        gwy_app_sync_data_items(data, gui.data, id, 0, FALSE, GWY_DATA_ITEM_GRADIENT, 0);

    gui.dialog = gwy_dialog_new(_("Coupled PDEs"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, dimensions_tab_new(&gui), gtk_label_new(_("Dimensions")));
    gtk_notebook_append_page(notebook, generator_tab_new(&gui), gtk_label_new(_("Generator")));
    gwy_param_active_page_link_to_notebook(args->params, PARAM_ACTIVE_PAGE, notebook);

    g_signal_connect_swapped(gui.table_dimensions, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_generator, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    GWY_OBJECT_UNREF(args->field);
    GWY_OBJECT_UNREF(args->result);

    return outcome;
}

static GtkWidget*
dimensions_tab_new(ModuleGUI *gui)
{
    gui->table_dimensions = gwy_param_table_new(gui->args->params);
    gwy_synth_append_dimensions_to_param_table(gui->table_dimensions, 0);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), gui->table_dimensions);

    return gwy_param_table_widget(gui->table_dimensions);
}

static GtkWidget*
generator_tab_new(ModuleGUI *gui)
{
    GwyParamTable *table;

    table = gui->table_generator = gwy_param_table_new(gui->args->params);

    /* FIXME: One day this module may have more generators.  Then this will have to be rewritten.  But until it does,
     * do not complicate things. */
    gwy_param_table_append_header(table, -1, _("Simulation Parameters"));
    gwy_param_table_append_combo(table, PARAM_PRESET);
    gwy_param_table_append_slider(table, PARAM_NITERS);
    gwy_param_table_slider_set_mapping(table, PARAM_NITERS, GWY_SCALE_MAPPING_LOG);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_TURING_SIZE);
    gwy_param_table_slider_add_alt(table, PARAM_TURING_SIZE);
    gwy_param_table_append_slider(table, PARAM_TURING_CHAOS);

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_seed(table, PARAM_SEED);
    gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_ANIMATED);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table_generator;

    if (gwy_synth_handle_param_changed(gui->table_dimensions, id))
        id = -1;

    if (id < 0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT) {
        static const gint zids[] = { PARAM_HEIGHT };

        gwy_synth_update_value_unitstrs(table, zids, G_N_ELEMENTS(zids));
        gwy_synth_update_like_current_button_sensitivity(table, BUTTON_LIKE_CURRENT_IMAGE);
    }
    if (id < 0
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL) {
        static const gint xyids[] = { PARAM_TURING_SIZE };

        gwy_synth_update_lateral_alts(table, xyids, G_N_ELEMENTS(xyids));
    }
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;

    if (response == GWY_RESPONSE_SYNTH_INIT_Z) {
        gdouble zscale = args->zscale;
        gint power10z;

        if (zscale > 0.0) {
            gwy_params_get_unit(args->params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
            gwy_param_table_set_double(gui->table_generator, PARAM_HEIGHT, zscale/pow10(power10z));
        }
    }
    else if (response == GWY_RESPONSE_SYNTH_TAKE_DIMS) {
        gwy_synth_use_dimensions_template(gui->table_dimensions);
    }
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    if (execute(gui->args, GTK_WINDOW(gui->dialog)))
        gwy_data_field_data_changed(gui->args->result);
}

static void
copy_domain_to_data_field(GwyDataField *field, gdouble *domain, guint which)
{
    gint n = gwy_data_field_get_xres(field) * gwy_data_field_get_yres(field);
    gdouble *d = gwy_data_field_get_data(field);

    gwy_assign(d, domain + n*which, n);
}

static void
init_field_randomly(GwyDataField *field, guint32 seed)
{
    gint i, n = gwy_data_field_get_xres(field) * gwy_data_field_get_yres(field);
    gdouble *d = gwy_data_field_get_data(field);
    GRand *rng = g_rand_new();

    g_rand_set_seed(rng, seed);
    for (i = 0; i < n; i++)
        d[i] = g_rand_double(rng);
    g_rand_free(rng);
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gboolean animated = gwy_params_get_boolean(params, PARAM_ANIMATED);
    const CpdeSynthPreset *preset = presets + gwy_params_get_enum(params, PARAM_PRESET);
    GwyDataField *field = args->result;
    gint xres, yres, i, power10z;
    gdouble preview_time = (animated ? 1.25 : 0.0);
    gdouble *d, *domain;
    GTimer *timer;
    gboolean finished = FALSE;

    gwy_app_wait_start(wait_window, _("Initializing..."));

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);

    if (field && do_initialise) {
        gwy_data_field_copy(args->field, field, FALSE);
        gwy_data_field_renormalize(field, -0.5, 0.5);
    }
    else
        init_field_randomly(field, gwy_params_get_int(params, PARAM_SEED));

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    d = gwy_data_field_get_data(field);

    g_assert(preset->domain_size >= 2);
    domain = g_new(gdouble, xres*yres * preset->domain_size);
    for (i = 0; i < xres*yres; i++)
        domain[i + xres*yres] = domain[i] = d[i] - 0.5;

    timer = g_timer_new();
    gwy_synth_update_progress(NULL, 0, 0, 0);
    if (!gwy_app_wait_set_message(_("Running computation...")))
        goto end;

    if (!preset->func(args, domain, timer, preview_time))
        goto end;

    copy_domain_to_data_field(field, domain, 0);
    gwy_data_field_multiply(field, height);
    finished = TRUE;

end:
    gwy_app_wait_finish();
    g_timer_destroy(timer);
    g_free(domain);

    return finished;
}

/*
 * Funny nonlinear function.  It has following properties
 * - odd
 * - large positive derivative at 0
 * - maximum at some positive value
 * - zero at some larger value
 * - then negative, but not too much
 */
static inline gdouble
funny_func(gdouble x)
{
    return x/(1.0 + 0.01*x*x) - 0.01*x;
}

/* Mixed rectangular-diagonal Laplacian. */
static inline gdouble
laplacian8(const gdouble *rowm, const gdouble *row, const gdouble *rowp, guint jm, guint j, guint jp)
{
    return (rowm[j] + row[jm] + row[jp] + rowp[j]
            + 0.25*(rowm[jm] + rowm[jp] + rowp[jm] + rowp[jp])
            - 5.0*row[j]);
}

static inline gdouble
smooth8(const gdouble *rowm, const gdouble *row, const gdouble *rowp, guint jm, guint j, guint jp)
{
    return (row[j]
            + 0.125*(rowm[j] + row[jm] + row[jp] + rowp[j])
            + 0.03125*(rowm[jm] + rowm[jp] + rowp[jm] + rowp[jp]))/1.625;
}

static gdouble
checker_smooth(gint xres, gint yres, gdouble *r, gdouble *tmp)
{
    gdouble rr = 0.0;
    gint i;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            reduction(+:rr) \
            shared(r,tmp,xres,yres) \
            private(i)
#endif
    for (i = 0; i < yres; i++) {
        gint ix = i*xres;
        gint ixp = ((i + 1) % yres)*xres;
        gint ixm = ((i + yres-1) % yres)*xres;
        gdouble t;
        gint j;

        t = smooth8(r + ixm, r + ix, r + ixp, xres-1, 0, 1);
        tmp[ix] = t;
        rr += t*t;

        for (j = 1; j < xres-1; j++) {
            t = smooth8(r + ixm, r + ix, r + ixp, j-1, j, j+1);
            tmp[ix + j] = t;
            rr += t*t;
        }

        t = smooth8(r + ixm, r + ix, r + ixp, xres-2, xres-1, 0);
        tmp[ix + xres-1] = t;
        rr += t*t;
    }

    gwy_assign(r, tmp, xres*yres);

    return rr;
}

static void
do_iter_turing(gint xres, gint yres, gdouble *domain, const gdouble *constants, gdouble size)
{
    gdouble realdt, cr0, cr1, rr0, rr1;
    gint i, n = xres*yres;

    cr0 = cr1 = 0.0;
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            reduction(+:cr0,cr1) \
            shared(domain,xres,yres,n,constants,size) \
            private(i)
#endif
    for (i = 0; i < yres; i++) {
        gdouble *c0 = domain;
        gdouble *c1 = domain + n;
        gdouble *r0 = domain + 2*n;
        gdouble *r1 = domain + 3*n;
        const gdouble p = constants[0];
        const gdouble q = constants[1];
        const gdouble p0 = constants[2];
        const gdouble q0 = constants[3];
        gdouble h = constants[4]/size;
        gdouble mu0h = 0.00001/h/h;
        gdouble mu1h = 0.0001/h/h;
        gint ix = i*xres;
        gint ixp = ((i + 1) % yres)*xres;
        gint ixm = ((i + yres-1) % yres)*xres;
        gdouble cx0, cx1, c0lap, c1lap;
        gint j;

        cx0 = c0[ix];
        cx1 = c1[ix];
        c0lap = laplacian8(c0 + ixm, c0 + ix, c0 + ixp, xres-1, 0, 1);
        c1lap = laplacian8(c1 + ixm, c1 + ix, c1 + ixp, xres-1, 0, 1);
        r0[ix] = q0*funny_func(cx0) + q*cx1 + mu0h*c0lap;
        r1[ix] = p0*funny_func(cx1) + p*cx0 + mu1h*c1lap;
        cr0 += cx0*cx0;
        cr1 += cx1*cx1;

        for (j = 1; j < xres-1; j++) {
            cx0 = c0[ix + j];
            cx1 = c1[ix + j];
            c0lap = laplacian8(c0 + ixm, c0 + ix, c0 + ixp, j-1, j, j+1);
            c1lap = laplacian8(c1 + ixm, c1 + ix, c1 + ixp, j-1, j, j+1);
            r0[ix + j] = q0*funny_func(cx0) + q*cx1 + mu0h*c0lap;
            r1[ix + j] = p0*funny_func(cx1) + p*cx0 + mu1h*c1lap;
            cr0 += cx0*cx0;
            cr1 += cx1*cx1;
        }

        cx0 = c0[ix + xres-1];
        cx1 = c1[ix + xres-1];
        c0lap = laplacian8(c0 + ixm, c0 + ix, c0 + ixp, xres-2, xres-1, 0);
        c1lap = laplacian8(c1 + ixm, c1 + ix, c1 + ixp, xres-2, xres-1, 0);
        r0[ix + xres-1] = q0*funny_func(cx0) + q*cx1 + mu0h*c0lap;
        r1[ix + xres-1] = p0*funny_func(cx1) + p*cx0 + mu1h*c1lap;
        cr0 += cx0*cx0;
        cr1 += cx1*cx1;
    }

    rr0 = checker_smooth(xres, yres, domain + 2*n, domain + 4*n);
    rr0 = sqrt(cr0/rr0);
    rr1 = checker_smooth(xres, yres, domain + 3*n, domain + 4*n);
    rr1 = sqrt(cr1/rr1);
    realdt = 0.5*MIN(rr0, rr1);
    for (i = 0; i < 2*n; i++)
        domain[i] += realdt*domain[2*n + i];
}

static gboolean
cpde_turing_pattern(ModuleArgs *args, gdouble *domain, GTimer *timer, gdouble preview_time)
{
    GwyParams *params = args->params;
    gdouble size = gwy_params_get_double(params, PARAM_TURING_SIZE);
    gdouble chaos = gwy_params_get_double(params, PARAM_TURING_CHAOS);
    guint niters = gwy_params_get_int(params, PARAM_NITERS);
    GwyDataField *field = args->result;
    GwySynthUpdateType update;
    gint xres, yres;
    guint i;
    gdouble constants[5];

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    constants[0] = 1.12;
    constants[1] = -1.4;
    constants[2] = -1.10 - 0.9*chaos;
    constants[3] = 0.75 + 0.5*chaos;
    constants[4] = G_PI/(138.0 - 18.0*chaos);

    for (i = 0; i < niters; i++) {
        do_iter_turing(xres, yres, domain, constants, size);
        if (i % 20 == 0) {
            update = gwy_synth_update_progress(timer, preview_time, i, niters);
            if (update == GWY_SYNTH_UPDATE_CANCELLED)
                return FALSE;
            if (update == GWY_SYNTH_UPDATE_DO_PREVIEW) {
                copy_domain_to_data_field(args->result, domain, 0);
                gwy_data_field_data_changed(args->result);
            }
        }
    }

    return TRUE;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
