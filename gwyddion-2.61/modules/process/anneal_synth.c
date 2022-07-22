/*
 *  $Id: anneal_synth.c 23773 2021-05-24 20:06:40Z yeti-dn $
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
#include <libgwyddion/gwyrandgenset.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    CELL_STATUS_HAVE_RNUM    = (1 << 0),
    CELL_STATUS_TRY_SWAPPING = (1 << 1),
};

/* Cannot change this without losing reproducibility again! */
enum {
    NRANDOM_GENERATORS = 24
};

enum {
    PARAM_NITERS,
    PARAM_T_INIT,
    PARAM_T_FINAL,
    PARAM_FRACTION,
    PARAM_THREE_COMP,
    PARAM_B_FRACTION,
    PARAM_DELTAE_AB,
    PARAM_DELTAE_AC,
    PARAM_DELTAE_BC,
    PARAM_HEIGHT,
    PARAM_AVERAGE,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_ANIMATED,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

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

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             anneal_synth        (GwyContainer *data,
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
static void             init_field_randomly (GwyDataField *field,
                                             guint32 seed);
static void             sanitise_params     (ModuleArgs *args);
static gboolean         fix_deltaE          (gdouble *deltaE,
                                             gint victim);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates images by annealing a lattice gas model."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, anneal_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("anneal_synth",
                              (GwyProcessFunc)&anneal_synth,
                              N_("/S_ynthetic/_Anneal..."),
                              GWY_STOCK_SYNTHETIC_ANNEAL,
                              RUN_MODES,
                              0,
                              N_("Generate image by annealing a lattice gas"));

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
    gwy_param_def_add_int(paramdef, PARAM_NITERS, "niters", _("_Number of iterations"), 1, 1000000, 5000);
    gwy_param_def_add_double(paramdef, PARAM_T_INIT, "T_init", _("_Initial temperature"), 0.001, 2.0, 1.25);
    gwy_param_def_add_double(paramdef, PARAM_T_FINAL, "T_final", _("Final _temperature"), 0.001, 2.0, 0.7);
    gwy_param_def_add_double(paramdef, PARAM_FRACTION, "fraction", _("Component _fraction"), 0.0001, 0.9999, 0.5);
    gwy_param_def_add_boolean(paramdef, PARAM_THREE_COMP, "three_comp", _("Enable three components"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_B_FRACTION, "B_fraction", _("F_raction of B"), 0.0001, 0.9999, 1.0/3.0);
    gwy_param_def_add_double(paramdef, PARAM_DELTAE_AB, "deltaE_AB", _("Mixing energy AB"), 0.0, 1.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_DELTAE_AC, "deltaE_AC", _("Mixing energy AC"), 0.0, 1.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_DELTAE_BC, "deltaE_BC", _("Mixing energy BC"), 0.0, 1.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height"), 1e-4, 1000.0, 1.0);
    /* TRANSLATORS: Average is verb here, `perform averaging'. */
    gwy_param_def_add_int(paramdef, PARAM_AVERAGE, "average", _("_Average iterations"), 1, 10000, 1);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_ANIMATED, "animated", _("Progressive preview"), TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
anneal_synth(GwyContainer *data, GwyRunType runtype)
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
    sanitise_params(&args);
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

    gui.dialog = gwy_dialog_new(_("Anneal"));
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
    static const gint fractions[] = { PARAM_FRACTION, PARAM_B_FRACTION };
    GwyParamTable *table;
    guint i;

    table = gui->table_generator = gwy_param_table_new(gui->args->params);

    gwy_param_table_append_header(table, -1, _("Simulation Parameters"));
    gwy_param_table_append_slider(table, PARAM_NITERS);
    gwy_param_table_slider_set_mapping(table, PARAM_NITERS, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_T_INIT);
    gwy_param_table_append_slider(table, PARAM_T_FINAL);
    gwy_param_table_append_slider(table, PARAM_FRACTION);

    gwy_param_table_append_header(table, -1, _("Three Component Model"));
    gwy_param_table_append_checkbox(table, PARAM_THREE_COMP);
    gwy_param_table_append_slider(table, PARAM_B_FRACTION);
    gwy_param_table_append_slider(table, PARAM_DELTAE_AB);
    gwy_param_table_slider_set_mapping(table, PARAM_DELTAE_AB, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_DELTAE_AC);
    gwy_param_table_slider_set_mapping(table, PARAM_DELTAE_AC, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_DELTAE_BC);
    gwy_param_table_slider_set_mapping(table, PARAM_DELTAE_BC, GWY_SCALE_MAPPING_LINEAR);

    for (i = 0; i < G_N_ELEMENTS(fractions); i++) {
        gwy_param_table_slider_set_mapping(table, fractions[i], GWY_SCALE_MAPPING_LINEAR);
        gwy_param_table_slider_set_factor(table, fractions[i], 100.0);
        gwy_param_table_set_unitstr(table, fractions[i], "%");
    }

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_AVERAGE);

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
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table_generator;
    gdouble T, deltaE[3];
    gint i;

    if (gwy_synth_handle_param_changed(gui->table_dimensions, id))
        id = -1;

    if (id < 0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT) {
        static const gint zids[] = { PARAM_HEIGHT };

        gwy_synth_update_value_unitstrs(table, zids, G_N_ELEMENTS(zids));
        gwy_synth_update_like_current_button_sensitivity(table, BUTTON_LIKE_CURRENT_IMAGE);
    }

    if (id == PARAM_T_INIT) {
        T = gwy_params_get_double(params, PARAM_T_INIT);
        if (gwy_params_get_double(params, PARAM_T_FINAL) > T)
            gwy_param_table_set_double(table, PARAM_T_FINAL, T);
    }
    if (id == PARAM_T_FINAL) {
        T = gwy_params_get_double(params, PARAM_T_FINAL);
        if (gwy_params_get_double(params, PARAM_T_INIT) < T)
            gwy_param_table_set_double(table, PARAM_T_INIT, T);
    }
    if (id == PARAM_DELTAE_AB || id == PARAM_DELTAE_AC || id == PARAM_DELTAE_BC) {
        for (i = 0; i < 3; i++)
            deltaE[i] = gwy_params_get_double(params, PARAM_DELTAE_AB + i);
        if (fix_deltaE(deltaE, id - PARAM_DELTAE_AB)) {
            for (i = 0; i < 3; i++)
                gwy_param_table_set_double(table, PARAM_DELTAE_AB + i, deltaE[i]);
        }
    }
    if (id < 0 || id == PARAM_THREE_COMP) {
        gboolean three_comp = gwy_params_get_boolean(params, PARAM_THREE_COMP);

        gwy_param_table_set_sensitive(table, PARAM_B_FRACTION, three_comp);
        gwy_param_table_set_sensitive(table, PARAM_DELTAE_AB, three_comp);
        gwy_param_table_set_sensitive(table, PARAM_DELTAE_AC, three_comp);
        gwy_param_table_set_sensitive(table, PARAM_DELTAE_BC, three_comp);
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

/* We explicitly partition the image into NRANDOM_GENERATORS pieces, which are independent on the number of threads
 * and never changes.  Then each thread takes a subset of the pieces and generates deterministically random numbers
 * for each of them. */
static void
replenish_random_numbers(guint32 *random_numbers, guint *cell_status, gint n,
                         GwyRandGenSet *rngset)
{
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(random_numbers,cell_status,n,rngset)
#endif
    {
        guint irfrom = gwy_omp_chunk_start(NRANDOM_GENERATORS);
        guint irto = gwy_omp_chunk_end(NRANDOM_GENERATORS);
        guint ir, ifrom, ito, i, have_rbits, cs;
        guint32 rbits;
        GRand *rng;

        for (ir = irfrom; ir < irto; ir++) {
            rng = gwy_rand_gen_set_rng(rngset, ir);
            ifrom = ir*n/NRANDOM_GENERATORS;
            ito = (ir + 1)*n/NRANDOM_GENERATORS;
            have_rbits = 0;
            for (i = ifrom; i < ito; i++) {
                cs = cell_status[i];
                if (!(cs & CELL_STATUS_HAVE_RNUM)) {
                    random_numbers[i] = g_rand_int(rng);
                    cs |= CELL_STATUS_HAVE_RNUM;
                }
                if (!have_rbits) {
                    rbits = g_rand_int(rng);
                    have_rbits = 32;
                }
                /* Probability of choosing a cell is 1/4. */
                if (rbits & 0x3)
                    cs &= ~CELL_STATUS_TRY_SWAPPING;
                else
                    cs |= CELL_STATUS_TRY_SWAPPING;
                rbits >>= 2;
                have_rbits -= 2;
                cell_status[i] = cs;
            }
        }
    }
}

static void
update_exp_table2(gdouble delta, guint32 *exp_table, guint n)
{
    gint diff;

    exp_table[0] = G_MAXUINT32;
    for (diff = 1; diff < n; diff++)
        exp_table[diff] = (guint)floor(G_MAXUINT32*exp(-delta*diff) + 0.1);
}

/* The table hold probabilities for the case when first cell < second cell,
 * i.e. combinations AB, AC and BC.  The reverse combinations must be
 * obtained by inverting the differences. */
static void
update_exp_table3(const gdouble *deltaE, gdouble invT, guint32 *exp_table)
{
    /* dAB is is the change of energy when swapping A and B between mixed and
     * separated. */
    gdouble dAB = deltaE[0], dAC = deltaE[1], dBC = deltaE[2];
    gint dnA, dnB;

    /* Calculate swap probabilities for all combinations of changes dnA and dnB of neighbours counts of the two
     * swapped cells (change dnC is given by the other two because the number of neighbours is fixed). */

    /* Cells to swap are A and B. */
    for (dnA = -3; dnA <= 3; dnA++) {
        for (dnB = -3; dnB <= 3; dnB++) {
            gdouble dE = dnA*(dBC - dAC - dAB) + dnB*(dBC - dAC + dAB) + 2*dAC;
            guint i = (dnA + 3)*7 + (dnB + 3);

            /* Always swap when the energy change is negative; otherwise calculate probability. */
            if (dE <= 1e-9)
                exp_table[i] = G_MAXUINT32;
            else
                exp_table[i] = (guint)floor(G_MAXUINT32*exp(-dE*invT) + 0.1);
        }
    }

    /* Cells to swap are A and C. */
    exp_table += 7*7;
    for (dnA = -3; dnA <= 3; dnA++) {
        for (dnB = -3; dnB <= 3; dnB++) {
            gdouble dE = 2*dAC*(1 - dnA) + dnB*(dAB - dAC - dBC);
            guint i = (dnA + 3)*7 + (dnB + 3);

            /* Always swap when the energy change is negative; otherwise calculate probability. */
            if (dE <= 1e-9)
                exp_table[i] = G_MAXUINT32;
            else
                exp_table[i] = (guint)floor(G_MAXUINT32*exp(-dE*invT) + 0.1);
        }
    }

    /* Cells to swap are B and C. */
    exp_table += 7*7;
    for (dnA = -3; dnA <= 3; dnA++) {
        for (dnB = -3; dnB <= 3; dnB++) {
            gdouble dE = 2*dBC*(1 - dnB) + dnA*(dAB - dAC - dBC);
            guint i = (dnA + 3)*7 + (dnB + 3);

            /* Always swap when the energy change is negative; otherwise calculate probability. */
            if (dE <= 1e-9)
                exp_table[i] = G_MAXUINT32;
            else
                exp_table[i] = (guint)floor(G_MAXUINT32*exp(-dE*invT) + 0.1);
        }
    }
}

static inline guint
count_neighbours(const guint *domain, guint xres, guint yres, guint i, guint j)
{
    guint idx = i*xres + j;
    guint idx_back = i ? idx - xres : idx + xres*(yres - 1);
    guint idx_forw = i < yres-1 ? idx + xres : idx - xres*(yres - 1);
    guint idx_left = j ? idx - 1 : idx + xres-1;
    guint idx_right = j < xres-1 ? idx + 1 : idx - (xres-1);

    return domain[idx_back] + domain[idx_left] + domain[idx_right] + domain[idx_forw];
}

/* Return TRUE if the random value was consumed. */
static guint
maybe_swap2(guint *domain, guint xres, guint yres, guint i1, guint j1, gboolean vertical,
            const guint32 *exp_table, guint32 random_value)
{
    guint i2 = i1, j2 = j1, idx1 = i1*xres + j1, idx2;
    guint a1, a2, neigh1, neigh2;
    gint Ef_before, Ef_after;

    if (vertical)
        i2 = i2 < yres-1 ? i2+1 : 0;
    else
        j2 = j1 < xres-1 ? j2+1 : 0;

    idx2 = i2*xres + j2;

    /* When the cells are the same swapping is no-op.  Quit early. */
    a1 = domain[idx1];
    a2 = domain[idx2];
    if (a1 == a2)
        return FALSE;

    neigh1 = count_neighbours(domain, xres, yres, i1, j1);
    neigh2 = count_neighbours(domain, xres, yres, i2, j2);

    /* Formation energy before. */
    Ef_before = (a1 ? 4 - neigh1 : neigh1) + (a2 ? 4 - neigh2 : neigh2);

    /* Formation energy after.  The cells are different so counting in the other cell's position we count the cell
     * itself as the same, but in fact the cells will be swapped so the cell in the neighbour's position will be
     * always different.  We have to add 1 to each term. */
    Ef_after = (a2 ? 4 - neigh1 : neigh1) + (a1 ? 4 - neigh2 : neigh2) + 2;

    /* There are many Markov processes that have produce the same required stationary probabilities 1/(1 + exp(-Δ))
     * and 1/(1 + exp(Δ)) corresponding to thermal equlibrium.  Of them we want the one that changes states most
     * frequently (assuming it means fastest convergence). This is the same as used in simulated annealing: we always
     * switch from higher energy to lower, and we switch from lower energy to higher with probability exp(-Δ). */
    if (Ef_after < Ef_before) {
        domain[idx1] = a2;
        domain[idx2] = a1;
        return FALSE;
    }

    if (random_value > exp_table[Ef_after - Ef_before])
        return TRUE;

    domain[idx1] = a2;
    domain[idx2] = a1;
    return TRUE;
}

static inline void
count_neighbours3(const guint *domain, guint xres, guint yres, guint i, guint j, gint *nA, gint *nB)
{
    guint idx = i*xres + j;
    guint idx_back = i ? idx - xres : idx + xres*(yres - 1);
    guint idx_forw = i < yres-1 ? idx + xres : idx - xres*(yres - 1);
    guint idx_left = j ? idx - 1 : idx + xres-1;
    guint idx_right = j < xres-1 ? idx + 1 : idx - (xres-1);

    *nA = ((domain[idx_back] == 0) + (domain[idx_left] == 0) + (domain[idx_right] == 0) + (domain[idx_forw] == 0));
    *nB = ((domain[idx_back] == 1) + (domain[idx_left] == 1) + (domain[idx_right] == 1) + (domain[idx_forw] == 1));
}

/* Return TRUE if the random value was consumed. */
static gboolean
maybe_swap3(guint *domain, guint xres, guint yres, guint i1, guint j1, gboolean vertical,
            const guint32 *exp_table, guint32 random_value)
{
    guint i2 = i1, j2 = j1, idx1 = i1*xres + j1, idx2, a1, a2, p, tidx;
    gint nA1, nB1, nA2, nB2, dnA, dnB;

    if (vertical)
        i2 = i2 < yres-1 ? i2+1 : 0;
    else
        j2 = j1 < xres-1 ? j2+1 : 0;

    idx2 = i2*xres + j2;

    /* When the cells are the same swapping is no-op.  Quit early. */
    a1 = domain[idx1];
    a2 = domain[idx2];
    if (a1 == a2)
        return FALSE;

    /* For correct exp_table() utilisation we need a1 < a2. */
    if (a1 > a2) {
        GWY_SWAP(guint, a1, a2);
        GWY_SWAP(guint, i1, i2);
        GWY_SWAP(guint, j1, j2);
        GWY_SWAP(guint, idx1, idx2);
    }

    count_neighbours3(domain, xres, yres, i1, j1, &nA1, &nB1);
    count_neighbours3(domain, xres, yres, i2, j2, &nA2, &nB2);
    dnA = nA2 - nA1;
    dnB = nB2 - nB1;

    if (dnA == 0 && dnB == 0)
        return FALSE;

    /* Choose the right probability table for the cell combination.  It is the probability of swapping, when
     * random_number > p corresponds we do NOT swap the cells. */
    tidx = (dnA + 3)*7 + (dnB + 3);
    /* a1 + a2 is 1 for AB, 2 for AC, 3 for BC. */
    p = exp_table[tidx + 7*7*(a1 + a2 - 1)];

    /* Do not consume the random number when p = 1. */
    if (p == G_MAXUINT32) {
        domain[idx1] = a2;
        domain[idx2] = a1;
        return FALSE;
    }

    if (random_value > p)
        return TRUE;

    domain[idx1] = a2;
    domain[idx2] = a1;
    return TRUE;
}

static void
process_sublattice_vertical2(guint *domain, guint xres, guint yres, guint sublattice,
                             guint32 *exp_table, const guint32 *random_numbers,
                             guint *cell_status)
{
    guint i;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(domain,random_numbers,cell_status,exp_table,sublattice,xres,yres) \
            private(i)
#endif
    for (i = 0; i < yres/2; i++) {
        guint hoff = (i + sublattice) % 2, voff = (sublattice % 4)/2;
        guint j, k = (xres/2)*i;

        for (j = 0; j < xres/2; j++, k++) {
            if ((cell_status[k] & CELL_STATUS_TRY_SWAPPING)
                && maybe_swap2(domain, xres, yres, 2*i + voff, 2*j + hoff, TRUE, exp_table, random_numbers[k]))
                cell_status[k] &= ~CELL_STATUS_HAVE_RNUM;
        }
    }
}

static void
process_sublattice_horizontal2(guint *domain, guint xres, guint yres, guint sublattice,
                               guint32 *exp_table, const guint32 *random_numbers,
                               guint *cell_status)
{
    guint i;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(domain,random_numbers,cell_status,exp_table,sublattice,xres,yres) \
            private(i)
#endif
    for (i = 0; i < yres/2; i++) {
        guint hoff = (sublattice % 4)/2;
        guint j, k = (xres/2)*i;

        for (j = 0; j < xres/2; j++, k++) {
            guint voff = (j + sublattice) % 2;

            if ((cell_status[k] & CELL_STATUS_TRY_SWAPPING)
                && maybe_swap2(domain, xres, yres, 2*i + voff, 2*j + hoff, FALSE, exp_table, random_numbers[k]))
                cell_status[k] &= ~CELL_STATUS_HAVE_RNUM;
        }
    }
}

static void
process_sublattice_vertical3(guint *domain, guint xres, guint yres,
                             guint sublattice, guint32 *exp_table,
                             const guint32 *random_numbers,
                             guint *cell_status)
{
    guint i;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(domain,random_numbers,cell_status,exp_table,sublattice,xres,yres) \
            private(i)
#endif
    for (i = 0; i < yres/2; i++) {
        guint hoff = (i + sublattice) % 2, voff = (sublattice % 4)/2;
        guint j, k = (xres/2)*i;

        for (j = 0; j < xres/2; j++, k++) {
            if ((cell_status[k] & CELL_STATUS_TRY_SWAPPING)
                && maybe_swap3(domain, xres, yres, 2*i + voff, 2*j + hoff, TRUE, exp_table, random_numbers[k]))
                cell_status[k] &= ~CELL_STATUS_HAVE_RNUM;
        }
    }
}

static void
process_sublattice_horizontal3(guint *domain, guint xres, guint yres,
                               guint sublattice, guint32 *exp_table,
                               const guint32 *random_numbers,
                               guint *cell_status)
{
    guint i;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(domain,random_numbers,cell_status,exp_table,sublattice,xres,yres) \
            private(i)
#endif
    for (i = 0; i < yres/2; i++) {
        guint hoff = (sublattice % 4)/2;
        guint j, k = (xres/2)*i;

        for (j = 0; j < xres/2; j++, k++) {
            guint voff = (j + sublattice) % 2;

            if ((cell_status[k] & CELL_STATUS_TRY_SWAPPING)
                && maybe_swap3(domain, xres, yres, 2*i + voff, 2*j + hoff, FALSE, exp_table, random_numbers[k]))
                cell_status[k] &= ~CELL_STATUS_HAVE_RNUM;
        }
    }
}

/* XXX: This requires even-sized domain in both x and y. */
static void
process_sublattice(guint *domain, guint xres, guint yres, gboolean three_comp,
                   guint sublattice, guint32 *exp_table,
                   const guint32 *random_numbers, guint *cell_status)
{
    if (three_comp) {
        if (sublattice < 4)
            process_sublattice_vertical3(domain, xres, yres, sublattice, exp_table, random_numbers, cell_status);
        else
            process_sublattice_horizontal3(domain, xres, yres, sublattice, exp_table, random_numbers, cell_status);
    }
    else {
        if (sublattice < 4)
            process_sublattice_vertical2(domain, xres, yres, sublattice, exp_table, random_numbers, cell_status);
        else
            process_sublattice_horizontal2(domain, xres, yres, sublattice, exp_table, random_numbers, cell_status);
    }
}

static void
init_domain2_from_data_field(GwyDataField *field, guint *domain, ModuleArgs *args)
{
    gdouble fraction = gwy_params_get_double(args->params, PARAM_FRACTION);
    guint xres = gwy_data_field_get_xres(field);
    guint yres = gwy_data_field_get_yres(field);
    gdouble *d = gwy_data_field_get_data(field);
    gdouble pvalue, threshold;
    guint xres2, yres2, i, j;
    gdouble *tmp;

    xres2 = (xres + 1)/2*2;
    yres2 = (yres + 1)/2*2;

    tmp = g_memdup(d, xres*yres*sizeof(gdouble));
    pvalue = 100.0*(1.0 - fraction);
    gwy_math_percentiles(xres*yres, tmp, GWY_PERCENTILE_INTERPOLATION_MIDPOINT, 1, &pvalue, &threshold);
    g_free(tmp);

    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++)
            domain[i*xres2 + j] = (d[i*xres + j] >= threshold);
    }

    if (xres < xres2) {
        for (i = 0; i < yres; i++)
            domain[i*xres2 + xres2-1] = domain[i*xres2 + (i % 2 ? 0 : xres-1)];
    }
    if (yres < yres2) {
        for (j = 0; j < xres; j++)
            domain[yres*xres2 + j] = domain[j + (i % 2 ? 0 : yres-1)*xres];
    }
    if (xres < xres2 && yres < yres2)
        domain[xres2*yres2 - 1] = domain[0];
}

static inline guint
average3(guint a1, guint a2)
{
    if (a1 == a2)
        return a1;
    if (a1 == 1)
        return a2;
    if (a2 == 1)
        return a1;
    return 1;
}

static void
init_domain3_from_data_field(GwyDataField *field, guint *domain, ModuleArgs *args)
{
    gdouble fraction = gwy_params_get_double(args->params, PARAM_FRACTION);
    gdouble B_fraction = gwy_params_get_double(args->params, PARAM_B_FRACTION);
    guint xres = gwy_data_field_get_xres(field);
    guint yres = gwy_data_field_get_yres(field);
    gdouble *d = gwy_data_field_get_data(field);
    gdouble pvalues[2], thresholds[2];
    guint xres2, yres2, i, j;
    gdouble *tmp;

    xres2 = (xres + 1)/2*2;
    yres2 = (yres + 1)/2*2;

    tmp = g_memdup(d, xres*yres*sizeof(gdouble));
    pvalues[0] = 100.0*(1.0 - fraction)*(1.0 - B_fraction);
    pvalues[1] = pvalues[0] + 100.0*B_fraction;
    gwy_math_percentiles(xres*yres, tmp, GWY_PERCENTILE_INTERPOLATION_MIDPOINT, 2, pvalues, thresholds);
    g_free(tmp);

    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++)
            domain[i*xres2 + j] = (d[i*xres + j] < thresholds[0] ? 0 : (d[i*xres + j] >= thresholds[1] ? 2 : 1));
    }

    if (xres < xres2) {
        for (i = 0; i < yres; i++)
            domain[i*xres2 + xres2-1] = average3(domain[i*xres2], domain[i*xres2 + xres-1]);
    }
    if (yres < yres2) {
        for (j = 0; j < xres; j++)
            domain[yres*xres2 + j] = average3(domain[j], domain[j + (yres-1)*xres2]);
    }
    if (xres < xres2 && yres < yres2)
        domain[xres2*yres2 - 1] = average3(domain[0], domain[xres2*(yres-1) + xres-1]);
}

static void
domain_add_to_data_field(const guint *u, GwyDataField *field)
{
    guint xres = gwy_data_field_get_xres(field);
    guint yres = gwy_data_field_get_yres(field);
    gdouble *d = gwy_data_field_get_data(field);
    guint xres2, i, j;

    xres2 = (xres + 1)/2*2;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++)
            d[i*xres + j] += u[i*xres2 + j];
    }
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    /* Multiply niters by 4 since the probability of choosing a particular cell is 1/4 in each iteration.  But if the
     * user wants no averaging, then really just use values from one iteration. */
    gint niters = 4*gwy_params_get_int(params, PARAM_NITERS);
    gint average = gwy_params_get_int(params, PARAM_AVERAGE);
    gboolean three_comp = gwy_params_get_boolean(params, PARAM_THREE_COMP);
    gdouble T_init = gwy_params_get_double(params, PARAM_T_INIT);
    gdouble T_final = gwy_params_get_double(params, PARAM_T_FINAL);
    gboolean animated = gwy_params_get_boolean(params, PARAM_ANIMATED);
    guint32 seed = gwy_params_get_int(params, PARAM_SEED);
    GwyDataField *result = args->result;
    guint32 *random_numbers = NULL, *exp_table = NULL;
    guint *cell_status = NULL;
    guint *domain = NULL;
    guint xres, yres, xres2, yres2, l;
    guint navg = MIN(4*average - 3, niters);
    gint power10z;
    gdouble deltaE[3];
    gdouble A = T_init, B = (A/T_final - 1.0)/niters;
    gdouble preview_time = (animated ? 1.25 : 0.0);
    GwySynthUpdateType update;
    GwyRandGenSet *rngset;
    gboolean finished = FALSE;
    guint lattices[8];
    gulong i;
    GTimer *timer;
    GRand *rng;

    for (l = 0; l < 3; l++)
        deltaE[l] = gwy_params_get_double(params, PARAM_DELTAE_AB + l);

    gwy_app_wait_start(wait_window, _("Initializing..."));

    rngset = gwy_rand_gen_set_new(NRANDOM_GENERATORS);
    gwy_rand_gen_set_init(rngset, seed);

    if (args->field && do_initialise)
        gwy_data_field_copy(args->field, result, FALSE);
    else
        init_field_randomly(result, seed);

    xres = gwy_data_field_get_xres(result);
    yres = gwy_data_field_get_yres(result);
    /* Create simulation domain with even dimensions. */
    xres2 = (xres + 1)/2*2;
    yres2 = (yres + 1)/2*2;

    timer = g_timer_new();
    gwy_synth_update_progress(NULL, 0, 0, 0);
    if (!gwy_app_wait_set_message(_("Running computation...")))
        goto end;

    for (l = 0; l < 8; l++)
        lattices[l] = l;

    domain = g_new(guint, xres2*yres2);
    if (three_comp) {
        init_domain3_from_data_field(result, domain, args);
        exp_table = g_new(guint, 3*7*7);
    }
    else {
        init_domain2_from_data_field(result, domain, args);
        exp_table = g_new(guint, 2*4 + 1);
    }

    random_numbers = g_new(guint32, 2*xres2*yres2);
    cell_status = g_new0(guint, 2*xres2*yres2);
    rng = gwy_rand_gen_set_rng(rngset, 0);

    for (i = 0; i < niters; i++) {
        gdouble T = A/(1.0 + i*B);

        if (three_comp)
            update_exp_table3(deltaE, 1.0/T, exp_table);
        else
            update_exp_table2(1.0/T, exp_table, 2*4 + 1);

        replenish_random_numbers(random_numbers, cell_status, 2*xres2*yres2, rngset);

        /* Split the 2*n edges into 8 subsets, where in each the edges are far enough from neighbours for updates to
         * be independent.   This means tiling the plane with shapes like
         *
         *    [a]
         * [a][A][a]
         * [b][B][b]
         *    [b]
         *
         * where the edge goes between A and B (to be potentially swapped); a and b are their neighbours which enter
         * the energy consideration.
         *
         * The we can parallelise freely updates in one of the subset.  Always run the update on the
         * entire domain, but choose the order of sublattice processing randomly. */
        for (l = 0; l < 8; l++) {
            guint ll = (l == 7 ? 7 : g_rand_int_range(rng, l, 8));
            guint roff = xres2*yres2/4 * l;

            GWY_SWAP(guint, lattices[l], lattices[ll]);
            ll = lattices[l];
            process_sublattice(domain, xres2, yres2, three_comp, ll, exp_table,
                               random_numbers + roff, cell_status + roff);
        }

        if (niters - i <= navg) {
            if (niters - i == navg)
                gwy_data_field_clear(result);
            domain_add_to_data_field(domain, result);
        }

        if (i % 100 == 0) {
            update = gwy_synth_update_progress(timer, preview_time, i, niters);
            if (update == GWY_SYNTH_UPDATE_CANCELLED)
                goto end;
            if (update == GWY_SYNTH_UPDATE_DO_PREVIEW) {
                /* When we are already averaging, just display what we have accumulated so far. */
                if (niters - i > navg) {
                    gwy_data_field_clear(result);
                    domain_add_to_data_field(domain, result);
                }
                gwy_data_field_invalidate(result);
                gwy_data_field_data_changed(result);
            }
        }
    }

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    gwy_data_field_renormalize(result, pow10(power10z)*height, 0.0);
    gwy_data_field_invalidate(result);
    finished = TRUE;

end:
    gwy_app_wait_finish();
    gwy_rand_gen_set_free(rngset);
    g_timer_destroy(timer);
    g_free(exp_table);
    g_free(random_numbers);
    g_free(cell_status);
    g_free(domain);

    return finished;
}

static void
init_field_randomly(GwyDataField *field, guint32 seed)
{
    gdouble *d = gwy_data_field_get_data(field);
    gint xres = gwy_data_field_get_xres(field);
    gint yres = gwy_data_field_get_yres(field);
    GRand *rng = g_rand_new();
    gint n = xres*yres, k;

    g_rand_set_seed(rng, seed);
    for (k = 0; k < n; k++)
        d[k] = g_rand_double(rng);

    g_rand_free(rng);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble T_init, T_final, deltaE[3];
    gint i;

    T_init = gwy_params_get_double(params, PARAM_T_INIT);
    T_final = gwy_params_get_double(params, PARAM_T_FINAL);
    if (T_init < T_final) {
        gwy_params_set_double(params, PARAM_T_INIT, 0.5*(T_init + T_final));
        gwy_params_set_double(params, PARAM_T_FINAL, 0.5*(T_init + T_final));
    }

    for (i = 0; i < 3; i++)
        deltaE[i] = gwy_params_get_double(params, PARAM_DELTAE_AB + i);
    if (fix_deltaE(deltaE, 0)) {
        for (i = 0; i < 3; i++)
            gwy_params_set_double(params, PARAM_DELTAE_AB + i, deltaE[i]);
    }
}

/* Ensure the maximum of the three deltaE values is always 1.0. */
static gboolean
fix_deltaE(gdouble *deltaE, gint victim)
{
    gdouble s;

    s = fmax(fmax(deltaE[0], deltaE[1]), deltaE[2]);
    if (s == 1.0)
        return FALSE;

    if (!(s > 0.0)) {
        deltaE[victim] = 1.0;
        return TRUE;
    }

    deltaE[0] /= s;
    deltaE[1] /= s;
    deltaE[2] /= s;

    return TRUE;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
