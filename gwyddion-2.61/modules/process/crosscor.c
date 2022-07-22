/*
 *  $Id: crosscor.c 24416 2021-10-24 07:25:04Z yeti-dn $
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
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocess.h>
#include <libprocess/arithmetic.h>
#include <libprocess/correlation.h>
#include <libprocess/filters.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_INTERACTIVE

enum {
    RESPONSE_GUESS_OFFSET = 1000,
};

typedef enum {
    OUTPUT_ABS       = 0,
    OUTPUT_X         = 1,
    OUTPUT_Y         = 2,
    OUTPUT_DIR       = 3,
    OUTPUT_SCORE     = 4,
    OUTPUT_CORRECTED = 5,
    OUTPUT_NTYPES
} CrosscorResult;

enum {
    PARAM_OTHER_IMAGE,
    PARAM_SEARCH_X,
    PARAM_SEARCH_Y,
    PARAM_SEARCH_XOFFSET,
    PARAM_SEARCH_YOFFSET,
    PARAM_WINDOW_X,
    PARAM_WINDOW_Y,
    PARAM_WINDOW,
    PARAM_OUTPUT,
    PARAM_ADD_LS_MASK,
    PARAM_THRESHOLD,
    PARAM_GAUSSIAN_WIDTH,
    PARAM_GAUSSIAN,
    PARAM_EXTEND,
    PARAM_MULTIPLE,
    PARAM_SECOND_SOURCE,
    PARAM_SECOND_OTHER,
    BUTTON_GUESS_OFFSET,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result[OUTPUT_NTYPES];
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_correlation;
    GwyParamTable *table_output;
    GwyParamTable *table_multichannel;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             crosscor            (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *wait_window);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             dialog_response     (ModuleGUI *gui,
                                             gint response);
static GtkWidget*       correlation_tab_new (ModuleGUI *gui);
static GtkWidget*       output_tab_new      (ModuleGUI *gui);
static GtkWidget*       multichannel_tab_new(ModuleGUI *gui);
static gboolean         other_image_filter  (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);
static gboolean         weaker_image_filter (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);
static void             guess_offsets       (ModuleGUI *gui);

static const GwyEnum outputs[] = {
    { N_("Absolute difference"),    (1 << OUTPUT_ABS),       },
    { N_("X difference"),           (1 << OUTPUT_X),         },
    { N_("Y difference"),           (1 << OUTPUT_Y),         },
    { N_("Direction"),              (1 << OUTPUT_DIR),       },
    { N_("Score"),                  (1 << OUTPUT_SCORE),     },
    { N_("Corrected second image"), (1 << OUTPUT_CORRECTED), },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates cross-correlation of two data fields."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, crosscor)

static gboolean
module_register(void)
{
    gwy_process_func_register("crosscor",
                              (GwyProcessFunc)&crosscor,
                              N_("/M_ultidata/_Cross-Correlation..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Cross-correlate two data fields"));

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
    gwy_param_def_add_image_id(paramdef, PARAM_OTHER_IMAGE, "other_image", _("Co_rrelate with"));
    gwy_param_def_add_int(paramdef, PARAM_SEARCH_X, "search_x", _("_Width"), 0, 100, 10);
    gwy_param_def_add_int(paramdef, PARAM_SEARCH_Y, "search_y", _("_Height"), 0, 100, 10);
    gwy_param_def_add_int(paramdef, PARAM_SEARCH_XOFFSET, "search_xoffset", _("_X offset"), -1000, 1000, 0);
    gwy_param_def_add_int(paramdef, PARAM_SEARCH_YOFFSET, "search_yoffset", _("_Y offset"), -1000, 1000, 0);
    gwy_param_def_add_int(paramdef, PARAM_WINDOW_X, "window_x", _("_Width"), 1, 200, 10);
    gwy_param_def_add_int(paramdef, PARAM_WINDOW_Y, "window_y", _("_Height"), 1, 200, 10);
    gwy_param_def_add_enum(paramdef, PARAM_WINDOW, "window", NULL, GWY_TYPE_WINDOWING_TYPE, GWY_WINDOWING_NONE);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT, "output", _("Output type"),
                               outputs, G_N_ELEMENTS(outputs), (1 << OUTPUT_ABS));
    gwy_param_def_add_boolean(paramdef, PARAM_ADD_LS_MASK, "add_ls_mask", _("Add _low score results mask"), TRUE);
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD, "threshold", _("T_hreshold"), -1.0, 1.0, 0.95);
    gwy_param_def_add_double(paramdef, PARAM_GAUSSIAN_WIDTH, "gaussian_width",
                             _("Apply Ga_ussian filter of width"), 0.1, 50.0, 10.0);
    gwy_param_def_add_boolean(paramdef, PARAM_GAUSSIAN, "gaussian", NULL, FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_EXTEND, "extend", _("Extend results to borders"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_MULTIPLE, "multiple", _("Multichannel cross-corelation"), FALSE);
    gwy_param_def_add_image_id(paramdef, PARAM_SECOND_SOURCE, "second_source", _("Second _source data"));
    gwy_param_def_add_image_id(paramdef, PARAM_SECOND_OTHER, "second_other", _("Co_rrelate with"));
    return paramdef;
}

static void
crosscor(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    GwyDataField *mask;
    ModuleArgs args;
    gint id, newid;
    guint i;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);
    args.params = gwy_params_new_from_settings(define_module_params());

    outcome = run_gui(&args);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
        goto end;

    for (i = 0; i < OUTPUT_NTYPES; i++) {
        if (!args.result[i])
            continue;

        newid = gwy_app_data_browser_add_data_field(args.result[i], data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
        gwy_app_set_data_field_title(data, newid, _(gwy_enum_to_string(1 << i, outputs, OUTPUT_NTYPES)));
        if (args.mask) {
            mask = gwy_data_field_duplicate(args.mask);
            gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), mask);
            g_object_unref(mask);
        }
        gwy_app_channel_log_add_proc(data, id, newid);
    }

end:
    g_object_unref(args.params);
    GWY_OBJECT_UNREF(args.mask);
    for (i = 0; i < OUTPUT_NTYPES; i++)
        GWY_OBJECT_UNREF(args.result[i]);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GtkNotebook *notebook;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Cross-Correlation"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gwy_dialog_add_content(dialog, GTK_WIDGET(notebook), FALSE, FALSE, 0);

    gtk_notebook_append_page(notebook, correlation_tab_new(&gui), gtk_label_new(_("Correlation")));
    gtk_notebook_append_page(notebook, output_tab_new(&gui), gtk_label_new(_("Output")));
    gtk_notebook_append_page(notebook, multichannel_tab_new(&gui), gtk_label_new(_("Multichannel")));

    g_signal_connect_swapped(gui.table_correlation, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_output, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_multichannel, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);

    return gwy_dialog_run(dialog);
}

static void
append_lateral_slider_pair(GwyParamTable *table, gint idx, gint idy, GwyDataField *field)
{
    gwy_param_table_append_slider(table, idx);
    gwy_param_table_slider_add_alt(table, idx);
    gwy_param_table_alt_set_field_pixel_x(table, idx, field);
    gwy_param_table_append_slider(table, idy);
    gwy_param_table_slider_add_alt(table, idy);
    gwy_param_table_alt_set_field_pixel_y(table, idy, field);
}

static GtkWidget*
correlation_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_correlation = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_OTHER_IMAGE);
    gwy_param_table_data_id_set_filter(table, PARAM_OTHER_IMAGE, other_image_filter, args->field, NULL);
    gwy_param_table_append_header(table, -1, _("Search Region"));
    append_lateral_slider_pair(table, PARAM_SEARCH_X, PARAM_SEARCH_Y, args->field);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_button(table, BUTTON_GUESS_OFFSET, -1, RESPONSE_GUESS_OFFSET, _("_Guess"));
    gwy_param_table_set_label(table, BUTTON_GUESS_OFFSET, _("Global offset of second image"));
    append_lateral_slider_pair(table, PARAM_SEARCH_XOFFSET, PARAM_SEARCH_YOFFSET, args->field);
    gwy_param_table_append_header(table, -1, _("Search Detail"));
    append_lateral_slider_pair(table, PARAM_WINDOW_X, PARAM_WINDOW_Y, args->field);
    gwy_param_table_append_combo(table, PARAM_WINDOW);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
output_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_output = gwy_param_table_new(args->params);
    gwy_param_table_append_checkboxes(table, PARAM_OUTPUT);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_ADD_LS_MASK);
    gwy_param_table_append_slider(table, PARAM_THRESHOLD);
    gwy_param_table_slider_set_mapping(table, PARAM_THRESHOLD, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_header(table, -1, _("Postprocessing"));
    gwy_param_table_append_slider(table, PARAM_GAUSSIAN_WIDTH);
    gwy_param_table_add_enabler(table, PARAM_GAUSSIAN, PARAM_GAUSSIAN_WIDTH);
    gwy_param_table_append_checkbox(table, PARAM_EXTEND);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
multichannel_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_multichannel = gwy_param_table_new(args->params);
    gwy_param_table_append_checkbox(table, PARAM_MULTIPLE);
    gwy_param_table_append_image_id(table, PARAM_SECOND_SOURCE);
    gwy_param_table_data_id_set_filter(table, PARAM_SECOND_SOURCE, weaker_image_filter, args->field, NULL);
    gwy_param_table_append_image_id(table, PARAM_SECOND_OTHER);
    gwy_param_table_data_id_set_filter(table, PARAM_SECOND_OTHER, weaker_image_filter, args->field, NULL);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gboolean multiple = gwy_params_get_boolean(params, PARAM_MULTIPLE);
    GwyParamTable *table;

    if (id < 0 || id == PARAM_OTHER_IMAGE || id == PARAM_SECOND_SOURCE || id == PARAM_SECOND_OTHER) {
        gboolean sens = !gwy_params_data_id_is_none(params, PARAM_OTHER_IMAGE);
        if (multiple) {
            /* This may be unnecessary because if the main, stricter, selector has something selected the other two
             * should always have something selected too. */
            sens = sens && !gwy_params_data_id_is_none(params, PARAM_SECOND_SOURCE);
            sens = sens && !gwy_params_data_id_is_none(params, PARAM_SECOND_OTHER);
        }
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, sens);
    }

    table = gui->table_output;
    if (id < 0 || id == PARAM_ADD_LS_MASK)
        gwy_param_table_set_sensitive(table, PARAM_THRESHOLD, gwy_params_get_boolean(params, PARAM_ADD_LS_MASK));

    table = gui->table_multichannel;
    if (id < 0 || id == PARAM_MULTIPLE) {
        gwy_param_table_set_sensitive(table, PARAM_SECOND_SOURCE, multiple);
        gwy_param_table_set_sensitive(table, PARAM_SECOND_OTHER, multiple);
    }
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == RESPONSE_GUESS_OFFSET)
        guess_offsets(gui);
}

static gboolean
other_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *otherfield, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &otherfield))
        return FALSE;
    if (otherfield == field)
        return FALSE;
    return !gwy_data_field_check_compatibility(field, otherfield,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL
                                               | GWY_DATA_COMPATIBILITY_VALUE);
}

static gboolean
weaker_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *otherfield, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &otherfield))
        return FALSE;
    if (otherfield == field)
        return FALSE;
    return !gwy_data_field_check_compatibility(field, otherfield,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL);
}

static void
guess_offsets(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyDataField *field1 = args->field, *field2 = gwy_params_get_image(args->params, PARAM_OTHER_IMAGE);
    GwyDataField *kernel, *score;
    gint xres, yres, xoffset, yoffset, xborder, yborder;
    gdouble maxscore, xoff, yoff;

    xres = gwy_data_field_get_xres(field1);
    yres = gwy_data_field_get_yres(field1);
    xborder = xres/5;
    yborder = yres/5;

    kernel = gwy_data_field_area_extract(field2, xborder, yborder, xres - 2*xborder, yres - 2*yborder);
    score = gwy_data_field_new_alike(field1, FALSE);
    gwy_data_field_correlate(field1, kernel, score, GWY_CORR_SEARCH_COVARIANCE_SCORE);
    g_object_unref(kernel);

    if (gwy_data_field_get_local_maxima_list(score, &xoff, &yoff, &maxscore, 1, 0, 0.0, FALSE)) {
        xoffset = GWY_ROUND(xoff) - xres/2;
        yoffset = GWY_ROUND(yoff) - yres/2;
    }
    else
        xoffset = yoffset = 0;
    g_object_unref(score);

    gwy_param_table_set_int(gui->table_correlation, PARAM_SEARCH_XOFFSET, xoffset);
    gwy_param_table_set_int(gui->table_correlation, PARAM_SEARCH_YOFFSET, yoffset);
}

static GwyDataField*
dir_field(GwyDataField *fieldx, GwyDataField *fieldy)
{
    GwyDataField *result = gwy_data_field_new_alike(fieldx, TRUE);
    const gdouble *xdata = gwy_data_field_get_data(fieldx);
    const gdouble *ydata = gwy_data_field_get_data(fieldy);
    gdouble *rdata = gwy_data_field_get_data(result);
    gint i, n;

    n = gwy_data_field_get_xres(fieldx)*gwy_data_field_get_yres(fieldx);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(result), NULL);

    for (i = 0; i < n; i++)
        rdata[i] = atan2(ydata[i], xdata[i]);

    return result;
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    GwyDataField *field1 = args->field,
                 *field2 = gwy_params_get_image(params, PARAM_OTHER_IMAGE),
                 *field3 = gwy_params_get_image(params, PARAM_SECOND_SOURCE),
                 *field4 = gwy_params_get_image(params, PARAM_SECOND_OTHER);
    GwyDataField *fieldx, *fieldy, *score;
    GwyDataField *field2b = NULL, *field4b = NULL, *fieldx2 = NULL, *fieldy2 = NULL, *score2 = NULL;
    gboolean multiple = gwy_params_get_boolean(params, PARAM_MULTIPLE);
    gint xoffset = gwy_params_get_int(params, PARAM_SEARCH_XOFFSET);
    gint yoffset = gwy_params_get_int(params, PARAM_SEARCH_YOFFSET);
    gint search_x = gwy_params_get_int(params, PARAM_SEARCH_X);
    gint search_y = gwy_params_get_int(params, PARAM_SEARCH_Y);
    gint window_x = gwy_params_get_int(params, PARAM_WINDOW_X);
    gint window_y = gwy_params_get_int(params, PARAM_WINDOW_Y);
    GwyWindowingType window = gwy_params_get_enum(params, PARAM_WINDOW);
    guint output = gwy_params_get_flags(params, PARAM_OUTPUT);
    GwyComputationState *state;
    gboolean ok = FALSE, state_finalised = FALSE;
    gint xres, yres, i, col, row;

    /* Result fields.  These three are always created because gwy_data_field_crosscorrelate_init() uses them. */
    fieldx = gwy_data_field_new_alike(field1, FALSE);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(fieldx), gwy_data_field_get_si_unit_xy(field1));
    fieldy = gwy_data_field_new_alike(fieldx, FALSE);
    score = gwy_data_field_new_alike(field1, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(score), NULL);

    xres = gwy_data_field_get_xres(field1);
    yres = gwy_data_field_get_yres(field1);

    if (multiple) {
        fieldx2 = gwy_data_field_new_alike(field1, FALSE);
        fieldy2 = gwy_data_field_new_alike(field1, FALSE);
        score2 = gwy_data_field_new_alike(field1, FALSE);
    }

    gwy_app_wait_start(wait_window, _("Initializing..."));

    /* If a shift is requested, make a copy and shift it. */
    if (xoffset || yoffset) {
        field2b = gwy_data_field_new_alike(field2, FALSE);
        gwy_data_field_fill(field2b, gwy_data_field_get_avg(field2));
        gwy_data_field_area_copy(field2, field2b, 0, 0, -1, -1, xoffset, yoffset);
        field2 = field2b;

        if (multiple) {
            field4b = gwy_data_field_new_alike(field4, FALSE);
            gwy_data_field_fill(field4b, gwy_data_field_get_avg(field4));
            gwy_data_field_area_copy(field4, field4b, 0, 0, -1, -1, xoffset, yoffset);
            field4 = field4b;
        }
    }

    /* Compute crosscorelation. */
    state = gwy_data_field_crosscorrelate_init(field1, field2, fieldx, fieldy, score,
                                               search_x, search_y, window_x, window_y);
    gwy_data_field_crosscorrelate_set_weights(state, window);

    if (!gwy_app_wait_set_message(multiple ? _("Correlating first set...") : _("Correlating...")))
        goto end;

    do {
        gwy_data_field_crosscorrelate_iteration(state);
        if (!gwy_app_wait_set_fraction(state->fraction))
            goto end;
    } while (state->state != GWY_COMPUTATION_STATE_FINISHED);

    gwy_data_field_crosscorrelate_finalize(state);

    /* Compute crosscorelation of second set if it is there. */
    if (multiple) {
        state = gwy_data_field_crosscorrelate_init(field3, field4, fieldx2, fieldy2, score2,
                                                   search_x, search_y, window_x, window_y);
        gwy_data_field_crosscorrelate_set_weights(state, window);

        if (!gwy_app_wait_set_message(_("Correlating second set...")))
            goto end;

        do {
            gwy_data_field_crosscorrelate_iteration(state);
            if (!gwy_app_wait_set_fraction(state->fraction))
                goto end;
        } while (state->state != GWY_COMPUTATION_STATE_FINISHED);

        gwy_data_field_crosscorrelate_finalize(state);

        gwy_data_field_linear_combination(fieldx, 0.5, fieldx, 0.5, fieldx2, 0.0);
        gwy_data_field_linear_combination(fieldy, 0.5, fieldy, 0.5, fieldy2, 0.0);
        gwy_data_field_linear_combination(score, 0.5, score, 0.5, score2, 0.0);
    }
    state_finalised = TRUE;

    if (xoffset)
        gwy_data_field_add(fieldx, gwy_data_field_jtor(fieldx, xoffset));
    if (yoffset)
        gwy_data_field_add(fieldy, gwy_data_field_itor(fieldx, yoffset));

    if (gwy_params_get_boolean(params, PARAM_EXTEND)) {
        GwyDataField *mask = gwy_data_field_new_alike(fieldx, TRUE);
        gint leftadd, rightadd, topadd, bottomadd;

        // Crop data if there was a global shift.
        leftadd = (xoffset < 0) ? 0 : xoffset;
        rightadd = (xoffset < 0) ? -xoffset : 0;
        topadd = (yoffset < 0) ? 0 : yoffset;
        bottomadd = (yoffset < 0) ? -yoffset : 0;

        gwy_data_field_area_fill(mask, 0, 0, window_x/2 + 2 + leftadd, yres, 1.0);
        gwy_data_field_area_fill(mask, 0, 0, xres, window_y/2 + 2 + topadd, 1.0);
        gwy_data_field_area_fill(mask, xres - window_x/2 - 2 - rightadd, 0, window_x/2 + 2 + rightadd, yres, 1.0);
        gwy_data_field_area_fill(mask, 0, yres - window_y/2 - 2 - bottomadd, xres, window_y/2 + 2 + bottomadd, 1.0);
        gwy_data_field_laplace_solve(fieldx, mask, -1, 0.8);
        gwy_data_field_laplace_solve(fieldy, mask, -1, 0.8);
        g_object_unref(mask);
    }

    if (gwy_params_get_boolean(params, PARAM_GAUSSIAN)) {
        gdouble width = gwy_params_get_double(params, PARAM_GAUSSIAN_WIDTH);
        gwy_data_field_filter_gaussian(fieldx, width);
        gwy_data_field_filter_gaussian(fieldy, width);
    }

    if (output & (1 << OUTPUT_CORRECTED)) {
        GwyDataField *corrected = gwy_data_field_new_alike(field2, FALSE);
        const gdouble *xdata = gwy_data_field_get_data(fieldx);
        const gdouble *ydata = gwy_data_field_get_data(fieldy);
        gdouble dx = gwy_data_field_get_dx(field1), dy = gwy_data_field_get_dy(field1);
        GwyXY *coords = g_new(GwyXY, xres*yres);

        for (row = i = 0; row < yres; row++) {
            for (col = 0; col < xres; col++, i++) {
                coords[i].x = col + xdata[i]/dx + 0.5 - xoffset;
                coords[i].y = row + ydata[i]/dy + 0.5 - yoffset;
            }
        }
        gwy_data_field_sample_distorted(field2, corrected, coords,
                                        GWY_INTERPOLATION_BILINEAR, GWY_EXTERIOR_BORDER_EXTEND, 0.0);
        args->result[OUTPUT_CORRECTED] = corrected;
        g_free(coords);
    }


    if (output & (1 << OUTPUT_ABS)) {
        args->result[OUTPUT_ABS] = gwy_data_field_new_alike(fieldx, FALSE);
        gwy_data_field_hypot_of_fields(args->result[OUTPUT_ABS], fieldx, fieldy);
    }
    if (output & (1 << OUTPUT_DIR))
        args->result[OUTPUT_DIR] = dir_field(fieldx, fieldy);
    if (output & (1 << OUTPUT_X))
        args->result[OUTPUT_X] = g_object_ref(fieldx);
    if (output & (1 << OUTPUT_Y))
        args->result[OUTPUT_Y] = g_object_ref(fieldy);
    if (output & (1 << OUTPUT_SCORE))
        args->result[OUTPUT_SCORE] = g_object_ref(score);
    if (gwy_params_get_boolean(params, PARAM_GAUSSIAN)) {
        args->mask = gwy_data_field_duplicate(score);
        gwy_data_field_threshold(args->mask, gwy_params_get_double(params, PARAM_GAUSSIAN_WIDTH), 1.0, 0.0);
    }

    ok = TRUE;

end:
    if (!state_finalised)
        gwy_data_field_crosscorrelate_finalize(state);
    gwy_app_wait_finish();

    g_object_unref(score);
    g_object_unref(fieldy);
    g_object_unref(fieldx);
    GWY_OBJECT_UNREF(fieldx2);
    GWY_OBJECT_UNREF(fieldy2);
    GWY_OBJECT_UNREF(score2);
    GWY_OBJECT_UNREF(field2b);
    GWY_OBJECT_UNREF(field4b);

    return ok;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
