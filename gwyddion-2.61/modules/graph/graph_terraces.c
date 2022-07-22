/*
 *  $Id: graph_terraces.c 24083 2021-09-06 16:37:12Z yeti-dn $
 *  Copyright (C) 2019-2020 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwycheckboxes.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"
#include "../process/preview.h"

/* Lower symmetric part indexing */
/* i MUST be greater or equal than j */
#define SLi(a, i, j) a[(i)*((i) + 1)/2 + (j)]

#define terrace_index(a, i) g_array_index(a, TerraceSegment, (i))

#define MAX_BROADEN 128.0
#define pwr 0.65

enum {
    MAX_DEGREE = 18,
};

typedef enum {
    PREVIEW_DATA_FIT   = 0,
    PREVIEW_DATA_POLY  = 1,
    PREVIEW_RESIDUUM   = 2,
    PREVIEW_TERRACES   = 3,
    PREVIEW_LEVELLED   = 4,
    PREVIEW_BACKGROUND = 5,
    PREVIEW_STEPS      = 6,
    PREVIEW_NTYPES
} PreviewMode;

typedef enum {
    OUTPUT_DATA_FIT   = (1 << 0),
    OUTPUT_DATA_POLY  = (1 << 1),
    OUTPUT_RESIDUUM   = (1 << 2),
    OUTPUT_TERRACES   = (1 << 3),
    OUTPUT_LEVELLED   = (1 << 4),
    OUTPUT_BACKGROUND = (1 << 5),
    OUTPUT_ALL        = (1 << 6) - 1,
} OutputFlags;

typedef struct {
    gint curve;
    gboolean use_selection;

    gint poly_degree;
    gdouble edge_kernel_size;
    gdouble edge_threshold;
    gdouble edge_broadening;
    GwyResultsReportType report_style;
    gdouble min_area_frac;
    gboolean independent;
    guint output_flags;

    gboolean survey_poly;
    gint poly_degree_min;
    gint poly_degree_max;
    gboolean survey_broadening;
    gint broadening_min;
    gint broadening_max;

    PreviewMode preview_mode;
} TerraceArgs;

static const gchar *guivalues[] = {
    "step", "resid", "discrep", "nterraces",
};

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
    TerraceArgs *args;
    GwyGraphModel *parent_gmodel;
    GtkWidget *dialogue;
    GtkWidget *graph;
    GtkWidget *curve;
    GtkObject *edge_kernel_size;
    GtkObject *edge_threshold;
    GtkObject *edge_broadening;
    GtkObject *poly_degree;
    GtkObject *min_area_frac;
    GtkWidget *preview_mode;
    GtkWidget *independent;
    GtkWidget *use_selection;
    GwyResults *results;
    GtkWidget *guivalues[G_N_ELEMENTS(guivalues)];
    GtkWidget *rexport_result;
    GtkWidget *message;
    GtkWidget *view;
    GtkWidget *terracelist;
    GtkWidget *rexport_list;
    GSList *output_flags;
    GtkWidget *survey_table;
    GtkWidget *survey_poly;
    GtkObject *poly_degree_min;
    GtkObject *poly_degree_max;
    GtkWidget *survey_broadening;
    GtkObject *broadening_min;
    GtkObject *broadening_max;
    GtkWidget *run_survey;
    GtkWidget *survey_message;
    GArray *terracesegments;
    GwyDataLine *edges;
    GwyDataLine *residuum;
    GwyDataLine *background;
    GdkPixbuf *colourpixbuf;
    GwySIValueFormat *vf;
    gulong sid;
    FitResult *fres;
} TerraceControls;

typedef struct {
    gdouble xfrom;
    gdouble xto;
    gint i;
    gint npixels;
    gint level;
    gdouble height;   /* estimate from free fit */
    gdouble error;    /* difference from free fit estimate */
    gdouble residuum; /* final fit residuum */
} TerraceSegment;

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

static gboolean   module_register          (void);
static void       graph_terraces           (GwyGraph *graph);
static void       graph_terraces_dialogue  (GwyContainer *data,
                                            GwyGraphModel *parent_gmodel,
                                            TerraceArgs *args);
static void       dialogue_response        (GtkDialog *dialogue,
                                            gint response_id,
                                            TerraceControls *controls);
static void       curve_changed            (GtkComboBox *combo,
                                            TerraceControls *controls);
static void       poly_degree_changed      (TerraceControls *controls,
                                            GtkAdjustment *adj);
static void       edge_kernel_size_changed (TerraceControls *controls,
                                            GtkAdjustment *adj);
static void       edge_threshold_changed   (TerraceControls *controls,
                                            GtkAdjustment *adj);
static void       edge_broadening_changed  (TerraceControls *controls,
                                            GtkAdjustment *adj);
static void       min_area_frac_changed    (TerraceControls *controls,
                                            GtkAdjustment *adj);
static void       use_selection_changed    (GtkToggleButton *toggle,
                                            TerraceControls *controls);
static void       independent_changed      (GtkToggleButton *toggle,
                                            TerraceControls *controls);
static void       preview_mode_changed     (GtkComboBox *combo,
                                            TerraceControls *controls);
static void       report_style_changed     (TerraceControls *controls,
                                            GwyResultsExport *rexport);
static void       output_flags_changed     (GtkWidget *button,
                                            TerraceControls *controls);
static void       survey_poly_changed      (GtkToggleButton *toggle,
                                            TerraceControls *controls);
static void       poly_degree_min_changed  (TerraceControls *controls,
                                            GtkAdjustment *adj);
static void       poly_degree_max_changed  (TerraceControls *controls,
                                            GtkAdjustment *adj);
static void       survey_broadening_changed(GtkToggleButton *toggle,
                                            TerraceControls *controls);
static void       broadening_min_changed   (TerraceControls *controls,
                                            GtkAdjustment *adj);
static void       broadening_max_changed   (TerraceControls *controls,
                                            GtkAdjustment *adj);
static void       graph_xsel_changed       (GwySelection *selection,
                                            gint hint,
                                            TerraceControls *controls);
static void       update_sensitivity       (TerraceControls *controls);
static void       invalidate               (TerraceControls *controls);
static gboolean   preview_gsource          (gpointer user_data);
static void       preview                  (TerraceControls *controls);
static void       fill_preview_graph       (TerraceControls *controls);
static FitResult* terrace_do               (const gdouble *xdata,
                                            const gdouble *ydata,
                                            guint ndata,
                                            GwyDataLine *edges,
                                            GwyDataLine *residuum,
                                            GwyDataLine *background,
                                            GArray *terracesegments,
                                            GwySelection *xsel,
                                            const TerraceArgs *args,
                                            const gchar** message);
static void       update_value_formats     (TerraceControls *controls);
static GtkWidget* parameters_tab_new       (TerraceControls *controls);
static GtkWidget* terrace_list_tab_new     (TerraceControls *controls);
static GtkWidget* output_tab_new           (TerraceControls *controls);
static GtkWidget* survey_tab_new           (TerraceControls *controls);
static void       create_output_graphs     (TerraceControls *controls,
                                            GwyContainer *data);
static void       save_report              (TerraceControls *controls);
static void       copy_report              (TerraceControls *controls);
static gchar*     format_report            (TerraceControls *controls);
static guint      count_survey_items       (TerraceArgs *args,
                                            guint *pndegrees,
                                            guint *pnbroadenings);
static void       run_survey               (TerraceControls *controls);
static void       load_args                (GwyContainer *settings,
                                            TerraceArgs *args);
static void       save_args                (GwyContainer *settings,
                                            TerraceArgs *args);

static const GwyEnum output_flags[] = {
    { N_("Data + fit"),            OUTPUT_DATA_FIT,   },
    { N_("Data + polynomials"),    OUTPUT_DATA_POLY,  },
    { N_("Difference"),            OUTPUT_RESIDUUM,   },
    { N_("Terraces (ideal)"),      OUTPUT_TERRACES,   },
    { N_("Leveled surface"),       OUTPUT_LEVELLED,   },
    { N_("Polynomial background"), OUTPUT_BACKGROUND, },
};

enum {
    OUTPUT_NFLAGS = G_N_ELEMENTS(output_flags)
};

static const TerraceArgs terraces_defaults = {
    0, FALSE,
    4,
    3.5, 40.0, 6.0,
    GWY_RESULTS_REPORT_TABSEP,
    1.5,
    FALSE,
    OUTPUT_DATA_POLY,
    FALSE, 0, MAX_DEGREE,
    FALSE, 0, MAX_BROADEN,

    PREVIEW_DATA_FIT,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Fits terraces with polynomial background."),
    "Yeti <yeti@gwyddion.net>",
    "1.5",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, graph_terraces)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_terraces",
                            (GwyGraphFunc)&graph_terraces,
                            N_("/Measure _Features/_Terraces..."),
                            GWY_STOCK_GRAPH_TERRACE_MEASURE,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Fit terraces with polynomial background"));

    return TRUE;
}

static void
graph_terraces(GwyGraph *graph)
{
    GwyContainer *data;
    TerraceArgs args;

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data, 0);
    load_args(gwy_app_settings_get(), &args);
    graph_terraces_dialogue(data, gwy_graph_get_model(graph), &args);
    save_args(gwy_app_settings_get(), &args);
}

static void
graph_terraces_dialogue(GwyContainer *data, GwyGraphModel *parent_gmodel,
                        TerraceArgs *args)
{
    GtkWidget *dialogue, *hbox, *notebook, *widget;
    GwyDataLine *dline;
    GwyGraphModel *gmodel;
    GwyGraphArea *area;
    GwyResults *results;
    GwySIUnit *unit;
    TerraceControls controls;
    GwySelection *xsel;
    gint width, height, response;

    gwy_clear(&controls, 1);
    controls.args = args;
    controls.parent_gmodel = parent_gmodel;
    controls.terracesegments = g_array_new(FALSE, FALSE,
                                           sizeof(TerraceSegment));
    gmodel = gwy_graph_model_new_alike(parent_gmodel);

    dline = gwy_data_line_new(1, 1.0, TRUE);
    g_object_get(gmodel, "si-unit-x", &unit, NULL);
    gwy_si_unit_assign(gwy_data_line_get_si_unit_x(dline), unit);
    g_object_unref(unit);
    g_object_get(gmodel, "si-unit-y", &unit, NULL);
    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(dline), unit);
    controls.edges = gwy_data_line_duplicate(dline);
    controls.residuum = gwy_data_line_duplicate(dline);
    controls.background = dline;

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    height |= 1;
    controls.colourpixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                                           height, height);

    controls.results = results = gwy_results_new();
    gwy_results_add_header(results, N_("Fit Results"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "graph", N_("Graph"));
    gwy_results_add_value_str(results, "curve", N_("Curve"));
    gwy_results_add_separator(results);
    /* TODO: We might also want to output the segmentation & fit settings. */
    gwy_results_add_value_z(results, "step", N_("Fitted step height"));
    gwy_results_add_value_z(results, "resid", N_("Mean square difference"));
    gwy_results_add_value_z(results, "discrep", N_("Terrace discrepancy"));
    gwy_results_add_value_int(results, "nterraces", N_("Number of terraces"));
    gwy_results_set_unit(results, "z", unit);
    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_graph(results, "graph", parent_gmodel);
    g_object_unref(unit);

    dialogue = gtk_dialog_new_with_buttons(_("Fit Terraces"), NULL, 0, NULL);
    controls.dialogue = dialogue;
    gtk_dialog_add_button(GTK_DIALOG(dialogue), GTK_STOCK_CLEAR,
                          RESPONSE_CLEAR);
    gtk_dialog_add_button(GTK_DIALOG(dialogue), GTK_STOCK_CANCEL,
                          GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialogue), GTK_STOCK_OK, GTK_RESPONSE_OK);
    gwy_help_add_to_graph_dialog(GTK_DIALOG(dialogue), GWY_HELP_DEFAULT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialogue), GTK_RESPONSE_OK);
    g_signal_connect(dialogue, "response",
                     G_CALLBACK(dialogue_response), &controls);

    hbox = gtk_hbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialogue)->vbox), hbox,
                       TRUE, TRUE, 0);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(hbox), notebook, FALSE, FALSE, 0);

    widget = parameters_tab_new(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), widget,
                             gtk_label_new(_("Parameters")));

    widget = terrace_list_tab_new(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), widget,
                             gtk_label_new(_("Terrace List")));

    widget = output_tab_new(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), widget,
                             gtk_label_new(_("Output")));

    widget = survey_tab_new(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), widget,
                             gtk_label_new(_("Survey")));

    /* Graph */
    controls.graph = gwy_graph_new(gmodel);
    g_object_set(gmodel, "label-visible", FALSE, NULL);
    g_object_unref(gmodel);
    gtk_widget_set_size_request(controls.graph, 480, 300);

    gwy_graph_enable_user_input(GWY_GRAPH(controls.graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.graph, TRUE, TRUE, 0);
    gwy_graph_set_status(GWY_GRAPH(controls.graph), GWY_GRAPH_STATUS_XSEL);

    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(controls.graph)));
    gwy_graph_area_set_selection_editable(area, TRUE);
    xsel = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(xsel, 1024);
    g_signal_connect(xsel, "changed",
                     G_CALLBACK(graph_xsel_changed), &controls);

    curve_changed(GTK_COMBO_BOX(controls.curve), &controls);
    use_selection_changed(GTK_TOGGLE_BUTTON(controls.use_selection), &controls);

    gtk_widget_show_all(dialogue);
    response = gtk_dialog_run(GTK_DIALOG(dialogue));
    if (controls.sid) {
        g_source_remove(controls.sid);
        controls.sid = 0;
    }
    /* Prevent invalidate during dialogue destruction. */
    controls.sid = G_MAXINT;
    if (response == GTK_RESPONSE_OK)
        create_output_graphs(&controls, data);
    gtk_widget_destroy(dialogue);

    gwy_si_unit_value_format_free(controls.vf);
    g_object_unref(controls.results);
    g_object_unref(controls.edges);
    g_object_unref(controls.residuum);
    g_object_unref(controls.background);
    g_object_unref(controls.colourpixbuf);
    g_array_free(controls.terracesegments, TRUE);
}

static void
update_value_formats(TerraceControls *controls)
{
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GtkTreeView *treeview;
    GList *columns, *l;
    GwySIUnit *yunit;
    gdouble min, max, yrange;

    gmodel = gwy_graph_get_model(GWY_GRAPH(controls->graph));
    gcmodel = gwy_graph_model_get_curve(gmodel, 0);
    g_object_get(gmodel, "si-unit-y", &yunit, NULL);

    gwy_graph_curve_model_get_y_range(gcmodel, &min, &max);
    yrange = max - min;
    controls->vf
        = gwy_si_unit_get_format_with_digits(yunit,
                                             GWY_SI_UNIT_FORMAT_MARKUP,
                                             yrange, 4, controls->vf);

    g_object_unref(yunit);

    treeview = GTK_TREE_VIEW(controls->terracelist);
    columns = gtk_tree_view_get_columns(treeview);
    for (l = columns; l; l = g_list_next(l)) {
        GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN(l->data);
        gboolean is_z = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column),
                                                          "is_z"));
        const gchar *title = g_object_get_data(G_OBJECT(column), "title");
        GtkWidget *label;
        gchar *s;

        label = gtk_tree_view_column_get_widget(column);
        if (is_z && *controls->vf->units)
            s = g_strdup_printf("<b>%s</b> [%s]", title, controls->vf->units);
        else
            s = g_strdup_printf("<b>%s</b>", title);
        gtk_label_set_markup(GTK_LABEL(label), s);
    }
    g_list_free(columns);
}

static GtkWidget*
parameters_tab_new(TerraceControls *controls)
{
    TerraceArgs *args = controls->args;
    GtkWidget *table, *spin, *label;
    GwyResultsExport *rexport;
    GwyResults *results;
    GString *str;
    guint i;
    gint row;

    /* Parameters */
    table = gtk_table_new(13, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    row = 0;

    controls->curve = gwy_combo_box_graph_curve_new(G_CALLBACK(curve_changed),
                                                    controls,
                                                    controls->parent_gmodel,
                                                    args->curve);
    gwy_table_attach_adjbar(table, row++, _("_Graph curve:"), NULL,
                            GTK_OBJECT(controls->curve),
                            GWY_HSCALE_WIDGET_NO_EXPAND);

    controls->edge_kernel_size = gtk_adjustment_new(args->edge_kernel_size,
                                                    1.0, 64.0, 0.1, 5.0, 0.0);
    gwy_table_attach_adjbar(table, row, _("_Step detection kernel:"), _("px"),
                            controls->edge_kernel_size, GWY_HSCALE_SQRT);
    g_signal_connect_swapped(controls->edge_kernel_size, "value-changed",
                             G_CALLBACK(edge_kernel_size_changed), controls);
    row++;

    controls->edge_threshold = gtk_adjustment_new(args->edge_threshold,
                                                  0.0, 100.0, 0.01, 0.1, 0.0);
    gwy_table_attach_adjbar(table, row, _("Step detection _threshold:"), "%",
                            controls->edge_threshold, GWY_HSCALE_SQRT);
    g_signal_connect_swapped(controls->edge_threshold, "value-changed",
                             G_CALLBACK(edge_threshold_changed), controls);
    row++;

    controls->edge_broadening = gtk_adjustment_new(args->edge_broadening,
                                                   0.0, MAX_BROADEN, 1.0, 10.0,
                                                   0.0);
    spin = gwy_table_attach_adjbar(table, row, _("Step _broadening:"), _("px"),
                                   controls->edge_broadening, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);
    g_signal_connect_swapped(controls->edge_broadening, "value-changed",
                             G_CALLBACK(edge_broadening_changed), controls);
    row++;

    controls->min_area_frac = gtk_adjustment_new(args->min_area_frac,
                                                 0.1, 40.0, 0.01, 1.0, 0.0);
    gwy_table_attach_adjbar(table, row, _("Minimum terrace _length:"), "%",
                            controls->min_area_frac, GWY_HSCALE_SQRT);
    g_signal_connect_swapped(controls->min_area_frac, "value-changed",
                             G_CALLBACK(min_area_frac_changed), controls);
    row++;

    controls->poly_degree = gtk_adjustment_new(args->poly_degree,
                                               0.0, MAX_DEGREE, 1.0, 2.0, 0.0);
    gwy_table_attach_adjbar(table, row, _("_Polynomial degree:"), NULL,
                            controls->poly_degree,
                            GWY_HSCALE_LINEAR | GWY_HSCALE_SNAP);
    g_signal_connect_swapped(controls->poly_degree, "value-changed",
                             G_CALLBACK(poly_degree_changed), controls);
    row++;

    controls->independent
        = gtk_check_button_new_with_mnemonic(_("_Independent heights"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->independent),
                                 args->independent);
    gtk_table_attach(GTK_TABLE(table), controls->independent,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls->independent, "toggled",
                     G_CALLBACK(independent_changed), controls);
    row++;

    controls->preview_mode
        = gwy_enum_combo_box_newl(G_CALLBACK(preview_mode_changed), controls,
                                  args->preview_mode,
                                  _("Data + fit"), PREVIEW_DATA_FIT,
                                  _("Data + polynomials"), PREVIEW_DATA_POLY,
                                  _("Difference"), PREVIEW_RESIDUUM,
                                  _("Terraces (ideal)"), PREVIEW_TERRACES,
                                  _("Leveled surface"), PREVIEW_LEVELLED,
                                  _("Polynomial background"), PREVIEW_BACKGROUND,
                                  _("Step detection"), PREVIEW_STEPS,
                                  NULL);
    gwy_table_attach_adjbar(table, row++, _("_Display:"), NULL,
                            GTK_OBJECT(controls->preview_mode),
                            GWY_HSCALE_WIDGET_NO_EXPAND);

    controls->use_selection
        = gtk_check_button_new_with_mnemonic(_("Select regions _manually"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->use_selection),
                                 args->use_selection);
    gtk_table_attach(GTK_TABLE(table), controls->use_selection,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls->use_selection, "toggled",
                     G_CALLBACK(use_selection_changed), controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);

    label = gwy_label_new_header(_("Result"));
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    str = g_string_new(NULL);
    results = controls->results;
    for (i = 0; i < G_N_ELEMENTS(guivalues); i++) {
        g_string_assign(str, gwy_results_get_label_with_symbol(results,
                                                               guivalues[i]));
        g_string_append_c(str, ':');
        controls->guivalues[i] = gtk_label_new(NULL);
        gwy_table_attach_adjbar(table, row++, str->str, NULL,
                                GTK_OBJECT(controls->guivalues[i]),
                                GWY_HSCALE_WIDGET_NO_EXPAND);
    }
    g_string_free(str, TRUE);

    controls->message = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls->message), 0.0, 0.5);
    set_widget_as_error_message(controls->message);
    gtk_table_attach(GTK_TABLE(table), controls->message,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls->rexport_result = gwy_results_export_new(args->report_style);
    rexport = GWY_RESULTS_EXPORT(controls->rexport_result);
    gwy_results_export_set_results(rexport, results);
    gwy_results_export_set_title(rexport, _("Save Fit Report"));
    gwy_results_export_set_actions_sensitive(rexport, FALSE);
    gtk_table_attach(GTK_TABLE(table), controls->rexport_result,
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(rexport, "format-changed",
                             G_CALLBACK(report_style_changed), controls);

    return table;
}

static void
render_id(G_GNUC_UNUSED GtkTreeViewColumn *column,
          GtkCellRenderer *renderer,
          GtkTreeModel *model,
          GtkTreeIter *iter,
          G_GNUC_UNUSED gpointer user_data)
{
    gchar buf[16];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    g_snprintf(buf, sizeof(buf), "%u", i+1);
    g_object_set(renderer, "text", buf, NULL);
}

static void
render_colour(G_GNUC_UNUSED GtkTreeViewColumn *column,
              G_GNUC_UNUSED GtkCellRenderer *renderer,
              GtkTreeModel *model,
              GtkTreeIter *iter,
              gpointer user_data)
{
    TerraceControls *controls = (TerraceControls*)user_data;
    guint i, pixel;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    pixel = 0xff | gwy_rgba_to_pixbuf_pixel(gwy_graph_get_preset_color(i+1));
    gdk_pixbuf_fill(controls->colourpixbuf, pixel);
}

static void
render_height(G_GNUC_UNUSED GtkTreeViewColumn *column,
              GtkCellRenderer *renderer,
              GtkTreeModel *model,
              GtkTreeIter *iter,
              gpointer user_data)
{
    TerraceControls *controls = (TerraceControls*)user_data;
    GwySIValueFormat *vf = controls->vf;
    TerraceSegment *seg;
    gchar buf[32];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    seg = &terrace_index(controls->terracesegments, i);
    g_snprintf(buf, sizeof(buf), "%.*f",
               vf->precision, seg->height/vf->magnitude);
    g_object_set(renderer, "text", buf, NULL);
}

static void
render_level(G_GNUC_UNUSED GtkTreeViewColumn *column,
            GtkCellRenderer *renderer,
            GtkTreeModel *model,
            GtkTreeIter *iter,
            gpointer user_data)
{
    TerraceControls *controls = (TerraceControls*)user_data;
    TerraceSegment *seg;
    gchar buf[16];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    seg = &terrace_index(controls->terracesegments, i);
    g_snprintf(buf, sizeof(buf), "%d", seg->level);
    g_object_set(renderer, "text", buf, NULL);
}

static void
render_area(G_GNUC_UNUSED GtkTreeViewColumn *column,
            GtkCellRenderer *renderer,
            GtkTreeModel *model,
            GtkTreeIter *iter,
            gpointer user_data)
{
    TerraceControls *controls = (TerraceControls*)user_data;
    TerraceSegment *seg;
    gchar buf[16];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    seg = &terrace_index(controls->terracesegments, i);
    g_snprintf(buf, sizeof(buf), "%u", seg->npixels);
    g_object_set(renderer, "text", buf, NULL);
}

static void
render_error(G_GNUC_UNUSED GtkTreeViewColumn *column,
             GtkCellRenderer *renderer,
             GtkTreeModel *model,
             GtkTreeIter *iter,
             gpointer user_data)
{
    TerraceControls *controls = (TerraceControls*)user_data;
    GwySIValueFormat *vf = controls->vf;
    TerraceSegment *seg;
    gchar buf[32];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    seg = &terrace_index(controls->terracesegments, i);
    g_snprintf(buf, sizeof(buf), "%.*f",
               vf->precision, seg->error/vf->magnitude);
    g_object_set(renderer, "text", buf, NULL);
}

static void
render_residuum(G_GNUC_UNUSED GtkTreeViewColumn *column,
                GtkCellRenderer *renderer,
                GtkTreeModel *model,
                GtkTreeIter *iter,
                gpointer user_data)
{
    TerraceControls *controls = (TerraceControls*)user_data;
    GwySIValueFormat *vf = controls->vf;
    TerraceSegment *seg;
    gchar buf[32];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    seg = &terrace_index(controls->terracesegments, i);
    g_snprintf(buf, sizeof(buf), "%.*f",
               vf->precision, seg->residuum/vf->magnitude);
    g_object_set(renderer, "text", buf, NULL);
}

static GtkTreeViewColumn*
append_text_column(GtkTreeCellDataFunc render_func, const gchar *title,
                   TerraceControls *controls, gboolean is_z)
{
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkWidget *label;

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            render_func, controls, NULL);

    label = gtk_label_new(NULL);
    g_object_set_data(G_OBJECT(column), "title", (gpointer)title);
    g_object_set_data(G_OBJECT(column), "is_z", GINT_TO_POINTER(is_z));
    gtk_tree_view_column_set_widget(column, label);
    gtk_widget_show(label);

    gtk_tree_view_append_column(GTK_TREE_VIEW(controls->terracelist), column);

    return column;
}

static GtkWidget*
terrace_list_tab_new(TerraceControls *controls)
{
    TerraceArgs *args = controls->args;
    GtkWidget *vbox, *scwin;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GwyNullStore *store;
    GwyResultsExport *rexport;

    vbox = gtk_vbox_new(FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    store = gwy_null_store_new(0);
    controls->terracelist = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_container_add(GTK_CONTAINER(scwin), controls->terracelist);

    column = append_text_column(render_id, "n", controls, FALSE);
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, FALSE);
    g_object_set(renderer, "pixbuf", controls->colourpixbuf, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            render_colour, controls, NULL);
    append_text_column(render_height, "h", controls, TRUE);
    append_text_column(render_level, "k", controls, FALSE);
    append_text_column(render_area, "N<sub>px</sub>", controls, FALSE);
    append_text_column(render_error, "Δ", controls, TRUE);
    append_text_column(render_residuum, "r", controls, TRUE);

    controls->rexport_list = gwy_results_export_new(args->report_style);
    rexport = GWY_RESULTS_EXPORT(controls->rexport_list);
    gwy_results_export_set_style(rexport, GWY_RESULTS_EXPORT_TABULAR_DATA);
    gwy_results_export_set_title(rexport, _("Save Terrace Table"));
    gwy_results_export_set_actions_sensitive(rexport, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), controls->rexport_list, FALSE, FALSE, 0);
    g_signal_connect_swapped(rexport, "format-changed",
                             G_CALLBACK(report_style_changed), controls);
    g_signal_connect_swapped(rexport, "copy",
                             G_CALLBACK(copy_report), controls);
    g_signal_connect_swapped(rexport, "save",
                             G_CALLBACK(save_report), controls);

    return vbox;
}

static GtkWidget*
output_tab_new(TerraceControls *controls)
{
    TerraceArgs *args = controls->args;
    GtkWidget *table;
    gint row;

    table = gtk_table_new(OUTPUT_NFLAGS, 1, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    row = 0;

    controls->output_flags
        = gwy_check_boxes_create(output_flags, OUTPUT_NFLAGS,
                                 G_CALLBACK(output_flags_changed), controls,
                                 args->output_flags);
    row = gwy_check_boxes_attach_to_table(controls->output_flags,
                                          GTK_TABLE(table), 1, row);

    return table;
}

static GtkWidget*
survey_tab_new(TerraceControls *controls)
{
    TerraceArgs *args = controls->args;
    GtkWidget *table, *align, *spin;
    gint row;

    table = controls->survey_table = gtk_table_new(8, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    row = 0;

    controls->survey_poly
        = gtk_check_button_new_with_mnemonic(_("_Polynomial degree"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->survey_poly),
                                 args->survey_poly);
    gtk_table_attach(GTK_TABLE(table), controls->survey_poly,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls->survey_poly, "toggled",
                     G_CALLBACK(survey_poly_changed), controls);
    row++;

    controls->poly_degree_min = gtk_adjustment_new(args->poly_degree_min,
                                                   0.0, MAX_DEGREE, 1.0, 2.0,
                                                   0.0);
    gwy_table_attach_adjbar(table, row, _("M_inimum polynomial degree:"), NULL,
                            controls->poly_degree_min,
                            GWY_HSCALE_LINEAR | GWY_HSCALE_SNAP);
    g_signal_connect_swapped(controls->poly_degree_min, "value-changed",
                             G_CALLBACK(poly_degree_min_changed), controls);
    row++;

    controls->poly_degree_max = gtk_adjustment_new(args->poly_degree_max,
                                                   0.0, MAX_DEGREE, 1.0, 2.0,
                                                   0.0);
    gwy_table_attach_adjbar(table, row, _("_Maximum polynomial degree:"), NULL,
                            controls->poly_degree_max,
                            GWY_HSCALE_LINEAR | GWY_HSCALE_SNAP);
    g_signal_connect_swapped(controls->poly_degree_max, "value-changed",
                             G_CALLBACK(poly_degree_max_changed), controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls->survey_broadening
        = gtk_check_button_new_with_mnemonic(_("Step _broadening"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->survey_broadening),
                                 args->survey_broadening);
    gtk_table_attach(GTK_TABLE(table), controls->survey_broadening,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls->survey_broadening, "toggled",
                     G_CALLBACK(survey_broadening_changed), controls);
    row++;

    controls->broadening_min = gtk_adjustment_new(args->broadening_min,
                                                  0.0, MAX_BROADEN, 1.0, 10.0,
                                                  0.0);
    spin = gwy_table_attach_adjbar(table, row,
                                   _("Minimum broadening:"), _("px"),
                                   controls->broadening_min, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);
    g_signal_connect_swapped(controls->broadening_min, "value-changed",
                             G_CALLBACK(broadening_min_changed), controls);
    row++;

    controls->broadening_max = gtk_adjustment_new(args->broadening_max,
                                                  0.0, MAX_BROADEN, 1.0, 10.0,
                                                  0.0);
    spin = gwy_table_attach_adjbar(table, row,
                                   _("Maximum broadening:"), _("px"),
                                   controls->broadening_max, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);
    g_signal_connect_swapped(controls->broadening_max, "value-changed",
                             G_CALLBACK(broadening_max_changed), controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls->survey_message = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls->survey_message), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls->survey_message,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls->run_survey = gtk_button_new_from_stock(GTK_STOCK_EXECUTE);
    align = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(align), controls->run_survey);
    gtk_table_attach(GTK_TABLE(table), align,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls->run_survey, "clicked",
                             G_CALLBACK(run_survey), controls);
    row++;

    return table;
}

static void
dialogue_response(GtkDialog *dialogue,
                  gint response_id,
                  TerraceControls *controls)
{
    static guint signal_id = 0;
    GwySelection *xsel;
    GwyGraphArea *area;

    if (response_id != RESPONSE_CLEAR)
        return;

    if (G_UNLIKELY(!signal_id))
        signal_id = g_signal_lookup("response", GTK_TYPE_DIALOG);

    g_signal_stop_emission(dialogue, signal_id, 0);
    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(controls->graph)));
    xsel = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);
    gwy_selection_clear(xsel);
}


static void
curve_changed(GtkComboBox *combo, TerraceControls *controls)
{
    TerraceArgs *args = controls->args;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    guint ndata;

    args->curve = gwy_enum_combo_box_get_active(combo);
    gmodel = gwy_graph_get_model(GWY_GRAPH(controls->graph));
    gwy_graph_model_remove_all_curves(gmodel);
    gcmodel = gwy_graph_model_get_curve(controls->parent_gmodel, args->curve);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    ndata = gwy_graph_curve_model_get_ndata(gcmodel);
    gwy_data_line_resample(controls->edges, ndata, GWY_INTERPOLATION_NONE);
    gwy_data_line_resample(controls->residuum, ndata, GWY_INTERPOLATION_NONE);
    gwy_data_line_resample(controls->background, ndata, GWY_INTERPOLATION_NONE);
    invalidate(controls);
    update_value_formats(controls);
}

static void
poly_degree_changed(TerraceControls *controls, GtkAdjustment *adj)
{
    TerraceArgs *args = controls->args;
    args->poly_degree = gwy_adjustment_get_int(adj);
    invalidate(controls);
}

static void
edge_kernel_size_changed(TerraceControls *controls, GtkAdjustment *adj)
{
    TerraceArgs *args = controls->args;
    args->edge_kernel_size = gtk_adjustment_get_value(adj);
    invalidate(controls);
}

static void
edge_threshold_changed(TerraceControls *controls, GtkAdjustment *adj)
{
    TerraceArgs *args = controls->args;
    args->edge_threshold = gtk_adjustment_get_value(adj);
    invalidate(controls);
}

static void
edge_broadening_changed(TerraceControls *controls, GtkAdjustment *adj)
{
    TerraceArgs *args = controls->args;
    args->edge_broadening = gtk_adjustment_get_value(adj);
    invalidate(controls);
}

static void
min_area_frac_changed(TerraceControls *controls, GtkAdjustment *adj)
{
    TerraceArgs *args = controls->args;
    args->min_area_frac = gtk_adjustment_get_value(adj);
    invalidate(controls);
}

static void
use_selection_changed(GtkToggleButton *toggle, TerraceControls *controls)
{
    TerraceArgs *args = controls->args;
    GwyGraph *graph = GWY_GRAPH(controls->graph);

    args->use_selection = gtk_toggle_button_get_active(toggle);
    gwy_graph_enable_user_input(graph, args->use_selection);
    if (args->use_selection) {
        gwy_graph_set_status(graph, GWY_GRAPH_STATUS_XSEL);
    }
    else {
        gwy_graph_set_status(graph, GWY_GRAPH_STATUS_PLAIN);
    }
    update_sensitivity(controls);
    invalidate(controls);
}

static void
independent_changed(GtkToggleButton *toggle, TerraceControls *controls)
{
    TerraceArgs *args = controls->args;
    args->independent = gtk_toggle_button_get_active(toggle);
    update_sensitivity(controls);
    invalidate(controls);
}

static void
preview_mode_changed(GtkComboBox *combo, TerraceControls *controls)
{
    TerraceArgs *args = controls->args;

    args->preview_mode = gwy_enum_combo_box_get_active(combo);
    fill_preview_graph(controls);
}

static void
report_style_changed(TerraceControls *controls, GwyResultsExport *rexport)
{
    controls->args->report_style = gwy_results_export_get_format(rexport);
}

static void
update_sensitivity(TerraceControls *controls)
{
    TerraceArgs *args = controls->args;
    GwyResultsExport *rexport_list, *rexport_result;
    gboolean sens;

    sens = !!controls->fres;
    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialogue),
                                      GTK_RESPONSE_OK, sens);
    rexport_list = GWY_RESULTS_EXPORT(controls->rexport_list);
    rexport_result = GWY_RESULTS_EXPORT(controls->rexport_result);
    gwy_results_export_set_actions_sensitive(rexport_list, sens);
    gwy_results_export_set_actions_sensitive(rexport_result, sens);

    sens = !args->use_selection;
    gwy_table_hscale_set_sensitive(controls->edge_kernel_size, sens);
    gwy_table_hscale_set_sensitive(controls->edge_threshold, sens);
    gwy_table_hscale_set_sensitive(controls->edge_broadening, sens);
    /* Unlike in 2D, where the mask can be still anything, here we do not
     * attempt to filter user's selection. */
    gwy_table_hscale_set_sensitive(controls->min_area_frac, sens);

    gtk_widget_set_sensitive(controls->survey_table, !args->independent);
    if (args->independent) {
        gtk_label_set_text(GTK_LABEL(controls->survey_message),
                           _("Survey cannot be run with independent degrees."));
    }
    else {
        sens = args->survey_poly || args->survey_broadening;
        if (sens) {
            TerraceArgs myargs = *args;
            guint n = count_survey_items(&myargs, NULL, NULL);
            gchar *s = g_strdup_printf(_("Number of combinations: %u."), n);

            gtk_label_set_text(GTK_LABEL(controls->survey_message), s);
            g_free(s);
        }
        else {
            gtk_label_set_text(GTK_LABEL(controls->survey_message),
                               _("No free parameters are selected."));
        }
        gtk_widget_set_sensitive(controls->run_survey, sens);
        sens = args->survey_poly;
        gwy_table_hscale_set_sensitive(controls->poly_degree_min, sens);
        gwy_table_hscale_set_sensitive(controls->poly_degree_max, sens);
        sens = args->survey_broadening;
        gwy_table_hscale_set_sensitive(controls->broadening_min, sens);
        gwy_table_hscale_set_sensitive(controls->broadening_max, sens);
    }
}

static void
output_flags_changed(GtkWidget *button, TerraceControls *controls)
{
    GSList *group = gwy_check_box_get_group(button);
    TerraceArgs *args = controls->args;

    args->output_flags = gwy_check_boxes_get_selected(group);
}

static void
survey_poly_changed(GtkToggleButton *toggle, TerraceControls *controls)
{
    TerraceArgs *args = controls->args;
    args->survey_poly = gtk_toggle_button_get_active(toggle);
    update_sensitivity(controls);
}

static void
poly_degree_min_changed(TerraceControls *controls, GtkAdjustment *adj)
{
    TerraceArgs *args = controls->args;
    args->poly_degree_min = gwy_adjustment_get_int(adj);
    if (args->poly_degree_min > args->poly_degree_max) {
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->poly_degree_max),
                                 args->poly_degree_min);
    }
    update_sensitivity(controls);
}

static void
poly_degree_max_changed(TerraceControls *controls, GtkAdjustment *adj)
{
    TerraceArgs *args = controls->args;
    args->poly_degree_max = gwy_adjustment_get_int(adj);
    if (args->poly_degree_min > args->poly_degree_max) {
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->poly_degree_min),
                                 args->poly_degree_max);
    }
    update_sensitivity(controls);
}

static void
survey_broadening_changed(GtkToggleButton *toggle, TerraceControls *controls)
{
    TerraceArgs *args = controls->args;
    args->survey_broadening = gtk_toggle_button_get_active(toggle);
    update_sensitivity(controls);
}

static void
broadening_min_changed(TerraceControls *controls, GtkAdjustment *adj)
{
    TerraceArgs *args = controls->args;
    args->broadening_min = gtk_adjustment_get_value(adj);
    if (args->broadening_min > args->broadening_max + 1e-14) {
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->broadening_max),
                                 args->broadening_min);
    }
    update_sensitivity(controls);
}

static void
broadening_max_changed(TerraceControls *controls, GtkAdjustment *adj)
{
    TerraceArgs *args = controls->args;
    args->broadening_max = gtk_adjustment_get_value(adj);
    if (args->broadening_min > args->broadening_max + 1e-14) {
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->broadening_min),
                                 args->broadening_max);
    }
    update_sensitivity(controls);
}

static void
graph_xsel_changed(G_GNUC_UNUSED GwySelection *selection,
                   G_GNUC_UNUSED gint hint,
                   TerraceControls *controls)
{
    if (controls->args->use_selection)
        invalidate(controls);
}

static void
invalidate(TerraceControls *controls)
{
    if (controls->sid)
        return;

    controls->sid = g_idle_add_full(G_PRIORITY_LOW, preview_gsource,
                                    controls, NULL);
}

static gboolean
preview_gsource(gpointer user_data)
{
    TerraceControls *controls = (TerraceControls*)user_data;

    if (!controls->sid)
        return FALSE;

    controls->sid = 0;
    preview(controls);

    return FALSE;
}

static void
free_fit_result(FitResult *fres)
{
    if (fres) {
        g_free(fres->solution);
        g_free(fres->invdiag);
        g_free(fres);
    }
}

static void
create_segmented_graph_curve(GwyGraphModel *gmodel,
                             GwyGraphCurveModel *gcmodel,
                             GArray *terracesegments,
                             const gdouble *xdata,
                             const gdouble *ydata)
{
    guint g, nterraces = terracesegments->len;
    GwyGraphCurveModel *gcmodel2;
    GString *str = g_string_new(NULL);

    for (g = 0; g < nterraces; g++) {
        const TerraceSegment *seg = &terrace_index(terracesegments, g);
        gcmodel2 = gwy_graph_curve_model_duplicate(gcmodel);
        g_string_printf(str, _("Segment %u"), g+1);
        g_object_set(gcmodel2,
                     "color", gwy_graph_get_preset_color(g+1),
                     "description", str->str,
                     NULL);
        gwy_graph_curve_model_set_data(gcmodel2,
                                       xdata + seg->i, ydata + seg->i,
                                       seg->npixels);
        gwy_graph_model_add_curve(gmodel, gcmodel2);
        g_object_unref(gcmodel2);
    }
    g_string_free(str, TRUE);
}

static int
compare_gint(const void *a, const void *b)
{
    const gint ia = *(const gint*)a;
    const gint ib = *(const gint*)b;

    if (ia < ib)
        return -1;
    if (ia > ib)
        return 1;
    return 0;
}

static void
create_one_output_graph(GwyGraphModel *gmodel,
                        GwyGraphModel *parent_gmodel,
                        TerraceArgs *args,
                        PreviewMode preview_mode,
                        GArray *terracesegments,
                        GwyDataLine *edges,
                        GwyDataLine *residuum,
                        GwyDataLine *background,
                        FitResult *fres,
                        gboolean for_preview)
{
    GwyGraphCurveModel *gcmodel;
    const gdouble *xdata, *ydata;
    GwyDataLine *dline;
    gdouble *d;
    guint i, j, ndata, nterraces;

    gcmodel = gwy_graph_model_get_curve(parent_gmodel, args->curve);
    xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    ndata = gwy_graph_curve_model_get_ndata(gcmodel);

    if (preview_mode == PREVIEW_DATA_FIT || preview_mode == PREVIEW_DATA_POLY) {
        gcmodel = gwy_graph_curve_model_duplicate(gcmodel);
        g_object_set(gcmodel,
                     "color", gwy_graph_get_preset_color(0),
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

    if (!fres && preview_mode != PREVIEW_STEPS)
        return;

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(1),
                 NULL);
    nterraces = terracesegments->len;

    if (preview_mode == PREVIEW_DATA_FIT) {
        /* Add segmented fit */
        dline = gwy_data_line_duplicate(residuum);
        d = gwy_data_line_get_data(dline);
        for (i = 0; i < ndata; i++)
            d[i] = ydata[i] - d[i];
        g_object_set(gcmodel, "line-width", 2, NULL);
        create_segmented_graph_curve(gmodel, gcmodel, terracesegments, xdata,
                                     d);
        g_object_unref(dline);
    }
    else if (preview_mode == PREVIEW_DATA_POLY) {
        /* Add full polynomial background shifted to all discrete levels. */
        const gdouble *solution = fres->solution;
        GwyGraphCurveModel *gcmodel2;
        GString *str = g_string_new(NULL);

        dline = gwy_data_line_duplicate(background);
        if (args->independent) {
            for (i = 0; i < nterraces; i++) {
                gwy_data_line_copy(background, dline);
                gwy_data_line_add(dline, solution[i]);

                gcmodel2 = gwy_graph_curve_model_duplicate(gcmodel);
                g_string_printf(str, _("Segment %u"), i+1);
                g_object_set(gcmodel2,
                             "color", gwy_graph_get_preset_color(i+1),
                             "description", str->str,
                             NULL);
                gwy_graph_curve_model_set_data(gcmodel2, xdata,
                                               gwy_data_line_get_data(dline),
                                               ndata);
                gwy_graph_model_add_curve(gmodel, gcmodel2);
                g_object_unref(gcmodel2);
            }
        }
        else {
            gint *levels = g_new(gint, nterraces);

            for (i = 0; i < nterraces; i++)
                levels[i] = terrace_index(terracesegments, i).level;

            qsort(levels, nterraces, sizeof(gint), compare_gint);
            for (i = 0; i < nterraces; i++) {
                if (i && levels[i-1] == levels[i])
                    continue;

                gwy_data_line_copy(background, dline);
                gwy_data_line_add(dline, solution[1] + levels[i]*solution[0]);

                gcmodel2 = gwy_graph_curve_model_duplicate(gcmodel);
                g_string_printf(str, _("Level %d"), levels[i]);
                g_object_set(gcmodel2, "description", str->str, NULL);
                gwy_graph_curve_model_set_data(gcmodel2, xdata,
                                               gwy_data_line_get_data(dline),
                                               ndata);
                gwy_graph_model_add_curve(gmodel, gcmodel2);
                g_object_unref(gcmodel2);
            }
            g_free(levels);
        }
        g_object_unref(dline);
        g_string_free(str, TRUE);
    }
    else if (preview_mode == PREVIEW_RESIDUUM) {
        /* Add segmented residuum. */
        create_segmented_graph_curve(gmodel, gcmodel, terracesegments, xdata,
                                     gwy_data_line_get_data(residuum));
    }
    else if (preview_mode == PREVIEW_TERRACES) {
        /* Add segmented ideal terraces. */
        const gdouble *solution = fres->solution;

        dline = gwy_data_line_new_alike(background, TRUE);
        d = gwy_data_line_get_data(dline);
        for (i = 0; i < nterraces; i++) {
            const TerraceSegment *seg = &terrace_index(terracesegments, i);
            gdouble h = (args->independent
                         ? solution[i]
                         : solution[1] + seg->level*solution[0]);

            for (j = 0; j < seg->npixels; j++)
                d[seg->i + j] = h;
        }
        create_segmented_graph_curve(gmodel, gcmodel, terracesegments, xdata,
                                     d);
        g_object_unref(dline);
    }
    else if (preview_mode == PREVIEW_LEVELLED) {
        /* Add segmented data minus background. */
        dline = gwy_data_line_duplicate(background);
        d = gwy_data_line_get_data(dline);
        for (i = 0; i < ndata; i++)
            d[i] = ydata[i] - d[i];
        gwy_graph_curve_model_set_data(gcmodel, xdata, d, ndata);
        g_object_set(gcmodel,
                     "color", gwy_graph_get_preset_color(0),
                     "description", _("Leveled surface"),
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        if (for_preview) {
            create_segmented_graph_curve(gmodel, gcmodel, terracesegments,
                                         xdata, d);
        }
        g_object_unref(dline);
    }
    else if (preview_mode == PREVIEW_BACKGROUND) {
        /* Add full background. */
        gwy_graph_curve_model_set_data(gcmodel, xdata,
                                       gwy_data_line_get_data(background),
                                       ndata);
        g_object_set(gcmodel,
                     "description", _("Polynomial background"),
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
    }
    else if (preview_mode == PREVIEW_STEPS) {
        gdouble stepxdata[2], stepydata[2];

        /* Add full step filter result. */
        g_object_set(gcmodel, "color", gwy_graph_get_preset_color(0), NULL);
        gwy_graph_curve_model_set_data(gcmodel, xdata,
                                       gwy_data_line_get_data(edges),
                                       ndata);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);

        gcmodel = gwy_graph_curve_model_new();
        stepxdata[0] = xdata[0];
        stepxdata[1] = xdata[ndata-1];
        stepydata[0] = stepydata[1]
            = args->edge_threshold/100.0*gwy_data_line_get_max(edges);
        gwy_graph_curve_model_set_data(gcmodel, stepxdata, stepydata, 2);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "line-style", GDK_LINE_ON_OFF_DASH,
                     "color", gwy_graph_get_preset_color(1),
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
    }

    g_object_unref(gcmodel);
}

static void
fill_preview_graph(TerraceControls *controls)
{
    GwyGraphModel *gmodel;

    gmodel = gwy_graph_get_model(GWY_GRAPH(controls->graph));
    gwy_graph_model_remove_all_curves(gmodel);
    create_one_output_graph(gmodel, controls->parent_gmodel,
                            controls->args, controls->args->preview_mode,
                            controls->terracesegments,
                            controls->edges,
                            controls->residuum,
                            controls->background,
                            controls->fres,
                            TRUE);
}

static void
preview(TerraceControls *controls)
{
    GwyDataLine *edges = controls->edges;
    GwyDataLine *residuum = controls->residuum;
    GwyDataLine *background = controls->background;
    GwyResults *results = controls->results;
    GwyGraphCurveModel *gcmodel;
    GwyGraphArea *area;
    GtkTreeModel *model;
    TerraceArgs *args = controls->args;
    FitResult *fres;
    const gchar *message = "";
    GwySelection *xsel;
    guint i;

    gwy_app_wait_cursor_start(GTK_WINDOW(controls->dialogue));

    free_fit_result(controls->fres);
    controls->fres = NULL;

    gcmodel = gwy_graph_model_get_curve(controls->parent_gmodel, args->curve);
    gwy_results_fill_graph_curve(results, "curve", gcmodel);
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(controls->terracelist));
    gwy_null_store_set_n_rows(GWY_NULL_STORE(model), 0);
    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(controls->graph)));
    xsel = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);
    fres = terrace_do(gwy_graph_curve_model_get_xdata(gcmodel),
                      gwy_graph_curve_model_get_ydata(gcmodel),
                      gwy_graph_curve_model_get_ndata(gcmodel),
                      edges, residuum, background,
                      controls->terracesegments, xsel, args, &message);

    gtk_label_set_text(GTK_LABEL(controls->message), message);
    controls->fres = fres;

    if (fres) {
        gwy_null_store_set_n_rows(GWY_NULL_STORE(model),
                                  controls->terracesegments->len);
        gwy_results_fill_values(results,
                                "nterraces", fres->nterraces,
                                "resid", fres->msq,
                                NULL);
        if (controls->args->independent)
            gwy_results_set_na(results, "step", "discrep", NULL);
        else {
            gwy_results_fill_values_with_errors(results,
                                                "step",
                                                fres->solution[0],
                                                sqrt(fres->invdiag[0])*fres->msq,
                                                NULL);
            gwy_results_fill_values(results, "discrep", fres->deltares, NULL);
        }

        for (i = 0; i < G_N_ELEMENTS(guivalues); i++) {
            gtk_label_set_markup(GTK_LABEL(controls->guivalues[i]),
                                 gwy_results_get_full(results, guivalues[i]));
        }
    }
    else {
        for (i = 0; i < G_N_ELEMENTS(guivalues); i++)
            gtk_label_set_text(GTK_LABEL(controls->guivalues[i]), "");
    }

#ifdef DEBUG
    if (fres) {
        printf("%d %g %g %g %g %u\n",
               controls->args->poly_degree,
               fres->solution[0],
               sqrt(fres->invdiag[0])*fres->msq,
               fres->msq,
               fres->deltares,
               fres->nterraces);
    }
#endif

    update_sensitivity(controls);
    fill_preview_graph(controls);
    gwy_app_wait_cursor_finish(GTK_WINDOW(controls->dialogue));
}

static void
make_segments_from_xsel(GArray *terracesegments,
                        const gdouble *xdata,
                        guint ndata,
                        GwySelection *xsel)
{
    gdouble epsilon = 1e-9*fabs(xdata[ndata-1] - xdata[0]);
    guint i, j, nsel;

    nsel = gwy_selection_get_data(xsel, NULL);
    for (i = 0; i < nsel; i++) {
        TerraceSegment seg;
        gdouble xseg[2];

        gwy_selection_get_object(xsel, i, xseg);
        xseg[0] -= epsilon;
        xseg[1] += epsilon;
        if (xseg[0] > xseg[1])
            GWY_SWAP(gdouble, xseg[0], xseg[1]);

        /* Find integer index ranges corresponding to the selection.  Be
         * careful to skip empty segments. */
        gwy_clear(&seg, 1);
        seg.xfrom = xseg[0];
        seg.xto = xseg[1];
        for (j = 0; j < ndata; j++) {
            if (xdata[j] >= xseg[0])
                break;
        }
        if (j == ndata)
            continue;

        seg.i = j;
        while (j < ndata && xdata[j] <= xseg[1])
            j++;
        if (j == seg.i)
            continue;

        seg.npixels = j - seg.i;
        g_array_append_val(terracesegments, seg);
    }
}

static inline void
step_gauss_line_integrals(gdouble t, gdouble x1, gdouble x2,
                          gdouble y1, gdouble y2,
                          gdouble *pu,
                          gdouble *s1, gdouble *sx, gdouble *sxx,
                          gdouble *sy, gdouble *sxy)
{
    gdouble u1 = *pu;
    gdouble u2 = exp(-0.5*t*t);
    gdouble h = x2 - x1;

    *s1 += h*(u1 + u2);
    *sx += h*(x1*u1 + x2*u2);
    *sxx += h*(x1*x1*u1 + x2*x2*u2);
    *sy += h*(u1*y1 + u2*y2);
    *sxy += h*(x1*y1*u1 + x2*y2*u2);

    *pu = u2;
}

/* Perform Gaussian step filter analogous to the 2D case, but try trapezoid
 * integration. The result values are in units of height and generally should
 * roughly estimate the edge heights. */
static void
apply_gaussian_step_filter(const gdouble *xdata,
                           const gdouble *ydata,
                           GwyDataLine *filtered,
                           gdouble dx,
                           gdouble sigma)
{
    gint n = gwy_data_line_get_res(filtered);
    gdouble *d = gwy_data_line_get_data(filtered);
    gint i;

    gwy_clear(d, n);
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(d,xdata,ydata,n,dx,sigma) \
            private(i)
#endif
    for (i = 2; i < n-2; i++) {
        gdouble t, u, xorigin = xdata[i];
        gdouble x1, x2, y1, y2, zlimfw, zlimback, det;
        gdouble s1, sx, sxx, sy, sxy;
        gint j;

        s1 = sx = sxx = sy = sxy = 0.0;
        u = t = x2 = 0.0;
        y2 = ydata[i];
        for (j = i+1; j < n; j++) {
            x1 = x2;
            y1 = y2;
            x2 = xdata[j] - xorigin;
            y2 = ydata[j];
            t = x2/(sigma*dx);
            step_gauss_line_integrals(t, x1, x2, y1, y2,
                                      &u, &s1, &sx, &sxx, &sy, &sxy);
            if (t > 8.0)
                break;
        }
        det = s1*sxx - sx*sx;
        zlimfw = (det > 0.0 ? (sy*sxx - sxy*sx)/det : ydata[i]);

        s1 = sx = sxx = sy = sxy = 0.0;
        u = t = x2 = 0.0;
        y2 = ydata[i];
        for (j = i-1; j >= 0; j--) {
            x1 = x2;
            y1 = y2;
            x2 = xorigin - xdata[j];
            y2 = ydata[j];
            t = x2/(sigma*dx);
            step_gauss_line_integrals(t, x1, x2, y1, y2,
                                      &u, &s1, &sx, &sxx, &sy, &sxy);
            if (t > 8.0)
                break;
        }

        det = s1*sxx - sx*sx;
        zlimback = (det > 0.0 ? (sy*sxx - sxy*sx)/det : ydata[i]);

        d[i] = fabs(zlimfw - zlimback);
    }
}

static void
enumerate_line_segments(GwyDataLine *marked, const gdouble *xdata,
                        GArray *terracesegments)
{
    gint n = gwy_data_line_get_res(marked);
    gdouble *md = gwy_data_line_get_data(marked);
    gint i, prevedge;
    TerraceSegment seg;

    g_array_set_size(terracesegments, 0);

    prevedge = 0;
    for (i = 1; i < n; i++) {
        /* An edge? */
        if (md[i-1] != md[i]) {
            if (md[i] == 0.0) {
                /* Downwards edge.  The previous segment is a grain. */
                gwy_clear(&seg, 1);
                seg.xfrom = (prevedge
                             ? 0.5*(xdata[prevedge-1] + xdata[prevedge])
                             : 1.5*xdata[0] - 0.5*xdata[1]);
                seg.xto = 0.5*(xdata[i-1] + xdata[i]);
                seg.i = prevedge;
                seg.npixels = i-prevedge;
                g_array_append_val(terracesegments, seg);
            }
            /* If it's an upwards edge, just remember its position.  Do that
             * in any case. */
            prevedge = i;
        }
    }

    /* Data ending inside a grain is the same as downwards edge. */
    if (md[n-1]) {
        gwy_clear(&seg, 1);
        seg.xfrom = (prevedge
                     ? 0.5*(xdata[prevedge-1] + xdata[prevedge])
                     : 1.5*xdata[0] - 0.5*xdata[1]);
        seg.xto = 1.5*xdata[n-1] - 0.5*xdata[n-2];
        seg.i = prevedge;
        seg.npixels = n-prevedge;
        g_array_append_val(terracesegments, seg);
    }
}

/* Shrink grains using real distance (i.e. possibly non-uniform sampling). */
static void
shrink_grains(GwyDataLine *marked, const gdouble *xdata, gdouble distance,
              GArray *terracesegments)
{
    gint j, n = gwy_data_line_get_res(marked);
    gdouble *md = gwy_data_line_get_data(marked);
    guint g, nseg;

    nseg = terracesegments->len;
    for (g = 0; g < nseg; g++) {
        const TerraceSegment *seg = &terrace_index(terracesegments, g);

        /* Extend non-grain forward from the left edge. */
        if (seg->i > 0) {
            for (j = seg->i+1; j < n && md[j]; j++) {
                if (xdata[j] - seg->xfrom <= distance)
                    md[j] = 0.0;
            }
        }
        /* Extend non-grain bacwkard from the right edge. */
        if (seg->i + seg->npixels < n) {
            for (j = seg->i + seg->npixels-1; j >= 0 && md[j]; j--) {
                if (seg->xto - xdata[j] <= distance)
                    md[j] = 0.0;
            }
        }
    }
}

/* Remove grains by size using real distance (i.e. possibly non-uniform
 * sampling). */
static void
remove_grains_by_size(GwyDataLine *marked, gdouble minsize,
                      GArray *terracesegments)
{
    gdouble *md = gwy_data_line_get_data(marked);
    guint g, nseg;

    /* Iterate backwards.  We do not need to care how the array changes beyond
     * the current item and the removals are slightly more efficient. */
    nseg = terracesegments->len;
    for (g = nseg; g; g--) {
        const TerraceSegment *seg = &terrace_index(terracesegments, g-1);

        if (seg->xto - seg->xfrom < minsize) {
            gwy_clear(md+seg->i, seg->npixels);
            g_array_remove_index(terracesegments, g-1);
        }
    }
}

static gboolean
find_terrace_segments(GArray *terracesegments,
                      const gdouble *xdata,
                      const gdouble *ydata,
                      guint ndata,
                      const TerraceArgs *args,
                      GwyDataLine *edges,
                      GwyDataLine *marked,
                      GwySelection *xsel,
                      gdouble *pxc,
                      gdouble *pxq)
{
    gint i, npixels;
    gdouble threshold, xlen, min, max, dx, xc, xq;
    guint g, nseg;

    g_array_set_size(terracesegments, 0);

    if (ndata < 3)
        return FALSE;

    /* Use a common `pixel' size. */
    gwy_assign(gwy_data_line_get_data(edges), xdata, ndata);
    gwy_data_line_get_min_max(edges, &min, &max);
    xlen = max - min;
    dx = xlen/ndata;

    /* Always calculate the Gaussian filter to have something to display
     * even when we do not use the result for fitting. */
    apply_gaussian_step_filter(xdata, ydata, edges, dx, args->edge_kernel_size);
    gwy_data_line_copy(edges, marked);

    if (args->use_selection) {
        /* Use provided selection as requested. */
        make_segments_from_xsel(terracesegments, xdata, ndata, xsel);
    }
    else {
        /* Mark flat areas in the profile. */
        threshold = args->edge_threshold/100.0*gwy_data_line_get_max(marked);
        gwy_data_line_threshold(marked, threshold, 1.0, 0.0);
        enumerate_line_segments(marked, xdata, terracesegments);
        shrink_grains(marked, xdata, args->edge_broadening*dx, terracesegments);

        /* Keep only large areas.  This inherently limits the maximum number of
         * areas too. */
        enumerate_line_segments(marked, xdata, terracesegments);
        remove_grains_by_size(marked, args->min_area_frac/100.0 * xlen,
                              terracesegments);
    }

    nseg = terracesegments->len;
    if (!nseg) {
        g_array_set_size(terracesegments, 0);
        return FALSE;
    }

    /* Normalise coordinates to have centre of mass at 0 and effective range in
     * the order of unity (we normalise the integral of x² to 1). */
    xc = 0.0;
    npixels = 0;
    for (g = 0; g < nseg; g++) {
        const TerraceSegment *seg = &terrace_index(terracesegments, g);
        gint n = seg->npixels;

        npixels += n;
        for (i = 0; i < n; i++)
            xc += xdata[i + seg->i];
    }
    xc /= npixels;
    *pxc = xc;

    xq = 0.0;
    for (g = 0; g < nseg; g++) {
        const TerraceSegment *seg = &terrace_index(terracesegments, g);
        gint n = seg->npixels;
        for (i = 0; i < n; i++) {
            gdouble t = xdata[i + seg->i];
            xq += t*t;
        }
    }
    xq /= npixels;
    *pxq = (xq > 0.0 ? 1.0/sqrt(xq) : 1.0);

    return TRUE;
}

/* Diagonal power-power matrix block.  Some of the entries could be
 * calculated from the per-terrace averages; the higher powers are only
 * used here though.  This is the slow part.  */
static gdouble*
calculate_power_matrix_block(GArray *terracesegments, const gdouble *xdata,
                             gdouble xc, gdouble xq, gint poly_degree)
{
    guint nterraces, kp, mp;
    gdouble *power_block;

    if (!poly_degree)
        return NULL;

    /* We multiply two powers together so the maximum power in the product is
     * twice the single maximum power.  Also, there would be poly_degree+1
     * powers, but we omit the 0th power so we have exactly poly_degree powers,
     * starting from 1. */
    nterraces = terracesegments->len;
    power_block = g_new0(gdouble, poly_degree*poly_degree);

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(terracesegments,poly_degree,power_block,xdata,xc,xq,nterraces)
#endif
    {
        gdouble *xpowers = g_new(gdouble, 2*poly_degree+1);
        gdouble *tpower_block = gwy_omp_if_threads_new0(power_block,
                                                        poly_degree*poly_degree);
        guint gfrom = gwy_omp_chunk_start(nterraces);
        guint gto = gwy_omp_chunk_end(nterraces);
        guint m, k, g, i;

        xpowers[0] = 1.0;

        for (g = gfrom; g < gto; g++) {
            TerraceSegment *seg = &terrace_index(terracesegments, g);
            guint ifrom = seg->i, npixels = seg->npixels;

            for (i = 0; i < npixels; i++) {
                gdouble x = xq*(xdata[ifrom + i] - xc);

                for (k = 1; k <= 2*poly_degree; k++)
                    xpowers[k] = xpowers[k-1]*x;

                for (k = 1; k <= poly_degree; k++) {
                    for (m = 1; m <= k; m++)
                        tpower_block[(k-1)*poly_degree + (m-1)] += xpowers[k+m];
                }
            }
        }
        g_free(xpowers);
        gwy_omp_if_threads_sum_double(power_block, tpower_block,
                                      poly_degree*poly_degree);
    }

    /* Redundant, but keep for simplicity. */
    for (kp = 0; kp < poly_degree; kp++) {
        for (mp = kp+1; mp < poly_degree; mp++)
            power_block[kp*poly_degree + mp] = power_block[mp*poly_degree + kp];
    }

    return power_block;
}

static void
calculate_residuum(GArray *terracesegments, FitResult *fres,
                   GwyDataLine *residuum,
                   const gdouble *xdata, const gdouble *ydata,
                   gdouble xc, gdouble xq,
                   gint poly_degree,
                   gboolean indep)
{
    const gdouble *solution = fres->solution, *solution_block;
    gdouble *resdata;
    guint g, i, k, nterraces = terracesegments->len, npixels;

    solution_block = solution + (indep ? nterraces : 2);
    gwy_data_line_clear(residuum);
    resdata = gwy_data_line_get_data(residuum);

    fres->msq = fres->deltares = 0.0;
    npixels = 0;
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint ifrom = seg->i, n = seg->npixels;
        gint ng = seg->level;
        gdouble z0 = (indep ? solution[g] : ng*solution[0] + solution[1]);
        gdouble ts = 0.0, toff = 0.0;

        for (i = 0; i < n; i++) {
            gdouble x = xq*(xdata[ifrom + i] - xc), y = ydata[ifrom + i];
            gdouble xp = 1.0, s = z0;

            for (k = 0; k < poly_degree; k++) {
                xp *= x;
                s += xp*solution_block[k];
            }
            s = y - s;
            resdata[ifrom + i] = s;
            ts += s*s;
            toff += s;
        }
        seg->residuum = sqrt(ts/n);
        seg->error = toff/n;
        fres->msq += ts;
        fres->deltares += seg->error*seg->error * n;
        npixels += n;
    }
    fres->msq = sqrt(fres->msq/npixels);
    fres->deltares = sqrt(fres->deltares/npixels);
}

static FitResult*
fit_terraces_arbitrary(GArray *terracesegments,
                       const gdouble *xdata, const gdouble *ydata,
                       gdouble xc, gdouble xq,
                       gint poly_degree,
                       const gdouble *power_block,
                       GwyDataLine *residuum,
                       const gchar **message)
{
    gint g, k, nterraces, matn, matsize, npixels;
    gdouble *mixed_block, *matrix, *invmat, *rhs;
    FitResult *fres;
    guint i, j;
    gboolean ok;

    fres = g_new0(FitResult, 1);
    nterraces = fres->nterrparam = fres->nterraces = terracesegments->len;
    fres->npowers = poly_degree;
    matn = nterraces + poly_degree;

    /* Calculate the matrix by pieces, the put it together.  The terrace block
     * is identity matrix I so we do not need to compute it. */
    mixed_block = g_new0(gdouble, poly_degree*nterraces);
    rhs = fres->solution = g_new0(gdouble, nterraces + poly_degree);
    fres->invdiag = g_new0(gdouble, matn);

    /* Mixed off-diagonal power-terrace matrix block (we represent it as the
     * upper right block) and power block on the right hand side. */
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint ifrom = seg->i, n = seg->npixels;
        gdouble *mixed_row = mixed_block + g*poly_degree;
        gdouble *rhs_block = rhs + nterraces;

        for (i = 0; i < n; i++) {
            gdouble x = xq*(xdata[ifrom + i] - xc), y = ydata[ifrom + i];
            gdouble xp = 1.0;

            for (k = 1; k <= poly_degree; k++) {
                xp *= x;
                mixed_row[k-1] += xp;
                rhs_block[k-1] += xp*y;
            }
        }
    }

    /* Terrace block of right hand side. */
    npixels = 0;
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint ifrom = seg->i, n = seg->npixels;

        for (i = 0; i < n; i++) {
            gdouble y = ydata[ifrom + i];
            rhs[g] += y;
        }
        npixels += n;
    }

    /* Construct the matrix. */
    matsize = (matn + 1)*matn/2;
    matrix = g_new(gdouble, matsize);
    gwy_debug("matrix (%u)", matn);
    for (i = 0; i < matn; i++) {
        for (j = 0; j <= i; j++) {
            gdouble t;

            if (i < nterraces && j < nterraces) {
                t = (i == j)*(terrace_index(terracesegments, i).npixels);
            }
            else if (j < nterraces)
                t = mixed_block[j*poly_degree + (i - nterraces)];
            else
                t = power_block[(i - nterraces)*poly_degree + (j - nterraces)];

            SLi(matrix, i, j) = t;
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
    gwy_math_choleski_solve(matn, matrix, rhs);

    if (residuum) {
        calculate_residuum(terracesegments, fres, residuum,
                           xdata, ydata, xc, xq, poly_degree, TRUE);
    }

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
    g_free(matrix);
    g_free(invmat);

    return fres;
}

static FitResult*
fit_terraces_same_step(GArray *terracesegments,
                       const gdouble *xdata, const gdouble *ydata,
                       gdouble xc, gdouble xq,
                       gint poly_degree,
                       const gdouble *power_block,
                       GwyDataLine *residuum,
                       const gchar **message)
{
    gint g, k, nterraces, matn, matsize, npixels;
    gdouble *sheight_block, *offset_block, *matrix, *invmat, *rhs;
    gdouble stepstep, stepoff, offoff;
    FitResult *fres;
    guint i, j;
    gboolean ok;

    fres = g_new0(FitResult, 1);
    nterraces = fres->nterraces = terracesegments->len;
    fres->npowers = poly_degree;
    fres->nterrparam = 2;
    matn = 2 + poly_degree;

    /* Calculate the matrix by pieces, the put it together.  */
    sheight_block = g_new0(gdouble, poly_degree);
    offset_block = g_new0(gdouble, poly_degree);
    rhs = fres->solution = g_new0(gdouble, matn);
    fres->invdiag = g_new0(gdouble, matn);

    /* Mixed two first upper right matrix rows and power block of right hand
     * side. */
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint ifrom = seg->i, n = seg->npixels;
        gint ng = seg->level;
        gdouble *rhs_block = rhs + 2;

        for (i = 0; i < n; i++) {
            gdouble x = xq*(xdata[ifrom + i] - xc), y = ydata[ifrom + i];
            gdouble xp = 1.0;

            for (k = 1; k <= poly_degree; k++) {
                xp *= x;
                sheight_block[k-1] += xp*ng;
                offset_block[k-1] += xp;
                rhs_block[k-1] += xp*y;
            }
        }
    }

    /* Remaining three independent elements in the top left corner of the
     * matrix. */
    stepstep = stepoff = 0.0;
    npixels = 0;
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint n = seg->npixels;
        gint ng = seg->level;

        /* Ensure ng does not converted to unsigned, with disasterous
         * consequences. */
        stepstep += ng*ng*(gdouble)n;
        stepoff += ng*(gdouble)n;
        npixels += n;
    }
    offoff = npixels;

    /* Remaining first two elements of the right hand side. */
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint ifrom = seg->i, n = seg->npixels;
        gint ng = seg->level;

        for (i = 0; i < n; i++) {
            gdouble y = ydata[ifrom + i];
            rhs[0] += ng*y;
            rhs[1] += y;
        }
    }

    /* Construct the matrix. */
    matsize = (matn + 1)*matn/2;
    matrix = g_new(gdouble, matsize);

    gwy_debug("matrix (%u)", matn);
    SLi(matrix, 0, 0) = stepstep;
#ifdef DEBUG_MATRIX
    printf("% .2e\n", stepstep);
#endif
    SLi(matrix, 1, 0) = stepoff;
    SLi(matrix, 1, 1) = offoff;
#ifdef DEBUG_MATRIX
    printf("% .2e % .2e\n", stepoff, offoff);
#endif

    for (i = 2; i < matn; i++) {
        for (j = 0; j <= i; j++) {
            gdouble t;

            if (j == 0)
                t = sheight_block[i-2];
            else if (j == 1)
                t = offset_block[i-2];
            else
                t = power_block[(i - 2)*poly_degree + (j - 2)];

            SLi(matrix, i, j) = t;
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
    gwy_math_choleski_solve(matn, matrix, rhs);

    if (residuum) {
        calculate_residuum(terracesegments, fres, residuum,
                           xdata, ydata, xc, xq, poly_degree, FALSE);
    }

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
    g_free(invmat);
    g_free(matrix);

    return fres;
}

static gint*
estimate_step_parameters(const gdouble *heights, guint n,
                         gdouble *stepheight, gdouble *offset,
                         const gchar **message)
{
    gdouble *steps;
    gdouble sh, off, p;
    gint *levels;
    gint g, ns, m;

    if (n < 2) {
        *message = _("No suitable terrace steps found");
        return NULL;
    }

    steps = g_memdup(heights, n*sizeof(gdouble));
    ns = n-1;
    for (g = 0; g < ns; g++) {
        steps[g] = fabs(steps[g+1] - steps[g]);
        gwy_debug("step%d: height %g nm", g, steps[g]/1e-9);
    }

    p = 85.0;
    gwy_math_percentiles(ns, steps, GWY_PERCENTILE_INTERPOLATION_LINEAR,
                         1, &p, &sh);
    gwy_debug("estimated step height %g nm", sh/1e-9);
    g_free(steps);

    *stepheight = sh;

    levels = g_new(gint, n);
    levels[0] = 0;
    m = 0;
    for (g = 1; g < n; g++) {
        levels[g] = levels[g-1] + GWY_ROUND((heights[g] - heights[g-1])/sh);
        m = MIN(m, levels[g]);
    }

    off = 0.0;
    for (g = 0; g < n; g++) {
        levels[g] -= m;
        off += heights[g] - sh*levels[g];
    }
    off /= n;
    *offset = off;
    gwy_debug("estimated base offset %g nm", off/1e-9);

    return levels;
}

/* XXX: The background is generally bogus far outside the fitted region.  This
 * usually means profile ends because they contain too small terrace bits. It
 * is more meaningful to only calculate it for marked area. */
static void
fill_background(GwyDataLine *background,
                const gdouble *xdata, gdouble xc, gdouble xq,
                gint poly_degree,
                const gdouble *coeffs)
{
    gint res, i, k;
    gdouble *d;

    res = gwy_data_line_get_res(background);
    d = gwy_data_line_get_data(background);
    for (i = 0; i < res; i++) {
        gdouble x = xq*(xdata[i] - xc);
        gdouble xp = 1.0, s = 0.0;

        for (k = 1; k <= poly_degree; k++) {
            xp *= x;
            s += xp*coeffs[k-1];
        }
        d[i] = s;
    }
}

static FitResult*
terrace_do(const gdouble *xdata, const gdouble *ydata, guint ndata,
           GwyDataLine *edges, GwyDataLine *residuum, GwyDataLine *background,
           GArray *terracesegments, GwySelection *xsel,
           const TerraceArgs *args,
           const gchar** message)
{
    guint nterraces;
    FitResult *fres;
    gdouble sheight, offset, xc, xq;
    gdouble *power_block;
    gboolean indep = args->independent;
    gint g, poly_degree = args->poly_degree;
    gint *levels;

    /* Use background dataline as a scratch space. */
    if (!find_terrace_segments(terracesegments, xdata, ydata, ndata, args,
                               edges, background, xsel, &xc, &xq)) {
        *message = _("No terraces were found");
        return NULL;
    }

    nterraces = terracesegments->len;
    power_block = calculate_power_matrix_block(terracesegments, xdata, xc, xq,
                                               poly_degree);
    if (!(fres = fit_terraces_arbitrary(terracesegments,
                                        xdata, ydata, xc, xq, poly_degree,
                                        power_block,
                                        indep ? residuum : NULL,
                                        message)))
        goto finalise;

    if (!(levels = estimate_step_parameters(fres->solution, nterraces,
                                            &sheight, &offset, message))) {
        g_array_set_size(terracesegments, 0);
        free_fit_result(fres);
        fres = NULL;
        goto finalise;
    }
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);

        /* This does not depend on whether we run the second stage fit. */
        seg->level = levels[g];
        seg->height = fres->solution[g];
        /* This will be recalculated in the second stage fit.  Note that error
         * is anyway with respect to the multiple of estimated step height
         * and normally similar in both fit types. */
        seg->error = fres->solution[g] - offset - seg->level*sheight;
    }
    g_free(levels);

    /* Normally also perform the second stage fitting with a single common
     * step height.  But if requested, avoid it, keeping the heights
     * independents. */
    if (!indep) {
        free_fit_result(fres);
        if (!(fres = fit_terraces_same_step(terracesegments,
                                            xdata, ydata, xc, xq, poly_degree,
                                            power_block,
                                            indep ? NULL : residuum,
                                            message)))
            goto finalise;
    }
    fill_background(background, xdata, xc, xq, poly_degree,
                    fres->solution + (indep ? nterraces : 2));

finalise:
    g_free(power_block);

    return fres;
}

static void
create_output_graphs(TerraceControls *controls, GwyContainer *data)
{
    const guint output_map[2*OUTPUT_NFLAGS] = {
        PREVIEW_DATA_FIT, OUTPUT_DATA_FIT,
        PREVIEW_DATA_POLY, OUTPUT_DATA_POLY,
        PREVIEW_RESIDUUM, OUTPUT_RESIDUUM,
        PREVIEW_TERRACES, OUTPUT_TERRACES,
        PREVIEW_LEVELLED, OUTPUT_LEVELLED,
        PREVIEW_BACKGROUND, OUTPUT_BACKGROUND,
    };
    TerraceArgs *args = controls->args;
    GArray *terracesegments = controls->terracesegments;
    GwyDataLine *edges = controls->edges,
                *residuum = controls->residuum,
                *background = controls->background;
    FitResult *fres = controls->fres;
    guint i, oflags = controls->args->output_flags;
    GwyGraphModel *gmodel, *parent_gmodel = controls->parent_gmodel;
    const gchar *title;

    for (i = 0; i < OUTPUT_NFLAGS; i++) {
        if (!(output_map[2*i + 1] & oflags))
            continue;

        gmodel = gwy_graph_model_new_alike(parent_gmodel);
        create_one_output_graph(gmodel, parent_gmodel, args, output_map[2*i],
                                terracesegments, edges, residuum, background,
                                fres, FALSE);
        title = gwy_enum_to_string(output_map[2*i + 1],
                                   output_flags, OUTPUT_NFLAGS);
        g_object_set(gmodel, "title", _(title), NULL);
        gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);
        g_object_unref(gmodel);
    }
}

static void
save_report(TerraceControls *controls)
{
    gchar *text;

    text = format_report(controls);
    gwy_save_auxiliary_data(_("Save Table"),
                            GTK_WINDOW(controls->dialogue), -1, text);
    g_free(text);
}

static void
copy_report(TerraceControls *controls)
{
    GtkClipboard *clipboard;
    GdkDisplay *display;
    gchar *text;

    text = format_report(controls);
    display = gtk_widget_get_display(controls->dialogue);
    clipboard = gtk_clipboard_get_for_display(display, GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    g_free(text);
}

static gchar*
format_report(TerraceControls *controls)
{
    GwyResultsReportType report_style = controls->args->report_style;
    GArray *terracesegments = controls->terracesegments;
    GwySIUnitFormatStyle style = GWY_SI_UNIT_FORMAT_UNICODE;
    GwySIValueFormat *vfz;
    GwySIUnit *yunit;
    GString *text;
    const gchar *k_header, *Npx_header;
    gchar *retval, *h_header, *Delta_header, *r_header;
    guint g, nterraces;

    text = g_string_new(NULL);
    if (!(report_style & GWY_RESULTS_REPORT_MACHINE)) {
        vfz = controls->vf;
    }
    else {
        g_object_get(controls->parent_gmodel, "si-unit-y", &yunit, NULL);
        vfz = gwy_si_unit_get_format_for_power10(yunit, style, 0, NULL);
        g_object_unref(yunit);
    }

    h_header = g_strdup_printf("h [%s]", vfz->units);
    k_header = "k";
    Npx_header = "Npx";
    Delta_header = g_strdup_printf("Δ [%s]", vfz->units);
    r_header = g_strdup_printf("r [%s]", vfz->units);
    gwy_format_result_table_strings(text, report_style, 5,
                                    h_header, k_header, Npx_header,
                                    Delta_header, r_header);
    g_free(h_header);
    g_free(Delta_header);
    g_free(r_header);

    nterraces = terracesegments->len;
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);

        gwy_format_result_table_mixed(text, report_style, "viivv",
                                      seg->height/vfz->magnitude,
                                      seg->level,
                                      seg->npixels,
                                      seg->error/vfz->magnitude,
                                      seg->residuum/vfz->magnitude);
    }

    retval = text->str;
    g_string_free(text, FALSE);

    return retval;
}

static gdouble
interpolate_broadening(gdouble a, gdouble b, gdouble t)
{
    return pow((1.0 - t)*pow(a, pwr) + t*pow(b, pwr), 1.0/pwr);
}

/* NB: Modifies args! */
static guint
count_survey_items(TerraceArgs *args,
                   guint *pndegrees, guint *pnbroadenings)
{
    guint ndegrees, nbroadenings;

    if (!args->survey_poly)
        args->poly_degree_min = args->poly_degree_max = args->poly_degree;

    ndegrees = args->poly_degree_max - args->poly_degree_min + 1;
    if (pndegrees)
        *pndegrees = ndegrees;

    if (!args->survey_broadening)
        args->broadening_min = args->broadening_max = args->edge_broadening;
    nbroadenings = GWY_ROUND(2.0*(pow(args->broadening_max, pwr)
                                  - pow(args->broadening_min, pwr))) + 1;

    if (pnbroadenings)
        *pnbroadenings = nbroadenings;

    return nbroadenings*ndegrees;
}

static void
run_survey(TerraceControls *controls)
{
    GwyDataLine *edges, *residuum, *background;
    GwyGraphCurveModel *gcmodel;
    GwyGraphArea *area;
    TerraceArgs myargs;
    FitResult *fres;
    GwySelection *xsel;
    GArray *terracesegments, *surveyout;
    GwyResultsReportType report_style;
    gint ndata;
    const gdouble *xdata, *ydata;
    const gchar *message;
    GString *str;
    guint i, w, ndegrees, nbroadenings, totalwork;
    gdouble *broadening_values;
    gint *degree_values;

    myargs = *controls->args;

    report_style = myargs.report_style & ~GWY_RESULTS_REPORT_MACHINE;
    if (report_style == GWY_RESULTS_REPORT_COLON)
        report_style = GWY_RESULTS_REPORT_TABSEP;
    report_style |= GWY_RESULTS_REPORT_MACHINE;

    gcmodel = gwy_graph_model_get_curve(controls->parent_gmodel, myargs.curve);
    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(controls->graph)));
    xsel = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);

    xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    ndata = gwy_graph_curve_model_get_ndata(gcmodel);

    edges = gwy_data_line_new_alike(controls->edges, FALSE);
    residuum = gwy_data_line_new_alike(controls->residuum, FALSE);
    background = gwy_data_line_new_alike(controls->background, FALSE);
    terracesegments = g_array_new(FALSE, FALSE, sizeof(TerraceSegment));
    surveyout = g_array_new(FALSE, FALSE, sizeof(TerraceSurveyRow));

    totalwork = count_survey_items(&myargs, &ndegrees, &nbroadenings);
    degree_values = g_new(gint, ndegrees);
    for (i = 0; i < ndegrees; i++)
        degree_values[i] = myargs.poly_degree_min + i;

    broadening_values = g_new(gdouble, nbroadenings);
    for (i = 0; i < nbroadenings; i++) {
        gdouble t = (nbroadenings == 1 ? 0.5 : i/(nbroadenings - 1.0));
        broadening_values[i] = interpolate_broadening(myargs.broadening_min,
                                                      myargs.broadening_max,
                                                      t);
    }

    gwy_app_wait_start(GTK_WINDOW(controls->dialogue),
                       _("Fitting in progress..."));

    for (w = 0; w < totalwork; w++) {
        TerraceSurveyRow srow;

        myargs.poly_degree = degree_values[w/nbroadenings];
        myargs.edge_broadening = broadening_values[w % nbroadenings];
        fres = terrace_do(xdata, ydata, ndata, edges, residuum, background,
                          terracesegments, xsel, &myargs, &message);

        gwy_clear(&srow, 1);
        srow.poly_degree = myargs.poly_degree;
        srow.edge_kernel_size = myargs.edge_kernel_size;
        srow.edge_threshold = myargs.edge_threshold;
        srow.edge_broadening = myargs.edge_broadening;
        srow.min_area_frac = myargs.min_area_frac;
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

    g_free(degree_values);
    g_free(broadening_values);
    g_array_free(terracesegments, TRUE);
    g_object_unref(edges);
    g_object_unref(residuum);
    g_object_unref(background);

    if (w != totalwork) {
        g_array_free(surveyout, TRUE);
        return;
    }

    str = g_string_new(NULL);
    gwy_format_result_table_strings(str, report_style, 11,
                                    "Poly degree", "Edge kernel size",
                                    "Edge threshold", "Edge broadening",
                                    "Min area frac", "Fit OK", "Num terraces",
                                    "Step height", "Step height err",
                                    "Msq residual", "Discrepancy");
    for (i = 0; i < surveyout->len; i++) {
        TerraceSurveyRow *srow = &g_array_index(surveyout, TerraceSurveyRow, i);
        gwy_format_result_table_mixed(str, report_style, "ivvvvyivvvv",
                                      srow->poly_degree,
                                      srow->edge_kernel_size,
                                      srow->edge_threshold,
                                      srow->edge_broadening,
                                      srow->min_area_frac,
                                      srow->fit_ok,
                                      srow->nterraces,
                                      srow->step,
                                      srow->step_err,
                                      srow->msq,
                                      srow->discrep);
    }
    g_array_free(surveyout, TRUE);

    gwy_save_auxiliary_data(_("Save Terrace Fit Survey"),
                            GTK_WINDOW(controls->dialogue),
                            str->len, str->str);
    g_string_free(str, TRUE);
}

static const gchar edge_broadening_key[]   = "/module/graph_terraces/edge_broadening";
static const gchar edge_kernel_size_key[]  = "/module/graph_terraces/edge_kernel_size";
static const gchar edge_threshold_key[]    = "/module/graph_terraces/edge_threshold";
static const gchar independent_key[]       = "/module/graph_terraces/independent";
static const gchar min_area_frac_key[]     = "/module/graph_terraces/min_area_frac";
static const gchar output_flags_key[]      = "/module/graph_terraces/output_flags";
static const gchar poly_degree_key[]       = "/module/graph_terraces/poly_degree";
static const gchar poly_degree_max_key[]   = "/module/graph_terraces/poly_degree_max";
static const gchar poly_degree_min_key[]   = "/module/graph_terraces/poly_degree_min";
static const gchar broadening_max_key[]    = "/module/graph_terraces/broadening_max";
static const gchar broadening_min_key[]    = "/module/graph_terraces/broadening_min";
static const gchar report_style_key[]      = "/module/graph_terraces/report_style";
static const gchar survey_poly_key[]       = "/module/graph_terraces/survey_poly";
static const gchar survey_broadening_key[] = "/module/graph_terraces/survey_broadening";
static const gchar use_selection_key[]     = "/module/graph_terraces/use_selection";

static void
sanitize_args(TerraceArgs *args)
{
    args->poly_degree = CLAMP(args->poly_degree, 0, MAX_DEGREE);
    args->edge_kernel_size = CLAMP(args->edge_kernel_size, 1.0, 64.0);
    args->edge_threshold = CLAMP(args->edge_threshold, 0.0, 100.0);
    args->edge_broadening = CLAMP(args->edge_broadening, 0.0, MAX_BROADEN);
    args->min_area_frac = CLAMP(args->min_area_frac, 0.1, 40.0);
    args->independent = !!args->independent;
    args->use_selection = !!args->use_selection;
    args->output_flags &= OUTPUT_ALL;
    args->survey_poly = !!args->survey_poly;
    args->survey_broadening = !!args->survey_broadening;
    args->poly_degree_min = CLAMP(args->poly_degree_min, 0, MAX_DEGREE);
    args->poly_degree_max = CLAMP(args->poly_degree_max,
                                  args->poly_degree_min, MAX_DEGREE);
    args->broadening_min = CLAMP(args->broadening_min, 0, MAX_BROADEN);
    args->broadening_max = CLAMP(args->broadening_max,
                                 args->broadening_min, MAX_BROADEN);
}

static void
load_args(GwyContainer *settings, TerraceArgs *args)
{
    *args = terraces_defaults;

    gwy_container_gis_int32_by_name(settings, poly_degree_key,
                                    &args->poly_degree);
    gwy_container_gis_double_by_name(settings, edge_kernel_size_key,
                                     &args->edge_kernel_size);
    gwy_container_gis_double_by_name(settings, edge_threshold_key,
                                     &args->edge_threshold);
    gwy_container_gis_enum_by_name(settings, report_style_key,
                                   &args->report_style);
    gwy_container_gis_double_by_name(settings, min_area_frac_key,
                                     &args->min_area_frac);
    gwy_container_gis_double_by_name(settings, edge_broadening_key,
                                     &args->edge_broadening);
    gwy_container_gis_boolean_by_name(settings, independent_key,
                                      &args->independent);
    gwy_container_gis_boolean_by_name(settings, use_selection_key,
                                      &args->use_selection);
    gwy_container_gis_int32_by_name(settings, output_flags_key,
                                    (gint32*)&args->output_flags);
    gwy_container_gis_boolean_by_name(settings, survey_poly_key,
                                      &args->survey_poly);
    gwy_container_gis_int32_by_name(settings, poly_degree_min_key,
                                    &args->poly_degree_min);
    gwy_container_gis_int32_by_name(settings, poly_degree_max_key,
                                    &args->poly_degree_max);
    gwy_container_gis_boolean_by_name(settings, survey_broadening_key,
                                      &args->survey_broadening);
    gwy_container_gis_int32_by_name(settings, broadening_min_key,
                                    &args->broadening_min);
    gwy_container_gis_int32_by_name(settings, broadening_max_key,
                                    &args->broadening_max);
    sanitize_args(args);
}

static void
save_args(GwyContainer *settings, TerraceArgs *args)
{
    gwy_container_set_int32_by_name(settings, poly_degree_key,
                                    args->poly_degree);
    gwy_container_set_double_by_name(settings, edge_kernel_size_key,
                                     args->edge_kernel_size);
    gwy_container_set_double_by_name(settings, edge_threshold_key,
                                     args->edge_threshold);
    gwy_container_set_enum_by_name(settings, report_style_key,
                                   args->report_style);
    gwy_container_set_double_by_name(settings, min_area_frac_key,
                                     args->min_area_frac);
    gwy_container_set_double_by_name(settings, edge_broadening_key,
                                     args->edge_broadening);
    gwy_container_set_boolean_by_name(settings, independent_key,
                                      args->independent);
    gwy_container_set_boolean_by_name(settings, use_selection_key,
                                      args->use_selection);
    gwy_container_set_int32_by_name(settings, output_flags_key,
                                    args->output_flags);
    gwy_container_set_boolean_by_name(settings, survey_poly_key,
                                      args->survey_poly);
    gwy_container_set_int32_by_name(settings, poly_degree_min_key,
                                    args->poly_degree_min);
    gwy_container_set_int32_by_name(settings, poly_degree_max_key,
                                    args->poly_degree_max);
    gwy_container_set_boolean_by_name(settings, survey_broadening_key,
                                      args->survey_broadening);
    gwy_container_set_int32_by_name(settings, broadening_min_key,
                                    args->broadening_min);
    gwy_container_set_int32_by_name(settings, broadening_max_key,
                                    args->broadening_max);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
