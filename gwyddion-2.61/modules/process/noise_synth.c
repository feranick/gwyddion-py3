/*
 *  $Id: noise_synth.c 23788 2021-05-26 12:14:14Z yeti-dn $
 *  Copyright (C) 2010-2021 David Necas (Yeti).
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
#include <libgwyddion/gwyrandgenset.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define DECLARE_NOISE(name) \
    static gdouble noise_##name##_both(GwyRandGenSet *rng, gdouble sigma); \
    static gdouble noise_##name##_up(GwyRandGenSet *rng, gdouble sigma); \
    static gdouble noise_##name##_down(GwyRandGenSet *rng, gdouble sigma);

#define NOISE_FUNCS(name) \
    { noise_##name##_both, noise_##name##_up, noise_##name##_down }

enum {
    PARAM_DISTRIBUTION,
    PARAM_DIRECTION,
    PARAM_SIGMA,
    PARAM_DENSITY,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

    PARAM_DIMS0
};

typedef enum {
    RNG_NOISE   = 0,
    RNG_DENSITY = 1,
    RNG_NRNGS
} NoiseSynthRng;

typedef enum {
    NOISE_DISTRIBUTION_GAUSSIAN    = 0,
    NOISE_DISTRIBUTION_EXPONENTIAL = 1,
    NOISE_DISTRIBUTION_UNIFORM     = 2,
    NOISE_DISTRIBUTION_TRIANGULAR  = 3,
    NOISE_DISTRIBUTION_SALT_PEPPER = 4,
} NoiseDistributionType;

typedef enum {
    NOISE_DIRECTION_BOTH = 0,
    NOISE_DIRECTION_UP   = 1,
    NOISE_DIRECTION_DOWN = 2,
    NOISE_DIRECTION_NTYPES
} NoiseDirectionType;

typedef gdouble (*PointNoiseFunc)(GwyRandGenSet *rng, gdouble sigma);

typedef struct {
    const gchar *name;
    PointNoiseFunc point_noise[NOISE_DIRECTION_NTYPES];
} NoiseSynthGenerator;

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

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             noise_synth         (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
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

DECLARE_NOISE(gaussian);
DECLARE_NOISE(exp);
DECLARE_NOISE(uniform);
DECLARE_NOISE(triangle);
DECLARE_NOISE(saltpepper);

/* NB: The order of these and everything else (like table_noise[]) must match the enums.  See obj_synth.c for how
 * to reorder it in the GUI. */
static const NoiseSynthGenerator generators[] = {
    { N_("distribution|Gaussian"),        NOISE_FUNCS(gaussian),   },
    { N_("distribution|Exponential"),     NOISE_FUNCS(exp),        },
    { N_("distribution|Uniform"),         NOISE_FUNCS(uniform),    },
    { N_("distribution|Triangular"),      NOISE_FUNCS(triangle),   },
    { N_("distribution|Salt and pepper"), NOISE_FUNCS(saltpepper), },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates uncorrelated random noise."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2010",
};

GWY_MODULE_QUERY2(module_info, noise_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("noise_synth",
                              (GwyProcessFunc)&noise_synth,
                              N_("/S_ynthetic/_Noise..."),
                              GWY_STOCK_SYNTHETIC_NOISE,
                              RUN_MODES,
                              0,
                              N_("Generate surface of uncorrelated noise"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum directions[] = {
        { N_("S_ymmetrical"),        NOISE_DIRECTION_BOTH, },
        { N_("One-sided _positive"), NOISE_DIRECTION_UP,   },
        { N_("One-sided _negative"), NOISE_DIRECTION_DOWN, },
    };
    static GwyParamDef *paramdef = NULL;
    static GwyEnum *distributions = NULL;

    if (paramdef)
        return paramdef;

    distributions = gwy_enum_fill_from_struct(NULL, G_N_ELEMENTS(generators), generators, sizeof(NoiseSynthGenerator),
                                              G_STRUCT_OFFSET(NoiseSynthGenerator, name), -1);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISTRIBUTION, "distribution", _("_Distribution"),
                              distributions, G_N_ELEMENTS(generators), NOISE_DISTRIBUTION_GAUSSIAN);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DIRECTION, "direction", _("_Noise sign"),
                              directions, G_N_ELEMENTS(directions), NOISE_DIRECTION_BOTH);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_RMS"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_double(paramdef, PARAM_DENSITY, "density", _("Densi_ty"), 1e-6, 1.0, 1.0);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
noise_synth(GwyContainer *data, GwyRunType runtype)
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
    execute(&args);
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

    gui.dialog = gwy_dialog_new(_("Random Noise"));
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
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

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

    gwy_param_table_append_header(table, -1, _("Distribution"));
    gwy_param_table_append_combo(table, PARAM_DISTRIBUTION);
    gwy_param_table_append_combo(table, PARAM_DIRECTION);
    gwy_param_table_append_slider(table, PARAM_DENSITY);
    gwy_param_table_slider_set_mapping(table, PARAM_DENSITY, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_slider_set_mapping(table, PARAM_SIGMA, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_seed(table, PARAM_SEED);
    gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

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
        static const gint zids[] = { PARAM_SIGMA };

        gwy_synth_update_value_unitstrs(table, zids, G_N_ELEMENTS(zids));
        gwy_synth_update_like_current_button_sensitivity(table, BUTTON_LIKE_CURRENT_IMAGE);
    }

    if ((id < PARAM_DIMS0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE)
        && id != PARAM_UPDATE && id != PARAM_RANDOMIZE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
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
            gwy_param_table_set_double(gui->table_generator, PARAM_SIGMA, zscale/pow10(power10z));
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

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    NoiseDistributionType distribution = gwy_params_get_enum(params, PARAM_DISTRIBUTION);
    NoiseDirectionType direction = gwy_params_get_enum(params, PARAM_DIRECTION);
    gdouble sigma = gwy_params_get_double(params, PARAM_SIGMA);
    gdouble density = gwy_params_get_double(params, PARAM_DENSITY);
    GwyDataField *field = args->field, *result = args->result;
    PointNoiseFunc point_noise = generators[distribution].point_noise[direction];
    GwyRandGenSet *rngset;
    gdouble *data;
    gint n, i, power10z;
    gboolean noise_everywhere = (density >= 1.0);

    rngset = gwy_rand_gen_set_new(RNG_NRNGS);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    if (field && do_initialise)
        gwy_data_field_copy(field, result, FALSE);
    else
        gwy_data_field_clear(result);

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    sigma *= pow10(power10z);

    n = gwy_data_field_get_xres(result) * gwy_data_field_get_yres(result);
    data = gwy_data_field_get_data(result);

    for (i = 0; i < n; i++) {
        gdouble r = point_noise(rngset, sigma);

        if (noise_everywhere || gwy_rand_gen_set_double(rngset, RNG_DENSITY) <= density)
            data[i] += r;
    }

    gwy_rand_gen_set_free(rngset);
}

/* XXX: Sometimes the generators seem unnecessarily complicated; this is to make the positive and negative noise
 * related to the symmetrical one. */
static gdouble
noise_gaussian_both(GwyRandGenSet *rng, gdouble sigma)
{
    return gwy_rand_gen_set_gaussian(rng, RNG_NOISE, sigma);
}

static gdouble
noise_gaussian_up(GwyRandGenSet *rng, gdouble sigma)
{
    return fabs(gwy_rand_gen_set_gaussian(rng, RNG_NOISE, sigma));
}

static gdouble
noise_gaussian_down(GwyRandGenSet *rng, gdouble sigma)
{
    return -fabs(gwy_rand_gen_set_gaussian(rng, RNG_NOISE, sigma));
}

static gdouble
noise_exp_both(GwyRandGenSet *rng, gdouble sigma)
{
    return gwy_rand_gen_set_exponential(rng, RNG_NOISE, sigma);
}

static gdouble
noise_exp_up(GwyRandGenSet *rng, gdouble sigma)
{
    return fabs(gwy_rand_gen_set_exponential(rng, RNG_NOISE, sigma));
}

static gdouble
noise_exp_down(GwyRandGenSet *rng, gdouble sigma)
{
    return -fabs(gwy_rand_gen_set_exponential(rng, RNG_NOISE, sigma));
}

static gdouble
noise_uniform_both(GwyRandGenSet *rng, gdouble sigma)
{
    return gwy_rand_gen_set_uniform(rng, RNG_NOISE, sigma);
}

static gdouble
noise_uniform_up(GwyRandGenSet *rng, gdouble sigma)
{
    return fabs(gwy_rand_gen_set_uniform(rng, RNG_NOISE, sigma));
}

static gdouble
noise_uniform_down(GwyRandGenSet *rng, gdouble sigma)
{
    return -fabs(gwy_rand_gen_set_uniform(rng, RNG_NOISE, sigma));
}

static gdouble
noise_triangle_both(GwyRandGenSet *rng, gdouble sigma)
{
    return gwy_rand_gen_set_triangular(rng, RNG_NOISE, sigma);
}

static gdouble
noise_triangle_up(GwyRandGenSet *rng, gdouble sigma)
{
    return fabs(gwy_rand_gen_set_triangular(rng, RNG_NOISE, sigma));
}

static gdouble
noise_triangle_down(GwyRandGenSet *rng, gdouble sigma)
{
    return -fabs(gwy_rand_gen_set_triangular(rng, RNG_NOISE, sigma));
}

static gdouble
noise_saltpepper_both(GwyRandGenSet *rng, gdouble sigma)
{
    return sigma*((gint)(gwy_rand_gen_set_int(rng, RNG_NOISE) & 1)*2 - 1);
}

static gdouble
noise_saltpepper_up(G_GNUC_UNUSED GwyRandGenSet *rng, gdouble sigma)
{
    return sigma;
}

static gdouble
noise_saltpepper_down(G_GNUC_UNUSED GwyRandGenSet *rng, gdouble sigma)
{
    return -sigma;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
