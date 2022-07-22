/*
 *  $Id: measure_lattice.c 24754 2022-03-29 14:19:00Z yeti-dn $
 *  Copyright (C) 2015-2022 David Necas (Yeti).
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
#include <libgwyddion/gwythreads.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/correct.h>
#include <libprocess/simplefft.h>
#include <libprocess/inttrans.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

typedef enum {
    IMAGE_DATA,
    IMAGE_ACF,
    IMAGE_PSDF,
    IMAGE_NMODES
} ImageMode;

typedef enum {
    SELECTION_LATTICE,
    SELECTION_POINT,
    SELECTION_NMODES,
} SelectionMode;

enum {
    VALUE_A1_X = 0,
    VALUE_A1_Y,
    VALUE_A1,
    VALUE_PHI1,
    VALUE_A2_X,
    VALUE_A2_Y,
    VALUE_A2,
    VALUE_PHI2,
    VALUE_PHI,
    VALUE_NVALUES,
};

enum {
    PARAM_ZOOM_ACF,
    PARAM_ZOOM_PSDF,
    PARAM_ZOOM,
    PARAM_FIX_HACF,
    PARAM_MASKING,
    PARAM_IMAGE_MODE,
    PARAM_SELECTION_MODE,
    PARAM_SHOW_NUMBERS,
    PARAM_REPORT_STYLE,

    WIDGET_VECTORS,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    /* We always keep the direct-space selection here. */
    gboolean have_xy;
    gdouble xy[4];
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GtkWidget *dataview;
    GtkWidget *value_labels[VALUE_NVALUES];
    GwyParamTable *table;
    GwyResults *results;
    GwyContainer *data;
    GwyDataField *acf;
    GwyDataField *psdf;
    GwySIValueFormat *xyvf;
    GwySIValueFormat *phivf;
    gulong selection_id;
} ModuleGUI;

static gboolean         module_register       (void);
static GwyParamDef*     define_module_params  (void);
static void             measure_lattice       (GwyContainer *data,
                                               GwyRunType runtype);
static GwyDialogOutcome run_gui               (ModuleArgs *args,
                                               GwyContainer *data,
                                               gint id);
static GwyResults*      create_results        (ModuleArgs *args,
                                               GwyContainer *data,
                                               gint id);
static void             param_changed         (ModuleGUI *gui,
                                               gint id);
static void             dialog_response       (ModuleGUI *gui,
                                               gint response);
static void             preview               (gpointer user_data);
static GtkWidget*       create_lattice_table  (gpointer user_data);
static void             set_selection         (ModuleGUI *gui);
static gboolean         get_selection         (ModuleGUI *gui,
                                               gdouble *xy);
static void             calculate_acf         (ModuleGUI *gui);
static void             calculate_psdf        (ModuleGUI *gui);
static void             calculate_zoomed_field(ModuleGUI *gui);
static void             selection_changed     (ModuleGUI *gui);
static gboolean         transform_selection   (gdouble *xy);
static void             center_selection      (GwyDataField *field,
                                               gdouble *xy,
                                               gint n,
                                               gint sign);
static void             invert_matrix         (gdouble *dest,
                                               const gdouble *src);
static gdouble          matrix_det            (const gdouble *m);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Measures parameters of two-dimensional lattices."),
    "Yeti <yeti@gwyddion.net>",
    "3.2",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, measure_lattice)

static gboolean
module_register(void)
{
    gwy_process_func_register("measure_lattice",
                              (GwyProcessFunc)&measure_lattice,
                              N_("/Measure _Features/_Lattice..."),
                              GWY_STOCK_MEASURE_LATTICE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Measure lattice"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum image_modes[] = {
        { N_("_Data"), IMAGE_DATA, },
        { N_("_ACF"),  IMAGE_ACF,  },
        { N_("_PSDF"), IMAGE_PSDF, },
    };
    static const GwyEnum selection_modes[] = {
        { N_("_Lattice"), SELECTION_LATTICE, },
        { N_("_Vectors"), SELECTION_POINT,   },
    };
    static GwyEnum zooms[5];
    static GwyParamDef *paramdef = NULL;
    guint i;

    if (paramdef)
        return paramdef;

    for (i = 0; i < G_N_ELEMENTS(zooms); i++) {
        zooms[i].value = 1u << i;
        zooms[i].name = g_strdup_printf("%u×", zooms[i].value);
    }

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    /* Use two saved but invisible parameters and one visible but auxiliary to represent the zoom duality. */
    gwy_param_def_add_gwyenum(paramdef, PARAM_ZOOM_ACF, "zoom_acf", NULL, zooms, G_N_ELEMENTS(zooms), 1);
    gwy_param_def_add_gwyenum(paramdef, PARAM_ZOOM_PSDF, "zoom_psdf", NULL, zooms, G_N_ELEMENTS(zooms), 1);
    gwy_param_def_add_gwyenum(paramdef, PARAM_ZOOM, NULL, _("Zoom"), zooms, G_N_ELEMENTS(zooms), 1);
    gwy_param_def_add_boolean(paramdef, PARAM_FIX_HACF, "fix_hacf", _("Interpolate _horizontal ACF"), FALSE);
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_IMAGE_MODE, "image_mode", _("Display"),
                              image_modes, G_N_ELEMENTS(image_modes), IMAGE_DATA);
    gwy_param_def_add_gwyenum(paramdef, PARAM_SELECTION_MODE, "selection_mode", _("Show lattice as"),
                              selection_modes, G_N_ELEMENTS(selection_modes), SELECTION_LATTICE);
    gwy_param_def_add_boolean(paramdef, PARAM_SHOW_NUMBERS, "show_numbers", _("Show vector numbers"), FALSE);
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", _("Save Parameters"),
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON);
    return paramdef;
}

static void
measure_lattice(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    GwySelection *selection;
    ModuleArgs args;
    gchar *selkey;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    /* Replace the field with an adjusted one. */
    args.field = gwy_data_field_duplicate(args.field);
    gwy_data_field_add(args.field, -gwy_data_field_get_avg(args.field));
    gwy_data_field_set_xoffset(args.field, -0.5*gwy_data_field_get_xreal(args.field));
    gwy_data_field_set_yoffset(args.field, -0.5*gwy_data_field_get_yreal(args.field));

    /* Restore lattice from data if any is present. */
    selkey = g_strdup_printf("/%d/select/lattice", id);
    if (gwy_container_gis_object_by_name(data, selkey, &selection))
        args.have_xy = gwy_selection_get_object(selection, 0, args.xy);

    args.params = gwy_params_new_from_settings(define_module_params());
    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);

    /* Save lattice to data if any is present. */
    if (args.have_xy && outcome == GWY_DIALOG_HAVE_RESULT) {
        selection = g_object_new(g_type_from_name("GwySelectionLattice"), "max-objects", 1, NULL);
        gwy_selection_set_data(selection, 1, args.xy);
        gwy_container_set_object_by_name(data, selkey, selection);
        g_object_unref(selection);
    }

    g_free(selkey);
    g_object_unref(args.params);
    g_object_unref(args.field);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox;
    GwyDialog *dialog;
    GwyDialogOutcome outcome;
    GwyParamTable *table;
    ModuleGUI gui;
    gint i;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.results = create_results(args, data, id);
    gui.xyvf = gwy_data_field_get_value_format_xy(args->field, GWY_SI_UNIT_FORMAT_MARKUP, NULL);
    gui.xyvf->precision += 2;

    gui.phivf = gwy_si_unit_value_format_new(G_PI/180.0, 2, _("deg"));
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(IMAGE_DATA), args->field);
    gui.acf = gwy_data_field_new_alike(args->field, FALSE);
    gui.psdf = gwy_data_field_new_alike(args->field, FALSE);
    for (i = 0; i < IMAGE_NMODES; i++)
        gwy_app_sync_data_items(data, gui.data, id, i, FALSE, GWY_DATA_ITEM_PALETTE, GWY_DATA_ITEM_REAL_SQUARE, 0);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE, GWY_DATA_ITEM_RANGE_TYPE, GWY_DATA_ITEM_RANGE, 0);
    gwy_container_set_enum(gui.data, gwy_app_get_data_range_type_key_for_id(IMAGE_ACF), GWY_LAYER_BASIC_RANGE_AUTO);
    gwy_container_set_enum(gui.data, gwy_app_get_data_range_type_key_for_id(IMAGE_PSDF), GWY_LAYER_BASIC_RANGE_AUTO);
    gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(IMAGE_PSDF), "DFit");

    gui.dialog = gwy_dialog_new(_("Measure Lattice"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, 0);
    gtk_dialog_add_button(GTK_DIALOG(dialog), gwy_sgettext("verb|_Estimate"), RESPONSE_ESTIMATE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Refine"), RESPONSE_REFINE);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_OK, 0);

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    /* Just for bootstrapping.  It will be actually set up properly in the initial param_changed(). */
    gwy_create_preview_vector_layer(GWY_DATA_VIEW(gui.dataview), 0, "Point", 2, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_IMAGE_MODE);
    gwy_param_table_append_radio_row(table, PARAM_ZOOM);
    gwy_param_table_append_radio(table, PARAM_SELECTION_MODE);
    gwy_param_table_append_checkbox(table, PARAM_SHOW_NUMBERS);
    gwy_param_table_append_separator(table);
    if (args->mask)
        gwy_param_table_append_combo(table, PARAM_MASKING);
    gwy_param_table_append_checkbox(table, PARAM_FIX_HACF);
    gwy_param_table_append_header(table, -1, _("Lattice Vectors"));
    gwy_param_table_append_foreign(table, WIDGET_VECTORS, create_lattice_table, &gui, NULL);
    gwy_param_table_append_report(table, PARAM_REPORT_STYLE);
    gwy_param_table_report_set_results(table, PARAM_REPORT_STYLE, gui.results);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);
    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    gwy_si_unit_value_format_free(gui.xyvf);
    gwy_si_unit_value_format_free(gui.phivf);
    g_object_unref(gui.data);
    g_object_unref(gui.results);
    g_object_unref(gui.acf);
    g_object_unref(gui.psdf);

    return outcome;
}

static GwyResults*
create_results(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyResults *results = gwy_results_new();

    gwy_results_add_header(results, N_("Measure Lattice"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "image", N_("Image"));
    gwy_results_add_separator(results);

    gwy_results_add_header(results, N_("First vector"));
    gwy_results_add_value(results, "a1x", N_("X-component"), "power-x", 1, "symbol", "a<sub>1x</sub>", NULL);
    gwy_results_add_value(results, "a1y", N_("Y-component"), "power-x", 1, "symbol", "a<sub>1y</sub>", NULL);
    gwy_results_add_value(results, "a1", N_("Length"), "power-x", 1, "symbol", "a<sub>1</sub>", NULL);
    gwy_results_add_value(results, "phi1", N_("Direction"), "is-angle", TRUE, "symbol", "φ<sub>1</sub>", NULL);
    gwy_results_add_separator(results);

    gwy_results_add_header(results, N_("Second vector"));
    gwy_results_add_value(results, "a2x", N_("X-component"), "power-x", 1, "symbol", "a<sub>2x</sub>", NULL);
    gwy_results_add_value(results, "a2y", N_("Y-component"), "power-x", 1, "symbol", "a<sub>2y</sub>", NULL);
    gwy_results_add_value(results, "a2", N_("Length"), "power-x", 1, "symbol", "a<sub>2</sub>", NULL);
    gwy_results_add_value(results, "phi2", N_("Direction"), "is-angle", TRUE, "symbol", "φ<sub>2</sub>", NULL);
    gwy_results_add_separator(results);

    gwy_results_add_header(results, N_("Primitive cell"));
    gwy_results_add_value(results, "A", N_("Area"), "power-x", 1, "power-y", 1, "symbol", "A", NULL);
    gwy_results_add_value(results, "phi", N_("Angle"), "is-angle", TRUE, "symbol", "φ", NULL);

    gwy_results_bind_formats(results, "a1x", "a1y", "a1", "a2x", "a2y", "a2", NULL);

    gwy_results_set_unit(results, "x", gwy_data_field_get_si_unit_xy(args->field));
    gwy_results_set_unit(results, "y", gwy_data_field_get_si_unit_xy(args->field));
    gwy_results_set_unit(results, "z", gwy_data_field_get_si_unit_z(args->field));
    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_channel(results, "image", data, id);

    return results;
}

static GtkWidget*
create_xaligned_label(const gchar *markup, gdouble xalign, gint width_chars)
{
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_misc_set_alignment(GTK_MISC(label), xalign, 0.5);
    if (width_chars > 0)
        gtk_label_set_width_chars(GTK_LABEL(label), width_chars);
    return label;
}

static GtkWidget*
create_lattice_table(gpointer user_data)
{
    static const gchar *headers[] = { "x", "y", N_("length"), N_("angle") };
    static const gboolean header_translatable[] = { FALSE, FALSE, TRUE, TRUE };
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GString *str;
    GwySIValueFormat *vf;
    GtkWidget *label;
    GtkTable *table;
    guint i;

    table = GTK_TABLE(gtk_table_new(4, 5, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);

    /* Header row. */
    str = g_string_new(NULL);
    for (i = 0; i < G_N_ELEMENTS(headers); i++) {
        g_string_assign(str, header_translatable[i] ? _(headers[i]) : headers[i]);
        vf = (i == G_N_ELEMENTS(headers)-1 ? gui->phivf : gui->xyvf);
        if (strlen(vf->units))
            g_string_append_printf(str, " [%s]", vf->units);
        gtk_table_attach(table, create_xaligned_label(str->str, 0.5, -1), i+1, i+2, 0, 1, GTK_FILL, 0, 0, 0);
    }
    g_string_free(str, TRUE);

    /* a1 */
    gtk_table_attach(table, create_xaligned_label("a<sub>1</sub>:", 0.0, -1),
                     0, 1, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    for (i = VALUE_A1_X; i <= VALUE_PHI1; i++) {
        gui->value_labels[i] = label = create_xaligned_label(NULL, 1.0, 8);
        gtk_table_attach(table, label, i+1 - VALUE_A1_X, i+2 - VALUE_A1_X, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    }

    /* a2 */
    gtk_table_attach(table, create_xaligned_label("a<sub>2</sub>:", 0.0, -1),
                     0, 1, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    for (i = VALUE_A2_X; i <= VALUE_PHI2; i++) {
        gui->value_labels[i] = label = create_xaligned_label(NULL, 1.0, 8);
        gtk_table_attach(table, label, i+1 - VALUE_A2_X, i+2 - VALUE_A2_X, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    }

    /* phi */
    gtk_table_attach(GTK_TABLE(table), create_xaligned_label("ϕ:", 1.0, -1),
                     3, 4, 3, 4, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gui->value_labels[VALUE_PHI] = label = create_xaligned_label(NULL, 1.0, 8);
    gtk_table_attach(GTK_TABLE(table), label, 4, 5, 3, 4, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    return GTK_WIDGET(table);
}

static void
set_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    ImageMode image_mode = gwy_params_get_enum(args->params, PARAM_IMAGE_MODE);
    GwyVectorLayer *vlayer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(gui->dataview));
    GwySelection *selection = gwy_vector_layer_ensure_selection(vlayer);
    GwyDataField *field = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(image_mode));
    gdouble xy[4];

    gwy_assign(xy, args->xy, 4);
    gwy_debug("image-space sel: (%g, %g) (%g, %g)", xy[0], xy[1], xy[2], xy[3]);
    if (image_mode == IMAGE_PSDF)
        transform_selection(xy);

    gwy_debug("real-space sel: (%g, %g) (%g, %g)", xy[0], xy[1], xy[2], xy[3]);
    if (g_type_is_a(G_OBJECT_TYPE(selection), g_type_from_name("GwySelectionLattice")))
        gwy_selection_set_data(selection, 1, xy);
    else if (g_type_is_a(G_OBJECT_TYPE(selection), g_type_from_name("GwySelectionPoint"))) {
        /* Point selections have origin of real coordinates in the top left corner. */
        center_selection(field, xy, 2, 1);
        gwy_selection_set_data(selection, 2, xy);
    }
}

static gboolean
get_selection(ModuleGUI *gui, gdouble *xy)
{
    ModuleArgs *args = gui->args;
    ImageMode image_mode = gwy_params_get_enum(args->params, PARAM_IMAGE_MODE);
    GwyVectorLayer *vlayer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(gui->dataview));
    GwySelection *selection = gwy_vector_layer_ensure_selection(vlayer);
    GwyDataField *field = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(image_mode));

    if (!gwy_selection_is_full(selection))
        return FALSE;

    gwy_selection_get_data(selection, xy);
    gwy_debug("image-space sel: (%g, %g) (%g, %g)", xy[0], xy[1], xy[2], xy[3]);
    /* Point selections have origin of real coordinates in the top left corner. */
    if (g_type_is_a(G_OBJECT_TYPE(selection), g_type_from_name("GwySelectionPoint")))
        center_selection(field, xy, 2, -1);
    if (image_mode == IMAGE_PSDF)
        transform_selection(xy);

    gwy_debug("real-space sel: (%g, %g) (%g, %g)", xy[0], xy[1], xy[2], xy[3]);
    return TRUE;
}

static void
switch_display(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyPixmapLayer *player = gwy_data_view_get_base_layer(GWY_DATA_VIEW(gui->dataview));
    ImageMode image_mode = gwy_params_get_enum(params, PARAM_IMAGE_MODE);

    calculate_zoomed_field(gui);
    g_object_set(player,
                 "gradient-key", g_quark_to_string(gwy_app_get_data_palette_key_for_id(image_mode)),
                 "data-key", g_quark_to_string(gwy_app_get_data_key_for_id(image_mode)),
                 "range-type-key", g_quark_to_string(gwy_app_get_data_range_type_key_for_id(image_mode)),
                 "min-max-key", g_quark_to_string(gwy_app_get_data_base_key_for_id(image_mode)),
                 NULL);
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SIZE);
    set_selection(gui);
}

static void
switch_selection_mode(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    SelectionMode sel_mode = gwy_params_get_enum(args->params, PARAM_SELECTION_MODE);
    gboolean show_numbers = gwy_params_get_boolean(args->params, PARAM_SHOW_NUMBERS);
    GwyDataView *dataview = GWY_DATA_VIEW(gui->dataview);
    GwyVectorLayer *vlayer = gwy_data_view_get_top_layer(dataview);
    GwySelection *selection = gwy_vector_layer_ensure_selection(vlayer);

    GWY_SIGNAL_HANDLER_DISCONNECT(selection, gui->selection_id);
    if (sel_mode == SELECTION_LATTICE) {
        selection = gwy_create_preview_vector_layer(dataview, 0, "Lattice", 1, TRUE);
        gwy_param_table_set_sensitive(gui->table, PARAM_SHOW_NUMBERS, FALSE);
    }
    else {
        selection = gwy_create_preview_vector_layer(dataview, 0, "Point", 2, TRUE);
        g_object_set(gwy_data_view_get_top_layer(dataview),
                     "draw-as-vector", TRUE,
                     "point-numbers", show_numbers,
                     NULL);
        gwy_param_table_set_sensitive(gui->table, PARAM_SHOW_NUMBERS, TRUE);
    }
    set_selection(gui);
    gui->selection_id = g_signal_connect_swapped(selection, "changed", G_CALLBACK(selection_changed), gui);
}

static void
calculate_acf(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyDataField *acf = gui->acf, *mid, *mask = args->mask, *field = args->field;
    GwyMaskingType masking = gwy_params_get_masking(params, PARAM_MASKING, &mask);
    gboolean fix_hacf = gwy_params_get_boolean(params, PARAM_FIX_HACF);
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    guint acfwidth = xres/2, acfheight = yres/2;

    gwy_data_field_area_2dacf_mask(field, acf, mask, masking, 0, 0, xres, yres, acfwidth, acfheight, NULL);
    if (fix_hacf) {
        mid = gwy_data_field_area_extract(acf, 0, acfheight/2-1, acfwidth, 3);
        mask = gwy_data_field_new(acfwidth, 3, acfwidth, 3, TRUE);
        gwy_data_field_area_fill(mask, 0, 1, acfwidth, 1, 1.0);
        gwy_data_field_set_val(mask, acfwidth/2, 1, 0.0);
        gwy_data_field_laplace_solve(mid, mask, -1, 1.0);
        gwy_data_field_area_copy(mid, acf, 0, 1, acfwidth, 1, 0, acfheight/2-1);
        g_object_unref(mask);
        g_object_unref(mid);
    }
}

static gint
reduce_size(gint n)
{
    gint n0 = (n & 1) ? n : n-1;
    gint nmin = MAX(n0, 65);
    gint nred = 3*n/4;
    gint nredodd = (nred & 1) ? nred : nred-1;
    return MIN(nmin, nredodd);
}

static void
calculate_psdf(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyDataField *psdf = gui->psdf, *mask = args->mask, *field = args->field;
    GwyDataField *extmask = NULL, *extfield, *fullpsdf;
    GwyMaskingType masking = gwy_params_get_masking(params, PARAM_MASKING, &mask);
    GwyWindowingType windowing = GWY_WINDOWING_HANN;
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    gint extxres = gwy_fft_find_nice_size(2*xres), extyres = gwy_fft_find_nice_size(2*yres);
    gint i, j, n, psdfwidth, psdfheight;
    gdouble *p;

    field = gwy_data_field_duplicate(field);
    gwy_data_field_add(field, -gwy_data_field_get_avg(field));
    gwy_fft_window_data_field(field, GWY_ORIENTATION_HORIZONTAL, windowing);
    gwy_fft_window_data_field(field, GWY_ORIENTATION_VERTICAL, windowing);
    extfield = gwy_data_field_extend(field, 0, extxres - xres, 0, extyres - yres,
                                     GWY_EXTERIOR_FIXED_VALUE, 0.0, FALSE);
    GWY_OBJECT_UNREF(field);
    if (mask) {
        extmask = gwy_data_field_extend(mask,0, extxres - xres, 0, extyres - yres,
                                        GWY_EXTERIOR_FIXED_VALUE, masking == GWY_MASK_INCLUDE ? 1.0 : 0.0, FALSE);
    }

    fullpsdf = gwy_data_field_new_alike(extfield, FALSE);
    gwy_data_field_area_2dpsdf_mask(extfield, fullpsdf, mask, masking, 0, 0, extxres, extyres, GWY_WINDOWING_NONE, 0);
    g_object_unref(extfield);
    GWY_OBJECT_UNREF(extmask);

    psdfwidth = reduce_size(extxres);
    psdfheight = reduce_size(extyres);
    i = extyres - psdfheight - (extyres - psdfheight)/2;
    j = extxres - psdfwidth - (extxres - psdfwidth)/2;
    gwy_data_field_resample(psdf, psdfwidth, psdfheight, GWY_INTERPOLATION_NONE);
    gwy_data_field_area_copy(fullpsdf, psdf, j, i, psdfwidth, psdfheight, 0, 0);
    /* Switch from our usual circular frequencies to plain frequencies to avoid factors in make matrix inversion,
     * making it identical for forward and backward transformations. */
    gwy_data_field_set_xreal(psdf, psdfwidth*gwy_data_field_get_dx(fullpsdf)/(2.0*G_PI));
    gwy_data_field_set_yreal(psdf, psdfheight*gwy_data_field_get_dy(fullpsdf)/(2.0*G_PI));
    gwy_data_field_set_xoffset(psdf, -0.5*gwy_data_field_get_xreal(psdf));
    gwy_data_field_set_yoffset(psdf, -0.5*gwy_data_field_get_yreal(psdf));
    g_object_unref(fullpsdf);

    /* We do not really care about modulus units nor its absolute scale.
     * We just have it to display the square root... */
    p = gwy_data_field_get_data(psdf);
    n = psdfwidth*psdfheight;
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(p,n) private(i)
#endif
    for (i = 0; i < n; i++)
        p[i] = (p[i] >= 0.0 ? sqrt(p[i]) : -sqrt(-p[i]));
}

static void
calculate_zoomed_field(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    ImageMode image_mode = gwy_params_get_enum(args->params, PARAM_IMAGE_MODE);
    GwyDataField *field, *zoomed;
    guint xres, yres, width, height, zoom;

    if (image_mode == IMAGE_ACF) {
        zoom = gwy_params_get_enum(args->params, PARAM_ZOOM_ACF);
        field = gui->acf;
    }
    else if (image_mode == IMAGE_PSDF) {
        zoom = gwy_params_get_enum(args->params, PARAM_ZOOM_PSDF);
        field = gui->psdf;
    }
    else
        return;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    if (zoom == 1)
        zoomed = g_object_ref(field);
    else {
        width = (xres/zoom) | 1;
        height = (yres/zoom) | 1;

        if (width < 17)
            width = MAX(width, MIN(17, xres));

        if (height < 17)
            height = MAX(height, MIN(17, yres));

        zoomed = gwy_data_field_area_extract(field, (xres - width)/2, (yres - height)/2, width, height);
        gwy_data_field_set_xoffset(zoomed, -0.5*gwy_data_field_get_xreal(zoomed));
        gwy_data_field_set_yoffset(zoomed, -0.5*gwy_data_field_get_yreal(zoomed));
    }
    gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(image_mode), zoomed);
    g_object_unref(zoomed);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    ImageMode image_mode = gwy_params_get_enum(params, PARAM_IMAGE_MODE);

    if (id < 0 || id == PARAM_IMAGE_MODE) {
        gwy_param_table_set_sensitive(gui->table, PARAM_ZOOM, image_mode != IMAGE_DATA);
        if (image_mode == IMAGE_ACF)
            gwy_param_table_set_enum(gui->table, PARAM_ZOOM, gwy_params_get_enum(params, PARAM_ZOOM_ACF));
        else if (image_mode == IMAGE_PSDF)
            gwy_param_table_set_enum(gui->table, PARAM_ZOOM, gwy_params_get_enum(params, PARAM_ZOOM_PSDF));
        switch_display(gui);
    }
    if (id < 0 || id == PARAM_ZOOM) {
        if (image_mode == IMAGE_ACF)
            gwy_params_set_enum(params, PARAM_ZOOM_ACF, gwy_params_get_enum(params, PARAM_ZOOM));
        else if (image_mode == IMAGE_PSDF)
            gwy_params_set_enum(params, PARAM_ZOOM_PSDF, gwy_params_get_enum(params, PARAM_ZOOM));

        calculate_zoomed_field(gui);
        gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SIZE);
        set_selection(gui);
    }
    if (id < 0 || id == PARAM_SELECTION_MODE)
        switch_selection_mode(gui);
    if (id < 0 || id == PARAM_SHOW_NUMBERS) {
        GwyVectorLayer *vlayer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(gui->dataview));
        if (g_type_is_a(G_OBJECT_TYPE(vlayer), g_type_from_name("GwyLayerPoint"))) {
            gboolean show_numbers = gwy_params_get_boolean(args->params, PARAM_SHOW_NUMBERS);
            g_object_set(vlayer, "point-numbers", show_numbers, NULL);
        }
    }

    if (id < 0 || id == PARAM_FIX_HACF || id == PARAM_MASKING)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    calculate_acf(gui);
    calculate_psdf(gui);
    switch_display(gui);
    gwy_data_field_data_changed(gui->acf);
    gwy_data_field_data_changed(gui->psdf);
    if (!gui->args->have_xy)
        dialog_response(gui, RESPONSE_ESTIMATE);
    /* Does not do anything really useful. */
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;
    ImageMode image_mode = gwy_params_get_enum(args->params, PARAM_IMAGE_MODE);
    gdouble xy[4];
    gboolean ok;

    if (response == GWY_RESPONSE_RESET)
        response = RESPONSE_ESTIMATE;
    if (response != RESPONSE_ESTIMATE && response != RESPONSE_REFINE)
        return;

    if (response == RESPONSE_REFINE) {
        if (!get_selection(gui, xy))
            return;
    }
    else
        gwy_clear(xy, 4);

    if (image_mode == IMAGE_PSDF)
        ok = gwy_data_field_measure_lattice_psdf(gui->psdf, xy);
    else
        ok = gwy_data_field_measure_lattice_acf(gui->acf, xy);

    gwy_debug("%s from %s: %s",
              (response == RESPONSE_ESTIMATE ? "estimate" : "refine"),
              (image_mode == IMAGE_PSDF ? "PSDF" : "ACF"),
              (ok ? "OK" : "BAD"));
    if (ok) {
        gwy_assign(args->xy, xy, 4);
        set_selection(gui);
        args->have_xy = TRUE;
        return;
    }

    if (response == RESPONSE_ESTIMATE) {
        args->xy[0] = gwy_data_field_get_xreal(args->field)/20.0;
        args->xy[3] = -gwy_data_field_get_yreal(args->field)/20.0;
        args->xy[1] = args->xy[2] = 0.0;
        args->have_xy = FALSE;
        set_selection(gui);
    }
    /* REFINE just keeps things unchanged on failure */
}

static void
update_value_label(GtkWidget *label, gdouble value, GwySIValueFormat *vf, GString *str)
{
    g_string_printf(str, "%.*f", vf->precision, value/vf->magnitude);
    gtk_label_set_text(GTK_LABEL(label), str->str);
}

static void
selection_changed(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gdouble a1, a2, phi1, phi2, phi, A;
    GString *str;
    gdouble xy[4];

    if (!get_selection(gui, xy)) {
        gwy_results_set_na(gui->results, "a1x", "a1y", "a1", "phi1", "a2x", "a2y", "a2", "phi2", "phi", "A", NULL);
        /* FIXME: We should also clear labels, right? */
        return;
    }
    gwy_assign(args->xy, xy, 4);

    a1 = hypot(xy[0], xy[1]);
    a2 = hypot(xy[2], xy[3]);
    phi1 = atan2(-xy[1], xy[0]);
    phi2 = atan2(-xy[3], xy[2]);
    phi = gwy_canonicalize_angle(phi2 - phi1, TRUE, TRUE);
    A = fabs(matrix_det(xy));

    gwy_results_fill_values(gui->results,
                            "a1x", xy[0], "a1y", -xy[1], "a1", a1, "phi1", phi1,
                            "a2x", xy[2], "a2y", -xy[3], "a2", a2, "phi2", phi2,
                            "phi", phi, "A", A,
                            NULL);

    str = g_string_new(NULL);
    update_value_label(gui->value_labels[VALUE_A1_X], xy[0], gui->xyvf, str);
    update_value_label(gui->value_labels[VALUE_A1_Y], -xy[1], gui->xyvf, str);
    update_value_label(gui->value_labels[VALUE_A1], a1, gui->xyvf, str);
    update_value_label(gui->value_labels[VALUE_PHI1], phi1, gui->phivf, str);
    update_value_label(gui->value_labels[VALUE_A2_X], xy[2], gui->xyvf, str);
    update_value_label(gui->value_labels[VALUE_A2_Y], -xy[3], gui->xyvf, str);
    update_value_label(gui->value_labels[VALUE_A2], a2, gui->xyvf, str);
    update_value_label(gui->value_labels[VALUE_PHI2], phi2, gui->phivf, str);
    update_value_label(gui->value_labels[VALUE_PHI], phi, gui->phivf, str);
    g_string_free(str, TRUE);
}

static void
center_selection(GwyDataField *field, gdouble *xy, gint n, gint sign)
{
    gdouble xoff = 0.5*gwy_data_field_get_xreal(field);
    gdouble yoff = 0.5*gwy_data_field_get_yreal(field);
    gint i;

    for (i = 0; i < n; i++) {
        xy[2*i + 0] += sign*xoff;
        xy[2*i + 1] += sign*yoff;
    }
}

static gboolean
transform_selection(gdouble *xy)
{
    gdouble D = matrix_det(xy);
    gdouble a = fabs(xy[0]*xy[3]) + fabs(xy[1]*xy[2]);

    if (fabs(D)/a < 1e-9)
        return FALSE;

    invert_matrix(xy, xy);
    /* Transpose. */
    GWY_SWAP(gdouble, xy[1], xy[2]);
    return TRUE;
}

/* Permit dest = src */
static void
invert_matrix(gdouble *dest, const gdouble *src)
{
    gdouble D = matrix_det(src);
    gdouble xy[4];

    gwy_debug("D %g", D);
    xy[0] = src[3]/D;
    xy[1] = -src[1]/D;
    xy[2] = -src[2]/D;
    xy[3] = src[0]/D;
    dest[0] = xy[0];
    dest[1] = xy[1];
    dest[2] = xy[2];
    dest[3] = xy[3];
}

static gdouble
matrix_det(const gdouble *m)
{
    return m[0]*m[3] - m[1]*m[2];
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
