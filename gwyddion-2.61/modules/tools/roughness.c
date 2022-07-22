/*
 *  $Id: roughness.c 23305 2021-03-18 14:40:27Z yeti-dn $
 *  Copyright (C) 2006-2017 Martin Hason, David Necas (Yeti), Petr Klapetek.
 *  E-mail: hasonm@physics.muni.cz, yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyresults.h>
#include <libprocess/datafield.h>
#include <libprocess/linestats.h>
#include <libprocess/inttrans.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-tool.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define GWY_TYPE_TOOL_ROUGHNESS           (gwy_tool_roughness_get_type())
#define GWY_TOOL_ROUGHNESS(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj), \
                                           GWY_TYPE_TOOL_ROUGHNESS, \
                                           GwyToolRoughness))
#define GWY_IS_TOOL_ROUGHNESS(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
                                           GWY_TYPE_TOOL_ROUGHNESS))
#define GWY_TOOL_ROUGHNESS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), \
                                           GWY_TYPE_TOOL_ROUGHNESS, \
                                           GwyToolRoughnessClass))

typedef enum {
    GWY_ROUGHNESS_GRAPH_TEXTURE   = 0,
    GWY_ROUGHNESS_GRAPH_WAVINESS  = 1,
    GWY_ROUGHNESS_GRAPH_ROUGHNESS = 2,
    GWY_ROUGHNESS_GRAPH_ADF       = 3,
    GWY_ROUGHNESS_GRAPH_BRC       = 4,
    GWY_ROUGHNESS_GRAPH_PC        = 5
} GwyRoughnessGraph;

typedef struct _GwyToolRoughness      GwyToolRoughness;
typedef struct _GwyToolRoughnessClass GwyToolRoughnessClass;

typedef struct {
    GwyDataLine *texture;
    GwyDataLine *roughness;
    GwyDataLine *waviness;

    GwyDataLine *adf;
    GwyDataLine *brc;
    GwyDataLine *pc;

    /* Temporary lines */
    GwyDataLine *extline;
    GwyDataLine *tmp;
    GwyDataLine *iin;
    GwyDataLine *rout;
    GwyDataLine *iout;
} GwyRoughnessProfiles;

typedef struct {
    gint thickness;
    gdouble cutoff;
    GwyInterpolationType interpolation;
    GwyResultsReportType report_style;
    guint expanded;
    GwyAppDataId target;
} ToolArgs;

struct _GwyToolRoughness {
    GwyPlainTool parent_instance;

    ToolArgs args;
    gboolean same_units;
    GwyResults *results;
    GtkTreeStore *store;

    /* data */
    gboolean have_data;
    GwyDataLine *dataline;
    GwyRoughnessProfiles profiles;
    GwyRoughnessGraph graph_type;

    /* graph */
    GwyGraphModel *gmodel;
    GtkWidget *graph;

    GwyGraphModel *graphmodel_profile;
    GtkWidget *graph_profile;

    GtkWidget *graph_out;

    GtkObject *thickness;
    GtkObject *cutoff;
    GtkWidget *cutoff_value;
    GtkWidget *cutoff_units;
    GtkWidget *interpolation;
    GtkWidget *target_graph;

    GtkWidget *rexport;
    GtkWidget *message_label;

    /* potential class data */
    GType layer_type_line;
};

struct _GwyToolRoughnessClass {
    GwyPlainToolClass parent_class;
};

static gboolean   module_register                     (void);
static GType      gwy_tool_roughness_get_type         (void)                            G_GNUC_CONST;
static void       gwy_tool_roughness_finalize         (GObject *object);
static void       gwy_tool_roughness_init_params      (GwyToolRoughness *tool);
static void       gwy_tool_roughness_init_dialog      (GwyToolRoughness *tool);
static GtkWidget* gwy_tool_roughness_param_view_new   (GwyToolRoughness *tool);
static void       gwy_tool_roughness_data_switched    (GwyTool *gwytool,
                                                       GwyDataView *data_view);
static void       gwy_tool_roughness_response         (GwyTool *tool,
                                                       gint response_id);
static void       gwy_tool_roughness_data_changed     (GwyPlainTool *plain_tool);
static void       gwy_tool_roughness_update           (GwyToolRoughness *tool);
static void       gwy_tool_roughness_update_units     (GwyToolRoughness *tool);
static void       gwy_tool_roughness_update_parameters(GwyToolRoughness *tool);
static void       gwy_tool_roughness_update_graphs    (GwyToolRoughness *tool);
static void       gwy_tool_roughness_selection_changed(GwyPlainTool *plain_tool,
                                                       gint hint);
static void       interpolation_changed               (GtkComboBox *combo,
                                                       GwyToolRoughness *tool);
static void       report_style_changed                (GwyToolRoughness *tool,
                                                       GwyResultsExport *rexport);
static void       thickness_changed                   (GtkAdjustment *adj,
                                                       GwyToolRoughness *tool);
static void       cutoff_changed                      (GtkAdjustment *adj,
                                                       GwyToolRoughness *tool);
static void       graph_changed                       (GtkWidget *combo,
                                                       GwyToolRoughness *tool);
static void       update_target_graphs                (GwyToolRoughness *tool);
static gboolean   filter_target_graphs                (GwyContainer *data,
                                                       gint id,
                                                       gpointer user_data);
static void       gwy_tool_roughness_target_changed   (GwyToolRoughness *tool);
static void       gwy_tool_roughness_apply            (GwyToolRoughness *tool);
static gint       gwy_data_line_extend                (GwyDataLine *dline,
                                                       GwyDataLine *extline);
static void       set_data_from_profile               (GwyRoughnessProfiles *profiles,
                                                       GwyDataLine *dline,
                                                       gdouble cutoff);
static gdouble    gwy_tool_roughness_Xz               (GwyDataLine *data_line);
static gdouble    gwy_tool_roughness_Ry               (GwyDataLine *data_line);
static gdouble    gwy_tool_roughness_Da               (GwyDataLine *data_line);
static gdouble    gwy_tool_roughness_Sm               (GwyDataLine *dline);
static gdouble    gwy_tool_roughness_l0               (GwyDataLine *data_line);
static void       gwy_tool_roughness_distribution     (GwyDataLine *data_line,
                                                       GwyDataLine *distr);
static void       gwy_tool_roughness_graph_adf        (GwyRoughnessProfiles *profiles);
static void       gwy_tool_roughness_graph_brc        (GwyRoughnessProfiles *profiles);
static void       gwy_tool_roughness_graph_pc         (GwyRoughnessProfiles *profiles);

static const ToolArgs default_args = {
    1,
    0.05,
    GWY_INTERPOLATION_LINEAR,
    GWY_RESULTS_REPORT_COLON,
    0,
    GWY_APP_DATA_ID_NONE,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculate surface profile parameters."),
    "Martin Hasoň <hasonm@physics.muni.cz>, Yeti <yeti@gwyddion.net>",
    "2.0",
    "Martin Hasoň & David Nečas (Yeti)",
    "2006",
};

static const gchar cutoff_key[]        = "/module/roughness/cutoff";
static const gchar expanded_key[]      = "/module/roughness/expanded";
static const gchar interpolation_key[] = "/module/roughness/interpolation";
static const gchar report_style_key[]  = "/module/roughness/report_style";
static const gchar thickness_key[]     = "/module/roughness/thickness";

GWY_MODULE_QUERY2(module_info, roughness)

G_DEFINE_TYPE(GwyToolRoughness, gwy_tool_roughness, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_ROUGHNESS);
    return TRUE;
}

static void
gwy_tool_roughness_class_init(GwyToolRoughnessClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_roughness_finalize;

    tool_class->stock_id = GWY_STOCK_ISO_ROUGHNESS;
    tool_class->title = _("Roughness");
    tool_class->tooltip = _("Calculate roughness parameters");
    tool_class->prefix = "/module/roughness";
    tool_class->default_width = 400;
    tool_class->default_height = 400;
    tool_class->data_switched = gwy_tool_roughness_data_switched;
    tool_class->response = gwy_tool_roughness_response;

    ptool_class->data_changed = gwy_tool_roughness_data_changed;
    ptool_class->selection_changed = gwy_tool_roughness_selection_changed;
}

static void
gwy_tool_roughness_finalize(GObject *object)
{
    GwyToolRoughness *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_ROUGHNESS(object);

    settings = gwy_app_settings_get();
    gwy_container_set_int32_by_name(settings, thickness_key,
                                    tool->args.thickness);
    gwy_container_set_double_by_name(settings, cutoff_key,
                                     tool->args.cutoff);
    gwy_container_set_enum_by_name(settings, interpolation_key,
                                   tool->args.interpolation);
    gwy_container_set_enum_by_name(settings, report_style_key,
                                   tool->args.report_style);
    gwy_container_set_int32_by_name(settings, expanded_key,
                                    tool->args.expanded);

    GWY_OBJECT_UNREF(tool->store);
    GWY_OBJECT_UNREF(tool->dataline);

    GWY_OBJECT_UNREF(tool->profiles.texture);
    GWY_OBJECT_UNREF(tool->profiles.waviness);
    GWY_OBJECT_UNREF(tool->profiles.roughness);
    GWY_OBJECT_UNREF(tool->profiles.adf);
    GWY_OBJECT_UNREF(tool->profiles.brc);
    GWY_OBJECT_UNREF(tool->profiles.pc);
    GWY_OBJECT_UNREF(tool->profiles.extline);
    GWY_OBJECT_UNREF(tool->profiles.iin);
    GWY_OBJECT_UNREF(tool->profiles.tmp);
    GWY_OBJECT_UNREF(tool->profiles.rout);
    GWY_OBJECT_UNREF(tool->profiles.iout);

    G_OBJECT_CLASS(gwy_tool_roughness_parent_class)->finalize(object);

    /* XXX: Window size saving may invoke size request and bad things happen
     * when we no longer have results. */
    GWY_OBJECT_UNREF(tool->results);
}

static void
gwy_tool_roughness_init(GwyToolRoughness *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_line = gwy_plain_tool_check_layer_type(plain_tool,
                                                            "GwyLayerLine");
    if (!tool->layer_type_line)
        return;

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_VFMARKUP;
    plain_tool->lazy_updates = TRUE;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_int32_by_name(settings, thickness_key,
                                    &tool->args.thickness);
    gwy_container_gis_double_by_name(settings, cutoff_key,
                                     &tool->args.cutoff);
    gwy_container_gis_enum_by_name(settings, interpolation_key,
                                   &tool->args.interpolation);
    gwy_container_gis_enum_by_name(settings, report_style_key,
                                   &tool->args.report_style);
    gwy_container_gis_int32_by_name(settings, expanded_key,
                                    &tool->args.expanded);

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_line,
                                     "line");

    gwy_tool_roughness_init_params(tool);
    gwy_tool_roughness_init_dialog(tool);
}

static void
add_group_rows(GtkTreeStore *store, GtkTreeIter *grpiter,
               const gchar **ids, guint nids)
{
    GtkTreeIter iter;
    guint j;

    gtk_tree_store_insert_after(store, &iter, grpiter, NULL);
    gtk_tree_store_set(store, &iter, 0, ids[0], -1);

    for (j = 1; j < nids; j++) {
        gtk_tree_store_insert_after(store, &iter, grpiter, &iter);
        gtk_tree_store_set(store, &iter, 0, ids[j], -1);
    }
}

static void
gwy_tool_roughness_init_params(GwyToolRoughness *tool)
{
    static const gchar *amplitude_guivalues[] = {
        "Ra", "Rq", "Rt", "Rv", "Rp", "Rtm", "Rvm", "Rpm",
        "R3z", "R3zISO", "Rz", "RzISO", "Ry",
        "Rsk", "Rku",
        "Wa", "Wq", "Wy", "Pt",
    };
    static const gchar *spatial_guivalues[] = {
        "Sm", "lambdaa", "lambdaq",
    };
    static const gchar *hybrid_guivalues[] = {
        "Deltaa", "Deltaq", "L", "L0", "lr",
    };

    GtkTreeIter grpiter;
    GwyResults *results;

    tool->results = results = gwy_results_new();
    gwy_results_add_header(results, N_("Roughness Parameters"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "image", N_("Image"));
    gwy_results_add_format(results, "isel", N_("Selected line"), TRUE,
                           N_("(%{x1}i, %{y1}i) to (%{x2}i, %{y2}i)"),
                           "unit-str", _("px"), "translate-unit", TRUE,
                           NULL);
    gwy_results_add_format(results, "realsel", "", TRUE,
                           N_("(%{x1}v, %{y1}v) to (%{x2}v, %{y2}v)"),
                           "power-x", 1,
                           NULL);
    gwy_results_add_value_x(results, "cutoff", N_("Cut-off"));
    gwy_results_add_separator(results);

    gwy_results_add_header(results, _("Amplitude"));
    gwy_results_add_value(results, "Ra", N_("Roughness average"),
                          "power-z", 1, "symbol", "<i>R</i><sub>a</sub>",
                          NULL);
    gwy_results_add_value(results, "Rq", N_("Root mean square roughness"),
                          "power-z", 1, "symbol", "<i>R</i><sub>q</sub>",
                          NULL);
    gwy_results_add_value(results, "Rt", N_("Maximum height of the roughness"),
                          "power-z", 1, "symbol", "<i>R</i><sub>t</sub>",
                          NULL);
    gwy_results_add_value(results, "Rv", N_("Maximum roughness valley depth"),
                          "power-z", 1, "symbol", "<i>R</i><sub>v</sub>",
                          NULL);
    gwy_results_add_value(results, "Rp", N_("Maximum roughness peak height"),
                          "power-z", 1, "symbol", "<i>R</i><sub>p</sub>",
                          NULL);
    gwy_results_add_value(results, "Rtm",
                          N_("Average maximum height of the roughness"),
                          "power-z", 1, "symbol", "<i>R</i><sub>tm</sub>",
                          NULL);
    gwy_results_add_value(results, "Rvm",
                          N_("Average maximum roughness valley depth"),
                          "power-z", 1, "symbol", "<i>R</i><sub>vm</sub>",
                          NULL);
    gwy_results_add_value(results, "Rpm",
                          N_("Average maximum roughness peak height"),
                          "power-z", 1, "symbol", "<i>R</i><sub>pm</sub>",
                          NULL);
    gwy_results_add_value(results, "R3z",
                          N_("Average third highest peak to third lowest "
                             "valley height"),
                          "power-z", 1, "symbol", "<i>R</i><sub>3z</sub>",
                          NULL);
    gwy_results_add_value(results, "R3zISO",
                          N_("Average third highest peak to third lowest "
                             "valley height"),
                          "power-z", 1, "symbol", "<i>R</i><sub>3z ISO</sub>",
                          NULL);
    gwy_results_add_value(results, "Rz",
                          N_("Average maximum height of the profile"),
                          "power-z", 1, "symbol", "<i>R</i><sub>z</sub>",
                          NULL);
    gwy_results_add_value(results, "RzISO",
                          N_("Average maximum height of the roughness"),
                          "power-z", 1, "symbol", "<i>R</i><sub>z ISO</sub>",
                          NULL);
    gwy_results_add_value(results, "Ry", N_("Maximum peak to valley roughness"),
                          "power-z", 1,
                          "symbol",
                          "<i>R</i><sub>y</sub> = <i>R</i><sub>max</sub>",
                          NULL);
    gwy_results_add_value(results, "Rsk", N_("Skewness"),
                          "symbol", "<i>R</i><sub>sk</sub>",
                          NULL);
    gwy_results_add_value(results, "Rku", N_("Kurtosis"),
                          "symbol", "<i>R</i><sub>ku</sub>",
                          NULL);
    gwy_results_add_value(results, "Wa", N_("Waviness average"),
                          "power-z", 1, "symbol", "<i>W</i><sub>a</sub>",
                          NULL);
    gwy_results_add_value(results, "Wq", N_("Root mean square waviness"),
                          "power-z", 1, "symbol", "<i>W</i><sub>q</sub>",
                          NULL);
    gwy_results_add_value(results, "Wy", N_("Waviness maximum height"),
                          "power-z", 1,
                          "symbol",
                          "<i>W</i><sub>y</sub> = <i>W</i><sub>max</sub>",
                          NULL);
    gwy_results_add_value(results, "Pt", N_("Maximum height of the profile"),
                          "power-z", 1, "symbol", "<i>P</i><sub>t</sub>",
                          NULL);
    gwy_results_add_separator(results);

    gwy_results_add_header(results, _("Spatial"));
    /* TODO (Spatial):
     * S, Mean spacing of local peaks of the profile
     * D, Profile peak density
     * Pc, Peak count (peak density)
     * HSC, Hight spot count
     */
    gwy_results_add_value(results, "Sm",
                          N_("Mean spacing of profile irregularities"),
                          "power-x", 1, "symbol", "<i>S</i><sub>m</sub>",
                          NULL);
    gwy_results_add_value(results, "lambdaa",
                          N_("Average wavelength of the profile"),
                          "power-x", 1, "symbol", "λ<sub>a</sub>",
                          NULL);
    gwy_results_add_value(results, "lambdaq",
                          N_("Root mean square (RMS) wavelength "
                             "of the profile"),
                          "power-x", 1, "symbol", "λ<sub>q</sub>",
                          NULL);
    gwy_results_add_separator(results);

    gwy_results_add_header(results, N_("parameters|Hybrid"));
    gwy_results_add_value(results, "Deltaa", N_("Average absolute slope"),
                          "power-z", 1, "power-x", -1,
                          "symbol", "Δ<sub>a</sub>",
                          NULL);
    gwy_results_add_value(results, "Deltaq", N_("Root mean square (RMS) slope"),
                          "power-z", 1, "power-x", -1,
                          "symbol", "Δ<sub>q</sub>",
                          NULL);
    gwy_results_add_value(results, "L", N_("Length"),
                          "power-x", 1, "symbol", "<i>L</i>",
                          NULL);
    gwy_results_add_value(results, "L0", N_("Developed profile length"),
                          "power-x", 1, "symbol", "<i>L</i><sub>0</sub>",
                          NULL);
    gwy_results_add_value(results, "lr", N_("Profile length ratio"),
                          "symbol", "<i>l</i><sub>r</sub>",
                          NULL);
    /* TODO (Functional):
     * H, Swedish height
     * Htp, Profile section height difference
     * Rk, Core roughness depth
     * Rkp, Reduced peak height
     * Rkv, Reduced valley depth
     * Mr1, Material portion...
     * Mr2, Material portion...
     */

    tool->store = gtk_tree_store_new(1, G_TYPE_POINTER);

    gtk_tree_store_insert_after(tool->store, &grpiter, NULL, NULL);
    gtk_tree_store_set(tool->store, &grpiter, 0, "::Amplitude", -1);
    add_group_rows(tool->store, &grpiter,
                   amplitude_guivalues, G_N_ELEMENTS(amplitude_guivalues));

    gtk_tree_store_insert_after(tool->store, &grpiter, NULL, &grpiter);
    gtk_tree_store_set(tool->store, &grpiter, 0, "::Spatial", -1);
    add_group_rows(tool->store, &grpiter,
                   spatial_guivalues, G_N_ELEMENTS(spatial_guivalues));

    gtk_tree_store_insert_after(tool->store, &grpiter, NULL, &grpiter);
    gtk_tree_store_set(tool->store, &grpiter, 0, "::Hybrid", -1);
    add_group_rows(tool->store, &grpiter,
                   hybrid_guivalues, G_N_ELEMENTS(hybrid_guivalues));
}

static void
gwy_tool_roughness_init_dialog(GwyToolRoughness *tool)
{
    static const GwyEnum graph_types[] =  {
        { N_("Texture"),    GWY_ROUGHNESS_GRAPH_TEXTURE,   },
        { N_("Waviness"),   GWY_ROUGHNESS_GRAPH_WAVINESS,  },
        { N_("Roughness"),  GWY_ROUGHNESS_GRAPH_ROUGHNESS, },
        { N_("ADF"),        GWY_ROUGHNESS_GRAPH_ADF,       },
        { N_("BRC"),        GWY_ROUGHNESS_GRAPH_BRC,       },
        { N_("Peak Count"), GWY_ROUGHNESS_GRAPH_PC,        },
    };

    GtkDialog *dialog;
    GtkSizeGroup *sizegroup;
    GtkWidget *dialog_vbox, *hbox, *vbox_left, *vbox_right, *table;
    GtkWidget *scwin, *treeview, *spin;
    GwyResultsExport *rexport;
    GwyAxis *axis;
    gint row;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    dialog_vbox = GTK_DIALOG(dialog)->vbox;

    hbox = gtk_hbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_box_pack_start(GTK_BOX(dialog_vbox), hbox, TRUE, TRUE, 0);

    vbox_left = gtk_vbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(hbox), vbox_left, TRUE, TRUE, 0);

    vbox_right = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox_right, TRUE, TRUE, 0);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox_left), scwin, TRUE, TRUE, 0);

    treeview = gwy_tool_roughness_param_view_new(tool);
    gtk_container_add(GTK_CONTAINER(scwin), treeview);

    hbox = gtk_hbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_box_pack_start(GTK_BOX(vbox_left), hbox, FALSE, FALSE, 0);

    tool->rexport = gwy_results_export_new(tool->args.report_style);
    rexport = GWY_RESULTS_EXPORT(tool->rexport);
    gwy_results_export_set_title(rexport, _("Save Roughness Parameters"));
    gwy_results_export_set_results(rexport, tool->results);
    gtk_box_pack_end(GTK_BOX(hbox), tool->rexport, FALSE, FALSE, 0);
    g_signal_connect_swapped(tool->rexport, "format-changed",
                             G_CALLBACK(report_style_changed), tool);

    tool->message_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(tool->message_label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), tool->message_label, TRUE, TRUE, 0);

    table = gtk_table_new(6, 3, FALSE);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox_left), table, FALSE, FALSE, 0);
    row = 0;

    tool->graph_out
        = gwy_enum_combo_box_new(graph_types, G_N_ELEMENTS(graph_types),
                                 G_CALLBACK(graph_changed),
                                 tool, tool->graph_type, TRUE);
    gwy_table_attach_adjbar(table, row, _("_Graph:"), NULL,
                            GTK_OBJECT(tool->graph_out),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    /* cut-off */
    tool->cutoff = gtk_adjustment_new(tool->args.cutoff,
                                      0.0, 0.3, 0.001, 0.1, 0);
    spin = gwy_table_attach_adjbar(table, row, _("C_ut-off:"), NULL,
                                   tool->cutoff, GWY_HSCALE_DEFAULT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);
    g_signal_connect(tool->cutoff, "value-changed",
                     G_CALLBACK(cutoff_changed), tool);
    row++;

    tool->cutoff_value = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(tool->cutoff_value), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), tool->cutoff_value,
                     1, 2, row, row+1, GTK_FILL, 0, 0, 0);

    tool->cutoff_units = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(tool->cutoff_units), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), tool->cutoff_units,
                     2, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    tool->thickness = gtk_adjustment_new(tool->args.thickness,
                                         1, 128, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("_Thickness:"), _("px"),
                            tool->thickness,
                            GWY_HSCALE_DEFAULT | GWY_HSCALE_SNAP);
    g_signal_connect(tool->thickness, "value-changed",
                     G_CALLBACK(thickness_changed), tool);
    row++;

    tool->interpolation = gwy_enum_combo_box_new
                           (gwy_interpolation_type_get_enum(), -1,
                            G_CALLBACK(interpolation_changed),
                            tool, tool->args.interpolation, TRUE);
    gwy_table_attach_adjbar(table, row, _("_Interpolation type:"), NULL,
                            GTK_OBJECT(tool->interpolation),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    tool->target_graph = gwy_data_chooser_new_graphs();
    gwy_data_chooser_set_none(GWY_DATA_CHOOSER(tool->target_graph),
                              _("New graph"));
    gwy_data_chooser_set_active(GWY_DATA_CHOOSER(tool->target_graph), NULL, -1);
    gwy_data_chooser_set_filter(GWY_DATA_CHOOSER(tool->target_graph),
                                filter_target_graphs, tool, NULL);
    gwy_table_attach_adjbar(table, row, _("Target _graph:"), NULL,
                            GTK_OBJECT(tool->target_graph),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    g_signal_connect_swapped(tool->target_graph, "changed",
                             G_CALLBACK(gwy_tool_roughness_target_changed),
                             tool);
    row++;

    tool->graphmodel_profile = gwy_graph_model_new();
    tool->graph_profile = gwy_graph_new(tool->graphmodel_profile);
    g_object_unref(tool->graphmodel_profile);
    gtk_widget_set_size_request(tool->graph_profile, 300, 250);
    gwy_graph_enable_user_input(GWY_GRAPH(tool->graph_profile), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox_right), tool->graph_profile, TRUE, TRUE, 0);

    tool->gmodel = gwy_graph_model_new();
    tool->graph = gwy_graph_new(tool->gmodel);
    g_object_unref(tool->gmodel);
    gtk_widget_set_size_request(tool->graph, 300, 250);
    gwy_graph_enable_user_input(GWY_GRAPH(tool->graph), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox_right), tool->graph, TRUE, TRUE, 0);

    sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    axis = gwy_graph_get_axis(GWY_GRAPH(tool->graph_profile), GTK_POS_LEFT);
    gtk_size_group_add_widget(sizegroup, GTK_WIDGET(axis));
    axis = gwy_graph_get_axis(GWY_GRAPH(tool->graph), GTK_POS_LEFT);
    gtk_size_group_add_widget(sizegroup, GTK_WIDGET(axis));
    g_object_unref(sizegroup);

    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), FALSE);
    gtk_dialog_add_button(dialog, GTK_STOCK_APPLY, GTK_RESPONSE_APPLY);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                             FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    gtk_widget_show_all(dialog_vbox);
}

static void
render_symbol(G_GNUC_UNUSED GtkTreeViewColumn *column,
              GtkCellRenderer *renderer,
              GtkTreeModel *model,
              GtkTreeIter *iter,
              gpointer user_data)
{
    GwyToolRoughness *tool = (GwyToolRoughness*)user_data;
    const gchar *id;

    gtk_tree_model_get(model, iter, 0, &id, -1);
    if (strncmp(id, "::", 2) == 0) {
        g_object_set(renderer, "text", "", NULL);
        return;
    }
    g_object_set(renderer,
                 "markup", gwy_results_get_symbol(tool->results, id),
                 NULL);
}

static void
render_name(G_GNUC_UNUSED GtkTreeViewColumn *column,
            GtkCellRenderer *renderer,
            GtkTreeModel *model,
            GtkTreeIter *iter,
            gpointer user_data)
{
    GwyToolRoughness *tool = (GwyToolRoughness*)user_data;
    PangoEllipsizeMode ellipsize = PANGO_ELLIPSIZE_END;
    PangoWeight weight = PANGO_WEIGHT_NORMAL;
    const gchar *id, *name;

    gtk_tree_model_get(model, iter, 0, &id, -1);
    if (strncmp(id, "::", 2) == 0) {
        ellipsize = PANGO_ELLIPSIZE_NONE;
        weight = PANGO_WEIGHT_BOLD;
        name = id+2;
    }
    else
        name = gwy_results_get_label(tool->results, id);

    g_object_set(renderer,
                 "ellipsize", ellipsize, "weight", weight, "markup", name,
                 NULL);
}

static void
render_value(G_GNUC_UNUSED GtkTreeViewColumn *column,
             GtkCellRenderer *renderer,
             GtkTreeModel *model,
             GtkTreeIter *iter,
             gpointer user_data)
{
    GwyToolRoughness *tool = (GwyToolRoughness*)user_data;
    const gchar *id, *value;

    if (!tool->have_data) {
        g_object_set(renderer, "text", "", NULL);
        return;
    }

    gtk_tree_model_get(model, iter, 0, &id, -1);
    if (strncmp(id, "::", 2) == 0) {
        g_object_set(renderer, "text", "", NULL);
        return;
    }
    value = gwy_results_get_full(tool->results, id);
    g_object_set(renderer, "markup", value, NULL);
}

static guint
group_bit_from_name(const gchar *name)
{
    guint i = gwy_stramong(name, "Amplitude", "Spatial", "Hybrid", NULL);
    g_return_val_if_fail(i > 0, 0);
    return 1 << (i-1);
}

static void
param_row_expanded_collapsed(GtkTreeView *treeview,
                             GtkTreeIter *iter,
                             GtkTreePath *path,
                             GwyToolRoughness *tool)
{
    const gchar *id;
    guint bit;

    gtk_tree_model_get(gtk_tree_view_get_model(treeview), iter, 0, &id, -1);
    bit = group_bit_from_name(id + 2);
    if (gtk_tree_view_row_expanded(treeview, path))
        tool->args.expanded |= bit;
    else
        tool->args.expanded &= ~bit;
}

static GtkWidget*
gwy_tool_roughness_param_view_new(GwyToolRoughness *tool)
{
    GtkWidget *treeview;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkTreeIter iter;

    model = GTK_TREE_MODEL(tool->store);
    treeview = gtk_tree_view_new_with_model(model);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            render_symbol, tool, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "weight-set", TRUE, "ellipsize-set", TRUE, NULL);
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            render_name, tool, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            render_value, tool, NULL);

    /* Restore set visibility state */
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            GtkTreePath *path;
            const gchar *id;

            gtk_tree_model_get(model, &iter, 0, &id, -1);
            if (strncmp(id, "::", 2) == 0
                && (tool->args.expanded & group_bit_from_name(id + 2))) {
                path = gtk_tree_model_get_path(model, &iter);
                gtk_tree_view_expand_row(GTK_TREE_VIEW(treeview), path, TRUE);
                gtk_tree_path_free(path);
            }
        } while (gtk_tree_model_iter_next(model, &iter));
    }
    g_signal_connect(treeview, "row-collapsed",
                     G_CALLBACK(param_row_expanded_collapsed), tool);
    g_signal_connect(treeview, "row-expanded",
                     G_CALLBACK(param_row_expanded_collapsed), tool);

    return treeview;
}

static void
gwy_tool_roughness_response(GwyTool *tool,
                            gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_roughness_parent_class)->response(tool,
                                                              response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_roughness_apply(GWY_TOOL_ROUGHNESS(tool));
}

static void
gwy_tool_roughness_data_switched(GwyTool *gwytool,
                                 GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolRoughness *tool;
    gboolean ignore;

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_roughness_parent_class)->data_switched(gwytool,
                                                                   data_view);

    if (ignore || plain_tool->init_failed)
        return;

    tool = GWY_TOOL_ROUGHNESS(gwytool);
    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_line,
                                "thickness", tool->args.thickness,
                                "line-numbers", FALSE,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);
        gwy_tool_roughness_update_units(tool);
        gtk_label_set_markup(GTK_LABEL(tool->cutoff_units),
                             plain_tool->coord_format->units);
    }
    else {
        gtk_label_set_markup(GTK_LABEL(tool->cutoff_value), NULL);
        gtk_label_set_markup(GTK_LABEL(tool->cutoff_units), NULL);
    }

    gwy_tool_roughness_update(tool);
    update_target_graphs(tool);
}

static void
gwy_tool_roughness_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolRoughness *tool;

    tool = GWY_TOOL_ROUGHNESS(plain_tool);
    gwy_tool_roughness_update(tool);
    gwy_tool_roughness_update_units(tool);
    update_target_graphs(tool);
}

static void
gwy_tool_roughness_selection_changed(GwyPlainTool *plain_tool,
                                     gint hint)
{
    GwyToolRoughness *tool;
    gint n = 0;

    tool = GWY_TOOL_ROUGHNESS(plain_tool);
    g_return_if_fail(hint <= 0);

    if (plain_tool->selection) {
        n = gwy_selection_get_data(plain_tool->selection, NULL);
        /* We can get here before set-max-objects takes effect. */
        if (!(n == 0 || n == 1))
            return;
    }

    gwy_tool_roughness_update(tool);
}

static void
interpolation_changed(GtkComboBox *combo, GwyToolRoughness *tool)
{
    tool->args.interpolation = gwy_enum_combo_box_get_active(combo);
    gwy_tool_roughness_update(tool);
}

static void
report_style_changed(GwyToolRoughness *tool, GwyResultsExport *rexport)
{
    tool->args.report_style = gwy_results_export_get_format(rexport);
}

static void
update_target_graphs(GwyToolRoughness *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_refilter(chooser);
}

static gboolean
filter_target_graphs(GwyContainer *data, gint id, gpointer user_data)
{
    GwyToolRoughness *tool = (GwyToolRoughness*)user_data;
    GwyGraphModel *gmodel, *targetgmodel;
    GQuark quark = gwy_app_get_graph_key_for_id(id);

    return ((gmodel = tool->gmodel)
            && gwy_container_gis_object(data, quark, (GObject**)&targetgmodel)
            && gwy_graph_model_units_are_compatible(gmodel, targetgmodel));
}

static void
gwy_tool_roughness_target_changed(GwyToolRoughness *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_get_active_id(chooser, &tool->args.target);
}

static void
thickness_changed(GtkAdjustment *adj, GwyToolRoughness *tool)
{
    GwyPlainTool *plain_tool;

    tool->args.thickness = gwy_adjustment_get_int(adj);
    plain_tool = GWY_PLAIN_TOOL(tool);
    if (plain_tool->layer)
        g_object_set(plain_tool->layer,
                     "thickness", tool->args.thickness,
                     NULL);
    gwy_tool_roughness_update(tool);
}

static void
cutoff_changed(GtkAdjustment *adj, GwyToolRoughness *tool)
{
    tool->args.cutoff = gtk_adjustment_get_value(adj);
    gwy_tool_roughness_update(tool);
}

static void
graph_changed(GtkWidget *combo, GwyToolRoughness *tool)
{
    tool->graph_type = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    gwy_tool_roughness_update_graphs(tool);
    update_target_graphs(tool);
}

static void
gwy_tool_roughness_apply(GwyToolRoughness *tool)
{
    GwyPlainTool *plain_tool;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    gchar *s;
    gint n;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);
    n = gwy_selection_get_data(plain_tool->selection, NULL);
    g_return_if_fail(n);

    if (tool->args.target.datano) {
        GwyContainer *data = gwy_app_data_browser_get(tool->args.target.datano);
        GQuark quark = gwy_app_get_graph_key_for_id(tool->args.target.id);
        gmodel = gwy_container_get_object(data, quark);
        g_return_if_fail(gmodel);
        gwy_graph_model_append_curves(gmodel, tool->gmodel, 1);
        return;
    }

    gmodel = gwy_graph_model_new_alike(tool->gmodel);
    g_object_set(gmodel, "label-visible", TRUE, NULL);
    gcmodel = gwy_graph_model_get_curve(tool->gmodel, 0);
    gcmodel = gwy_graph_curve_model_duplicate(gcmodel);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);
    g_object_get(gcmodel, "description", &s, NULL);
    g_object_set(gmodel, "title", s, NULL);
    g_free(s);
    gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container,
                                         TRUE);
    g_object_unref(gmodel);
}

static gboolean
emit_row_changed(GtkTreeModel *model,
                 GtkTreePath *path,
                 GtkTreeIter *iter,
                 G_GNUC_UNUSED gpointer user_data)
{
    gtk_tree_model_row_changed(model, path, iter);
    return FALSE;
}

static void
update_controls(GwyToolRoughness *tool, gboolean have_data)
{
    const gchar *message = have_data ? NULL : _("No profile selected.");
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    gdouble real, cutoff;
    gint lineres;
    gchar buf[24];

    tool->have_data = have_data;
    gwy_tool_roughness_update_graphs(tool);

    if (tool->store) {
        gtk_tree_model_foreach(GTK_TREE_MODEL(tool->store),
                               emit_row_changed, NULL);
    }
    gtk_label_set_text(GTK_LABEL(tool->message_label), message);

    if (have_data) {
        lineres = gwy_data_line_get_res(tool->dataline);
        real = gwy_data_line_get_real(tool->dataline);
        if (tool->args.cutoff > 0.0) {
            cutoff = 2.0*real/lineres/tool->args.cutoff;
            g_snprintf(buf, sizeof(buf), "%.*f",
                       plain_tool->coord_format->precision+1,
                       cutoff/plain_tool->coord_format->magnitude);
            gwy_results_fill_values(tool->results, "cutoff", cutoff, NULL);
            gtk_label_set_text(GTK_LABEL(tool->cutoff_value), buf);
        }
        else {
            gtk_label_set_text(GTK_LABEL(tool->cutoff_value), "∞");
            gwy_results_set_na(tool->results, "cutoff", NULL);
        }
    }
    else
        gtk_label_set_text(GTK_LABEL(tool->cutoff_value), NULL);

    gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                             have_data);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(GWY_TOOL(tool)->dialog),
                                      GTK_RESPONSE_APPLY, have_data);
}

void
gwy_tool_roughness_update(GwyToolRoughness *tool)
{
    GwyPlainTool *plain_tool;
    GwyDataField *field;
    gdouble line[4];
    gint xl1, yl1, xl2, yl2;
    gint n, lineres;
    gdouble xoff, yoff;
    GwyResults *results = tool->results;

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (!plain_tool->selection
        || !(n = gwy_selection_get_data(plain_tool->selection, NULL))) {
        update_controls(tool, FALSE);
        return;
    }

    g_return_if_fail(plain_tool->selection);
    g_return_if_fail(gwy_selection_get_object(plain_tool->selection, 0, line));

    field = plain_tool->data_field;
    xl1 = floor(gwy_data_field_rtoj(field, line[0]));
    yl1 = floor(gwy_data_field_rtoi(field, line[1]));
    xl2 = floor(gwy_data_field_rtoj(field, line[2]));
    yl2 = floor(gwy_data_field_rtoi(field, line[3]));

    lineres = ROUND(hypot(abs(xl1 - xl2) + 1, abs(yl1 - yl2) + 1));
    if (lineres < 8) {
        update_controls(tool, FALSE);
        return;
    }

    plain_tool->pending_updates = 0;
    tool->have_data = TRUE;
    xoff = gwy_data_field_get_xoffset(field);
    yoff = gwy_data_field_get_yoffset(field);
    gwy_results_fill_format(results, "isel",
                            "x1", xl1, "y1", yl1, "x2", xl2, "y2", yl2,
                            NULL);
    gwy_results_fill_format(results, "realsel",
                            "x1", line[0] + xoff, "y1", line[1] + yoff,
                            "x2", line[2] + xoff, "y2", line[3] + yoff,
                            NULL);
    tool->dataline = gwy_data_field_get_profile(field, tool->dataline,
                                                xl1, yl1, xl2, yl2,
                                                lineres,
                                                tool->args.thickness,
                                                tool->args.interpolation);

    gwy_results_fill_filename(results, "file", plain_tool->container);
    gwy_results_fill_channel(results, "image",
                             plain_tool->container, plain_tool->id);

    set_data_from_profile(&tool->profiles, tool->dataline, tool->args.cutoff);

    gwy_tool_roughness_update_graphs(tool);
    gwy_tool_roughness_update_parameters(tool);
    update_controls(tool, TRUE);
}

static void
gwy_tool_roughness_update_units(GwyToolRoughness *tool)
{
    GwySIUnit *siunitxy, *siunitz;
    GwyDataField *dfield;
    GwyRoughnessProfiles *profiles;

    dfield = GWY_PLAIN_TOOL(tool)->data_field;
    profiles = &tool->profiles;
    siunitxy = gwy_data_field_get_si_unit_xy(dfield);
    siunitz = gwy_data_field_get_si_unit_z(dfield);
    gwy_results_set_unit(tool->results, "x", siunitxy);
    gwy_results_set_unit(tool->results, "y", siunitxy);
    gwy_results_set_unit(tool->results, "z", siunitz);
    tool->same_units = gwy_si_unit_equal(siunitxy, siunitz);

    if (profiles->texture) {
        gwy_data_field_copy_units_to_data_line(dfield, profiles->texture);
        gwy_data_field_copy_units_to_data_line(dfield, profiles->waviness);
        gwy_data_field_copy_units_to_data_line(dfield, profiles->roughness);
        /* ADF and BRC are updated upon calculation */
    }
}

static void
gwy_tool_roughness_update_parameters(GwyToolRoughness *tool)
{
    GwyDataLine *roughness, *waviness, *texture;
    gdouble ra, rq, da, dq, rp, rv, rpm, rvm, rtm, l0, real;

    roughness = tool->profiles.roughness;
    waviness = tool->profiles.waviness;
    texture = tool->profiles.texture;

    /* This definitely does something because we do not have form removed.
     * Not sure; it affects only RP which seems to be defined for zero-mean
     * profiles. */
    gwy_data_line_add(texture, -gwy_data_line_get_avg(texture));
    /* This definitely does something because we do not have form removed and
     * it should be also correct. */
    gwy_data_line_add(waviness, -gwy_data_line_get_avg(waviness));
    /* This should essentially do nothing but it is safe. */
    gwy_data_line_add(roughness, -gwy_data_line_get_avg(roughness));

    ra = gwy_data_line_get_ra(roughness);
    rq = gwy_data_line_get_rms(roughness);
    rv = gwy_data_line_get_xvm(roughness, 1, 1);
    rp = gwy_data_line_get_xpm(roughness, 1, 1);
    rvm = gwy_data_line_get_xvm(roughness, 5, 1);
    rpm = gwy_data_line_get_xpm(roughness, 5, 1);
    rtm = rvm + rpm;
    da = gwy_tool_roughness_Da(roughness);
    dq = gwy_data_line_get_tan_beta0(roughness);
    real = gwy_data_line_get_real(roughness);
    gwy_results_fill_values(tool->results,
                            "Ra", ra, "Rq", rq,
                            "Rv", rv, "Rp", rp, "Rt", rp + rv,
                            "Rvm", rvm, "Rpm", rpm, "Rtm", rtm,
                            "R3z", gwy_data_line_get_xtm(roughness, 1, 3),
                            "R3zISO", gwy_data_line_get_xtm(roughness, 5, 3),
                            "Rz", gwy_tool_roughness_Xz(roughness),
                            "RzISO", rtm,
                            "Ry", gwy_tool_roughness_Ry(roughness),
                            "Rsk", gwy_data_line_get_skew(roughness),
                            "Rku", gwy_data_line_get_kurtosis(roughness) + 3.0,
                            "Wa", gwy_data_line_get_ra(waviness),
                            "Wq", gwy_data_line_get_rms(waviness),
                            "Wy", gwy_data_line_get_xtm(waviness, 1, 1),
                            "Pt", gwy_data_line_get_xtm(texture, 1, 1),
                            "Deltaa", da,
                            "Deltaq", dq,
                            "Sm", gwy_tool_roughness_Sm(roughness),
                            "lambdaa", 2*G_PI*ra/da,
                            "lambdaq", 2*G_PI*rq/dq,
                            "L", real,
                            NULL);
    if (tool->same_units) {
        l0 = gwy_tool_roughness_l0(roughness);
        gwy_results_fill_values(tool->results, "L0", l0, "lr", l0/real, NULL);
    }
    else
        gwy_results_set_na(tool->results, "L0", "lr", NULL);

    gwy_tool_roughness_graph_adf(&tool->profiles);
    gwy_tool_roughness_graph_brc(&tool->profiles);
    gwy_tool_roughness_graph_pc(&tool->profiles);
}

static void
gwy_tool_roughness_update_graphs(GwyToolRoughness *tool)
{
    typedef struct {
        const gchar *title;
        GwyDataLine *dataline;
    } Graph;

    /* Subset to show in profile graphs */
    static const guint profile_graphs[] = {
        GWY_ROUGHNESS_GRAPH_TEXTURE,
        GWY_ROUGHNESS_GRAPH_WAVINESS,
        GWY_ROUGHNESS_GRAPH_ROUGHNESS,
    };

    /* XXX: This array is indexed by GwyRoughnessGraph values */
    Graph graphs[] =  {
        { N_("Texture"),                         tool->profiles.texture,   },
        { N_("Waviness"),                        tool->profiles.waviness,  },
        { N_("Roughness"),                       tool->profiles.roughness, },
        { N_("Amplitude Distribution Function"), tool->profiles.adf,       },
        { N_("The Bearing Ratio Curve"),         tool->profiles.brc,       },
        { N_("Peak Count"),                      tool->profiles.pc,        },
    };

    GwyGraphCurveModel *gcmodel;
    GwyGraphModel *gmodel;
    Graph *graph;
    gint i;

    if (!tool->have_data) {
        gwy_graph_model_remove_all_curves(tool->gmodel);
        gwy_graph_model_remove_all_curves(tool->graphmodel_profile);
        return;
    }

    gmodel = tool->graphmodel_profile;
    for (i = 0; i < G_N_ELEMENTS(profile_graphs); i++) {
        graph = graphs + profile_graphs[i];
        if (i < gwy_graph_model_get_n_curves(gmodel))
            gcmodel = gwy_graph_model_get_curve(gmodel, i);
        else {
            gcmodel = gwy_graph_curve_model_new();
            g_object_set(gcmodel,
                         "mode", GWY_GRAPH_CURVE_LINE,
                         "color", gwy_graph_get_preset_color(i),
                         "description", gettext(graph->title),
                         NULL);
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);
        }
        if (graph->dataline)
            gwy_graph_curve_model_set_data_from_dataline(gcmodel,
                                                         graph->dataline, 0, 0);

    }
    g_object_set(gmodel, "title", _("Surface Profiles"), NULL);
    gwy_graph_model_set_units_from_data_line(gmodel, tool->dataline);

    graph = graphs + tool->graph_type;
    gmodel = tool->gmodel;
    i = 0;
    if (gwy_graph_model_get_n_curves(gmodel))
        gcmodel = gwy_graph_model_get_curve(gmodel, i);
    else {
        gcmodel = gwy_graph_curve_model_new();
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(i),
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }
    g_object_set(gcmodel, "description", graph->title, NULL);
    g_object_set(gmodel, "title", graph->title, NULL);
    if (graph->dataline) {
        gwy_graph_model_set_units_from_data_line(gmodel,
                                                 graph->dataline);
        gwy_graph_curve_model_set_data_from_dataline(gcmodel,
                                                     graph->dataline, 0, 0);
    }
}

static gint
gwy_data_line_extend(GwyDataLine *dline,
                     GwyDataLine *extline)
{
    enum { SMEAR = 6 };
    gint n, next, k, i;
    gdouble *data, *edata;
    gdouble der0, der1;

    n = gwy_data_line_get_res(dline);
    next = gwy_fft_find_nice_size(4*n/3);
    g_return_val_if_fail(next < 3*n, n);

    gwy_data_line_resample(extline, next, GWY_INTERPOLATION_NONE);
    gwy_data_line_set_real(extline, next*gwy_data_line_get_real(dline)/n);
    data = gwy_data_line_get_data(dline);
    edata = gwy_data_line_get_data(extline);

    gwy_assign(edata, data, n);
    /* 0 and 1 in extension data coordinates, not primary data */
    der0 = (2*data[n-1] - data[n-2] - data[n-3])/3;
    der1 = (2*data[0] - data[1] - data[2])/3;
    k = next - n;
    for (i = 0; i < k; i++) {
        gdouble x, y, ww, w;

        y = w = 0.0;
        if (i < SMEAR) {
            ww = 2.0*(SMEAR-1 - i)/SMEAR;
            y += ww*(data[n-1] + der0*(i + 1));
            w += ww;
        }
        if (k-1 - i < SMEAR) {
            ww = 2.0*(i + SMEAR-1 - (k-1))/SMEAR;
            y += ww*(data[0] + der1*(k - i));
            w += ww;
        }
        if (i < n) {
            x = 1.0 - i/(k - 1.0);
            ww = x*x;
            y += ww*data[n-1 - i];
            w += ww;
        }
        if (k-1 - i < n) {
            x = 1.0 - (k-1 - i)/(k - 1.0);
            ww = x*x;
            y += ww*data[k-1 - i];
            w += ww;
        }
        edata[n + i] = y/w;
    }

    return next;
}

static void
set_data_from_profile(GwyRoughnessProfiles *profiles,
                      GwyDataLine *dline,
                      gdouble cutoff)
{
    gint n, next, i;
    gdouble *re, *im, *wdata, *rdata;
    const gdouble *tdata, *data;

    n = gwy_data_line_get_res(dline);
    if (profiles->texture) {
        gdouble real;

        real = gwy_data_line_get_real(dline);
        gwy_data_line_assign(profiles->texture, dline);
        gwy_data_line_resample(profiles->waviness, n, GWY_INTERPOLATION_NONE);
        gwy_data_line_set_real(profiles->waviness, real);
        gwy_data_line_resample(profiles->roughness, n, GWY_INTERPOLATION_NONE);
        gwy_data_line_set_real(profiles->roughness, real);
    }
    else {
        profiles->texture = gwy_data_line_duplicate(dline);
        g_object_set_data(G_OBJECT(profiles->texture), "name", "texture");
        profiles->waviness = gwy_data_line_new_alike(dline, FALSE);
        g_object_set_data(G_OBJECT(profiles->waviness), "name", "waviness");
        profiles->roughness = gwy_data_line_new_alike(dline, FALSE);
        g_object_set_data(G_OBJECT(profiles->roughness), "name", "roughness");
        profiles->extline = gwy_data_line_new_alike(dline, FALSE);
    }

    next = gwy_data_line_extend(dline, profiles->extline);
    if (profiles->iin) {
        gwy_data_line_resample(profiles->iin, next, GWY_INTERPOLATION_NONE);
        gwy_data_line_resample(profiles->tmp, next, GWY_INTERPOLATION_NONE);
        gwy_data_line_resample(profiles->rout, next, GWY_INTERPOLATION_NONE);
        gwy_data_line_resample(profiles->iout, next, GWY_INTERPOLATION_NONE);
    }
    else {
        profiles->iin = gwy_data_line_new_alike(profiles->extline, FALSE);
        profiles->tmp = gwy_data_line_new_alike(profiles->extline, FALSE);
        profiles->rout = gwy_data_line_new_alike(profiles->extline, FALSE);
        profiles->iout = gwy_data_line_new_alike(profiles->extline, FALSE);
    }

    gwy_data_line_clear(profiles->iin);
    gwy_data_line_fft_raw(profiles->extline, profiles->iin,
                          profiles->rout, profiles->iout,
                          GWY_TRANSFORM_DIRECTION_FORWARD);

    re = gwy_data_line_get_data(profiles->rout);
    im = gwy_data_line_get_data(profiles->iout);
    for (i = 0; i < next; i++) {
        gdouble f;

        f = 2.0*MIN(i, next-i)/next;
        if (f > cutoff)
            re[i] = im[i] = 0.0;
    }

    gwy_data_line_fft_raw(profiles->rout, profiles->iout,
                          profiles->tmp, profiles->iin,
                          GWY_TRANSFORM_DIRECTION_BACKWARD);

    data = gwy_data_line_get_data_const(profiles->extline);
    tdata = gwy_data_line_get_data_const(profiles->tmp);
    wdata = gwy_data_line_get_data(profiles->waviness);
    rdata = gwy_data_line_get_data(profiles->roughness);
    for (i = 0; i < n; i++) {
        wdata[i] = tdata[i];
        rdata[i] = data[i] - tdata[i];
    }
}

static gdouble
gwy_tool_roughness_Xz(GwyDataLine *data_line)
{
    gdouble p, v;

    gwy_data_line_get_kth_peaks(data_line, 1, 5, TRUE, TRUE, 0.0, 0.0, &p);
    gwy_data_line_get_kth_peaks(data_line, 1, 5, FALSE, TRUE, 0.0, 0.0, &v);

    return p + v;
}

static gdouble
gwy_tool_roughness_Ry(GwyDataLine *data_line)
{
    gdouble p[5], v[5], Ry = 0.0;
    guint i;

    gwy_data_line_get_kth_peaks(data_line, 5, 1, TRUE, FALSE, 0.0, 0.0, p);
    gwy_data_line_get_kth_peaks(data_line, 5, 1, FALSE, FALSE, 0.0, 0.0, v);

    for (i = 0; i < 5; i++) {
        if (p[i] >= 0.0 && v[i] >= 0.0 && p[i] + v[i] > Ry)
            Ry = p[i] + v[i];
    }

    return Ry;
}

static gdouble
gwy_tool_roughness_Da(GwyDataLine *dline)
{
    return gwy_data_line_get_variation(dline)/gwy_data_line_get_real(dline);
}

static gdouble
gwy_tool_roughness_Sm(GwyDataLine *dline)
{
    gint count = gwy_data_line_count_peaks(dline, TRUE, 0.0, 0.0);
    gdouble real = gwy_data_line_get_real(dline);
    return real/count;
}

static gdouble
gwy_tool_roughness_l0(GwyDataLine *data_line)
{
    /* This might be not according to the norm.  However, the original
     * definitions can give lr < 1 for short lines which is obviously wrong.
     * It has to be corrected for the res vs. res-1 ratio somehow. */
    return gwy_data_line_get_length(data_line);
}

static void
gwy_tool_roughness_distribution(GwyDataLine *data_line, GwyDataLine *distr)
{
    gdouble max;

    gwy_data_line_dh(data_line, distr, 0.0, 0.0, gwy_data_line_get_res(distr));
    /* FIXME */
    if (data_line->real == 0.0)
        data_line->real = 1.0;

    max = gwy_data_line_get_max(distr);
    if (max > 0.0)
        gwy_data_line_multiply(distr, 1.0/max);

    gwy_si_unit_assign(gwy_data_line_get_si_unit_x(distr),
                       gwy_data_line_get_si_unit_y(data_line));
}

static void
gwy_tool_roughness_graph_adf(GwyRoughnessProfiles *profiles)
{
    if (!profiles->adf)
        profiles->adf = gwy_data_line_new(101, 1.0, FALSE);

    gwy_tool_roughness_distribution(profiles->roughness, profiles->adf);
}

static void
gwy_tool_roughness_graph_brc(GwyRoughnessProfiles *profiles)
{
    gdouble max;

    if (!profiles->brc)
        profiles->brc = gwy_data_line_new(101, 1.0, FALSE);

    gwy_tool_roughness_distribution(profiles->roughness, profiles->brc);
    gwy_data_line_cumulate(profiles->brc);
    max = gwy_data_line_get_max(profiles->brc);
    if (max > 0.0)
        gwy_data_line_multiply(profiles->brc, 1.0/max);
}

static void
gwy_tool_roughness_graph_pc(GwyRoughnessProfiles *profiles)
{
    GwyDataLine *roughness, *pc;
    gint samples;
    gdouble ymax, dy, threshold, real;
    gint i, peakcount;

    roughness = profiles->roughness;
    if (!profiles->pc)
        profiles->pc = gwy_data_line_new(121, 1.0, FALSE);
    pc = profiles->pc;

    ymax = gwy_data_line_get_max(roughness);
    gwy_data_line_set_real(pc, ymax);
    samples = gwy_data_line_get_res(pc);
    real = gwy_data_line_get_real(roughness);
    dy = ymax/samples;

    gwy_si_unit_power(gwy_data_line_get_si_unit_y(roughness), 1,
                      gwy_data_line_get_si_unit_x(pc));
    gwy_si_unit_power(gwy_data_line_get_si_unit_x(roughness), -1,
                      gwy_data_line_get_si_unit_y(pc));

    for (i = 0; i < samples; i++) {
        threshold = dy*i;
        peakcount = gwy_data_line_count_peaks(roughness, TRUE,
                                              threshold, threshold);
        gwy_data_line_set_val(pc, i, peakcount/real);
    }
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
