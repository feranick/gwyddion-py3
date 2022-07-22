/*
 *  $Id: corrlen.c 23305 2021-03-18 14:40:27Z yeti-dn $
 *  Copyright (C) 2020 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwythreads.h>
#include <libgwyddion/gwynlfitpreset.h>
#include <libgwyddion/gwyresults.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/correct.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define GWY_TYPE_TOOL_CORR_LEN            (gwy_tool_corr_len_get_type())
#define GWY_TOOL_CORR_LEN(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_CORR_LEN, GwyToolCorrLen))
#define GWY_IS_TOOL_CORR_LEN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_CORR_LEN))
#define GWY_TOOL_CORR_LEN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_CORR_LEN, GwyToolCorrLenClass))

typedef struct _GwyToolCorrLen      GwyToolCorrLen;
typedef struct _GwyToolCorrLenClass GwyToolCorrLenClass;

typedef struct {
    GwyMaskingType masking;
    GwyResultsReportType report_style;
    gboolean instant_update;
    gint level;
    GwyOrientation orientation;
} ToolArgs;

typedef struct {
    guint numer;
    guint denom;
    guint nsegments;
} Subdivision;

static const gchar *guivalues[] = {
    "acf_1e", "acf_1e_extrap", "acf_0", "psdf_gauss", "psdf_exp",
    "alpha", "L_T",
};

enum { NGUIVALUES = G_N_ELEMENTS(guivalues) };

struct _GwyToolCorrLen {
    GwyPlainTool parent_instance;

    ToolArgs args;
    GwyResults *results;

    GwyRectSelectionLabels *rlabels;
    GtkWidget *update;
    GtkWidget *rexport;
    GtkWidget *level;
    GSList *orientation;

    GtkWidget *guivalues[NGUIVALUES];
    gint isel[4];
    gint isel_prev[4];
    gdouble rsel[4];

    GSList *masking;
    GtkWidget *instant_update;

    GwyDataField *cached_flipped_field;
    GwyDataField *cached_flipped_mask;

    /* potential class data */
    GType layer_type_rect;
};

struct _GwyToolCorrLenClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType    gwy_tool_corr_len_get_type         (void)                       G_GNUC_CONST;
static void     gwy_tool_corr_len_finalize         (GObject *object);
static void     gwy_tool_corr_len_init_dialog      (GwyToolCorrLen *tool);
static void     gwy_tool_corr_len_data_switched    (GwyTool *gwytool,
                                                    GwyDataView *data_view);
static void     gwy_tool_corr_len_data_changed     (GwyPlainTool *plain_tool);
static void     gwy_tool_corr_len_mask_changed     (GwyPlainTool *plain_tool);
static void     gwy_tool_corr_len_response         (GwyTool *tool,
                                                    gint response_id);
static void     gwy_tool_corr_len_selection_changed(GwyPlainTool *plain_tool,
                                                    gint hint);
static void     report_style_changed               (GwyToolCorrLen *tool,
                                                    GwyResultsExport *rexport);
static void     update_selected_rectangle          (GwyToolCorrLen *tool);
static void     update_labels                      (GwyToolCorrLen *tool);
static gboolean gwy_tool_corr_len_calculate        (GwyToolCorrLen *tool);
static void     update_units                       (GwyToolCorrLen *tool);
static void     masking_changed                    (GtkWidget *button,
                                                    GwyToolCorrLen *tool);
static void     instant_update_changed             (GtkToggleButton *check,
                                                    GwyToolCorrLen *tool);
static void     orientation_changed                (GObject *button,
                                                    GwyToolCorrLen *tool);
static void     gwy_tool_profile_level_changed     (GtkComboBox *combo,
                                                    GwyToolCorrLen *tool);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Correlation length tool."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2020",
};

static const gchar instant_update_key[] = "/module/corrlen/instant_update";
static const gchar level_key[]          = "/module/corrlen/level";
static const gchar masking_key[]        = "/module/corrlen/masking";
static const gchar orientation_key[]    = "/module/corrlen/orientation";
static const gchar report_style_key[]   = "/module/corrlen/report_style";

static const ToolArgs default_args = {
    GWY_MASK_IGNORE,
    GWY_RESULTS_REPORT_COLON,
    FALSE,
    0,
    GWY_ORIENTATION_HORIZONTAL,
};

GWY_MODULE_QUERY2(module_info, corrlen)

G_DEFINE_TYPE(GwyToolCorrLen, gwy_tool_corr_len, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_CORR_LEN);

    return TRUE;
}

static void
gwy_tool_corr_len_class_init(GwyToolCorrLenClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_corr_len_finalize;

    tool_class->stock_id = GWY_STOCK_CORRELATION_LENGTH;
    tool_class->title = _("Correlation Length");
    tool_class->tooltip = _("Correlation Length");
    tool_class->prefix = "/module/corrlen";
    tool_class->data_switched = gwy_tool_corr_len_data_switched;
    tool_class->response = gwy_tool_corr_len_response;

    ptool_class->data_changed = gwy_tool_corr_len_data_changed;
    ptool_class->mask_changed = gwy_tool_corr_len_mask_changed;
    ptool_class->selection_changed = gwy_tool_corr_len_selection_changed;
}

static void
gwy_tool_corr_len_finalize(GObject *object)
{
    GwyToolCorrLen *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_CORR_LEN(object);

    settings = gwy_app_settings_get();
    gwy_container_set_enum_by_name(settings, masking_key, tool->args.masking);
    gwy_container_set_enum_by_name(settings, orientation_key,
                                   tool->args.orientation);
    gwy_container_set_int32_by_name(settings, level_key, tool->args.level);
    gwy_container_set_enum_by_name(settings, report_style_key,
                                   tool->args.report_style);
    gwy_container_set_boolean_by_name(settings, instant_update_key,
                                      tool->args.instant_update);
    GWY_OBJECT_UNREF(tool->results);
    GWY_OBJECT_UNREF(tool->cached_flipped_field);
    GWY_OBJECT_UNREF(tool->cached_flipped_mask);

    G_OBJECT_CLASS(gwy_tool_corr_len_parent_class)->finalize(object);
}

static void
gwy_tool_corr_len_init(GwyToolCorrLen *tool)
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
    gwy_container_gis_enum_by_name(settings, orientation_key,
                                   &tool->args.orientation);
    gwy_container_gis_int32_by_name(settings, level_key, &tool->args.level);
    gwy_container_gis_enum_by_name(settings, report_style_key,
                                   &tool->args.report_style);
    gwy_container_gis_boolean_by_name(settings, instant_update_key,
                                      &tool->args.instant_update);

    tool->args.masking = gwy_enum_sanitize_value(tool->args.masking,
                                                 GWY_TYPE_MASKING_TYPE);
    tool->args.orientation = gwy_enum_sanitize_value(tool->args.masking,
                                                     GWY_TYPE_ORIENTATION);

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_rect,
                                     "rectangle");
    memset(tool->isel_prev, 0xff, 4*sizeof(gint));

    results = tool->results = gwy_results_new();
    gwy_results_add_header(results, N_("Correlation Length"));
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

    gwy_results_add_header(results, N_("Correlation Length T"));
    gwy_results_add_value_x(results, "acf_1e", N_("Naïve ACF decay to 1/e"));
    gwy_results_add_value_x(results, "acf_1e_extrap",
                            N_("Extrapolated ACF decay to 1/e"));
    gwy_results_add_value_x(results, "acf_0", N_("ACF decay to zero"));
    gwy_results_add_value_x(results, "psdf_gauss", N_("PSDF Gaussian fit"));
    gwy_results_add_value_x(results, "psdf_exp", N_("PSDF exponential fit"));
    gwy_results_bind_formats(results,
                             "acf_1e", "acf_1e_extrap", "acf_0",
                             "psdf_gauss", "psdf_exp",
                             NULL);
    gwy_results_add_separator(results);

    gwy_results_add_header(results, N_("Relation to Image Size"));
    gwy_results_add_value_plain(results, "alpha", N_("Ratio α = T/L"));
    gwy_results_add_value_plain(results, "L_T", N_("Image size measured in T"));

    gwy_tool_corr_len_init_dialog(tool);
}

static void
gwy_tool_corr_len_rect_updated(GwyToolCorrLen *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_rect_selection_labels_select(tool->rlabels,
                                     plain_tool->selection,
                                     plain_tool->data_field);
}

static void
gwy_tool_corr_len_init_dialog(GwyToolCorrLen *tool)
{
    static const GwyEnum orientations[] = {
        { N_("_Horizontal direction"), GWY_ORIENTATION_HORIZONTAL, },
        { N_("_Vertical direction"),   GWY_ORIENTATION_VERTICAL,   },
    };
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
                         (TRUE, G_CALLBACK(gwy_tool_corr_len_rect_updated), tool);
    gtk_box_pack_start(GTK_BOX(vbox),
                       gwy_rect_selection_labels_get_table(tool->rlabels),
                       FALSE, FALSE, 0);

    /* Options */
    table = GTK_TABLE(gtk_table_new(16, 3, FALSE));
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

    tool->level = gwy_enum_combo_box_newl
                                  (G_CALLBACK(gwy_tool_profile_level_changed),
                                   tool,
                                   tool->args.level,
                                   gwy_sgettext("line-leveling|None"), 0,
                                   _("Offset"), 1,
                                   _("Tilt"), 2,
                                   NULL);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row++,
                            _("Line leveling:"), NULL,
                            GTK_OBJECT(tool->level),
                            GWY_HSCALE_WIDGET_NO_EXPAND);

    tool->orientation = gwy_radio_buttons_create
                                     (orientations, G_N_ELEMENTS(orientations),
                                     G_CALLBACK(orientation_changed), tool,
                                     tool->args.orientation);
    row = gwy_radio_buttons_attach_to_table(tool->orientation, table, 2, row);

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
    table = GTK_TABLE(gtk_table_new(NGUIVALUES + 2, 2, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(table), TRUE, TRUE, 0);
    row = 0;

    str = g_string_new(NULL);
    for (i = 0; i < NGUIVALUES; i++) {
        header = NULL;
        if (i == 0)
            header = _("Correlation Length T");
        else if (i == 5)
            header = _("Relation to Image Size");

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
gwy_tool_corr_len_data_switched(GwyTool *gwytool,
                                GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolCorrLen *tool;
    gboolean ignore;

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    tool = GWY_TOOL_CORR_LEN(gwytool);
    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_corr_len_parent_class)->data_switched(gwytool,
                                                               data_view);
    if (ignore || plain_tool->init_failed)
        return;

    GWY_OBJECT_UNREF(tool->cached_flipped_field);
    GWY_OBJECT_UNREF(tool->cached_flipped_mask);

    gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                             FALSE);
    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_rect,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);

        update_units(tool);
        update_labels(tool);
    }
}

static void
update_units(GwyToolCorrLen *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyDataField *field = plain_tool->data_field;
    GwySIUnit *siunitxy, *siunitz;

    siunitxy = gwy_data_field_get_si_unit_xy(field);
    siunitz = gwy_data_field_get_si_unit_z(field);
    gwy_results_set_unit(tool->results, "x", siunitxy);
    gwy_results_set_unit(tool->results, "y", siunitxy);
    gwy_results_set_unit(tool->results, "z", siunitz);
}

static void
update_selected_rectangle(GwyToolCorrLen *tool)
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
gwy_tool_corr_len_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolCorrLen *tool = GWY_TOOL_CORR_LEN(plain_tool);

    GWY_OBJECT_UNREF(tool->cached_flipped_field);
    update_selected_rectangle(tool);
    update_units(tool);
    update_labels(tool);
}

static void
gwy_tool_corr_len_mask_changed(GwyPlainTool *plain_tool)
{
    GwyToolCorrLen *tool = GWY_TOOL_CORR_LEN(plain_tool);

    GWY_OBJECT_UNREF(tool->cached_flipped_mask);
    if (GWY_TOOL_CORR_LEN(plain_tool)->args.masking != GWY_MASK_IGNORE)
        update_labels(GWY_TOOL_CORR_LEN(plain_tool));
}

static void
gwy_tool_corr_len_response(GwyTool *tool,
                           gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_corr_len_parent_class)->response(tool, response_id);

    if (response_id == GWY_TOOL_RESPONSE_UPDATE)
        update_labels(GWY_TOOL_CORR_LEN(tool));
}

static void
report_style_changed(GwyToolCorrLen *tool, GwyResultsExport *rexport)
{
    tool->args.report_style = gwy_results_export_get_format(rexport);
}

static void
gwy_tool_corr_len_selection_changed(GwyPlainTool *plain_tool,
                                    gint hint)
{
    GwyToolCorrLen *tool = GWY_TOOL_CORR_LEN(plain_tool);

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
update_labels(GwyToolCorrLen *tool)
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

    if (!gwy_tool_corr_len_calculate(tool))
        return;

    for (i = 0; i < NGUIVALUES; i++) {
        gtk_label_set_markup(GTK_LABEL(tool->guivalues[i]),
                             gwy_results_get_full(tool->results, guivalues[i]));
    }
}

static gdouble
find_decay_point(GwyDataLine *line, gdouble q)
{
    const gdouble *d = gwy_data_line_get_data(line);
    guint res = gwy_data_line_get_res(line);
    gdouble max = d[0];
    gdouble threshold = q*max;
    guint i;
    gdouble v0, v1, t;

    for (i = 1; i < res; i++) {
        if (d[i] <= threshold) {
            if (d[i] == threshold)
                return gwy_data_line_itor(line, i);

            v0 = d[i-1] - threshold;
            v1 = d[i] - threshold;
            t = v0/(v0 - v1);
            return gwy_data_line_itor(line, i-1 + t);
        }
    }

    return -1.0;
}

static gdouble*
make_xdata(GwyDataLine *line, guint n)
{
    guint i, res = gwy_data_line_get_res(line);
    gdouble dx = gwy_data_line_get_dx(line);
    gdouble *xdata = g_new(gdouble, n);

    g_assert(n <= res);

    for (i = 0; i < n; i++)
        xdata[i] = dx*i;

    return xdata;
}

static gdouble
fit_T_from_psdf(GwyDataLine *psdf, const gchar *presetname,
                gdouble T_estim)
{
    GwyNLFitPreset *preset = gwy_inventory_get_item(gwy_nlfit_presets(),
                                                    presetname);
    guint nfit, i, res = gwy_data_line_get_res(psdf);
    const gdouble *ydata = gwy_data_line_get_data(psdf);
    gdouble *xdata, *xbuf;
    gdouble T, t = 0.0, s = gwy_data_line_get_sum(psdf);
    GwyNLFitter *fitter;
    gdouble params[2], errors[2];

    for (nfit = 0; nfit < res; nfit++) {
        t += ydata[nfit];
        if (t > 0.999*s)
            break;
    }
    xdata = xbuf = make_xdata(psdf, nfit);

    /* Try to skip the smallest frequencies.  Unfortunately, we cannot do that
     * for tiny data. */
    for (i = 0; i < 4; i++) {
        if (nfit > (4 << i)) {
            xdata++;
            ydata++;
            nfit--;
        }
    }

    params[0] = sqrt(s*gwy_data_line_get_dx(psdf));
    params[1] = T_estim;
    fitter = gwy_nlfit_preset_fit(preset, NULL, nfit, xdata, ydata,
                                  params, errors, NULL);
    T = (gwy_math_nlfit_succeeded(fitter) ? params[1] : -1.0);
    gwy_math_nlfit_free(fitter);
    g_free(xbuf);

    return T;
}

static GwyDataLine*
make_subdivided_row_acf(GwyDataField *field,
                        GwyDataField *mask, GwyMaskingType masking,
                        gint col, gint row, gint width, gint height,
                        gint level,
                        const Subdivision *subdiv,
                        gdouble *inv_L)
{
    GwyDataLine *acf, *acfsum, *w, *wsum;
    guint swidth, colfrom, nzeros, i, res;
    gdouble *d, *m;

    swidth = subdiv->numer*width/subdiv->denom;
    if (swidth < 4 || swidth >= width || subdiv->nsegments == 1) {
        *inv_L = 1.0;
        return gwy_data_field_area_row_acf(field, mask, masking,
                                           col, row, width, height, level,
                                           NULL);
    }

    *inv_L = (gdouble)width/swidth;
    wsum = gwy_data_line_new(1, 1.0, FALSE);
    acfsum = gwy_data_field_area_row_acf(field, mask, masking,
                                         col, row, swidth, height, level,
                                         wsum);
    gwy_data_line_multiply_lines(acfsum, acfsum, wsum);

    w = gwy_data_line_new_alike(wsum, FALSE);
    for (i = 1; i < subdiv->nsegments; i++) {
        colfrom = col + i*(width - swidth)/(subdiv->nsegments - 1);
        acf = gwy_data_field_area_row_acf(field, mask, masking,
                                          colfrom, row, swidth, height, level,
                                          w);
        gwy_data_line_multiply_lines(acf, acf, w);
        gwy_data_line_sum_lines(acfsum, acfsum, acf);
        gwy_data_line_sum_lines(wsum, wsum, w);
        g_object_unref(acf);
    }
    g_object_unref(w);

    res = gwy_data_line_get_res(acfsum);
    d = gwy_data_line_get_data(acfsum);
    m = gwy_data_line_get_data(wsum);
    nzeros = 0;
    for (i = 0; i < res; i++) {
        if (m[i] > 0.0) {
            d[i] /= m[i];
            m[i] = 0.0;
        }
        else {
            m[i] = 1.0;
            nzeros++;
        }
    }
    gwy_data_line_correct_laplace(acfsum, wsum);
    g_object_unref(wsum);

    return acfsum;
}

#if 0
static gdouble
estimate_missing_spectral_weight(GwyDataLine *psdf)
{
    const gdouble *d = gwy_data_line_get_data(psdf);
    guint i, imax, res = gwy_data_line_get_res(psdf);
    gdouble dx = gwy_data_line_get_dx(psdf);
    gdouble max = -G_MAXDOUBLE, w = 0.0;

    /* Find where the initial increasing part of PSDF ends. */
    max = d[0];
    imax = 0;
    for (i = 1; i+1 < res; i++) {
        if (d[i] <= max)
            break;
        max = d[i];
    }
    imax = i-1;

    /* Calculate the missing weight by assuming PSDF values for all smaller
     * frequencies are equal to max.  This is still underestimated, but in many
     * reasonable cases the error is small. */
    for (i = 0; i < imax; i++)
        w += max - d[i];

    return w*dx;
}
#endif

static gboolean
gwy_tool_corr_len_calculate(GwyToolCorrLen *tool)
{
    static const Subdivision subdivisions[] = {
        { 1, 1, 1 },   /* The naïve estimate.  Keep it first. */
        { 9, 10, 2 },
        { 5, 6, 2 },
        { 3, 4, 2 },
        { 2, 3, 3 },
        { 3, 5, 3 },
        { 5, 9, 3 },
        { 1, 2, 3 },
    };
    enum { NSUBDIVISIONS = G_N_ELEMENTS(subdivisions) };

    GwyPlainTool *plain_tool;
    GwyDataField *field, *mask, *field_to_use, *mask_to_use;
    GwyDataLine *psdf;
    GwyMaskingType masking;
    GwyResults *results = tool->results;
    gdouble xoff, yoff;
    gdouble L, T_estim, acf_1e, acf_0, psdf_gauss, psdf_exp;
    gdouble T_extrapol;
    gdouble inv_L[NSUBDIVISIONS], T_1e[NSUBDIVISIONS];
    gdouble coeffs[2];
    GwyOrientation orientation = tool->args.orientation;
    gint col, row, w, h, level = tool->args.level;
    guint i;

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

    if (w < 4 || h < 4)
        return FALSE;

    masking = tool->args.masking;
    mask = plain_tool->mask_field;

    field_to_use = field;
    mask_to_use = NULL;
    if (orientation == GWY_ORIENTATION_VERTICAL) {
        if (!tool->cached_flipped_field) {
            tool->cached_flipped_field = gwy_data_field_new_alike(field,
                                                                  FALSE);
            gwy_data_field_flip_xy(field, tool->cached_flipped_field, FALSE);
        }
        field_to_use = tool->cached_flipped_field;
        if (masking != GWY_MASK_IGNORE && mask) {
            if (!tool->cached_flipped_mask) {
                tool->cached_flipped_mask = gwy_data_field_new_alike(mask,
                                                                     FALSE);
                gwy_data_field_flip_xy(mask, tool->cached_flipped_mask, FALSE);
            }
        }
        mask_to_use = tool->cached_flipped_mask;

        GWY_SWAP(gint, col, row);
        GWY_SWAP(gint, w, h);
    }
    L = w*gwy_data_field_get_dx(field_to_use);

    /************************************************************************
     *
     * ACF-based calculations.
     *
     ************************************************************************/
    acf_0 = sizeof("shut up, GCC!");
    /* Cannot use default(none) because of incompatible change in OpenMP specs
     * for static const variables. */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) \
            shared(field_to_use,mask_to_use,masking,col,row,w,h,level,inv_L,T_1e,acf_0) \
            private(i)
#endif
    for (i = 0; i < NSUBDIVISIONS; i++) {
        GwyDataLine *acf;

        acf = make_subdivided_row_acf(field_to_use, mask_to_use, masking,
                                      col, row, w, h, level,
                                      subdivisions + i, inv_L + i);
        T_1e[i] = find_decay_point(acf, exp(-1.0));
        if (i == 0)
            acf_0 = find_decay_point(acf, 0.0);
        g_object_unref(acf);
    }
    acf_1e = T_1e[0];

    gwy_math_fit_polynom(NSUBDIVISIONS, inv_L, T_1e, 1, coeffs);
    T_extrapol = coeffs[0];

    /************************************************************************
     *
     * PSDF-based calculations.
     *
     ************************************************************************/
    psdf = gwy_data_field_area_row_psdf(field_to_use, mask_to_use, masking,
                                        col, row, w, h,
                                        GWY_WINDOWING_HANN, level);
    T_estim = (acf_1e > 0.0 ? acf_1e : 0.05*L);
    psdf_gauss = fit_T_from_psdf(psdf, "Gaussian (PSDF)", T_estim);
    psdf_exp = fit_T_from_psdf(psdf, "Exponential (PSDF)", T_estim);
    /* missing_w = estimate_missing_spectral_weight(psdf); */
    g_object_unref(psdf);

    /************************************************************************
     *
     * Results.
     *
     ************************************************************************/
    gwy_results_fill_format(results, "isel",
                            "w", w, "h", h, "x", col, "y", row,
                            NULL);
    gwy_results_fill_format(results, "realsel",
                            "w", fabs(tool->rsel[2] - tool->rsel[0]),
                            "h", fabs(tool->rsel[3] - tool->rsel[1]),
                            "x", MIN(tool->rsel[0], tool->rsel[2]) + xoff,
                            "y", MIN(tool->rsel[1], tool->rsel[3]) + yoff,
                            NULL);
    gwy_results_fill_values(results, "masking", !!mask, NULL);

    if (acf_1e > 0.0) {
        gwy_results_fill_values(results, "acf_1e", acf_1e, NULL);
        if (T_extrapol > 0.0) {
            gwy_results_fill_values(results,
                                    "acf_1e_extrap", T_extrapol,
                                    "alpha", T_extrapol/L,
                                    "L_T", L/T_extrapol,
                                    NULL);
        }
    }
    else {
        gwy_results_set_na(results,
                           "acf_1e", "acf_1e_extrap", "alpha", "L_T",
                           NULL);
    }

    if (acf_0 > 0.0)
        gwy_results_fill_values(results, "acf_0", acf_0, NULL);
    else
        gwy_results_set_na(results, "acf_0", NULL);

    if (psdf_gauss > 0.0)
        gwy_results_fill_values(results, "psdf_gauss", psdf_gauss, NULL);
    else
        gwy_results_set_na(results, "psdf_gauss", NULL);

    if (psdf_exp > 0.0)
        gwy_results_fill_values(results, "psdf_exp", psdf_exp, NULL);
    else
        gwy_results_set_na(results, "psdf_exp", NULL);

    gwy_results_fill_filename(results, "file", plain_tool->container);
    gwy_results_fill_channel(results, "image",
                             plain_tool->container, plain_tool->id);

    gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                             TRUE);
    return TRUE;
}

static void
masking_changed(GtkWidget *button, GwyToolCorrLen *tool)
{
    GwyPlainTool *plain_tool;

    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
        return;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->args.masking = gwy_radio_button_get_value(button);
    GWY_OBJECT_UNREF(tool->cached_flipped_mask);
    if (tool->args.instant_update
        && plain_tool->data_field
        && plain_tool->mask_field)
        update_labels(tool);
}

static void
instant_update_changed(GtkToggleButton *check, GwyToolCorrLen *tool)
{
    tool->args.instant_update = gtk_toggle_button_get_active(check);
    gtk_widget_set_sensitive(tool->update, !tool->args.instant_update);
    if (tool->args.instant_update)
        gwy_tool_corr_len_selection_changed(GWY_PLAIN_TOOL(tool), -1);
}

static void
orientation_changed(G_GNUC_UNUSED GObject *button, GwyToolCorrLen *tool)
{
    tool->args.orientation = gwy_radio_buttons_get_current(tool->orientation);
    GWY_OBJECT_UNREF(tool->cached_flipped_field);
    GWY_OBJECT_UNREF(tool->cached_flipped_mask);
    if (tool->args.instant_update)
        update_labels(tool);
}

static void
gwy_tool_profile_level_changed(GtkComboBox *combo, GwyToolCorrLen *tool)
{
    tool->args.level = gwy_enum_combo_box_get_active(combo);
    if (tool->args.instant_update)
        update_labels(tool);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
