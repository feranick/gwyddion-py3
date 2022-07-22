/*
 *  $Id: terracefit.c 24719 2022-03-22 13:03:46Z yeti-dn $
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
#include <libgwyddion/gwythreads.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"
#include "preview.h"

#define TERRACE_RUN_MODES (GWY_RUN_INTERACTIVE)

#define FIT_GRADIENT_NAME "__GwyFitDiffGradient"

/* Lower symmetric part indexing */
/* i MUST be greater or equal than j */
#define SLi(a, i, j) a[(i)*((i) + 1)/2 + (j)]

#define coord_index(a,i) g_array_index(a,TerraceCoords,i)
#define info_index(a,i) g_array_index(a,TerraceInfo,i)

#define pwr 0.65

enum {
    RESPONSE_FIT    = 1000,
    RESPONSE_SURVEY = 1001,
};

enum {
    PARAM_POLY_DEGREE,
    PARAM_EDGE_KERNEL_SIZE,
    PARAM_EDGE_THRESHOLD,
    PARAM_EDGE_BROADENING,
    PARAM_FIT_REPORT_STYLE,
    PARAM_MIN_AREA_FRAC,
    PARAM_INDEPENDENT,
    PARAM_MASKING,
    PARAM_USE_ONLY_MASK,
    PARAM_MASK_COLOR,
    PARAM_DISPLAY,
    PARAM_TERRACE_REPORT_STYLE,
    PARAM_OUTPUT,
    PARAM_SURVEY_POLY,
    PARAM_SURVEY_BROADENING,
    PARAM_POLY_DEGREE_MIN,
    PARAM_POLY_DEGREE_MAX,
    PARAM_BROADENING_MIN,
    PARAM_BROADENING_MAX,
    WIDGET_RESULTS,
    LABEL_FIT_RESULT,
    LABEL_SURVEY,
    BUTTON_RUN_SURVEY,
};

enum {
    COLUMN_ID,
    COLUMN_HEIGHT,
    COLUMN_LEVEL,
    COLUMN_AREA,
    COLUMN_ERROR,
    COLUMN_RESIDUUM,
};

typedef enum {
    PREVIEW_DATA       = 0,
    PREVIEW_SEGMENTED  = 1,
    PREVIEW_FITTED     = 2,
    PREVIEW_RESIDUUM   = 3,
    PREVIEW_TERRACES   = 4,
    PREVIEW_LEVELLED   = 5,
    PREVIEW_BACKGROUND = 6,
    PREVIEW_NTYPES,
} PreviewMode;

typedef enum {
    OUTPUT_SEGMENTED  = (1 << 0),
    OUTPUT_FITTED     = (1 << 1),
    OUTPUT_RESIDUUM   = (1 << 2),
    OUTPUT_TERRACES   = (1 << 3),
    OUTPUT_LEVELLED   = (1 << 4),
    OUTPUT_BACKGROUND = (1 << 5),
} OutputFlags;

typedef struct {
    GwyXYZ *xyz;
    guint *pixels;
    guint ncoords;
    gint level;
    /* Quantities gathered for terrace info. */
    gdouble msq;
    gdouble off;
} TerraceCoords;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    /* The field for DATA is actually the mask.  And we never output SEGMENTED (the colour terraces). */
    GwyDataField *result[PREVIEW_NTYPES];
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyContainer *data;
    GwyResults *results;
    GwyParamTable *table_param;
    GwyParamTable *table_terraces;
    GwyParamTable *table_output;
    GwyParamTable *table_survey;
    GtkWidget *dataview;
    GtkWidget *terracelist;

    GArray *terraceinfo;
    GwyGradient *diff_gradient;
    GdkPixbuf *colourpixbuf;
    GwySIValueFormat *vf;
    GArray *terracecoords;      /* Non-NULL if we have segmented terraces. */
    gboolean fit_ok;            /* We have fitted terraces. */
    gdouble xc, yc;
} ModuleGUI;

typedef struct {
    guint nterrparam;
    guint npowers;
    guint nterraces;
    gdouble msq;
    gdouble deltares;
    gdouble *solution;
    gdouble *invdiag;
} FitResult;

typedef struct {
    GwyRGBA colour;
    gdouble height;   /* estimate from free fit */
    gdouble error;    /* difference from free fit estimate */
    gdouble residuum; /* final fit residuum */
    guint npixels;
    gint level;
} TerraceInfo;

typedef struct {
    gint poly_degree;
    gdouble edge_kernel_size;
    gdouble edge_threshold;
    gdouble edge_broadening;
    gdouble min_area_frac;
    gint fit_ok;
    gint nterraces;
    gdouble step;
    gdouble step_err;
    gdouble msq;
    gdouble discrep;
} TerraceSurveyRow;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             terrace                 (GwyContainer *data,
                                                 GwyRunType run);
static void             create_output_fields    (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static GwyResults*      create_results          (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             dialog_response         (ModuleGUI *gui,
                                                 gint response);
static void             run_segmentation        (gpointer user_data);
static void             terrace_fit             (ModuleGUI *gui);
static FitResult*       terrace_do              (GwyDataField *marked,
                                                 GwyDataField *residuum,
                                                 GwyDataField *background,
                                                 GwyDataField *terraces,
                                                 GArray *terracecoords,
                                                 GArray *terraceinfo,
                                                 GwyParams *params,
                                                 gdouble xc,
                                                 gdouble yc,
                                                 gboolean fill_bg_and_terraces,
                                                 const gchar **message);
static void             update_results          (ModuleGUI *gui,
                                                 FitResult *fres);
static void             update_terrace_colours  (ModuleGUI *gui);
static GtkWidget*       parameters_tab_new      (ModuleGUI *gui);
static GtkWidget*       terrace_list_tab_new    (ModuleGUI *gui);
static GtkWidget*       output_tab_new          (ModuleGUI *gui);
static GtkWidget*       survey_tab_new          (ModuleGUI *gui);
static void             free_terrace_coordinates(GArray *terracecoords);
static void             reset_images            (ModuleGUI *gui);
static void             update_diff_gradient    (ModuleGUI *gui);
static gchar*           format_report           (gpointer user_data);
static guint            prepare_survey          (GwyParams *params,
                                                 GArray *degrees,
                                                 GArray *broadenings);
static void             run_survey              (ModuleGUI *gui);
static void             sanitise_params         (ModuleArgs *args);

static const GwyEnum output_flags[] = {
    { N_("Marked terraces"),       OUTPUT_SEGMENTED,  },
    { N_("Fitted shape"),          OUTPUT_FITTED,     },
    { N_("Difference"),            OUTPUT_RESIDUUM,   },
    { N_("Terraces (ideal)"),      OUTPUT_TERRACES,   },
    { N_("Leveled surface"),       OUTPUT_LEVELLED,   },
    { N_("Polynomial background"), OUTPUT_BACKGROUND, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Fits terraces with polynomial background."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, terracefit)

static gboolean
module_register(void)
{
    gwy_process_func_register("terracefit",
                              (GwyProcessFunc)&terrace,
                              N_("/Measure _Features/_Terraces..."),
                              GWY_STOCK_TERRACE_MEASURE,
                              TERRACE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Fit terraces with polynomial background"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum previews[] = {
        { N_("Data"),                  PREVIEW_DATA,       },
        { N_("Marked terraces"),       PREVIEW_SEGMENTED,  },
        { N_("Fitted shape"),          PREVIEW_FITTED,     },
        { N_("Difference"),            PREVIEW_RESIDUUM,   },
        { N_("Terraces (ideal)"),      PREVIEW_TERRACES,   },
        { N_("Leveled surface"),       PREVIEW_LEVELLED,   },
        { N_("Polynomial background"), PREVIEW_BACKGROUND, },
    };
    const gdouble MAX_BROADEN = 128.0;
    const gint MAX_DEGREE = 18;

    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_int(paramdef, PARAM_POLY_DEGREE, "poly_degree", _("_Polynomial degree"), 0, MAX_DEGREE, 6);
    gwy_param_def_add_double(paramdef, PARAM_EDGE_KERNEL_SIZE, "edge_kernel_size", _("_Step detection kernel"),
                             1.0, 64.0, 3.5);
    gwy_param_def_add_percentage(paramdef, PARAM_EDGE_THRESHOLD, "edge_threshold", _("Step detection _threshold"),
                                 0.25);
    gwy_param_def_add_double(paramdef, PARAM_EDGE_BROADENING, "edge_broadening", _("Step _broadening"), 0.0, 16.0, 3.0);
    gwy_param_def_add_report_type(paramdef, PARAM_FIT_REPORT_STYLE, "fit_report_style", _("Save Fit Report"),
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON);
    gwy_param_def_add_double(paramdef, PARAM_MIN_AREA_FRAC, "min_area_frac", _("Minimum terrace _area"),
                             0.0, 0.4, 0.015);
    gwy_param_def_add_boolean(paramdef, PARAM_INDEPENDENT, "independent", _("_Independent heights"), FALSE);
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_boolean(paramdef, PARAM_USE_ONLY_MASK, "use_only_mask", _("Do not _segment, use only mask"),
                              FALSE);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, NULL, gwy_sgettext("verb|Display"),
                              previews, G_N_ELEMENTS(previews), PREVIEW_DATA);
    gwy_param_def_add_report_type(paramdef, PARAM_TERRACE_REPORT_STYLE, "terrace_report_style", _("Save Terrace Table"),
                                  GWY_RESULTS_EXPORT_TABULAR_DATA, GWY_RESULTS_REPORT_TABSEP);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT, "output", _("Output"),
                               output_flags, G_N_ELEMENTS(output_flags), OUTPUT_SEGMENTED);
    gwy_param_def_add_boolean(paramdef, PARAM_SURVEY_POLY, "survey_poly", _("_Polynomial degree"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_SURVEY_BROADENING, "survey_broadening", _("Step _broadening"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_POLY_DEGREE_MIN, "poly_degree_min", _("M_inimum polynomial degree"),
                          0, MAX_DEGREE, 0);
    gwy_param_def_add_int(paramdef, PARAM_POLY_DEGREE_MAX, "poly_degree_max", _("_Maximum polynomial degree"),
                          0, MAX_DEGREE, MAX_DEGREE);
    gwy_param_def_add_double(paramdef, PARAM_BROADENING_MIN, "broadening_min", _("Minimum broadening"),
                             0, MAX_BROADEN, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_BROADENING_MAX, "broadening_max", _("Maximum broadening"),
                             0, MAX_BROADEN, MAX_BROADEN);
    return paramdef;
}

static void
terrace(GwyContainer *data, GwyRunType run)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    guint i;
    gint id;

    g_return_if_fail(run & TERRACE_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    for (i = 0; i < PREVIEW_NTYPES; i++) {
        args.result[i] = gwy_data_field_new_alike(args.field, TRUE);
        if (i == PREVIEW_DATA)
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result[i]), NULL);
    }
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        goto end;

    create_output_fields(&args, data, id);

end:
    for (i = 0; i < PREVIEW_NTYPES; i++)
        g_object_unref(args.result[i]);
    g_object_unref(args.params);
}

static void
create_output_fields(ModuleArgs *args, GwyContainer *data, gint id)
{
    static const struct {
        OutputFlags output;
        PreviewMode preview;
        gboolean add_inv_mask;
    }
    output_map[G_N_ELEMENTS(output_flags)] = {
        { OUTPUT_SEGMENTED,  PREVIEW_DATA,       FALSE, },
        { OUTPUT_FITTED,     PREVIEW_FITTED,     TRUE,  },
        { OUTPUT_RESIDUUM,   PREVIEW_RESIDUUM,   TRUE,  },
        { OUTPUT_TERRACES,   PREVIEW_TERRACES,   TRUE,  },
        { OUTPUT_LEVELLED,   PREVIEW_LEVELLED,   TRUE,  },
        { OUTPUT_BACKGROUND, PREVIEW_BACKGROUND, FALSE, },
    };
    OutputFlags output = gwy_params_get_flags(args->params, PARAM_OUTPUT);
    GwyDataField *mask, *invmask, *field;
    const gchar *title;
    GQuark quark;
    gint newid;
    guint i;

    mask = args->result[PREVIEW_SEGMENTED];
    for (i = 0; i < G_N_ELEMENTS(output_map); i++) {
        if (!(output & output_map[i].output))
            continue;

        field = args->result[output_map[i].preview];
        if (output_map[i].output == OUTPUT_SEGMENTED) {
            quark = gwy_app_get_mask_key_for_id(id);
            gwy_app_undo_qcheckpointv(data, 1, &quark);
            gwy_container_set_object(data, quark, field);
            gwy_app_channel_log_add_proc(data, id, id);
            continue;
        }
        newid = gwy_app_data_browser_add_data_field(field, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                GWY_DATA_ITEM_MASK_COLOR,
                                0);
        if (output_map[i].add_inv_mask) {
            invmask = gwy_data_field_duplicate(mask);
            gwy_data_field_grains_invert(invmask);
            gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), invmask);
            g_object_unref(invmask);
        }
        title = _(gwy_enum_to_string(output_map[i].output, output_flags, G_N_ELEMENTS(output_flags)));
        gwy_app_set_data_field_title(data, newid, title);
        gwy_app_channel_log_add_proc(data, id, newid);
    }
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox;
    GwyDialogOutcome outcome;
    GtkNotebook *notebook;
    GwyDialog *dialog;
    ModuleGUI gui;
    gint width, height, i;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.results = create_results(args, data, id);
    gui.vf = gwy_data_field_get_value_format_z(args->field, GWY_SI_UNIT_FORMAT_MARKUP, NULL);
    gui.vf->precision++;
    gui.diff_gradient = gwy_inventory_new_item(gwy_gradients(), GWY_GRADIENT_DEFAULT, FIT_GRADIENT_NAME);
    gwy_resource_use(GWY_RESOURCE(gui.diff_gradient));
    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    gui.colourpixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, height | 1, height | 1);

    gui.data = gwy_container_new();
    for (i = PREVIEW_DATA; i < PREVIEW_NTYPES; i++) {
        if (i == PREVIEW_DATA) {
            gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(i), args->field);
            gwy_container_set_object(gui.data, gwy_app_get_mask_key_for_id(i), args->result[i]);
            gwy_app_sync_data_items(data, gui.data, id, i, FALSE,
                                    GWY_DATA_ITEM_RANGE_TYPE,
                                    GWY_DATA_ITEM_RANGE,
                                    GWY_DATA_ITEM_GRADIENT,
                                    GWY_DATA_ITEM_REAL_SQUARE,
                                    GWY_DATA_ITEM_MASK_COLOR,
                                    0);
        }
        else {
            gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(i), args->result[i]);
            gwy_container_set_enum(gui.data, gwy_app_get_data_range_type_key_for_id(i), GWY_LAYER_BASIC_RANGE_FULL);
            gwy_app_sync_data_items(data, gui.data, id, i, FALSE,
                                    GWY_DATA_ITEM_GRADIENT,
                                    GWY_DATA_ITEM_REAL_SQUARE,
                                    0);
        }
    }
    gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(PREVIEW_SEGMENTED), "DFit");
    gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(PREVIEW_RESIDUUM), FIT_GRADIENT_NAME);

    gui.dialog = gwy_dialog_new(_("Fit Terraces"));
    dialog = GWY_DIALOG(gui.dialog);
    gtk_dialog_add_button(GTK_DIALOG(dialog), gwy_sgettext("verb|_Fit"), RESPONSE_FIT);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.dataview), FALSE);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, parameters_tab_new(&gui), gtk_label_new(_("Parameters")));
    gtk_notebook_append_page(notebook, terrace_list_tab_new(&gui), gtk_label_new(_("Terrace List")));
    gtk_notebook_append_page(notebook, output_tab_new(&gui), gtk_label_new(_("Output")));
    gtk_notebook_append_page(notebook, survey_tab_new(&gui), gtk_label_new(_("Survey")));

    g_signal_connect_swapped(gui.table_param, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_terraces, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_output, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_survey, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, run_segmentation, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    if (outcome != GWY_DIALOG_CANCEL && (gwy_params_get_flags(args->params, PARAM_OUTPUT) & OUTPUT_SEGMENTED))
        gwy_app_sync_data_items(gui.data, data, i, id, FALSE, GWY_DATA_ITEM_MASK_COLOR, 0);

    gwy_resource_release(GWY_RESOURCE(gui.diff_gradient));
    gwy_inventory_delete_item(gwy_gradients(), FIT_GRADIENT_NAME);
    gwy_si_unit_value_format_free(gui.vf);
    g_object_unref(gui.results);
    g_object_unref(gui.colourpixbuf);
    g_object_unref(gui.data);
    free_terrace_coordinates(gui.terracecoords);
    g_array_free(gui.terraceinfo, TRUE);

    return outcome;
}

static GwyResults*
create_results(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyResults *results = gwy_results_new();

    gwy_results_add_header(results, N_("Fit Results"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "image", N_("Image"));
    gwy_results_add_value_yesno(results, "masking", N_("Mask in use"));
    gwy_results_add_separator(results);
    /* TODO: We might also want to output the segmentation & fit settings. */
    gwy_results_add_value_z(results, "step", N_("Fitted step height"));
    gwy_results_add_value_z(results, "resid", N_("Mean square difference"));
    gwy_results_add_value_z(results, "discrep", N_("Terrace discrepancy"));
    gwy_results_add_value_int(results, "nterraces", N_("Number of terraces"));
    gwy_results_set_unit(results, "z", gwy_data_field_get_si_unit_z(args->field));
    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_channel(results, "image", data, id);

    return results;
}

static void
free_terrace_coordinates(GArray *terracecoords)
{
    guint g;

    if (!terracecoords)
        return;

    for (g = 0; g < terracecoords->len; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        g_free(tc->xyz);
        g_free(tc->pixels);
    }
    g_array_free(terracecoords, TRUE);
}

static void
reset_images(ModuleGUI *gui)
{
    PreviewMode i;

    for (i = PREVIEW_DATA; i < PREVIEW_NTYPES; i++) {
        /* These are always available. */
        if (i != PREVIEW_DATA && i != PREVIEW_SEGMENTED) {
            gwy_data_field_clear(gui->args->result[i]);
            gwy_data_field_data_changed(gui->args->result[i]);
        }
    }
}

static GtkWidget*
parameters_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_param = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_EDGE_KERNEL_SIZE);
    gwy_param_table_slider_add_alt(table, PARAM_EDGE_KERNEL_SIZE);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_EDGE_KERNEL_SIZE, args->field);
    gwy_param_table_append_slider(table, PARAM_EDGE_THRESHOLD);
    gwy_param_table_append_slider(table, PARAM_EDGE_BROADENING);
    gwy_param_table_slider_set_steps(table, PARAM_EDGE_BROADENING, 0.1, 1.0);
    gwy_param_table_slider_set_digits(table, PARAM_EDGE_BROADENING, 1);
    gwy_param_table_slider_add_alt(table, PARAM_EDGE_BROADENING);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_EDGE_BROADENING, args->field);
    gwy_param_table_append_slider(table, PARAM_MIN_AREA_FRAC);
    gwy_param_table_slider_set_factor(table, PARAM_MIN_AREA_FRAC, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_MIN_AREA_FRAC, "%");
    /* FIXME: We can show the area in real units (as a custom alt). */
    gwy_param_table_append_slider(table, PARAM_POLY_DEGREE);
    gwy_param_table_slider_set_mapping(table, PARAM_POLY_DEGREE, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_checkbox(table, PARAM_INDEPENDENT);
    gwy_param_table_append_combo(table, PARAM_DISPLAY);
    if (args->mask) {
        gwy_param_table_append_combo(table, PARAM_MASKING);
        gwy_param_table_append_checkbox(table, PARAM_USE_ONLY_MASK);
    }
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui->data, PREVIEW_DATA, NULL, -1);

    gwy_param_table_append_header(table, -1, _("Result"));
    gwy_param_table_append_results(table, WIDGET_RESULTS, gui->results, "step", "resid", "discrep", "nterraces", NULL);
    gwy_param_table_append_message(table, LABEL_FIT_RESULT, NULL);
    gwy_param_table_message_set_type(table, LABEL_FIT_RESULT, GTK_MESSAGE_ERROR);
    gwy_param_table_append_report(table, PARAM_FIT_REPORT_STYLE);
    gwy_param_table_report_set_results(table, PARAM_FIT_REPORT_STYLE, gui->results);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static void
render_colour(G_GNUC_UNUSED GtkTreeViewColumn *column, G_GNUC_UNUSED GtkCellRenderer *renderer,
              GtkTreeModel *model, GtkTreeIter *iter,
              gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    TerraceInfo *info;
    guint i, pixel;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    info = &info_index(gui->terraceinfo, i);
    pixel = 0xff | gwy_rgba_to_pixbuf_pixel(&info->colour);
    gdk_pixbuf_fill(gui->colourpixbuf, pixel);
}

static void
render_text_column(GtkTreeViewColumn *column, GtkCellRenderer *renderer,
                   GtkTreeModel *model, GtkTreeIter *iter,
                   gpointer user_data)
{
    guint column_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(column), "column-id"));
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GwySIValueFormat *vf = gui->vf;
    TerraceInfo *info;
    gchar buf[32];
    guint i;

    if (!gui->fit_ok && (column_id == COLUMN_HEIGHT
                         || column_id == COLUMN_LEVEL
                         || column_id == COLUMN_ERROR
                         || column_id == COLUMN_RESIDUUM)) {
        g_object_set(renderer, "text", "", NULL);
        return;
    }

    gtk_tree_model_get(model, iter, 0, &i, -1);
    info = &info_index(gui->terraceinfo, i);
    if (column_id == COLUMN_ID)
        g_snprintf(buf, sizeof(buf), "%u", i+1);
    else if (column_id == COLUMN_AREA)
        g_snprintf(buf, sizeof(buf), "%u", info->npixels);
    else if (column_id == COLUMN_HEIGHT)
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, info->height/vf->magnitude);
    else if (column_id == COLUMN_LEVEL)
        g_snprintf(buf, sizeof(buf), "%d", info->level);
    else if (column_id == COLUMN_ERROR)
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, info->error/vf->magnitude);
    else if (column_id == COLUMN_RESIDUUM)
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, info->residuum/vf->magnitude);
    else {
        g_assert_not_reached();
    }
    g_object_set(renderer, "text", buf, NULL);
}

static GtkTreeViewColumn*
append_text_column(ModuleGUI *gui, guint column_id, const gchar *title, gboolean is_z)
{
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkWidget *label;
    gchar *s;

    column = gtk_tree_view_column_new();
    g_object_set_data(G_OBJECT(column), "column-id", GUINT_TO_POINTER(column_id));
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer, render_text_column, gui, NULL);

    label = gtk_label_new(NULL);
    if (is_z && *gui->vf->units)
        s = g_strdup_printf("<b>%s</b> [%s]", title, gui->vf->units);
    else
        s = g_strdup_printf("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(label), s);
    g_free(s);
    gtk_tree_view_column_set_widget(column, label);
    gtk_widget_show(label);

    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->terracelist), column);

    return column;
}

static GtkWidget*
terrace_list_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GtkWidget *vbox, *hbox, *scwin;
    GwyParamTable *table;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GwyNullStore *store;

    vbox = gwy_vbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    gui->terraceinfo = g_array_new(FALSE, FALSE, sizeof(TerraceInfo));

    store = gwy_null_store_new(0);
    gui->terracelist = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_container_add(GTK_CONTAINER(scwin), gui->terracelist);

    column = append_text_column(gui, COLUMN_ID, "n", FALSE);
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, FALSE);
    g_object_set(renderer, "pixbuf", gui->colourpixbuf, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, render_colour, gui, NULL);
    append_text_column(gui, COLUMN_HEIGHT, "h", TRUE);
    append_text_column(gui, COLUMN_LEVEL, "k", FALSE);
    append_text_column(gui, COLUMN_AREA, "A<sub>px</sub>", FALSE);
    append_text_column(gui, COLUMN_ERROR, "Δ", TRUE);
    append_text_column(gui, COLUMN_RESIDUUM, "r", TRUE);

    table = gui->table_terraces = gwy_param_table_new(args->params);
    gwy_param_table_append_report(table, PARAM_TERRACE_REPORT_STYLE);
    gwy_param_table_report_set_formatter(table, PARAM_TERRACE_REPORT_STYLE, format_report, gui, NULL);
    /* XXX: Silly.  Just want to right-align the export controls for consistency. */
    hbox = gwy_hbox_new(0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return vbox;
}

static GtkWidget*
output_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_output = gwy_param_table_new(args->params);
    gwy_param_table_append_checkboxes(table, PARAM_OUTPUT);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
survey_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_survey = gwy_param_table_new(args->params);
    gwy_param_table_append_checkbox(table, PARAM_SURVEY_POLY);
    gwy_param_table_append_slider(table, PARAM_POLY_DEGREE_MIN);
    gwy_param_table_slider_set_mapping(table, PARAM_POLY_DEGREE_MIN, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_POLY_DEGREE_MAX);
    gwy_param_table_slider_set_mapping(table, PARAM_POLY_DEGREE_MAX, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_SURVEY_BROADENING);
    gwy_param_table_append_slider(table, PARAM_BROADENING_MIN);
    gwy_param_table_slider_add_alt(table, PARAM_BROADENING_MIN);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_BROADENING_MIN, args->field);
    gwy_param_table_append_slider(table, PARAM_BROADENING_MAX);
    gwy_param_table_slider_add_alt(table, PARAM_BROADENING_MAX);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_BROADENING_MAX, args->field);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_button(table, BUTTON_RUN_SURVEY, -1, RESPONSE_SURVEY, _("_Execute"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_message(table, LABEL_SURVEY, NULL);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static void
switch_preview(ModuleGUI *gui)
{
    PreviewMode display = gwy_params_get_enum(gui->args->params, PARAM_DISPLAY);
    GwyPixmapLayer *player;
    GwyLayerBasic *blayer;

    player = gwy_data_view_get_base_layer(GWY_DATA_VIEW(gui->dataview));
    gwy_pixmap_layer_set_data_key(player, g_quark_to_string(gwy_app_get_data_key_for_id(display)));
    blayer = GWY_LAYER_BASIC(player);
    gwy_layer_basic_set_gradient_key(blayer, g_quark_to_string(gwy_app_get_data_palette_key_for_id(display)));
    gwy_layer_basic_set_range_type_key(blayer, g_quark_to_string(gwy_app_get_data_range_type_key_for_id(display)));
    gwy_layer_basic_set_min_max_key(blayer, g_quark_to_string(gwy_app_get_data_base_key_for_id(display)));
    player = gwy_data_view_get_alpha_layer(GWY_DATA_VIEW(gui->dataview));
    if (display == PREVIEW_DATA)
        gwy_pixmap_layer_set_data_key(player, g_quark_to_string(gwy_app_get_mask_key_for_id(display)));
    else
        gwy_pixmap_layer_set_data_key(player, "/no/mask");
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table;
    gboolean survey_changed = (id == PARAM_SURVEY_POLY || id == PARAM_SURVEY_BROADENING);

    table = gui->table_param;
    if (args->mask && (id < 0 || id == PARAM_MASKING)) {
        GwyMaskingType masking = gwy_params_get_enum(params, PARAM_MASKING);
        gwy_param_table_set_sensitive(table, PARAM_USE_ONLY_MASK, masking != GWY_MASK_IGNORE);
    }
    if (args->mask && (id < 0 || id == PARAM_USE_ONLY_MASK)) {
        gboolean use_only_mask = gwy_params_get_boolean(params, PARAM_USE_ONLY_MASK);
        gwy_param_table_set_sensitive(table, PARAM_EDGE_KERNEL_SIZE, !use_only_mask);
        gwy_param_table_set_sensitive(table, PARAM_EDGE_THRESHOLD, !use_only_mask);
        gwy_param_table_set_sensitive(table, PARAM_EDGE_BROADENING, !use_only_mask);
    }
    if (id == PARAM_DISPLAY)
        switch_preview(gui);

    table = gui->table_survey;
    if (id == PARAM_POLY_DEGREE_MIN || id == PARAM_POLY_DEGREE_MAX) {
        gint min_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE_MIN);
        gint max_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE_MAX);
        if (min_degree > max_degree) {
            if (id == PARAM_POLY_DEGREE_MAX)
                gwy_param_table_set_int(table, PARAM_POLY_DEGREE_MIN, (min_degree = max_degree));
            else
                gwy_param_table_set_int(table, PARAM_POLY_DEGREE_MAX, (max_degree = min_degree));
        }
        survey_changed = TRUE;
    }
    if (id == PARAM_BROADENING_MIN || id == PARAM_BROADENING_MAX) {
        gdouble min_broadening = gwy_params_get_double(params, PARAM_BROADENING_MIN);
        gdouble max_broadening = gwy_params_get_double(params, PARAM_BROADENING_MAX);
        if (min_broadening > max_broadening) {
            if (id == PARAM_BROADENING_MAX)
                gwy_param_table_set_double(table, PARAM_BROADENING_MIN, (min_broadening = max_broadening));
            else
                gwy_param_table_set_double(table, PARAM_BROADENING_MAX, (max_broadening = min_broadening));
        }
        survey_changed = TRUE;
    }

    if (id < 0 || id == PARAM_INDEPENDENT || survey_changed) {
        gboolean independent = gwy_params_get_boolean(params, PARAM_INDEPENDENT);
        gboolean survey_poly = gwy_params_get_boolean(params, PARAM_SURVEY_POLY);
        gboolean survey_broadening = gwy_params_get_boolean(params, PARAM_SURVEY_BROADENING);
        const gchar *message;
        gchar *s = NULL;

        gwy_param_table_set_sensitive(table, PARAM_SURVEY_POLY, !independent);
        gwy_param_table_set_sensitive(table, PARAM_POLY_DEGREE_MIN, !independent && survey_poly);
        gwy_param_table_set_sensitive(table, PARAM_POLY_DEGREE_MAX, !independent && survey_poly);
        gwy_param_table_set_sensitive(table, PARAM_SURVEY_BROADENING, !independent);
        gwy_param_table_set_sensitive(table, PARAM_BROADENING_MIN, !independent && survey_broadening);
        gwy_param_table_set_sensitive(table, PARAM_BROADENING_MAX, !independent && survey_broadening);
        gwy_param_table_set_sensitive(table, BUTTON_RUN_SURVEY, !independent && (survey_poly || survey_broadening));
        if (independent)
            message = _("Survey cannot be run with independent heights.");
        else if (!survey_poly && !survey_broadening)
            message = _("No free parameters are selected.");
        else
            message = s = g_strdup_printf(_("Number of combinations: %u."), prepare_survey(params, NULL, NULL));

        gwy_param_table_set_label(table, LABEL_SURVEY, message);
        g_free(s);
    }

    /* Only segmentation parameters cause immediate update. */
    if (id < 0 || id == PARAM_EDGE_KERNEL_SIZE || id == PARAM_EDGE_THRESHOLD || id == PARAM_EDGE_BROADENING
        || id == PARAM_MIN_AREA_FRAC || id == PARAM_MASKING || id == PARAM_USE_ONLY_MASK)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == RESPONSE_SURVEY)
        run_survey(gui);
    else if (response == RESPONSE_FIT)
        terrace_fit(gui);
}

static void
update_diff_gradient(ModuleGUI *gui)
{
    gdouble min, max, dispmin, dispmax;

    gwy_data_field_get_min_max(gui->args->result[PREVIEW_RESIDUUM], &min, &max);
    gwy_data_field_get_autorange(gui->args->result[PREVIEW_RESIDUUM], &dispmin, &dispmax);
    gwy_debug("residuum min %g, max %g", min, max);
    set_gradient_for_residuum(gui->diff_gradient, min, max, &dispmin, &dispmax);

    gwy_container_set_enum(gui->data, gwy_app_get_data_range_type_key_for_id(PREVIEW_RESIDUUM),
                           GWY_LAYER_BASIC_RANGE_FIXED);
    gwy_container_set_double(gui->data, gwy_app_get_data_range_min_key_for_id(PREVIEW_RESIDUUM), dispmin);
    gwy_container_set_double(gui->data, gwy_app_get_data_range_max_key_for_id(PREVIEW_RESIDUUM), dispmax);
}

static void
improve_edge_connectivity(GwyDataField *steps,
                          GwyDataField *tmp,
                          gdouble radius)
{
    gint xres, yres, i, r, r2lim;
    const gdouble *d;
    gdouble *t;

    gwy_data_field_clear(tmp);
    xres = gwy_data_field_get_xres(steps);
    yres = gwy_data_field_get_yres(steps);
    d = gwy_data_field_get_data_const(steps);
    t = gwy_data_field_get_data(tmp);
    r = (gint)floor(radius);
    r2lim = (gint)0.7*radius*radius;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(xres,yres,r,r2lim,t,d) \
            private(i)
#endif
    for (i = r; i < yres-r; i++) {
        gint j;

        for (j = r; j < xres-r; j++) {
            gint ii, jj;

            if (d[i*xres + j] <= 0.0)
                continue;

            for (ii = -r; ii <= r; ii++) {
                for (jj = -r; jj <= r; jj++) {
                    if (ii*ii + jj*jj > 0.7*r2lim
                        && d[(i+ii)*xres + (j+jj)] >= 1.0
                        && d[(i-ii)*xres + (j-jj)] >= 1.0) {
                        gint ic = (ii > 0 ? i + ii/2 : i - (-ii)/2);
                        gint jc = (jj > 0 ? j + jj/2 : j - (-jj)/2);
                        if (d[ic*xres + jc] <= 0.0)
                            t[ic*xres + jc] += 1.0;
                    }
                }
            }
        }
    }

    gwy_data_field_max_of_fields(steps, steps, tmp);
}

static GArray*
find_terrace_coordinates(GwyDataField *field, GwyDataField *mask, GwyParams *params,
                         GwyDataField *marked, GwyDataField *terraceids,
                         gdouble *pxc, gdouble *pyc)
{
    gboolean use_only_mask = gwy_params_get_boolean(params, PARAM_USE_ONLY_MASK);
    GwyMaskingType masking = gwy_params_get_enum(params, PARAM_MASKING);
    gdouble edge_kernel_size = gwy_params_get_double(params, PARAM_EDGE_KERNEL_SIZE);
    gdouble edge_threshold = gwy_params_get_double(params, PARAM_EDGE_THRESHOLD);
    gdouble edge_broadening = gwy_params_get_double(params, PARAM_EDGE_BROADENING);
    gdouble min_area_frac = gwy_params_get_double(params, PARAM_MIN_AREA_FRAC);
    gint xres = gwy_data_field_get_xres(field);
    gint yres = gwy_data_field_get_yres(field);
    gint i, j, g, minsize, ngrains, n = xres*yres, npixels;
    const gdouble *d = gwy_data_field_get_data_const(field);
    gint *grains, *sizes;
    GArray *terracecoords;
    gdouble threshold, xc, yc;
    gdouble *ti;

    if (mask && use_only_mask) {
        /* Use provided mask as requested. */
        gwy_data_field_copy(mask, marked, FALSE);
        if (masking == GWY_MASK_EXCLUDE)
            gwy_data_field_grains_invert(marked);
    }
    else {
        /* Mark flat areas in the field. */
        gwy_data_field_copy(field, marked, FALSE);
        gwy_data_field_filter_gauss_step(marked, edge_kernel_size);
        threshold = edge_threshold*gwy_data_field_get_max(marked);
        gwy_data_field_threshold(marked, threshold, 0.0, 1.0);
        /* Use terraceids as a buffer. */
        improve_edge_connectivity(marked, terraceids, 11.5);
        improve_edge_connectivity(marked, terraceids, 9.5);
        gwy_data_field_grains_invert(marked);
        gwy_data_field_grains_shrink(marked, edge_broadening, GWY_DISTANCE_TRANSFORM_EUCLIDEAN, FALSE);

        /* Combine with existing mask if required. */
        if (mask && masking != GWY_MASK_IGNORE) {
            if (masking == GWY_MASK_INCLUDE)
                gwy_data_field_grains_intersect(marked, mask);
            else {
                gwy_data_field_grains_invert(marked);
                gwy_data_field_grains_add(marked, mask);
                gwy_data_field_grains_invert(marked);
            }
        }
    }

    /* Keep only large areas.  This inherently limits the maximum number of
     * areas too. */
    minsize = GWY_ROUND(min_area_frac*n);
    gwy_data_field_grains_remove_by_size(marked, minsize);

    /* Gather coordinates for each terrace into an array. */
    grains = g_new0(gint, n);
    ngrains = gwy_data_field_number_grains(marked, grains);
    if (!ngrains) {
        g_free(grains);
        return NULL;
    }

    sizes = gwy_data_field_get_grain_sizes(marked, ngrains, grains, NULL);
    terracecoords = g_array_sized_new(FALSE, FALSE, sizeof(TerraceCoords), ngrains);
    for (g = 1; g <= ngrains; g++) {
        TerraceCoords tc;

        tc.ncoords = 0;
        tc.xyz = g_new(GwyXYZ, sizes[g]);
        tc.pixels = g_new(guint, sizes[g]);
        g_array_append_val(terracecoords, tc);
    }

    /* Normalise coordinates to have centre of mass at 0. */
    ti = gwy_data_field_get_data(terraceids);
    gwy_clear(sizes, ngrains+1);
    xc = yc = 0.0;
    npixels = 0;
    for (i = 0; i < yres; i++) {
        gdouble y = (2.0*i + 1 - yres)/(yres - 1);
        for (j = 0; j < xres; j++) {
            guint k = i*xres + j;
            if ((g = grains[k])) {
                TerraceCoords *tc = &coord_index(terracecoords, g-1);
                GwyXYZ *xyz = tc->xyz + tc->ncoords;

                tc->pixels[tc->ncoords] = k;
                xyz->x = (2.0*j + 1 - xres)/(xres - 1);
                xyz->y = y;
                xyz->z = d[k];
                xc += xyz->x;
                yc += xyz->y;
                tc->ncoords++;
                npixels++;
            }
            ti[k] = g;
        }
    }
    xc /= npixels;
    yc /= npixels;

    for (g = 0; g < ngrains; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        GwyXYZ *xyz = tc->xyz;
        guint ncoords = tc->ncoords;

        for (i = 0; i < ncoords; i++) {
            xyz[i].x -= xc;
            xyz[i].y -= yc;
        }
    }

    g_free(sizes);
    g_free(grains);

    *pxc = xc;
    *pyc = yc;

    return terracecoords;
}

static gint*
make_term_powers_except0(gint poly_degree, gint *pnterms)
{
    gint nterms = (poly_degree + 1)*(poly_degree + 2)/2 - 1;
    gint *term_powers = g_new(gint, 2*nterms);
    gint i, j, k;

    for (i = k = 0; i <= poly_degree; i++) {
        for (j = 0; j <= poly_degree - i; j++) {
            if (i || j) {
                term_powers[k++] = i;
                term_powers[k++] = j;
            }
        }
    }

    *pnterms = nterms;
    return term_powers;
}

static guint
find_maximum_power(guint npowers, const gint *term_powers)
{
    guint k, maxpower;

    maxpower = 0;
    for (k = 0; k < 2*npowers; k++)
        maxpower = MAX(maxpower, term_powers[k]);

    return maxpower;
}

/* Diagonal power-power matrix block.  Some of the entries could be
 * calculated from the per-terrace averages; the higher powers are only
 * used here though.  This is the slow part.  */
static gdouble*
calculate_power_matrix_block(GArray *terracecoords,
                             guint npowers, const gint *term_powers)
{
    guint maxpower, nterraces, kp, mp;
    gdouble *power_block;

    /* We multiply two powers together so the maximum power in the product is
     * twice the single maximum power. */
    maxpower = 2*find_maximum_power(npowers, term_powers);
    nterraces = terracecoords->len;
    power_block = g_new0(gdouble, npowers*npowers);

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(terracecoords,maxpower,npowers,term_powers,power_block,nterraces)
#endif
    {
        gdouble *xpowers = g_new(gdouble, maxpower+1);
        gdouble *ypowers = g_new(gdouble, maxpower+1);
        gdouble *tpower_block = gwy_omp_if_threads_new0(power_block, npowers*npowers);
        guint gfrom = gwy_omp_chunk_start(nterraces);
        guint gto = gwy_omp_chunk_end(nterraces);
        guint m, k, g, i;

        xpowers[0] = ypowers[0] = 1.0;

        for (g = gfrom; g < gto; g++) {
            TerraceCoords *tc = &coord_index(terracecoords, g);
            GwyXYZ *xyz = tc->xyz;
            guint ncoords = tc->ncoords;

            for (i = 0; i < ncoords; i++) {
                gdouble x = xyz[i].x, y = xyz[i].y;

                for (k = 1; k <= maxpower; k++) {
                    xpowers[k] = xpowers[k-1]*x;
                    ypowers[k] = ypowers[k-1]*y;
                }

                for (k = 0; k < npowers; k++) {
                    for (m = 0; m <= k; m++) {
                        gint powx = term_powers[2*k + 0] + term_powers[2*m + 0];
                        gint powy = term_powers[2*k + 1] + term_powers[2*m + 1];
                        gdouble xp = xpowers[powx];
                        gdouble yp = ypowers[powy];
                        tpower_block[k*npowers + m] += xp*yp;
                    }
                }
            }
        }
        g_free(xpowers);
        g_free(ypowers);
        gwy_omp_if_threads_sum_double(power_block, tpower_block, npowers*npowers);
    }

    /* Redundant, but keep for simplicity. */
    for (kp = 0; kp < npowers; kp++) {
        for (mp = kp+1; mp < npowers; mp++)
            power_block[kp*npowers + mp] = power_block[mp*npowers + kp];
    }

    return power_block;
}

static void
free_fit_result(FitResult *fres)
{
    g_free(fres->solution);
    g_free(fres->invdiag);
    g_free(fres);
}

static void
calculate_residuum(GArray *terracecoords, FitResult *fres,
                   GwyDataField *residuum,
                   const gint *term_powers, guint npowers, guint maxpower,
                   gdouble *xpowers, gdouble *ypowers,
                   gboolean indep)
{
    const gdouble *solution = fres->solution, *solution_block;
    gdouble *resdata;
    guint g, i, k, nterraces = terracecoords->len, npixels;

    solution_block = solution + (indep ? nterraces : 2);
    gwy_data_field_clear(residuum);
    resdata = gwy_data_field_get_data(residuum);

    fres->msq = fres->deltares = 0.0;
    npixels = 0;
    for (g = 0; g < nterraces; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        GwyXYZ *xyz = tc->xyz;
        guint ncoords = tc->ncoords;
        gint ng = tc->level;
        gdouble z0 = (indep ? solution[g] : ng*solution[0] + solution[1]);
        gdouble ts = 0.0, toff = 0.0;

        for (i = 0; i < ncoords; i++) {
            gdouble x = xyz[i].x, y = xyz[i].y, z = xyz[i].z;
            gdouble s = z0;

            for (k = 1; k <= maxpower; k++) {
                xpowers[k] = xpowers[k-1]*x;
                ypowers[k] = ypowers[k-1]*y;
            }

            for (k = 0; k < npowers; k++) {
                gint powx = term_powers[2*k + 0];
                gint powy = term_powers[2*k + 1];
                gdouble xp = xpowers[powx];
                gdouble yp = ypowers[powy];
                s += xp*yp*solution_block[k];
            }
            s = z - s;
            resdata[tc->pixels[i]] = s;
            ts += s*s;
            toff += s;
        }
        tc->msq = ts/ncoords;
        tc->off = toff/ncoords;
        fres->msq += ts;
        fres->deltares += tc->off*tc->off * ncoords;
        npixels += ncoords;
    }
    fres->msq = sqrt(fres->msq/npixels);
    fres->deltares = sqrt(fres->deltares/npixels);
}

static FitResult*
fit_terraces_arbitrary(GArray *terracecoords,
                       const gint *term_powers, guint npowers,
                       const gdouble *power_block,
                       GwyDataField *residuum,
                       const gchar **message)
{
    gint g, k, nterraces, matn, matsize, npixels, maxpower;
    gdouble *mixed_block, *matrix, *invmat, *rhs, *xpowers, *ypowers;
    FitResult *fres;
    guint i, j;
    gboolean ok;

    fres = g_new0(FitResult, 1);
    nterraces = fres->nterrparam = fres->nterraces = terracecoords->len;
    fres->npowers = npowers;
    matn = nterraces + npowers;

    maxpower = find_maximum_power(npowers, term_powers);
    xpowers = g_new(gdouble, maxpower+1);
    ypowers = g_new(gdouble, maxpower+1);
    xpowers[0] = ypowers[0] = 1.0;

    /* Calculate the matrix by pieces, the put it together.  The terrace block
     * is identity matrix I so we do not need to compute it. */
    mixed_block = g_new0(gdouble, npowers*nterraces);
    rhs = fres->solution = g_new0(gdouble, nterraces + npowers);
    fres->invdiag = g_new0(gdouble, matn);

    /* Mixed off-diagonal power-terrace matrix block (we represent it as the
     * upper right block) and power block on the right hand side. */
    for (g = 0; g < nterraces; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        GwyXYZ *xyz = tc->xyz;
        guint ncoords = tc->ncoords;
        gdouble *mixed_row = mixed_block + g*npowers;
        gdouble *rhs_block = rhs + nterraces;

        for (i = 0; i < ncoords; i++) {
            gdouble x = xyz[i].x, y = xyz[i].y, z = xyz[i].z;

            for (k = 1; k <= maxpower; k++) {
                xpowers[k] = xpowers[k-1]*x;
                ypowers[k] = ypowers[k-1]*y;
            }

            for (k = 0; k < npowers; k++) {
                gint powx = term_powers[2*k + 0];
                gint powy = term_powers[2*k + 1];
                gdouble xp = xpowers[powx];
                gdouble yp = ypowers[powy];
                mixed_row[k] += xp*yp;
                rhs_block[k] += xp*yp*z;
            }
        }
    }

    /* Terrace block of right hand side. */
    npixels = 0;
    for (g = 0; g < nterraces; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        GwyXYZ *xyz = tc->xyz;
        guint ncoords = tc->ncoords;

        for (i = 0; i < ncoords; i++) {
            gdouble z = xyz[i].z;
            rhs[g] += z;
        }
        npixels += ncoords;
    }

    /* Construct the matrix. */
    matsize = (matn + 1)*matn/2;
    matrix = g_new(gdouble, matsize);
    gwy_debug("matrix (%u)", matn);
    for (i = 0; i < matn; i++) {
        for (j = 0; j <= i; j++) {
            gdouble t;

            if (i < nterraces && j < nterraces)
                t = (i == j)*(coord_index(terracecoords, i).ncoords);
            else if (j < nterraces)
                t = mixed_block[j*npowers + (i - nterraces)];
            else
                t = power_block[(i - nterraces)*npowers + (j - nterraces)];

            SLi(matrix, i, j) = t/npixels;
#ifdef DEBUG_MATRIX
            printf("% .2e ", t);
#endif
        }
#ifdef DEBUG_MATRIX
        printf("\n");
#endif
    }
    g_free(mixed_block);

    invmat = g_memdup(matrix, matsize*sizeof(gdouble));
    ok = gwy_math_choleski_decompose(matn, matrix);
    gwy_debug("decomposition: %s", ok ? "OK" : "FAIL");
    if (!ok) {
        *message = _("Fit failed");
        free_fit_result(fres);
        fres = NULL;
        goto finalise;
    }
    for (i = 0; i < matn; i++)
        rhs[i] /= npixels;

    gwy_math_choleski_solve(matn, matrix, rhs);

    if (residuum)
        calculate_residuum(terracecoords, fres, residuum, term_powers, npowers, maxpower, xpowers, ypowers, TRUE);

    ok = gwy_math_choleski_invert(matn, invmat);
    gwy_debug("inversion: %s", ok ? "OK" : "FAIL");
    if (!ok) {
        *message = _("Fit failed");
        free_fit_result(fres);
        fres = NULL;
        goto finalise;
    }
    for (i = 0; i < matn; i++)
        fres->invdiag[i] = SLi(invmat, i, i);

finalise:
    g_free(xpowers);
    g_free(ypowers);
    g_free(matrix);
    g_free(invmat);

    return fres;
}

static FitResult*
fit_terraces_same_step(GArray *terracecoords,
                       const gint *term_powers, guint npowers,
                       const gdouble *power_block,
                       GwyDataField *residuum,
                       const gchar **message)
{
    gint g, k, nterraces, matn, matsize, npixels, maxpower;
    gdouble *sheight_block, *offset_block, *matrix, *invmat, *rhs;
    gdouble *xpowers, *ypowers;
    gdouble stepstep, stepoff, offoff;
    FitResult *fres;
    guint i, j;
    gboolean ok;

    fres = g_new0(FitResult, 1);
    nterraces = fres->nterraces = terracecoords->len;
    fres->npowers = npowers;
    fres->nterrparam = 2;
    matn = 2 + npowers;

    maxpower = find_maximum_power(npowers, term_powers);
    xpowers = g_new(gdouble, maxpower+1);
    ypowers = g_new(gdouble, maxpower+1);
    xpowers[0] = ypowers[0] = 1.0;

    /* Calculate the matrix by pieces, the put it together.  */
    sheight_block = g_new0(gdouble, npowers);
    offset_block = g_new0(gdouble, npowers);
    rhs = fres->solution = g_new0(gdouble, matn);
    fres->invdiag = g_new0(gdouble, matn);

    /* Mixed two first upper right matrix rows and power block of right hand
     * side. */
    for (g = 0; g < nterraces; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        GwyXYZ *xyz = tc->xyz;
        guint ncoords = tc->ncoords;
        gint ng = tc->level;
        gdouble *rhs_block = rhs + 2;

        for (i = 0; i < ncoords; i++) {
            gdouble x = xyz[i].x, y = xyz[i].y, z = xyz[i].z;

            for (k = 1; k <= maxpower; k++) {
                xpowers[k] = xpowers[k-1]*x;
                ypowers[k] = ypowers[k-1]*y;
            }

            for (k = 0; k < npowers; k++) {
                gint powx = term_powers[2*k + 0];
                gint powy = term_powers[2*k + 1];
                gdouble xp = xpowers[powx];
                gdouble yp = ypowers[powy];
                sheight_block[k] += xp*yp*ng;
                offset_block[k] += xp*yp;
                rhs_block[k] += xp*yp*z;
            }
        }
    }

    /* Remaining three independent elements in the top left corner of the
     * matrix. */
    stepstep = stepoff = 0.0;
    npixels = 0;
    for (g = 0; g < nterraces; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        guint ncoords = tc->ncoords;
        gint ng = tc->level;

        /* Ensure ng does not converted to unsigned, with disasterous
         * consequences. */
        stepstep += ng*ng*(gdouble)ncoords;
        stepoff += ng*(gdouble)ncoords;
        npixels += ncoords;
    }
    offoff = npixels;

    /* Remaining first two elements of the right hand side. */
    for (g = 0; g < nterraces; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        GwyXYZ *xyz = tc->xyz;
        guint ncoords = tc->ncoords;
        gint ng = tc->level;

        for (i = 0; i < ncoords; i++) {
            gdouble z = xyz[i].z;
            rhs[0] += ng*z;
            rhs[1] += z;
        }
    }

    /* Construct the matrix. */
    matsize = (matn + 1)*matn/2;
    matrix = g_new(gdouble, matsize);

    gwy_debug("matrix (%u)", matn);
    SLi(matrix, 0, 0) = stepstep/npixels;
#ifdef DEBUG_MATRIX
    printf("% .2e\n", SLi(matrix, 0, 0));
#endif
    SLi(matrix, 1, 0) = stepoff/npixels;
    SLi(matrix, 1, 1) = offoff/npixels;
#ifdef DEBUG_MATRIX
    printf("% .2e % .2e\n", SLi(matrix, 1, 0), SLi(matrix, 1, 1));
#endif

    for (i = 2; i < matn; i++) {
        for (j = 0; j <= i; j++) {
            gdouble t;

            if (j == 0)
                t = sheight_block[i-2];
            else if (j == 1)
                t = offset_block[i-2];
            else
                t = power_block[(i - 2)*npowers + (j - 2)];

            SLi(matrix, i, j) = t/npixels;
#ifdef DEBUG_MATRIX
            printf("% .2e ", t);
#endif
        }
#ifdef DEBUG_MATRIX
        printf("\n");
#endif
    }
    g_free(sheight_block);
    g_free(offset_block);

    invmat = g_memdup(matrix, matsize*sizeof(gdouble));
    ok = gwy_math_choleski_decompose(matn, matrix);
    gwy_debug("decomposition: %s", ok ? "OK" : "FAIL");
    if (!ok) {
        *message = _("Fit failed");
        free_fit_result(fres);
        fres = NULL;
        goto finalise;
    }
    for (i = 0; i < matn; i++)
        rhs[i] /= npixels;

    gwy_math_choleski_solve(matn, matrix, rhs);

    if (residuum)
        calculate_residuum(terracecoords, fres, residuum, term_powers, npowers, maxpower, xpowers, ypowers, FALSE);

    ok = gwy_math_choleski_invert(matn, invmat);
    gwy_debug("inversion: %s", ok ? "OK" : "FAIL");
    if (!ok) {
        *message = _("Fit failed");
        free_fit_result(fres);
        fres = NULL;
        goto finalise;
    }

    /* Compensate division of all matrix elements by npixels. */
    for (i = 0; i < matsize; i++)
        invmat[i] /= npixels;
    for (i = 0; i < matn; i++)
        fres->invdiag[i] = SLi(invmat, i, i);

finalise:
    g_free(xpowers);
    g_free(ypowers);
    g_free(invmat);
    g_free(matrix);

    return fres;
}

static gboolean
estimate_step_parameters(const gdouble *heights, guint n,
                         gdouble *stepheight, gdouble *offset,
                         const gchar **message)
{
    gdouble *steps;
    gdouble sh, smin, bestoff, p;
    gint i, g, ns, noff = 120;

    if (n < 2) {
        *message = _("No suitable terrace steps found");
        return FALSE;
    }

    steps = g_memdup(heights, n*sizeof(gdouble));
    gwy_math_sort(n, steps);
    ns = n-1;
    for (g = 0; g < ns; g++) {
        steps[g] = steps[g+1] - steps[g];
        gwy_debug("step%d: height %g nm", g, steps[g]/1e-9);
    }

    p = 85.0;
    gwy_math_percentiles(ns, steps, GWY_PERCENTILE_INTERPOLATION_LINEAR, 1, &p, &sh);
    gwy_debug("estimated step height %g nm", sh/1e-9);
    g_free(steps);

    *stepheight = sh;

    /* Find a good offset value. */
    smin = G_MAXDOUBLE;
    bestoff = 0.0;
    for (i = 0; i < noff; i++) {
        gdouble off = sh*i/noff;
        gdouble s = 0.0;

        for (g = 0; g < n; g++) {
            gint ng = GWY_ROUND((heights[g] - off)/sh);

            s += fabs(heights[g] - off - ng*sh);
        }
        if (s < smin) {
            smin = s;
            bestoff = off;
        }
    }
    gwy_debug("estimated base offset %g nm", bestoff/1e-9);

    *offset = bestoff;

    return TRUE;
}

static void
fill_fitted_image(GwyDataField *field,
                  GwyDataField *marked,
                  GwyDataField *residuum,
                  GwyDataField *fitted)
{
    gint xres = gwy_data_field_get_xres(field);
    gint yres = gwy_data_field_get_yres(field);
    const gdouble *d = gwy_data_field_get_data_const(field);
    const gdouble *r = gwy_data_field_get_data_const(residuum);
    const gdouble *m = gwy_data_field_get_data_const(marked);
    gdouble *f;
    gint n = xres*yres, k;
    gdouble avg;

    avg = gwy_data_field_get_avg(field);
    gwy_data_field_fill(fitted, avg);

    f = gwy_data_field_get_data(fitted);
    for (k = 0; k < n; k++) {
        if (m[k] > 0.0)
            f[k] = d[k] - r[k];
    }
}

static void
terrace_fit(ModuleGUI *gui)
{
    GwyDataField *marked, *fitted, *residuum, *background, *levelled, *terraces;
    GtkTreeModel *model;
    ModuleArgs *args = gui->args;
    FitResult *fres;
    const gchar *message = "";

    if (!gui->terracecoords)
        return;

    gwy_app_wait_cursor_start(GTK_WINDOW(gui->dialog));
    gui->fit_ok = FALSE;

    marked = args->result[PREVIEW_DATA];
    fitted = args->result[PREVIEW_FITTED];
    residuum = args->result[PREVIEW_RESIDUUM];
    terraces = args->result[PREVIEW_TERRACES];
    levelled = args->result[PREVIEW_LEVELLED];
    background = args->result[PREVIEW_BACKGROUND];

    gwy_param_table_set_sensitive(gui->table_param, PARAM_FIT_REPORT_STYLE, FALSE);
    gwy_param_table_set_sensitive(gui->table_terraces, PARAM_TERRACE_REPORT_STYLE, FALSE);
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(gui->terracelist));
    fres = terrace_do(marked, residuum, background, terraces, gui->terracecoords, gui->terraceinfo,
                      args->params, gui->xc, gui->yc, TRUE, &message);
    gwy_param_table_set_label(gui->table_param, LABEL_FIT_RESULT, message);
    update_results(gui, fres);
    gui->fit_ok = !!fres;
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, gui->fit_ok);
    if (!gui->fit_ok) {
        reset_images(gui);
        gwy_app_wait_cursor_finish(GTK_WINDOW(gui->dialog));
        return;
    }

    gwy_param_table_set_label(gui->table_param, LABEL_FIT_RESULT, "");
    gwy_param_table_set_sensitive(gui->table_param, PARAM_FIT_REPORT_STYLE, TRUE);
    gwy_param_table_set_sensitive(gui->table_terraces, PARAM_TERRACE_REPORT_STYLE, TRUE);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));

    /* Rerender the terrace table. */
    gwy_null_store_set_n_rows(GWY_NULL_STORE(model), 0);
    gwy_null_store_set_n_rows(GWY_NULL_STORE(model), fres->nterraces);

    free_fit_result(fres);
    fill_fitted_image(args->field, marked, residuum, fitted);
    gwy_data_field_subtract_fields(levelled, args->field, background);

    update_diff_gradient(gui);
    gwy_data_field_data_changed(fitted);
    gwy_data_field_data_changed(residuum);
    gwy_data_field_data_changed(terraces);
    gwy_data_field_data_changed(levelled);
    gwy_data_field_data_changed(background);

    gwy_app_wait_cursor_finish(GTK_WINDOW(gui->dialog));
}

static void
run_segmentation(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyDataField *marked, *terraceids;
    GtkTreeModel *model;
    GArray *terracecoords, *terraceinfo;
    guint g, nterraces;

    gui->fit_ok = FALSE;
    free_terrace_coordinates(gui->terracecoords);
    gui->terracecoords = NULL;
    terraceinfo = gui->terraceinfo;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, FALSE);

    marked = gwy_container_get_object_by_name(gui->data, "/0/mask");
    terraceids = args->result[PREVIEW_SEGMENTED];

    gwy_param_table_set_sensitive(gui->table_param, PARAM_FIT_REPORT_STYLE, FALSE);
    gwy_param_table_set_sensitive(gui->table_terraces, PARAM_TERRACE_REPORT_STYLE, FALSE);
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(gui->terracelist));
    gwy_null_store_set_n_rows(GWY_NULL_STORE(model), 0);
    g_array_set_size(terraceinfo, 0);
    terracecoords = find_terrace_coordinates(args->field, args->mask, args->params,
                                             marked, terraceids, &gui->xc, &gui->yc);
    gui->terracecoords = terracecoords;

    if (terracecoords) {
        nterraces = terracecoords->len;
        gwy_param_table_set_label(gui->table_param, LABEL_FIT_RESULT, "");
        for (g = 0; g < nterraces; g++) {
            TerraceCoords *tc = &coord_index(terracecoords, g);
            TerraceInfo info;

            gwy_clear(&info, 1);
            info.npixels = tc->ncoords;
            g_array_append_val(terraceinfo, info);
        }
        gwy_null_store_set_n_rows(GWY_NULL_STORE(model), nterraces);
    }
    else
        gwy_param_table_set_label(gui->table_param, LABEL_FIT_RESULT, _("No terraces were found"));

    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_REFINE, !!terracecoords);

    update_results(gui, NULL);
    update_terrace_colours(gui);
    gwy_data_field_data_changed(marked);
    gwy_data_field_data_changed(terraceids);
    reset_images(gui);
}

static void
update_results(ModuleGUI *gui, FitResult *fres)
{
    ModuleArgs *args = gui->args;
    GwyResults *results = gui->results;
    GwyDataField *mask = args->mask;
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    gboolean independent = gwy_params_get_boolean(args->params, PARAM_INDEPENDENT);

    if (!gui->terracecoords) {
        gwy_param_table_results_clear(gui->table_param, WIDGET_RESULTS);
        return;
    }

    gwy_results_fill_values(gui->results, "masking", masking, NULL);
    gwy_results_fill_values(results, "nterraces", gui->terracecoords->len, NULL);

    if (fres) {
        if (independent)
            gwy_results_set_na(results, "step", "discrep", NULL);
        else {
            gwy_results_fill_values_with_errors(results,
                                                "step", fres->solution[0], sqrt(fres->invdiag[0])*fres->msq,
                                                NULL);
            gwy_results_fill_values(results, "discrep", fres->deltares, NULL);
        }
        gwy_results_fill_values(results, "resid", fres->msq, NULL);
    }

    gwy_param_table_results_fill(gui->table_param, WIDGET_RESULTS);
}

static void
update_terrace_colours(ModuleGUI *gui)
{
    GArray *terraceinfo = gui->terraceinfo;
    guint g, nterraces = terraceinfo->len;
    GwyGradient *gradient;

    gradient = gwy_inventory_get_item_or_default(gwy_gradients(), "DFit");
    g_return_if_fail(gradient);

    for (g = 0; g < nterraces; g++) {
        TerraceInfo *info = &info_index(terraceinfo, g);
        gwy_gradient_get_color(gradient, (g + 1.0)/nterraces, &info->colour);
    }
}

static void
fill_terraces(GwyDataField *terraces, GwyDataField *marked,
              GArray *terracecoords,
              const gdouble *sheights, gboolean independent)
{
    GwyDataField *mask;
    guint i, g, nterraces, xres, yres, ng;
    gint minlevel = 0;
    gint *grains;
    gdouble *d, *zmap;

    nterraces = terracecoords->len;
    if (!independent) {
        minlevel = G_MAXINT;
        for (g = 0; g < nterraces; g++) {
            TerraceCoords *tc = &coord_index(terracecoords, g);
            minlevel = MIN(minlevel, tc->level);
        }
    }

    mask = gwy_data_field_duplicate(marked);
    xres = gwy_data_field_get_xres(mask);
    yres = gwy_data_field_get_yres(mask);
    gwy_data_field_grains_grow(mask, 25, GWY_DISTANCE_TRANSFORM_EUCLIDEAN, TRUE);
    gwy_data_field_grains_grow(mask, 25, GWY_DISTANCE_TRANSFORM_EUCLIDEAN, TRUE);
    gwy_data_field_grains_grow(mask, 25, GWY_DISTANCE_TRANSFORM_EUCLIDEAN, TRUE);
    gwy_data_field_grains_grow(mask, 25, GWY_DISTANCE_TRANSFORM_EUCLIDEAN, TRUE);

    grains = g_new0(gint, xres*yres);
    ng = gwy_data_field_number_grains(mask, grains);
    zmap = g_new(gdouble, ng+1);
    zmap[0] = 0.0;

    for (g = 0; g < nterraces; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);

        i = grains[tc->pixels[0]];
        zmap[i] = (independent ? sheights[g] : (tc->level+1 - minlevel)*sheights[0]);
    }

    gwy_data_field_clear(terraces);
    d = gwy_data_field_get_data(terraces);
    for (i = 0; i < xres*yres; i++) {
        d[i] = zmap[grains[i]];
    }
    g_free(grains);

    gwy_data_field_laplace_solve(terraces, mask, 0, 1.0);
    g_object_unref(mask);
}

/* XXX: The background is generally bogus far outside the fitted region.  This usually means image corners because
 * they contain too small terrace bits. It is more meaningful to only calculate it for marked area. */
static void
fill_background(GwyDataField *background,
                const gint *term_powers, guint npowers,
                const gdouble *coeffs, gdouble xc, gdouble yc)
{
    gint maxpower, xres, yres, i, j, k;
    gdouble *xpowers, *ypowers, *d;

    maxpower = find_maximum_power(npowers, term_powers);
    xpowers = g_new(gdouble, maxpower+1);
    ypowers = g_new(gdouble, maxpower+1);
    xpowers[0] = ypowers[0] = 1.0;

    xres = gwy_data_field_get_xres(background);
    yres = gwy_data_field_get_yres(background);
    d = gwy_data_field_get_data(background);
    for (i = 0; i < yres; i++) {
        gdouble y = (2.0*i + 1 - yres)/(yres - 1) - yc;
        for (j = 0; j < xres; j++) {
            gdouble x = (2.0*j + 1 - xres)/(xres - 1) - xc;
            gdouble s = 0.0;

            for (k = 1; k <= maxpower; k++) {
                xpowers[k] = xpowers[k-1]*x;
                ypowers[k] = ypowers[k-1]*y;
            }

            for (k = 0; k < npowers; k++) {
                gint powx = term_powers[2*k + 0];
                gint powy = term_powers[2*k + 1];
                gdouble xp = xpowers[powx];
                gdouble yp = ypowers[powy];
                s += xp*yp*coeffs[k];
            }
            d[i*xres + j] = s;
        }
    }
    g_free(xpowers);
    g_free(ypowers);
}

static gboolean
analyse_topology(GArray *terracecoords, GwyParams *params, GwyDataField *terraces,
                 const gdouble *heights, gdouble sheight)
{
    guint xres, yres, nterraces, g, gg, i, j, k, kk, nreached;
    gdouble edge_kernel_size = gwy_params_get_double(params, PARAM_EDGE_KERNEL_SIZE);
    gdouble edge_broadening = gwy_params_get_double(params, PARAM_EDGE_BROADENING);
    gint maxdist2, ldiff;
    GArray **boundaries;
    guint *ids, *neighcounts, *neighter;
    gboolean *reached = NULL;
    gboolean ok = FALSE;

    nterraces = terracecoords->len;
    xres = gwy_data_field_get_xres(terraces);
    yres = gwy_data_field_get_yres(terraces);

    /* Find boundary pixels of all terraces. */
    ids = g_new0(guint, xres*yres);
    for (g = 0; g < nterraces; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        guint ncoords = tc->ncoords;

        for (k = 0; k < ncoords; k++)
            ids[tc->pixels[k]] = g+1;
    }

    boundaries = g_new0(GArray*, nterraces);
    for (g = 0; g < nterraces; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        guint ncoords = tc->ncoords;

        boundaries[g] = g_array_new(FALSE, FALSE, sizeof(guint));
        for (k = 0; k < ncoords; k++) {
            kk = tc->pixels[k];
            i = kk/xres;
            j = kk % xres;
            if ((i > 0 && ids[kk-xres] != g+1)
                || (j > 0 && ids[kk-1] != g+1)
                || (j < xres-1 && ids[kk+1] != g+1)
                || (i < yres-1 && ids[kk+xres] != g+1)) {
                g_array_append_val(boundaries[g], i);
                g_array_append_val(boundaries[g], j);
            }
        }
        gwy_debug("terrace #%u has %u boundary pixels", g, boundaries[g]->len/2);
    }

    g_free(ids);

    /* Go through all pairs of terraces and check if their have pixels which are sufficiently close.  We base the
     * criterion on kernel size and edge broadening as they give natural neighbour terrace separation. */
    maxdist2 = GWY_ROUND(gwy_powi(2.0*(edge_kernel_size + edge_broadening), 2) + 0.5*log(xres*yres));
    neighcounts = g_new0(guint, nterraces*nterraces);
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(nterraces,boundaries,maxdist2,neighcounts) \
            private(k,g,gg)
#endif
    for (k = 0; k < (nterraces-1)*nterraces/2; k++) {
        guint nb, nb2, ib, ib2, n;
        guint *b, *b2;

        g = (guint)floor(0.5*(sqrt(8*k+1) + 1) + 0.00001);
        gg = k - g*(g-1)/2;

        nb = boundaries[g]->len/2;
        nb2 = boundaries[gg]->len/2;
        b = &g_array_index(boundaries[g], guint, 0);
        n = 0;

        for (ib = 0; ib < nb; ib++) {
            gint xb1, yb1;

            yb1 = *(b++);
            xb1 = *(b++);
            b2 = &g_array_index(boundaries[gg], guint, 0);
            for (ib2 = 0; ib2 < nb2; ib2++) {
                gint dxb2, dyb2;

                dyb2 = *(b2++) - yb1;
                dxb2 = *(b2++) - xb1;

                if (dxb2*dxb2 + dyb2*dyb2 <= maxdist2)
                    n++;
            }
        }
        if (n < sqrt(fmin(nb, nb2)))
            continue;

        neighcounts[g*nterraces + gg] = neighcounts[gg*nterraces + g] = n;
    }

    for (g = 0; g < nterraces; g++)
        g_array_free(boundaries[g], TRUE);
    g_free(boundaries);

    /* Here comes the difficult part.  Make a consistent guess which terrace
     * is at what level based on relations to neighbours. */
    neighter = g_new0(guint, nterraces);
    for (g = 0; g < nterraces; g++) {
        for (gg = 0; gg < nterraces; gg++) {
            if (neighcounts[g*nterraces + gg]) {
                neighter[g]++;
                gwy_debug("%u and %u are neighbours (%u), level diff %d (%g nm)",
                          g+1, gg+1, neighcounts[g*nterraces + gg],
                          GWY_ROUND((heights[gg] - heights[g])/sheight), (heights[gg] - heights[g])/1e-9);
            }
        }
    }

    /* Find a terrace with the most neighbours. */
    g = 0;
    k = 0;
    for (gg = 0; gg < nterraces; gg++) {
        if (neighter[gg] > k) {
            k = neighter[gg];
            g = gg;
        }
    }
    if (!k) {
        /* Nothing is a neigbour of anything else.  So we cannot proceed. */
        gwy_debug("no neighbours");
        goto fail;
    }

    reached = g_new0(gboolean, nterraces);
    reached[g] = TRUE;
    nreached = 1;
    coord_index(terracecoords, g).level = 0;

    while (nreached < nterraces) {
        gboolean did_anything = FALSE;

        for (g = 0; g < nterraces; g++) {
            TerraceCoords *tc = &coord_index(terracecoords, g);
            for (gg = 0; gg < nterraces; gg++) {
                TerraceCoords *tc2 = &coord_index(terracecoords, gg);

                if (!reached[g] || reached[gg] || !neighcounts[g*nterraces + gg])
                    continue;

                reached[gg] = TRUE;
                ldiff = GWY_ROUND((heights[gg] - heights[g])/sheight);
                tc2->level = tc->level + ldiff;
                gwy_debug("%u level is %d, based on connection to %u (%d)", gg+1, tc2->level, g+1, tc->level);
                nreached++;
                did_anything = TRUE;
            }
        }

        if (!did_anything) {
            /* The graph is not connected.  We could perhaps still proceed,
             * but for now just give up.  */
            gwy_debug("neighbour graph is not connected.");
            goto fail;
        }

        for (g = 0; g < nterraces; g++) {
            TerraceCoords *tc = &coord_index(terracecoords, g);
            for (gg = 0; gg < nterraces; gg++) {
                TerraceCoords *tc2 = &coord_index(terracecoords, gg);

                if (!reached[g] || !reached[gg] || !neighcounts[g*nterraces + gg])
                    continue;

                ldiff = GWY_ROUND((heights[gg] - heights[g])/sheight);
                if (tc2->level != tc->level + ldiff) {
                    gwy_debug("inconsistent level differences");
                    gwy_debug("%u level should be %d, based on connection to %u (%d), but it is %d",
                              gg+1, tc->level + ldiff, g+1, tc->level, tc2->level);
                    goto fail;
                }
            }
        }
    }
    gwy_debug("level assignment OK");
    ok = TRUE;

fail:
    g_free(reached);
    g_free(neighter);
    g_free(neighcounts);

    return ok;
}

static FitResult*
terrace_do(GwyDataField *marked, GwyDataField *residuum,
           GwyDataField *background, GwyDataField *terraces,
           GArray *terracecoords, GArray *terraceinfo,
           GwyParams *params,
           gdouble xc, gdouble yc,
           gboolean fill_bg_and_terraces,
           const gchar **message)
{
    gint poly_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE);
    gboolean independent = gwy_params_get_boolean(params, PARAM_INDEPENDENT);
    guint nterraces, npowers;
    FitResult *fres;
    gdouble sheight, offset;
    gint *term_powers;
    gdouble *power_block;
    gint g;

    nterraces = terracecoords->len;
    if (!nterraces) {
        *message = _("No terraces were found");
        return NULL;
    }

    term_powers = make_term_powers_except0(poly_degree, &npowers);
    power_block = calculate_power_matrix_block(terracecoords, npowers, term_powers);
    if (!(fres = fit_terraces_arbitrary(terracecoords, term_powers, npowers, power_block, independent ? residuum : NULL,
                                        message)))
        goto end;
    if (!estimate_step_parameters(fres->solution, nterraces, &sheight, &offset, message)) {
        free_fit_result(fres);
        fres = NULL;
        goto end;
    }

    if (!analyse_topology(terracecoords, params, terraces, fres->solution, sheight)) {
        gwy_debug("assigning levels by plain rounding");
        for (g = 0; g < nterraces; g++) {
            TerraceCoords *tc = &coord_index(terracecoords, g);
            tc->level = GWY_ROUND((fres->solution[g] - offset)/sheight);
        }
    }
    for (g = 0; g < nterraces; g++) {
        TerraceCoords *tc = &coord_index(terracecoords, g);
        TerraceInfo *info = &info_index(terraceinfo, g);

        /* This does not depend on whether we run the second stage fit. */
        info->level = tc->level;
        info->height = fres->solution[g];
        /* This will be recalculated in the second stage fit.  Note that error
         * is anyway with respect to the multiple of estimated step height
         * and normally similar in both fit types. */
        info->error = fres->solution[g] - offset - tc->level*sheight;
        info->residuum = sqrt(tc->msq);
    }

    /* Normally also perform the second stage fitting with a single common
     * step height.  But if requested, avoid it, keeping the heights
     * independents. */
    if (!independent) {
        free_fit_result(fres);
        if (!(fres = fit_terraces_same_step(terracecoords, term_powers, npowers, power_block,
                                            independent ? NULL : residuum,
                                            message)))
            goto end;

        for (g = 0; g < nterraces; g++) {
            TerraceCoords *tc = &coord_index(terracecoords, g);
            TerraceInfo *info = &info_index(terraceinfo, g);

            info->error = tc->off;
            info->residuum = sqrt(tc->msq);
        }
    }
    if (fill_bg_and_terraces) {
        fill_background(background, term_powers, npowers, fres->solution + (independent ? nterraces : 2), xc, yc);
        fill_terraces(terraces, marked, terracecoords, fres->solution, independent);
    }

end:
    g_free(power_block);
    g_free(term_powers);

    return fres;
}

static gchar*
format_report(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyResultsReportType report_style = gwy_params_get_report_type(args->params, PARAM_TERRACE_REPORT_STYLE);
    GArray *terraceinfo = gui->terraceinfo;
    GwySIUnitFormatStyle style = GWY_SI_UNIT_FORMAT_UNICODE;
    GwySIValueFormat *vfz;
    GwySIUnit *zunit;
    GString *text;
    const gchar *k_header, *Apx_header;
    gchar *h_header, *Delta_header, *r_header;
    guint g, nterraces;

    text = g_string_new(NULL);
    zunit = gwy_data_field_get_si_unit_z(args->field);
    if (!(report_style & GWY_RESULTS_REPORT_MACHINE))
        vfz = gwy_si_unit_value_format_copy(gui->vf);
    else
        vfz = gwy_si_unit_get_format_for_power10(zunit, style, 0, NULL);

    h_header = g_strdup_printf("h [%s]", vfz->units);
    k_header = "k";
    Apx_header = "Apx";
    Delta_header = g_strdup_printf("Δ [%s]", vfz->units);
    r_header = g_strdup_printf("r [%s]", vfz->units);
    gwy_format_result_table_strings(text, report_style, 5,
                                    h_header, k_header, Apx_header, Delta_header, r_header);
    g_free(h_header);
    g_free(Delta_header);
    g_free(r_header);

    nterraces = terraceinfo->len;
    for (g = 0; g < nterraces; g++) {
        TerraceInfo *info = &info_index(terraceinfo, g);

        gwy_format_result_table_mixed(text, report_style, "viivv",
                                      info->height/vfz->magnitude,
                                      info->level,
                                      info->npixels,
                                      info->error/vfz->magnitude,
                                      info->residuum/vfz->magnitude);
    }

    gwy_si_unit_value_format_free(vfz);

    return g_string_free(text, FALSE);
}

static gdouble
interpolate_broadening(gdouble a, gdouble b, gdouble t)
{
    return pow((1.0 - t)*pow(a, pwr) + t*pow(b, pwr), 1.0/pwr);
}

static guint
prepare_survey(GwyParams *params, GArray *degrees, GArray *broadenings)
{
    gint min_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE_MIN);
    gint max_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE_MAX);
    gdouble min_broadening = gwy_params_get_double(params, PARAM_BROADENING_MIN);
    gdouble max_broadening = gwy_params_get_double(params, PARAM_BROADENING_MAX);
    guint i, ndegrees, nbroadenings;

    if (!gwy_params_get_boolean(params, PARAM_SURVEY_POLY))
        min_degree = max_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE);
    if (!gwy_params_get_boolean(params, PARAM_SURVEY_BROADENING))
        min_broadening = max_broadening = gwy_params_get_double(params, PARAM_EDGE_BROADENING);

    ndegrees = max_degree+1 - min_degree;
    nbroadenings = GWY_ROUND(2.0*(pow(max_broadening, pwr) - pow(min_broadening, pwr))) + 1;

    if (degrees) {
        g_array_set_size(degrees, ndegrees);
        for (i = 0; i < ndegrees; i++)
            g_array_index(degrees, gint, i) = min_degree + i;
    }
    if (broadenings) {
        g_array_set_size(broadenings, nbroadenings);
        for (i = 0; i < nbroadenings; i++) {
            gdouble t = (nbroadenings == 1 ? 0.5 : i/(nbroadenings - 1.0));
            g_array_index(broadenings, gdouble, i) = interpolate_broadening(min_broadening, max_broadening, t);
        }
    }

    return nbroadenings*ndegrees;
}

static void
run_survey(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyDataField *marked, *residuum, *terraces, *terraceids, *field = args->field, *mask = args->mask;
    GwyParams *surveyparams = gwy_params_duplicate(args->params);
    GwyResultsReportType report_style = gwy_params_get_report_type(surveyparams, PARAM_TERRACE_REPORT_STYLE);
    GArray *terracecoords = NULL, *degrees, *broadenings, *terraceinfo, *surveyout;
    guint i, w, ndegrees, nbroadenings, totalwork;
    const gchar *message;
    FitResult *fres;
    GString *str;
    gdouble xc, yc;

    report_style |= GWY_RESULTS_REPORT_MACHINE;
    marked = gwy_data_field_new_alike(field, FALSE);
    terraceids = gwy_data_field_new_alike(field, FALSE);
    residuum = gwy_data_field_new_alike(field, FALSE);
    terraces = gwy_data_field_new_alike(field, FALSE);

    terraceinfo = g_array_new(FALSE, FALSE, sizeof(TerraceInfo));
    g_array_append_vals(terraceinfo, gui->terraceinfo->data, gui->terraceinfo->len);
    surveyout = g_array_new(FALSE, FALSE, sizeof(TerraceSurveyRow));

    degrees = g_array_new(FALSE, FALSE, sizeof(gint));
    broadenings = g_array_new(FALSE, FALSE, sizeof(gdouble));
    totalwork = prepare_survey(surveyparams, degrees, broadenings);
    ndegrees = degrees->len;
    nbroadenings = broadenings->len;

    gwy_app_wait_start(GTK_WINDOW(gui->dialog), _("Fitting in progress..."));

    /* We only want to re-run segmentation when broadening changes.  This means we must have broadening (or any other
     * segmentation parameter) in the outer cycle and polynomial degree as the inner cycle! */
    for (w = 0; w < totalwork; w++) {
        TerraceSurveyRow srow;

        gwy_params_set_int(surveyparams, PARAM_POLY_DEGREE, g_array_index(degrees, gint, w % ndegrees));
        gwy_params_set_double(surveyparams, PARAM_EDGE_BROADENING, g_array_index(broadenings, gdouble, w/ndegrees));
        if (w/nbroadenings != (w-1)/nbroadenings) {
            free_terrace_coordinates(terracecoords);
            terracecoords = find_terrace_coordinates(field, mask, surveyparams, marked, terraceids, &xc, &yc);
        }
        fres = terrace_do(marked, residuum, NULL, terraces, terracecoords, terraceinfo, surveyparams, xc, yc, FALSE,
                          &message);

        gwy_clear(&srow, 1);
        srow.poly_degree = gwy_params_get_int(surveyparams, PARAM_POLY_DEGREE);
        srow.edge_kernel_size = gwy_params_get_double(surveyparams, PARAM_EDGE_KERNEL_SIZE);
        srow.edge_threshold = gwy_params_get_double(surveyparams, PARAM_EDGE_THRESHOLD);
        srow.edge_broadening = gwy_params_get_double(surveyparams, PARAM_EDGE_BROADENING);
        srow.min_area_frac = gwy_params_get_double(surveyparams, PARAM_MIN_AREA_FRAC);
        srow.fit_ok = !!fres;
        if (fres) {
            srow.nterraces = fres->nterraces;
            srow.step = fres->solution[0];
            srow.step_err = sqrt(fres->invdiag[0])*fres->msq;
            srow.msq = fres->msq;
            srow.discrep = fres->deltares;
        }
        g_array_append_val(surveyout, srow);
        free_fit_result(fres);

        if (!gwy_app_wait_set_fraction((w + 1.0)/totalwork))
            break;
    }

    gwy_app_wait_finish();

    free_terrace_coordinates(terracecoords);
    g_array_free(degrees, TRUE);
    g_array_free(broadenings, TRUE);
    g_array_free(terraceinfo, TRUE);
    g_object_unref(terraces);
    g_object_unref(residuum);
    g_object_unref(marked);
    g_object_unref(terraceids);
    g_object_unref(surveyparams);

    if (w != totalwork) {
        g_array_free(surveyout, TRUE);
        return;
    }

    str = g_string_new(NULL);
    gwy_format_result_table_strings(str, report_style, 11,
                                    "Poly degree", "Edge kernel size", "Edge threshold", "Edge broadening",
                                    "Min area frac", "Fit OK", "Num terraces", "Step height", "Step height err",
                                    "Msq residual", "Discrepancy");
    for (i = 0; i < surveyout->len; i++) {
        TerraceSurveyRow *srow = &g_array_index(surveyout, TerraceSurveyRow, i);
        gwy_format_result_table_mixed(str, report_style, "ivvvvyivvvv",
                                      srow->poly_degree,
                                      srow->edge_kernel_size, srow->edge_threshold, srow->edge_broadening,
                                      srow->min_area_frac,
                                      srow->fit_ok,
                                      srow->nterraces, srow->step, srow->step_err, srow->msq, srow->discrep);
    }
    g_array_free(surveyout, TRUE);

    gwy_save_auxiliary_data(_("Save Terrace Fit Survey"), GTK_WINDOW(gui->dialog), str->len, str->str);
    g_string_free(str, TRUE);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gint min_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE_MIN);
    gint max_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE_MAX);
    gdouble min_broadening = gwy_params_get_double(params, PARAM_BROADENING_MIN);
    gdouble max_broadening = gwy_params_get_double(params, PARAM_BROADENING_MAX);

    if (min_degree > max_degree) {
        gwy_params_set_int(params, PARAM_POLY_DEGREE_MIN, max_degree);
        gwy_params_set_int(params, PARAM_POLY_DEGREE_MAX, min_degree);
    }
    if (min_broadening > max_broadening) {
        gwy_params_set_double(params, PARAM_BROADENING_MIN, max_broadening);
        gwy_params_set_double(params, PARAM_BROADENING_MAX, min_broadening);
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
