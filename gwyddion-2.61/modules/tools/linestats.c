/*
 *  $Id: linestats.c 24167 2021-09-20 17:02:41Z yeti-dn $
 *  Copyright (C) 2003-2018 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define GWY_TYPE_TOOL_LINE_STATS            (gwy_tool_line_stats_get_type())
#define GWY_TOOL_LINE_STATS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_LINE_STATS, GwyToolLineStats))
#define GWY_IS_TOOL_LINE_STATS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_LINE_STATS))
#define GWY_TOOL_LINE_STATS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_LINE_STATS, GwyToolLineStatsClass))

enum {
    MIN_RESOLUTION = 4,
    MAX_RESOLUTION = 16384
};

typedef struct _GwyToolLineStats      GwyToolLineStats;
typedef struct _GwyToolLineStatsClass GwyToolLineStatsClass;

typedef struct {
    GwyLineStatQuantity output_type;
    gboolean options_visible;
    gboolean instant_update;
    GwyOrientation direction;
    GwyMaskingType masking;
    GwyAppDataId target;
} ToolArgs;

struct _GwyToolLineStats {
    GwyPlainTool parent_instance;

    ToolArgs args;

    GwyRectSelectionLabels *rlabels;

    GwyDataLine *line;
    GwyDataLine *weights;
    gint isel[4];
    gint isel_prev[4];

    GtkWidget *graph;
    GwyGraphModel *gmodel;

    GtkWidget *options;
    GtkWidget *output_type;
    GtkWidget *instant_update;
    GSList *direction;
    GtkWidget *masking;
    GtkWidget *update;
    GtkWidget *apply;
    GtkWidget *average_label;
    GtkWidget *target_graph;

    /* potential class data */
    GType layer_type_rect;
};

struct _GwyToolLineStatsClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType    gwy_tool_line_stats_get_type         (void)                        G_GNUC_CONST;
static void     gwy_tool_line_stats_finalize         (GObject *object);
static void     gwy_tool_line_stats_init_dialog      (GwyToolLineStats *tool);
static void     gwy_tool_line_stats_data_switched    (GwyTool *gwytool,
                                                      GwyDataView *data_view);
static void     gwy_tool_line_stats_response         (GwyTool *tool,
                                                      gint response_id);
static void     gwy_tool_line_stats_data_changed     (GwyPlainTool *plain_tool);
static void     gwy_tool_line_stats_mask_changed     (GwyPlainTool *plain_tool);
static void     gwy_tool_line_stats_selection_changed(GwyPlainTool *plain_tool,
                                                      gint hint);
static void     update_selected_rectangle            (GwyToolLineStats *tool);
static void     update_sensitivity                   (GwyToolLineStats *tool);
static void     update_curve                         (GwyToolLineStats *tool);
static void     instant_update_changed               (GtkToggleButton *check,
                                                      GwyToolLineStats *tool);
static void     output_type_changed                  (GtkComboBox *combo,
                                                      GwyToolLineStats *tool);
static void     direction_changed                    (GObject *button,
                                                      GwyToolLineStats *tool);
static void     masking_changed                      (GtkComboBox *combo,
                                                      GwyToolLineStats *tool);
static void     update_target_graphs                 (GwyToolLineStats *tool);
static gboolean filter_target_graphs                 (GwyContainer *data,
                                                      gint id,
                                                      gpointer user_data);
static void     target_changed                       (GwyToolLineStats *tool);
static void     gwy_tool_line_stats_options_expanded (GtkExpander *expander,
                                                      GParamSpec *pspec,
                                                      GwyToolLineStats *tool);
static void     gwy_tool_line_stats_apply            (GwyToolLineStats *tool);
static void     calculate_avg_rms_for_rms            (GwyDataLine *dline,
                                                      gdouble *avg,
                                                      gdouble *rms);
static gint     set_data_from_dataline_filtered      (GwyGraphCurveModel *gcmodel,
                                                      GwyDataLine *dline,
                                                      GwyDataLine *weight,
                                                      gdouble threshold);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Row/column statistical function tool, mean values, medians, maxima, "
       "minima, RMS, ..., of rows or columns."),
    "Yeti <yeti@gwyddion.net>",
    "2.5",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

static const gchar direction_key[]       = "/module/linestats/direction";
static const gchar instant_update_key[]  = "/module/linestats/instant_update";
static const gchar masking_key[]         = "/module/linestats/masking";
static const gchar options_visible_key[] = "/module/linestats/options_visible";
static const gchar output_type_key[]     = "/module/linestats/output_type";

static const ToolArgs default_args = {
    GWY_LINE_STAT_MEAN,
    FALSE,
    TRUE,
    GWY_ORIENTATION_HORIZONTAL,
    GWY_MASK_IGNORE,
    GWY_APP_DATA_ID_NONE,
};

/* If you add something here, consider adding it also to
 * ../volume/volume_linestat.c */
static const GwyEnum sf_types[] =  {
    { N_("Mean"),               GWY_LINE_STAT_MEAN,      },
    { N_("Median"),             GWY_LINE_STAT_MEDIAN,    },
    { N_("Minimum"),            GWY_LINE_STAT_MINIMUM,   },
    { N_("Maximum"),            GWY_LINE_STAT_MAXIMUM,   },
    { N_("Range"),              GWY_LINE_STAT_RANGE,     },
    { N_("Developed length"),   GWY_LINE_STAT_LENGTH,    },
    { N_("Slope"),              GWY_LINE_STAT_SLOPE,     },
    { N_("tan β<sub>0</sub>"),  GWY_LINE_STAT_TAN_BETA0, },
    { N_("Variation"),          GWY_LINE_STAT_VARIATION, },
    { N_("Ra"),                 GWY_LINE_STAT_RA,        },
    { N_("Rq (RMS)"),           GWY_LINE_STAT_RMS,       },
    { N_("Rz"),                 GWY_LINE_STAT_RZ,        },
    { N_("Rt"),                 GWY_LINE_STAT_RT,        },
    { N_("Skew"),               GWY_LINE_STAT_SKEW,      },
    { N_("Excess kurtosis"),    GWY_LINE_STAT_KURTOSIS,  },
    { N_("Min. position"),      GWY_LINE_STAT_MINPOS,    },
    { N_("Max. position"),      GWY_LINE_STAT_MAXPOS,    },
};

GWY_MODULE_QUERY2(module_info, linestats)

G_DEFINE_TYPE(GwyToolLineStats, gwy_tool_line_stats, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_LINE_STATS);

    return TRUE;
}

static void
gwy_tool_line_stats_class_init(GwyToolLineStatsClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_line_stats_finalize;

    tool_class->stock_id = GWY_STOCK_GRAPH_VERTICAL;
    tool_class->title = _("Row/Column Statistics");
    tool_class->tooltip = _("Calculate row/column statistical functions");
    tool_class->prefix = "/module/line_stats";
    tool_class->default_width = 640;
    tool_class->default_height = 400;
    tool_class->data_switched = gwy_tool_line_stats_data_switched;
    tool_class->response = gwy_tool_line_stats_response;

    ptool_class->data_changed = gwy_tool_line_stats_data_changed;
    ptool_class->mask_changed = gwy_tool_line_stats_mask_changed;
    ptool_class->selection_changed = gwy_tool_line_stats_selection_changed;
}

static void
gwy_tool_line_stats_finalize(GObject *object)
{
    GwyToolLineStats *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_LINE_STATS(object);

    settings = gwy_app_settings_get();
    gwy_container_set_enum_by_name(settings, output_type_key,
                                   tool->args.output_type);
    gwy_container_set_boolean_by_name(settings, options_visible_key,
                                      tool->args.options_visible);
    gwy_container_set_boolean_by_name(settings, instant_update_key,
                                      tool->args.instant_update);
    gwy_container_set_enum_by_name(settings, masking_key,
                                   tool->args.masking);
    gwy_container_set_enum_by_name(settings, direction_key,
                                   tool->args.direction);

    GWY_OBJECT_UNREF(tool->line);
    GWY_OBJECT_UNREF(tool->weights);
    GWY_OBJECT_UNREF(tool->gmodel);

    G_OBJECT_CLASS(gwy_tool_line_stats_parent_class)->finalize(object);
}

static void
gwy_tool_line_stats_init(GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_rect = gwy_plain_tool_check_layer_type(plain_tool,
                                                           "GwyLayerRectangle");
    if (!tool->layer_type_rect)
        return;

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_MARKUP;
    plain_tool->lazy_updates = TRUE;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_enum_by_name(settings, output_type_key,
                                   &tool->args.output_type);
    gwy_container_gis_boolean_by_name(settings, options_visible_key,
                                      &tool->args.options_visible);
    gwy_container_gis_boolean_by_name(settings, instant_update_key,
                                      &tool->args.instant_update);
    gwy_container_gis_enum_by_name(settings, masking_key,
                                   &tool->args.masking);
    tool->args.masking = gwy_enum_sanitize_value(tool->args.masking,
                                                 GWY_TYPE_MASKING_TYPE);
    gwy_container_gis_enum_by_name(settings, direction_key,
                                   &tool->args.direction);
    tool->args.direction
        = gwy_enum_sanitize_value(tool->args.direction, GWY_TYPE_ORIENTATION);

    tool->line = gwy_data_line_new(4, 1.0, FALSE);
    tool->weights = gwy_data_line_new(4, 1.0, FALSE);

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_rect,
                                     "rectangle");
    memset(tool->isel_prev, 0xff, 4*sizeof(gint));

    gwy_tool_line_stats_init_dialog(tool);
}

static void
gwy_tool_line_stats_rect_updated(GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_rect_selection_labels_select(tool->rlabels,
                                     plain_tool->selection,
                                     plain_tool->data_field);
}

static void
gwy_tool_line_stats_init_dialog(GwyToolLineStats *tool)
{
    static const GwyEnum directions[] = {
        { N_("Ro_ws"),    GWY_ORIENTATION_HORIZONTAL, },
        { N_("Co_lumns"), GWY_ORIENTATION_VERTICAL,   },
    };
    GtkDialog *dialog;
    GtkWidget *label, *hbox, *vbox, *hbox2, *image;
    GtkTable *table;
    guint row;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, TRUE, TRUE, 0);

    /* Left pane */
    vbox = gtk_vbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    /* Selection info */
    tool->rlabels = gwy_rect_selection_labels_new
                         (TRUE, G_CALLBACK(gwy_tool_line_stats_rect_updated),
                          tool);
    gtk_box_pack_start(GTK_BOX(vbox),
                       gwy_rect_selection_labels_get_table(tool->rlabels),
                       FALSE, FALSE, 0);

    /* Output type */
    hbox2 = gtk_hbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox2), 4);
    gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, TRUE, 0);

    label = gtk_label_new_with_mnemonic(_("_Quantity:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, FALSE, 0);

    tool->output_type = gwy_enum_combo_box_new
                           (sf_types, G_N_ELEMENTS(sf_types),
                            G_CALLBACK(output_type_changed), tool,
                            tool->args.output_type, TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), tool->output_type);
    gtk_box_pack_end(GTK_BOX(hbox2), tool->output_type, FALSE, FALSE, 0);

    /* Average */
    hbox2 = gtk_hbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox2), 4);
    gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 0);

    label = gtk_label_new(_("Average:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, FALSE, 0);

    tool->average_label = label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox2), label, TRUE, TRUE, 0);

    /* Options */
    tool->options = gtk_expander_new(_("<b>Options</b>"));
    gtk_expander_set_use_markup(GTK_EXPANDER(tool->options), TRUE);
    gtk_expander_set_expanded(GTK_EXPANDER(tool->options),
                              tool->args.options_visible);
    g_signal_connect(tool->options, "notify::expanded",
                     G_CALLBACK(gwy_tool_line_stats_options_expanded), tool);
    gtk_box_pack_start(GTK_BOX(vbox), tool->options, FALSE, FALSE, 0);

    table = GTK_TABLE(gtk_table_new(5, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_container_add(GTK_CONTAINER(tool->options), GTK_WIDGET(table));
    row = 0;

    tool->instant_update
        = gtk_check_button_new_with_mnemonic(_("_Instant updates"));
    gtk_table_attach(table, tool->instant_update,
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->instant_update),
                                 tool->args.instant_update);
    g_signal_connect(tool->instant_update, "toggled",
                     G_CALLBACK(instant_update_changed), tool);
    row++;

    tool->direction = gwy_radio_buttons_create
                        (directions, G_N_ELEMENTS(directions),
                        G_CALLBACK(direction_changed), tool,
                        tool->args.direction);
    row = gwy_radio_buttons_attach_to_table(tool->direction, table, 3, row);
    gtk_table_set_row_spacing(table, row-1, 8);

    tool->masking = gwy_enum_combo_box_new
                            (gwy_masking_type_get_enum(), -1,
                             G_CALLBACK(masking_changed), tool,
                             tool->args.masking, TRUE);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Masking:"), NULL,
                            GTK_OBJECT(tool->masking),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    tool->target_graph = gwy_data_chooser_new_graphs();
    gwy_data_chooser_set_none(GWY_DATA_CHOOSER(tool->target_graph),
                              _("New graph"));
    gwy_data_chooser_set_active(GWY_DATA_CHOOSER(tool->target_graph), NULL, -1);
    gwy_data_chooser_set_filter(GWY_DATA_CHOOSER(tool->target_graph),
                                filter_target_graphs, tool, NULL);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("Target _graph:"), NULL,
                            GTK_OBJECT(tool->target_graph),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    g_signal_connect_swapped(tool->target_graph, "changed",
                             G_CALLBACK(target_changed), tool);
    row++;

    tool->gmodel = gwy_graph_model_new();

    tool->graph = gwy_graph_new(tool->gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(tool->graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), tool->graph, TRUE, TRUE, 2);

    tool->update = gtk_dialog_add_button(dialog, _("_Update"),
                                         GWY_TOOL_RESPONSE_UPDATE);
    image = gtk_image_new_from_stock(GTK_STOCK_EXECUTE, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(tool->update), image);
    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), FALSE);
    tool->apply = gtk_dialog_add_button(dialog, GTK_STOCK_APPLY,
                                        GTK_RESPONSE_APPLY);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    update_sensitivity(tool);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_line_stats_data_switched(GwyTool *gwytool,
                                  GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolLineStats *tool;
    gboolean ignore;

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_line_stats_parent_class)->data_switched(gwytool,
                                                                    data_view);

    if (ignore || plain_tool->init_failed)
        return;

    tool = GWY_TOOL_LINE_STATS(gwytool);
    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_rect,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);
    }

    update_curve(tool);
}

static void
gwy_tool_line_stats_response(GwyTool *tool,
                             gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_line_stats_parent_class)->response(tool,
                                                               response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_line_stats_apply(GWY_TOOL_LINE_STATS(tool));
    else if (response_id == GWY_TOOL_RESPONSE_UPDATE)
        update_curve(GWY_TOOL_LINE_STATS(tool));
}

static void
gwy_tool_line_stats_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolLineStats *tool = GWY_TOOL_LINE_STATS(plain_tool);
    update_selected_rectangle(tool);
    update_curve(tool);
}

static void
gwy_tool_line_stats_mask_changed(GwyPlainTool *plain_tool)
{
    if (GWY_TOOL_LINE_STATS(plain_tool)->args.masking != GWY_MASK_IGNORE)
        update_curve(GWY_TOOL_LINE_STATS(plain_tool));
}

static void
update_selected_rectangle(GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwySelection *selection = plain_tool->selection;
    GwyDataField *field = plain_tool->data_field;
    gint n;

    n = selection ? gwy_selection_get_data(selection, NULL) : 0;
    gwy_rect_selection_labels_fill(tool->rlabels, n == 1 ? selection : NULL,
                                   field, NULL, tool->isel);
}

static void
gwy_tool_line_stats_selection_changed(GwyPlainTool *plain_tool,
                                      gint hint)
{
    GwyToolLineStats *tool = GWY_TOOL_LINE_STATS(plain_tool);

    g_return_if_fail(hint <= 0);
    update_selected_rectangle(tool);
    if (tool->args.instant_update) {
        if (memcmp(tool->isel, tool->isel_prev, 4*sizeof(gint)) != 0)
            update_curve(tool);
    }
}

static void
update_sensitivity(GwyToolLineStats *tool)
{
    gtk_widget_set_sensitive(tool->update, !tool->args.instant_update);
}

static void
update_curve(GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool;

    GwyGraphCurveModel *gcmodel;
    GwyDataField *field;
    gchar *result;
    gint n, nsel, col, row, w, h;
    const gchar *title;
    GwySIUnit *siunit;
    GwySIValueFormat *format;
    gdouble avg, rms;

    plain_tool = GWY_PLAIN_TOOL(tool);
    field = plain_tool->data_field;

    if (!field) {
        gwy_graph_model_remove_all_curves(tool->gmodel);
        gtk_label_set_text(GTK_LABEL(tool->average_label), "");
        gtk_widget_set_sensitive(tool->apply, FALSE);
        return;
    }

    if (tool->args.output_type == GWY_LINE_STAT_LENGTH) {
        GwySIUnit *xyunit, *zunit;

        xyunit = gwy_data_field_get_si_unit_xy(field);
        zunit = gwy_data_field_get_si_unit_z(field);
        if (!gwy_si_unit_equal(xyunit, zunit)) {
            gwy_graph_model_remove_all_curves(tool->gmodel);
            gtk_label_set_text(GTK_LABEL(tool->average_label), "");
            gtk_widget_set_sensitive(tool->apply, FALSE);
            return;
        }
    }

    if (plain_tool->pending_updates & GWY_PLAIN_TOOL_CHANGED_SELECTION)
        update_selected_rectangle(tool);
    plain_tool->pending_updates = 0;

    gwy_assign(tool->isel_prev, tool->isel, 4);
    n = gwy_graph_model_get_n_curves(tool->gmodel);
    col = tool->isel[0];
    row = tool->isel[1];
    w = tool->isel[2]+1 - tool->isel[0];
    h = tool->isel[3]+1 - tool->isel[1];
    nsel = (w >= 4 && h >= 4) ? 1 : 0;
    gwy_debug("%d x %d at (%d, %d)", w, h, col, row);

    gtk_widget_set_sensitive(tool->apply, nsel > 0);

    if (nsel == 0 && n == 0)
        return;

    if (nsel == 0 && n > 0) {
        gtk_label_set_text(GTK_LABEL(tool->average_label), "");
        gwy_graph_model_remove_all_curves(tool->gmodel);
        gtk_widget_set_sensitive(tool->apply, FALSE);
        return;
    }

    gwy_data_field_get_line_stats_mask(field,
                                       plain_tool->mask_field,
                                       tool->args.masking,
                                       tool->line, tool->weights,
                                       col, row, w, h,
                                       tool->args.output_type,
                                       tool->args.direction);

    if (nsel > 0 && n == 0) {
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_model_add_curve(tool->gmodel, gcmodel);
        g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
        g_object_unref(gcmodel);
    }
    else
        gcmodel = gwy_graph_model_get_curve(tool->gmodel, 0);

    if (!set_data_from_dataline_filtered(gcmodel,
                                         tool->line, tool->weights, 5.0)) {
        gtk_label_set_text(GTK_LABEL(tool->average_label), "");
        gwy_graph_model_remove_all_curves(tool->gmodel);
        return;
    }

    title = gettext(gwy_enum_to_string(tool->args.output_type,
                                       sf_types, G_N_ELEMENTS(sf_types)));
    g_object_set(gcmodel, "description", title, NULL);
    g_object_set(tool->gmodel, "title", title, NULL);
    gwy_graph_model_set_units_from_data_line(tool->gmodel, tool->line);
    update_target_graphs(tool);

    siunit = gwy_data_line_get_si_unit_y(tool->line);
    format = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_MARKUP,
                                    gwy_data_line_get_avg(tool->line), NULL);
    if (tool->args.output_type == GWY_LINE_STAT_RMS)
        calculate_avg_rms_for_rms(tool->line, &avg, &rms);
    else {
        avg = gwy_data_line_get_avg(tool->line);
        rms = gwy_data_line_get_rms(tool->line);
    }

    if (*format->units) {
        result = g_strdup_printf("(%.4g ± %.4g) %s",
                                 avg/format->magnitude, rms/format->magnitude,
                                 format->units);
    }
    else {
        result = g_strdup_printf("%.4g ± %.4g",
                                 avg/format->magnitude, rms/format->magnitude);
    }
    gtk_label_set_markup(GTK_LABEL(tool->average_label), result);
    g_free(result);
    gtk_widget_set_sensitive(tool->apply, TRUE);
}

static void
instant_update_changed(GtkToggleButton *check, GwyToolLineStats *tool)
{
    tool->args.instant_update = gtk_toggle_button_get_active(check);
    update_sensitivity(tool);
    if (tool->args.instant_update)
        update_curve(tool);
}

static void
output_type_changed(GtkComboBox *combo, GwyToolLineStats *tool)
{
    tool->args.output_type = gwy_enum_combo_box_get_active(combo);
    update_sensitivity(tool);
    update_curve(tool);
}

static void
direction_changed(G_GNUC_UNUSED GObject *button, GwyToolLineStats *tool)
{
    tool->args.direction = gwy_radio_buttons_get_current(tool->direction);
    update_curve(tool);
}

static void
masking_changed(GtkComboBox *combo, GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->args.masking = gwy_enum_combo_box_get_active(combo);
    if (plain_tool->data_field && plain_tool->mask_field)
        update_curve(tool);
}

static void
update_target_graphs(GwyToolLineStats *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_refilter(chooser);
}

static gboolean
filter_target_graphs(GwyContainer *data, gint id, gpointer user_data)
{
    GwyToolLineStats *tool = (GwyToolLineStats*)user_data;
    GwyGraphModel *gmodel, *targetgmodel;
    GQuark quark = gwy_app_get_graph_key_for_id(id);

    return ((gmodel = tool->gmodel)
            && gwy_container_gis_object(data, quark, (GObject**)&targetgmodel)
            && gwy_graph_model_units_are_compatible(gmodel, targetgmodel));
}

static void
target_changed(GwyToolLineStats *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_get_active_id(chooser, &tool->args.target);
}

static void
gwy_tool_line_stats_options_expanded(GtkExpander *expander,
                                     G_GNUC_UNUSED GParamSpec *pspec,
                                     GwyToolLineStats *tool)
{
    tool->args.options_visible = gtk_expander_get_expanded(expander);
}

static void
gwy_tool_line_stats_apply(GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool;
    GwyGraphModel *gmodel;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);

    if (tool->args.target.datano) {
        GwyContainer *data = gwy_app_data_browser_get(tool->args.target.datano);
        GQuark quark = gwy_app_get_graph_key_for_id(tool->args.target.id);
        gmodel = gwy_container_get_object(data, quark);
        g_return_if_fail(gmodel);
        gwy_graph_model_append_curves(gmodel, tool->gmodel, 1);
        return;
    }

    gmodel = gwy_graph_model_duplicate(tool->gmodel);
    gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container, TRUE);
    g_object_unref(gmodel);
}

static void
calculate_avg_rms_for_rms(GwyDataLine *dline, gdouble *avg, gdouble *rms)
{
    const gdouble *d = gwy_data_line_get_data_const(dline);
    gdouble z, s2 = 0.0, s4 = 0.0;
    gint n, i;

    n = gwy_data_line_get_res(dline);
    for (i = 0; i < n; i++) {
        z = d[i];
        s2 += z*z;
    }
    s2 /= n;
    for (i = 0; i < n; i++) {
        z = d[i];
        s4 += (z*z - s2)*(z*z - s2);
    }
    s4 /= n;

    *avg = sqrt(s2);
    *rms = 0.5*sqrt(s4)/(*avg);
}

static gint
set_data_from_dataline_filtered(GwyGraphCurveModel *gcmodel,
                                GwyDataLine *dline, GwyDataLine *weight,
                                gdouble threshold)
{
    gint res, i, n = 0;
    gdouble off, dx;
    gdouble *xdata, *ydata;
    const gdouble *d, *w;

    res = gwy_data_line_get_res(dline);
    dx = gwy_data_line_get_real(dline)/res;
    off = gwy_data_line_get_offset(dline);
    d = gwy_data_line_get_data(dline);
    w = gwy_data_line_get_data(weight);

    xdata = g_new(gdouble, res);
    ydata = g_new(gdouble, res);
    for (i = 0; i < res; i++) {
        if (w[i] >= threshold) {
            xdata[n] = i*dx + off;
            ydata[n] = d[i];
            n++;
        }
    }

    if (n)
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, n);
    else {
        xdata[0] = ydata[0] = 0.0;
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, 1);
    }

    g_free(xdata);
    g_free(ydata);
    return n;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
