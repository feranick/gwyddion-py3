/*
 *  $Id: stats.c 23305 2021-03-18 14:40:27Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
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
#include <errno.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyresults.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/datafield.h>
#include <libprocess/stats.h>
#include <libprocess/stats_uncertainty.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define GWY_TYPE_TOOL_STATS            (gwy_tool_stats_get_type())
#define GWY_TOOL_STATS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_STATS, GwyToolStats))
#define GWY_IS_TOOL_STATS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_STATS))
#define GWY_TOOL_STATS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_STATS, GwyToolStatsClass))

typedef struct _GwyToolStats      GwyToolStats;
typedef struct _GwyToolStatsClass GwyToolStatsClass;

typedef struct {
    GwyMaskingType masking;
    GwyResultsReportType report_style;
    gboolean instant_update;
} ToolArgs;

static const gchar *guivalues[] = {
    /* Moment-based */
    "avg", "rms", "rms_gw", "Sa",
    "skew", "kurtosis",
    /* Order-based */
    "min", "max", "median",
    "Sp", "Sv", "Sz",
    /* Hybrid */
    "projarea", "area", "volume",
    "Sdq", "var",
    "theta", "phi",
    /* Other */
    "linedis",
};

enum { NGUIVALUES = G_N_ELEMENTS(guivalues) };

typedef struct {
    gdouble avg;
    gdouble Sa;
    gdouble rms;
    gdouble skew;
    gdouble kurtosis;
    gdouble projarea;
    gdouble theta;
    gdouble phi;
} StatsUncertanties;

struct _GwyToolStats {
    GwyPlainTool parent_instance;

    ToolArgs args;
    GwyResults *results;

    GwyRectSelectionLabels *rlabels;
    GtkWidget *update;
    GtkWidget *rexport;

    GtkWidget *guivalues[NGUIVALUES];
    gint isel[4];
    gint isel_prev[4];
    gdouble rsel[4];

    GSList *masking;
    GtkWidget *instant_update;

    gboolean same_units;

    gboolean has_calibration;
    GwyDataField *xunc;
    GwyDataField *yunc;
    GwyDataField *zunc;

    /* potential class data */
    GType layer_type_rect;
};

struct _GwyToolStatsClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType    gwy_tool_stats_get_type         (void)                       G_GNUC_CONST;
static void     gwy_tool_stats_finalize         (GObject *object);
static void     gwy_tool_stats_init_dialog      (GwyToolStats *tool);
static void     gwy_tool_stats_data_switched    (GwyTool *gwytool,
                                                 GwyDataView *data_view);
static void     gwy_tool_stats_data_changed     (GwyPlainTool *plain_tool);
static void     gwy_tool_stats_mask_changed     (GwyPlainTool *plain_tool);
static void     gwy_tool_stats_response         (GwyTool *tool,
                                                 gint response_id);
static void     gwy_tool_stats_selection_changed(GwyPlainTool *plain_tool,
                                                 gint hint);
static void     report_style_changed            (GwyToolStats *tool,
                                                 GwyResultsExport *rexport);
static void     update_selected_rectangle       (GwyToolStats *tool);
static void     update_labels                   (GwyToolStats *tool);
static gboolean gwy_tool_stats_calculate        (GwyToolStats *tool);
static void     calculate_uncertainties         (GwyToolStats *tool,
                                                 StatsUncertanties *unc,
                                                 GwyDataField *field,
                                                 GwyDataField *mask,
                                                 GwyMaskingType masking,
                                                 guint nn,
                                                 gint col,
                                                 gint row,
                                                 gint w,
                                                 gint h);
static void     update_units                    (GwyToolStats *tool);
static void     masking_changed                 (GtkWidget *button,
                                                 GwyToolStats *tool);
static void     instant_update_changed          (GtkToggleButton *check,
                                                 GwyToolStats *tool);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Statistics tool."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "3.6",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

static const gchar instant_update_key[] = "/module/stats/instant_update";
static const gchar masking_key[]        = "/module/stats/masking";
static const gchar report_style_key[]   = "/module/stats/report_style";

static const ToolArgs default_args = {
    GWY_MASK_IGNORE,
    GWY_RESULTS_REPORT_COLON,
    FALSE,
};

GWY_MODULE_QUERY2(module_info, stats)

G_DEFINE_TYPE(GwyToolStats, gwy_tool_stats, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_STATS);

    return TRUE;
}

static void
gwy_tool_stats_class_init(GwyToolStatsClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_stats_finalize;

    tool_class->stock_id = GWY_STOCK_STAT_QUANTITIES;
    tool_class->title = _("Statistical Quantities");
    tool_class->tooltip = _("Statistical quantities");
    tool_class->prefix = "/module/stats";
    tool_class->data_switched = gwy_tool_stats_data_switched;
    tool_class->response = gwy_tool_stats_response;

    ptool_class->data_changed = gwy_tool_stats_data_changed;
    ptool_class->mask_changed = gwy_tool_stats_mask_changed;
    ptool_class->selection_changed = gwy_tool_stats_selection_changed;
}

static void
gwy_tool_stats_finalize(GObject *object)
{
    GwyToolStats *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_STATS(object);

    settings = gwy_app_settings_get();
    gwy_container_set_enum_by_name(settings, masking_key, tool->args.masking);
    gwy_container_set_enum_by_name(settings, report_style_key,
                                   tool->args.report_style);
    gwy_container_set_boolean_by_name(settings, instant_update_key,
                                      tool->args.instant_update);
    GWY_OBJECT_UNREF(tool->results);

    G_OBJECT_CLASS(gwy_tool_stats_parent_class)->finalize(object);
}

static void
gwy_tool_stats_init(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;
    GwyResults *results;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_rect = gwy_plain_tool_check_layer_type(plain_tool,
                                                           "GwyLayerRectangle");
    if (!tool->layer_type_rect)
        return;

    plain_tool->lazy_updates = TRUE;
    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_VFMARKUP;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_enum_by_name(settings, masking_key, &tool->args.masking);
    gwy_container_gis_enum_by_name(settings, report_style_key,
                                   &tool->args.report_style);
    gwy_container_gis_boolean_by_name(settings, instant_update_key,
                                      &tool->args.instant_update);

    tool->args.masking = gwy_enum_sanitize_value(tool->args.masking,
                                                 GWY_TYPE_MASKING_TYPE);

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_rect,
                                     "rectangle");
    memset(tool->isel_prev, 0xff, 4*sizeof(gint));

    results = tool->results = gwy_results_new();
    gwy_results_add_header(results, N_("Statistical Quantities"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "image", N_("Image"));
    gwy_results_add_format(results, "isel", N_("Selected area"), TRUE,
                           N_("%{w}i × %{h}i at (%{x}i, %{y}i)"),
                           "unit-str", N_("px"), "translate-unit", TRUE,
                           NULL);
    gwy_results_add_format(results, "realsel", "", TRUE,
                           N_("%{w}v × %{h}v at (%{x}v, %{y}v)"),
                           "power-x", 1,
                           NULL);
    gwy_results_add_value_yesno(results, "masking", N_("Mask in use"));
    gwy_results_add_separator(results);

    gwy_results_add_value_z(results, "avg", N_("Average value"));
    gwy_results_add_value(results, "rms", N_("RMS roughness"),
                          "power-z", 1, "symbol", "Sq", NULL);
    gwy_results_add_value_z(results, "rms_gw", N_("RMS (grain-wise)"));
    gwy_results_add_value(results, "Sa", N_("Mean roughness"),
                          "power-z", 1, "symbol", "Sa", NULL);
    gwy_results_bind_formats(results, "Sa", "rms", "rms_gw", NULL);
    gwy_results_add_value(results, "skew", N_("Skew"), "symbol", "Ssk", NULL);
    gwy_results_add_value_plain(results, "kurtosis", N_("Excess kurtosis"));
    gwy_results_add_separator(results);

    gwy_results_add_value_z(results, "min", N_("Minimum"));
    gwy_results_add_value_z(results, "max", N_("Maximum"));
    gwy_results_add_value_z(results, "median", N_("Median"));
    gwy_results_add_value(results, "Sp", N_("Maximum peak height"),
                          "power-z", 1, "symbol", "Sp", NULL);
    gwy_results_add_value(results, "Sv", N_("Maximum pit depth"),
                          "power-z", 1, "symbol", "Sv", NULL);
    gwy_results_add_value(results, "Sz", N_("Maximum height"),
                          "power-z", 1, "symbol", "Sz", NULL);
    gwy_results_bind_formats(results,
                             "min", "max", "avg", "median", "Sp", "Sv", "Sz",
                             NULL);
    gwy_results_add_separator(results);

    gwy_results_add_value(results, "projarea", N_("Projected area"),
                          "type", GWY_RESULTS_VALUE_FLOAT,
                          "power-x", 1, "power-y", 1,
                          NULL);
    gwy_results_add_value(results, "area", N_("Surface area"),
                          "type", GWY_RESULTS_VALUE_FLOAT,
                          "power-x", 1, "power-y", 1,
                          NULL);
    gwy_results_add_value(results, "Sdq", N_("Surface slope"),
                          "type", GWY_RESULTS_VALUE_FLOAT,
                          "power-x", -1, "power-z", 1,
                          "symbol", "Sdq",
                          NULL);
    gwy_results_add_value(results, "volume", N_("Volume"),
                          "type", GWY_RESULTS_VALUE_FLOAT,
                          "power-x", 1, "power-y", 1, "power-z", 1,
                          NULL);
    gwy_results_add_value(results, "var", N_("Variation"),
                          "type", GWY_RESULTS_VALUE_FLOAT,
                          "power-x", 1, "power-z", 1,
                          NULL);
    gwy_results_add_value_angle(results, "theta", N_("Inclination θ"));
    gwy_results_add_value_angle(results, "phi", N_("Inclination φ"));
    gwy_results_add_separator(results);

    gwy_results_add_value_plain(results, "linedis", N_("Scan line discrepancy"));

    gwy_tool_stats_init_dialog(tool);
}

static void
gwy_tool_stats_rect_updated(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_rect_selection_labels_select(tool->rlabels,
                                     plain_tool->selection,
                                     plain_tool->data_field);
}

static void
gwy_tool_stats_init_dialog(GwyToolStats *tool)
{
    GtkDialog *dialog;
    GtkWidget *hbox, *vbox, *image, *label;
    GwyResultsExport *rexport;
    GtkTable *table;
    GString *str;
    const gchar *header;
    gint i, row;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    hbox = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, FALSE, FALSE, 0);

    /* Selection info */
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    tool->rlabels = gwy_rect_selection_labels_new
                         (TRUE, G_CALLBACK(gwy_tool_stats_rect_updated), tool);
    gtk_box_pack_start(GTK_BOX(vbox),
                       gwy_rect_selection_labels_get_table(tool->rlabels),
                       FALSE, FALSE, 0);

    /* Options */
    table = GTK_TABLE(gtk_table_new(6, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(table), FALSE, FALSE, 0);
    row = 0;

    label = gwy_label_new_header(_("Masking Mode"));
    gtk_table_attach(table, label, 0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    tool->masking
        = gwy_radio_buttons_create(gwy_masking_type_get_enum(), -1,
                                   G_CALLBACK(masking_changed), tool,
                                   tool->args.masking);
    row = gwy_radio_buttons_attach_to_table(tool->masking, table, 3, row);
    gtk_table_set_row_spacing(table, row-1, 8);

    label = gwy_label_new_header(_("Options"));
    gtk_table_attach(table, label, 0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    tool->instant_update
        = gtk_check_button_new_with_mnemonic(_("_Instant updates"));
    gtk_table_attach(table, tool->instant_update,
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->instant_update),
                                 tool->args.instant_update);
    g_signal_connect(tool->instant_update, "toggled",
                     G_CALLBACK(instant_update_changed), tool);
    row++;

    /* Parameters */
    table = GTK_TABLE(gtk_table_new(NGUIVALUES + 5, 2, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(table), TRUE, TRUE, 0);
    row = 0;

    str = g_string_new(NULL);
    for (i = 0; i < NGUIVALUES; i++) {
        header = NULL;
        if (i == 0)
            header = _("Moment-Based");
        else if (i == 6)
            header = _("Order-Based");
        else if (i == 12)
            header = gwy_sgettext("parameters|Hybrid");
        else if (i == 19)
            header = _("Other");

        if (header) {
            if (row)
                gtk_table_set_row_spacing(table, row-1, 8);
            gtk_table_attach(table, gwy_label_new_header(header),
                             0, 2, row, row+1, GTK_FILL, 0, 0, 0);
            row++;
        }

        g_string_assign(str, gwy_results_get_label_with_symbol(tool->results,
                                                               guivalues[i]));
        g_string_append_c(str, ':');
        label = gtk_label_new(str->str);
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
        gtk_table_attach(table, label, 0, 1, row, row+1, GTK_FILL, 0, 0, 0);

        /* XXX: We cannot split the labels to values + units because
         * 1) We cannot alight two GtkLabels to baseline, apparently.
         * 2) It can make selecting value + units difficult.
         * In principle we can add some padding to fix the positions, but
         * that is fragile... */
        tool->guivalues[i] = label = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_label_set_selectable(GTK_LABEL(label), TRUE);
        gtk_table_attach(table, label, 1, 2, row, row+1,
                         GTK_EXPAND | GTK_FILL, 0, 0, 0);
        row++;
    }
    g_string_free(str, TRUE);

    tool->rexport = gwy_results_export_new(tool->args.report_style);
    rexport = GWY_RESULTS_EXPORT(tool->rexport);
    gwy_results_export_set_title(rexport, _("Save Statistical Quantities"));
    gwy_results_export_set_results(rexport, tool->results);
    gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                             FALSE);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), tool->rexport, FALSE, FALSE, 0);
    g_signal_connect_swapped(tool->rexport, "format-changed",
                             G_CALLBACK(report_style_changed), tool);

    tool->update = gtk_dialog_add_button(dialog, _("_Update"),
                                         GWY_TOOL_RESPONSE_UPDATE);
    image = gtk_image_new_from_stock(GTK_STOCK_EXECUTE, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(tool->update), image);
    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), TRUE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    gtk_widget_set_sensitive(tool->update, !tool->args.instant_update);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_stats_data_switched(GwyTool *gwytool,
                             GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyContainer *container;
    GwyToolStats *tool;
    gboolean ignore;
    gchar xukey[24], yukey[24], zukey[24];

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    tool = GWY_TOOL_STATS(gwytool);
    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_stats_parent_class)->data_switched(gwytool,
                                                               data_view);
    if (ignore || plain_tool->init_failed)
        return;

    gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                             FALSE);
    if (data_view) {
        container = plain_tool->container;
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_rect,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);

        g_snprintf(xukey, sizeof(xukey), "/%d/data/cal_xunc", plain_tool->id);
        g_snprintf(yukey, sizeof(yukey), "/%d/data/cal_yunc", plain_tool->id);
        g_snprintf(zukey, sizeof(zukey), "/%d/data/cal_zunc", plain_tool->id);

        tool->has_calibration = FALSE;
        if (gwy_container_gis_object_by_name(container, xukey, &tool->xunc)
            && gwy_container_gis_object_by_name(container, zukey, &tool->zunc))
            tool->has_calibration = TRUE;

        update_units(tool);
        update_labels(tool);
    }
}

static void
update_units(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyDataField *field = plain_tool->data_field;
    GwySIUnit *siunitxy, *siunitz;

    siunitxy = gwy_data_field_get_si_unit_xy(field);
    siunitz = gwy_data_field_get_si_unit_z(field);
    gwy_results_set_unit(tool->results, "x", siunitxy);
    gwy_results_set_unit(tool->results, "y", siunitxy);
    gwy_results_set_unit(tool->results, "z", siunitz);

    tool->same_units = gwy_si_unit_equal(siunitxy, siunitz);
}

static void
update_selected_rectangle(GwyToolStats *tool)
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
gwy_tool_stats_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolStats *tool = GWY_TOOL_STATS(plain_tool);
    GwyContainer *container = plain_tool->container;
    gchar xukey[24], yukey[24], zukey[24];

    g_snprintf(xukey, sizeof(xukey), "/%d/data/cal_xunc", plain_tool->id);
    g_snprintf(yukey, sizeof(yukey), "/%d/data/cal_yunc", plain_tool->id);
    g_snprintf(zukey, sizeof(zukey), "/%d/data/cal_zunc", plain_tool->id);

    tool->has_calibration = FALSE;
    if (gwy_container_gis_object_by_name(container, xukey, &tool->xunc)
        && gwy_container_gis_object_by_name(container, yukey, &tool->yunc)
        && gwy_container_gis_object_by_name(container, zukey, &tool->zunc))
        GWY_TOOL_STATS(plain_tool)->has_calibration = TRUE;

    update_selected_rectangle(tool);
    update_units(tool);
    update_labels(tool);
}

static void
gwy_tool_stats_mask_changed(GwyPlainTool *plain_tool)
{
    if (GWY_TOOL_STATS(plain_tool)->args.masking != GWY_MASK_IGNORE)
        update_labels(GWY_TOOL_STATS(plain_tool));
}

static void
gwy_tool_stats_response(GwyTool *tool,
                        gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_stats_parent_class)->response(tool, response_id);

    if (response_id == GWY_TOOL_RESPONSE_UPDATE)
        update_labels(GWY_TOOL_STATS(tool));
}

static void
report_style_changed(GwyToolStats *tool, GwyResultsExport *rexport)
{
    tool->args.report_style = gwy_results_export_get_format(rexport);
}

static void
gwy_tool_stats_selection_changed(GwyPlainTool *plain_tool,
                                 gint hint)
{
    GwyToolStats *tool = GWY_TOOL_STATS(plain_tool);

    g_return_if_fail(hint <= 0);
    update_selected_rectangle(tool);
    if (tool->args.instant_update) {
        if (memcmp(tool->isel, tool->isel_prev, 4*sizeof(gint)) != 0)
            update_labels(tool);
    }
    else
        gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                                 FALSE);
}

static void
update_labels(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    guint i;

    plain_tool = GWY_PLAIN_TOOL(tool);

    if (!plain_tool->data_field) {
        for (i = 0; i < NGUIVALUES; i++)
            gtk_label_set_text(GTK_LABEL(tool->guivalues[i]), "");
        return;
    }

    if (plain_tool->pending_updates & GWY_PLAIN_TOOL_CHANGED_SELECTION)
        update_selected_rectangle(tool);
    plain_tool->pending_updates = 0;

    if (!gwy_tool_stats_calculate(tool))
        return;

    for (i = 0; i < NGUIVALUES; i++) {
        gtk_label_set_markup(GTK_LABEL(tool->guivalues[i]),
                             gwy_results_get_full(tool->results, guivalues[i]));
    }
}

static gdouble
gwy_data_field_scan_line_discrepancy(GwyDataField *dfield,
                                     GwyDataField *mask,
                                     GwyMaskingType masking,
                                     gint col, gint row,
                                     gint width, gint height)
{
    gint xres = dfield->xres, yres = dfield->yres, i, j, n;
    const gdouble *drow, *drowp, *drown, *mrow;
    gdouble v, s2 = 0.0;

    if (masking == GWY_MASK_IGNORE)
        mask = NULL;
    if (!mask)
        masking = GWY_MASK_IGNORE;

    /* Cannot calculate discrepancy with only one scan line. */
    if (yres < 2)
        return 0.0;

    n = 0;
    for (i = 0; i < height; i++) {
        /* Take the two neighbour rows, except for the first and last one,
         * where we simply calculate the difference from the sole neigbour. */
        drow = dfield->data + (row + i)*xres + col;
        drowp = (row + i > 0) ? drow - xres : drow + xres;
        drown = (row + i < yres-1) ? drow + xres : drow - xres;
        if (masking == GWY_MASK_INCLUDE) {
            mrow = mask->data + (row + i)*xres + col;
            for (j = 0; j < width; j++) {
                if (mrow[j] > 0.0) {
                    v = drow[j] - 0.5*(drowp[j] + drown[j]);
                    s2 += v*v;
                    n++;
                }
            }
        }
        else if (masking == GWY_MASK_EXCLUDE) {
            mrow = mask->data + (row + i)*xres + col;
            for (j = 0; j < width; j++) {
                if (mrow[j] <= 0.0) {
                    v = drow[j] - 0.5*(drowp[j] + drown[j]);
                    s2 += v*v;
                    n++;
                }
            }
        }
        else {
            for (j = 0; j < width; j++) {
                v = drow[j] - 0.5*(drowp[j] + drown[j]);
                s2 += v*v;
            }
            n += width;
        }
    }

    return n ? sqrt(s2/n) : 0.0;
}

static gboolean
gwy_tool_stats_calculate(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool;
    GwyDataField *field, *mask;
    GwyMaskingType masking;
    GwyResults *results = tool->results;
    StatsUncertanties unc;
    gdouble xoff, yoff, q;
    gdouble min, max, avg, median, Sa, rms, rms_gw, skew, kurtosis,
            projarea, area, Sdq, volume, var, phi, theta, linedis;
    gint nn, col, row, w, h;

    plain_tool = GWY_PLAIN_TOOL(tool);
    field = plain_tool->data_field;

    gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                             FALSE);
    gwy_assign(tool->isel_prev, tool->isel, 4);
    col = tool->isel[0];
    row = tool->isel[1];
    w = tool->isel[2]+1 - tool->isel[0];
    h = tool->isel[3]+1 - tool->isel[1];
    gwy_debug("%d x %d at (%d, %d)", w, h, col, row);

    xoff = gwy_data_field_get_xoffset(field);
    yoff = gwy_data_field_get_yoffset(field);

    if (!w || !h)
        return FALSE;

    masking = tool->args.masking;
    mask = plain_tool->mask_field;
    if (!mask || masking == GWY_MASK_IGNORE) {
        /* If one says masking is not used, set the other accordingly. */
        masking = GWY_MASK_IGNORE;
        mask = NULL;
    }

    q = gwy_data_field_get_dx(field) * gwy_data_field_get_dy(field);
    if (mask) {
        if (masking == GWY_MASK_INCLUDE)
            gwy_data_field_area_count_in_range(mask, NULL, col, row, w, h,
                                               0.0, 0.0, &nn, NULL);
        else
            gwy_data_field_area_count_in_range(mask, NULL, col, row, w, h,
                                               1.0, 1.0, NULL, &nn);
        nn = w*h - nn;
    }
    else
        nn = w*h;
    projarea = nn * q;
    /* TODO: do something more reasonable when nn == 0 */

    gwy_data_field_area_get_min_max_mask(field, mask, masking, col, row, w, h,
                                         &min, &max);
    gwy_data_field_area_get_stats_mask(field, mask, masking, col, row, w, h,
                                       &avg, &Sa, &rms, &skew, &kurtosis);
    rms_gw = gwy_data_field_area_get_grainwise_rms(field, mask, masking,
                                                   col, row, w, h);
    median = gwy_data_field_area_get_median_mask(field, mask, masking,
                                                 col, row, w, h);
    var = gwy_data_field_area_get_variation(field, mask, masking,
                                            col, row, w, h);
    Sdq = gwy_data_field_area_get_surface_slope_mask(field, mask, masking,
                                                     col, row, w, h);

    linedis = gwy_data_field_scan_line_discrepancy(field, mask, masking,
                                                   col, row, w, h);
    if (linedis > 0.0) {
        linedis /= sqrt(gwy_data_field_area_get_mean_square(field,
                                                            mask, masking,
                                                            col, row, w, h));
    }

    if (tool->same_units) {
        area = gwy_data_field_area_get_surface_area_mask(field, mask, masking,
                                                         col, row, w, h);
    }
    else
        area = 0.0;   /* Silly GCC. */

    volume = gwy_data_field_area_get_volume(field, NULL, mask, col, row, w, h);
    if (masking == GWY_MASK_EXCLUDE) {
        volume = gwy_data_field_area_get_volume(field, NULL, NULL,
                                                col, row, w, h) - volume;
    }

    if (tool->same_units && !mask) {
        gwy_data_field_area_get_inclination(field, col, row, w, h,
                                            &theta, &phi);
    }

    gwy_results_fill_format(results, "isel",
                            "w", w, "h", h, "x", col, "y", row,
                            NULL);
    gwy_results_fill_format(results, "realsel",
                            "w", fabs(tool->rsel[2] - tool->rsel[0]),
                            "h", fabs(tool->rsel[3] - tool->rsel[1]),
                            "x", MIN(tool->rsel[0], tool->rsel[2]) + xoff,
                            "y", MIN(tool->rsel[1], tool->rsel[3]) + yoff,
                            NULL);
    gwy_results_fill_values(results,
                            "masking", !!mask,
                            "min", min,
                            "max", max,
                            "median", median,
                            "Sp", max - avg,
                            "Sv", avg - min,
                            "Sz", max - min,
                            "rms_gw", rms_gw,
                            "area", area,
                            "Sdq", Sdq,
                            "volume", volume,
                            "var", var,
                            "linedis", linedis,
                            NULL);

    /* Try to have the same format for area and projected area but only if
     * it is sane. */
    gwy_results_unbind_formats(results, "area", "projarea", NULL);
    if (area < 120.0*projarea)
        gwy_results_bind_formats(results, "area", "projarea", NULL);

    if (tool->has_calibration) {
        calculate_uncertainties(tool, &unc, field, mask, masking, nn,
                                col, row, w, h);
        gwy_results_fill_values_with_errors(results,
                                            "avg", avg, unc.avg,
                                            "Sa", Sa, unc.Sa,
                                            "rms", rms, unc.rms,
                                            "skew", skew, unc.skew,
                                            "kurtosis", kurtosis, unc.kurtosis,
                                            "projarea", projarea, unc.projarea,
                                            "phi", phi, unc.phi,
                                            "theta", theta, unc.theta,
                                            NULL);
    }
    else {
        gwy_results_fill_values(results,
                                "avg", avg,
                                "Sa", Sa,
                                "rms", rms,
                                "skew", skew,
                                "kurtosis", kurtosis,
                                "projarea", projarea,
                                "phi", phi,
                                "theta", theta,
                                NULL);
    }
    if (mask)
        gwy_results_set_na(results, "phi", "theta", NULL);
    if (!tool->same_units)
        gwy_results_set_na(tool->results, "area", "theta", "phi", NULL);

    gwy_results_fill_filename(results, "file", plain_tool->container);
    gwy_results_fill_channel(results, "image",
                             plain_tool->container, plain_tool->id);

    gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                             TRUE);
    return TRUE;
}

static void
calculate_uncertainties(GwyToolStats *tool, StatsUncertanties *unc,
                        GwyDataField *field, GwyDataField *mask,
                        GwyMaskingType masking, guint nn,
                        gint col, gint row, gint w, gint h)
{
    gint xres, yres, oldx, oldy;

    g_return_if_fail(tool->has_calibration);

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    oldx = gwy_data_field_get_xres(tool->xunc);
    oldy = gwy_data_field_get_yres(tool->xunc);
    // FIXME, functions should work with data of any size
    gwy_data_field_resample(tool->xunc, xres, yres, GWY_INTERPOLATION_BILINEAR);
    gwy_data_field_resample(tool->yunc, xres, yres, GWY_INTERPOLATION_BILINEAR);
    gwy_data_field_resample(tool->zunc, xres, yres, GWY_INTERPOLATION_BILINEAR);

    unc->projarea
        = gwy_data_field_area_get_projected_area_uncertainty(nn, tool->xunc,
                                                             tool->yunc);

    gwy_data_field_area_get_stats_uncertainties_mask(field, tool->zunc,
                                                     mask, masking,
                                                     col, row, w, h,
                                                     &unc->avg,
                                                     &unc->Sa,
                                                     &unc->rms,
                                                     &unc->skew,
                                                     &unc->kurtosis);

    if (tool->same_units && !mask) {
        gwy_data_field_area_get_inclination_uncertainty(field,
                                                        tool->zunc,
                                                        tool->xunc,
                                                        tool->yunc,
                                                        col, row, w, h,
                                                        &unc->theta, &unc->phi);
    }

    gwy_data_field_resample(tool->xunc, oldx, oldy, GWY_INTERPOLATION_BILINEAR);
    gwy_data_field_resample(tool->yunc, oldx, oldy, GWY_INTERPOLATION_BILINEAR);
    gwy_data_field_resample(tool->zunc, oldx, oldy, GWY_INTERPOLATION_BILINEAR);
}

static void
masking_changed(GtkWidget *button, GwyToolStats *tool)
{
    GwyPlainTool *plain_tool;

    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
        return;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->args.masking = gwy_radio_button_get_value(button);
    if (plain_tool->data_field && plain_tool->mask_field)
        update_labels(tool);
}

static void
instant_update_changed(GtkToggleButton *check, GwyToolStats *tool)
{
    tool->args.instant_update = gtk_toggle_button_get_active(check);
    gtk_widget_set_sensitive(tool->update, !tool->args.instant_update);
    if (tool->args.instant_update)
        gwy_tool_stats_selection_changed(GWY_PLAIN_TOOL(tool), -1);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
