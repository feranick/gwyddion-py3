/*
 *  $Id: distance.c 23305 2021-03-18 14:40:27Z yeti-dn $
 *  Copyright (C) 2003-2008 Nenad Ocelic, David Necas (Yeti), Petr Klapetek.
 *  E-mail: ocelic@biochem.mpg.de, yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwynullstore.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define GWY_TYPE_TOOL_DISTANCE            (gwy_tool_distance_get_type())
#define GWY_TOOL_DISTANCE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_DISTANCE, GwyToolDistance))
#define GWY_IS_TOOL_DISTANCE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_DISTANCE))
#define GWY_TOOL_DISTANCE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_DISTANCE, GwyToolDistanceClass))

enum {
    NLINES = 1024
};

enum {
    COLUMN_I, COLUMN_DX, COLUMN_DY, COLUMN_PHI, COLUMN_R, COLUMN_DZ, NCOLUMNS
};

typedef struct _GwyToolDistance      GwyToolDistance;
typedef struct _GwyToolDistanceClass GwyToolDistanceClass;

typedef struct {
    gboolean number_lines;
    GwyResultsReportType report_style;
} ToolArgs;

struct _GwyToolDistance {
    GwyPlainTool parent_instance;

    ToolArgs args;

    GtkTreeView *treeview;
    GtkTreeModel *model;
    GtkWidget *rexport;
    GtkWidget *copy;
    GtkWidget *save;
    GtkWidget *number_lines;

    GwyDataField *xerr;
    GwyDataField *yerr;
    GwyDataField *zerr;
    GwyDataField *xunc;
    GwyDataField *yunc;
    GwyDataField *zunc;

    gboolean has_calibration;

    /* potential class data */
    GwySIValueFormat *angle_format;
    GType layer_type_line;
};

struct _GwyToolDistanceClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType  gwy_tool_distance_get_type         (void)                       G_GNUC_CONST;
static void   gwy_tool_distance_finalize         (GObject *object);
static void   gwy_tool_distance_init_dialog      (GwyToolDistance *tool);
static void   gwy_tool_distance_data_switched    (GwyTool *gwytool,
                                                  GwyDataView *data_view);
static void   gwy_tool_distance_data_changed     (GwyPlainTool *plain_tool);
static void   gwy_tool_distance_selection_changed(GwyPlainTool *plain_tool,
                                                  gint hint);
static void   number_lines_changed               (GtkToggleButton *check,
                                                  GwyToolDistance *tool);
static void   report_style_changed               (GwyToolDistance *tool,
                                                  GwyResultsExport *rexport);
static void   update_headers                     (GwyToolDistance *tool);
static void   render_cell                        (GtkCellLayout *layout,
                                                  GtkCellRenderer *renderer,
                                                  GtkTreeModel *model,
                                                  GtkTreeIter *iter,
                                                  gpointer user_data);
static void   gwy_tool_distance_save             (GwyToolDistance *tool);
static void   gwy_tool_distance_copy             (GwyToolDistance *tool);
static gchar* gwy_tool_distance_create_report    (GwyToolDistance *tool);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Distance measurement tool, measures distances and angles."),
    "Nenad Ocelic <ocelic@biochem.mpg.de>",
    "2.15",
    "Nenad Ocelic & David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

static const gchar number_lines_key[] = "/module/distance/number_lines";
static const gchar report_style_key[] = "/module/distance/report_style";

static const ToolArgs default_args = {
    TRUE, GWY_RESULTS_REPORT_TABSEP,
};

GWY_MODULE_QUERY2(module_info, distance)

G_DEFINE_TYPE(GwyToolDistance, gwy_tool_distance, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_DISTANCE);

    return TRUE;
}

static void
gwy_tool_distance_class_init(GwyToolDistanceClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_distance_finalize;

    tool_class->stock_id = GWY_STOCK_DISTANCE;
    tool_class->title = _("Distance");
    tool_class->tooltip = _("Measure distances and directions between points");
    tool_class->prefix = "/module/distance";
    tool_class->default_height = 240;
    tool_class->data_switched = gwy_tool_distance_data_switched;

    ptool_class->data_changed = gwy_tool_distance_data_changed;
    ptool_class->selection_changed = gwy_tool_distance_selection_changed;
}

static void
gwy_tool_distance_finalize(GObject *object)
{
    GwyToolDistance *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_DISTANCE(object);

    settings = gwy_app_settings_get();
    gwy_container_set_boolean_by_name(settings, number_lines_key,
                                      tool->args.number_lines);
    gwy_container_set_enum_by_name(settings, report_style_key,
                                   tool->args.report_style);

    if (tool->model) {
        gtk_tree_view_set_model(tool->treeview, NULL);
        GWY_OBJECT_UNREF(tool->model);
    }
    GWY_SI_VALUE_FORMAT_FREE(tool->angle_format);

    G_OBJECT_CLASS(gwy_tool_distance_parent_class)->finalize(object);
}

static void
gwy_tool_distance_init(GwyToolDistance *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_line = gwy_plain_tool_check_layer_type(plain_tool,
                                                            "GwyLayerLine");
    if (!tool->layer_type_line)
        return;

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_MARKUP;
    plain_tool->lazy_updates = TRUE;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_boolean_by_name(settings, number_lines_key,
                                      &tool->args.number_lines);
    gwy_container_gis_enum_by_name(settings, report_style_key,
                                   &tool->args.report_style);

    tool->angle_format = gwy_si_unit_value_format_new(1.0, 1, _("deg"));
    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_line,
                                     "line");

    gwy_tool_distance_init_dialog(tool);
}

static void
gwy_tool_distance_init_dialog(GwyToolDistance *tool)
{
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkDialog *dialog;
    GtkWidget *scwin, *label;
    GwyResultsExport *rexport;
    GwyNullStore *store;
    guint i;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    store = gwy_null_store_new(0);
    tool->model = GTK_TREE_MODEL(store);
    tool->treeview = GTK_TREE_VIEW(gtk_tree_view_new_with_model(tool->model));
    gwy_plain_tool_enable_object_deletion(GWY_PLAIN_TOOL(tool), tool->treeview);

    for (i = 0; i < NCOLUMNS; i++) {
        column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_expand(column, TRUE);
        gtk_tree_view_column_set_alignment(column, 0.5);
        g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(i));
        renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "xalign", 1.0, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column), renderer,
                                           render_cell, tool, NULL);
        label = gtk_label_new(NULL);
        gtk_tree_view_column_set_widget(column, label);
        gtk_widget_show(label);
        gtk_tree_view_append_column(tool->treeview, column);
    }

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scwin), GTK_WIDGET(tool->treeview));
    gtk_box_pack_start(GTK_BOX(dialog->vbox), scwin, TRUE, TRUE, 0);

    tool->rexport = gwy_results_export_new(tool->args.report_style);
    rexport = GWY_RESULTS_EXPORT(tool->rexport);
    gwy_results_export_set_style(rexport, GWY_RESULTS_EXPORT_TABULAR_DATA);
    gwy_results_export_set_title(rexport, _("Save Distance Table"));
    gwy_results_export_set_actions_sensitive(rexport, FALSE);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), tool->rexport, FALSE, FALSE, 0);
    g_signal_connect_swapped(tool->rexport, "format-changed",
                             G_CALLBACK(report_style_changed), tool);
    g_signal_connect_swapped(tool->rexport, "copy",
                             G_CALLBACK(gwy_tool_distance_copy), tool);
    g_signal_connect_swapped(tool->rexport, "save",
                             G_CALLBACK(gwy_tool_distance_save), tool);

    tool->number_lines
    /* TRANSLATORS: Number is verb here. */
        = gtk_check_button_new_with_mnemonic(_("_Number lines"));
    gtk_box_pack_start(GTK_BOX(tool->rexport), tool->number_lines,
                       FALSE, FALSE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->number_lines),
                                 tool->args.number_lines);
    g_signal_connect(tool->number_lines, "toggled",
                     G_CALLBACK(number_lines_changed), tool);

    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), TRUE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    update_headers(tool);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_distance_data_switched(GwyTool *gwytool,
                                GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolDistance *tool;
    gboolean ignore;
    gchar xukey[24], yukey[24], zukey[24];

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_distance_parent_class)->data_switched(gwytool,
                                                                  data_view);

    if (ignore || plain_tool->init_failed)
        return;

    tool = GWY_TOOL_DISTANCE(gwytool);

    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_line,
                                "line-numbers", tool->args.number_lines,
                                "thickness", 1,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, NLINES);
        g_snprintf(xukey, sizeof(xukey), "/%d/data/cal_xunc", plain_tool->id);
        g_snprintf(yukey, sizeof(yukey), "/%d/data/cal_yunc", plain_tool->id);
        g_snprintf(zukey, sizeof(zukey), "/%d/data/cal_zunc", plain_tool->id);

        if (gwy_container_gis_object_by_name(plain_tool->container, xukey,
                                             &(tool->xunc))
            && gwy_container_gis_object_by_name(plain_tool->container, yukey,
                                                &(tool->yunc))
            && gwy_container_gis_object_by_name(plain_tool->container, zukey,
                                                &(tool->zunc))) {
            tool->has_calibration = TRUE;
        }
        else {
            tool->has_calibration = FALSE;
        }

    }
    update_headers(tool);
}

static void
gwy_tool_distance_data_changed(GwyPlainTool *plain_tool)
{
    update_headers(GWY_TOOL_DISTANCE(plain_tool));
}

static void
gwy_tool_distance_selection_changed(GwyPlainTool *plain_tool,
                                    gint hint)
{
    GwyToolDistance *tool;
    GwyNullStore *store;
    gboolean ok;
    gint n;

    tool = GWY_TOOL_DISTANCE(plain_tool);
    store = GWY_NULL_STORE(tool->model);
    n = gwy_null_store_get_n_rows(store);
    g_return_if_fail(hint <= n);

    if (hint < 0) {
        gtk_tree_view_set_model(tool->treeview, NULL);
        if (plain_tool->selection)
            n = gwy_selection_get_data(plain_tool->selection, NULL);
        else
            n = 0;
        gwy_null_store_set_n_rows(store, n);
        gtk_tree_view_set_model(tool->treeview, tool->model);
    }
    else {
        GtkTreeSelection *selection;
        GtkTreePath *path;
        GtkTreeIter iter;

        if (hint < n)
            gwy_null_store_row_changed(store, hint);
        else
            gwy_null_store_set_n_rows(store, n+1);

        gtk_tree_model_iter_nth_child(tool->model, &iter, NULL, hint);
        path = gtk_tree_model_get_path(tool->model, &iter);
        selection = gtk_tree_view_get_selection(tool->treeview);
        gtk_tree_selection_select_iter(selection, &iter);
        gtk_tree_view_scroll_to_cell(tool->treeview, path, NULL,
                                     FALSE, 0.0, 0.0);
        gtk_tree_path_free(path);
    }

    ok = (plain_tool->selection
          && gwy_selection_get_data(plain_tool->selection, NULL));
    gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                             ok);
}

static void
number_lines_changed(GtkToggleButton *check, GwyToolDistance *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    tool->args.number_lines = gtk_toggle_button_get_active(check);
    if (plain_tool->layer) {
        g_object_set(plain_tool->layer,
                     "line-numbers", tool->args.number_lines,
                     NULL);
    }
}

static void
report_style_changed(GwyToolDistance *tool, GwyResultsExport *rexport)
{
    tool->args.report_style = gwy_results_export_get_format(rexport);
}

static void
gwy_tool_distance_update_header(GwyToolDistance *tool,
                                guint col,
                                GString *str,
                                const gchar *title,
                                GwySIValueFormat *vf)
{
    GtkTreeViewColumn *column;
    GtkLabel *label;

    column = gtk_tree_view_get_column(tool->treeview, col);
    label = GTK_LABEL(gtk_tree_view_column_get_widget(column));

    g_string_assign(str, "<b>");
    g_string_append(str, title);
    g_string_append(str, "</b>");
    if (vf)
        g_string_append_printf(str, " [%s]", vf->units);
    gtk_label_set_markup(label, str->str);
}

static void
update_headers(GwyToolDistance *tool)
{
    GwyPlainTool *plain_tool;
    gboolean ok;
    GString *str;

    plain_tool = GWY_PLAIN_TOOL(tool);
    str = g_string_new(NULL);

    gwy_tool_distance_update_header(tool, COLUMN_I, str,
                                    "n", NULL);
    gwy_tool_distance_update_header(tool, COLUMN_DX, str,
                                    "Δx", plain_tool->coord_format);
    gwy_tool_distance_update_header(tool, COLUMN_DY, str,
                                    "Δy", plain_tool->coord_format);
    gwy_tool_distance_update_header(tool, COLUMN_PHI, str,
                                    "φ", tool->angle_format);
    gwy_tool_distance_update_header(tool, COLUMN_R, str,
                                    "R", plain_tool->coord_format);
    gwy_tool_distance_update_header(tool, COLUMN_DZ, str,
                                    "Δz", plain_tool->value_format);

    g_string_free(str, TRUE);

    ok = (plain_tool->selection
          && gwy_selection_get_data(plain_tool->selection, NULL));
    gwy_results_export_set_actions_sensitive(GWY_RESULTS_EXPORT(tool->rexport),
                                             ok);
}

static void
render_cell(GtkCellLayout *layout,
            GtkCellRenderer *renderer,
            GtkTreeModel *model,
            GtkTreeIter *iter,
            gpointer user_data)
{
    GwyToolDistance *tool = (GwyToolDistance*)user_data;
    GwyPlainTool *plain_tool;
    const GwySIValueFormat *vf;
    gchar buf[32];
    gdouble line[4];
    gdouble val;
    gdouble unc = 0.0;
    guint idx, id;
    gint x, y;

    id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(layout), "id"));
    gtk_tree_model_get(model, iter, 0, &idx, -1);
    if (id == COLUMN_I) {
        g_snprintf(buf, sizeof(buf), "%d", idx + 1);
        g_object_set(renderer, "text", buf, NULL);
        return;
    }

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_selection_get_object(plain_tool->selection, idx, line);

    switch (id) {
        case COLUMN_DX:
        vf = plain_tool->coord_format;
        val = line[2] - line[0];
        if (tool->has_calibration) {
            unc = gwy_data_field_get_dval_real(tool->xunc, line[0], line[1],
                                               GWY_INTERPOLATION_BILINEAR);
            unc *= unc;
            unc += (gwy_data_field_get_dval_real(tool->xunc, line[2], line[3],
                                                 GWY_INTERPOLATION_BILINEAR)
                    * gwy_data_field_get_dval_real(tool->xunc, line[2], line[3],
                                                   GWY_INTERPOLATION_BILINEAR));
            unc = sqrt(unc);
        }
        break;

        case COLUMN_DY:
        vf = plain_tool->coord_format;
        val = line[3] - line[1];
        if (tool->has_calibration) {
            unc = gwy_data_field_get_dval_real(tool->yunc, line[0], line[1],
                                               GWY_INTERPOLATION_BILINEAR);
            unc *= unc;
            unc += (gwy_data_field_get_dval_real(tool->yunc, line[2], line[3],
                                                 GWY_INTERPOLATION_BILINEAR)
                    * gwy_data_field_get_dval_real(tool->yunc, line[2], line[3],
                                                  GWY_INTERPOLATION_BILINEAR));
            unc = sqrt(unc);
        }
        break;

        case COLUMN_R:
        vf = plain_tool->coord_format;
        val = hypot(line[2] - line[0], line[3] - line[1]);
        break;

        case COLUMN_PHI:
        vf = tool->angle_format;
        val = atan2(line[1] - line[3], line[2] - line[0]) * 180.0/G_PI;
        break;

        case COLUMN_DZ:
        {
            x = floor(gwy_data_field_rtoj(plain_tool->data_field, line[2]));
            y = floor(gwy_data_field_rtoi(plain_tool->data_field, line[3]));
            val = gwy_data_field_get_val(plain_tool->data_field, x, y);
            x = floor(gwy_data_field_rtoj(plain_tool->data_field, line[0]));
            y = floor(gwy_data_field_rtoi(plain_tool->data_field, line[1]));
            val -= gwy_data_field_get_val(plain_tool->data_field, x, y);
            vf = plain_tool->value_format;
            if (tool->has_calibration) {
                unc = gwy_data_field_get_dval_real(tool->zunc, line[0], line[1],
                                                   GWY_INTERPOLATION_BILINEAR);
                unc *= unc;
                unc += (gwy_data_field_get_dval_real(tool->zunc,
                                                     line[2], line[3],
                                                     GWY_INTERPOLATION_BILINEAR)
                        * gwy_data_field_get_dval_real(tool->zunc,
                                                       line[2], line[3],
                                                       GWY_INTERPOLATION_BILINEAR));
                unc = sqrt(unc);
            }
        }
        break;

        default:
        g_return_if_reached();
        break;
    }

    if (tool->has_calibration) {
        if (vf)
            g_snprintf(buf, sizeof(buf), "%.*f±%.*f",
                       vf->precision, val/vf->magnitude,
                       vf->precision, unc/vf->magnitude);
        else
            g_snprintf(buf, sizeof(buf), "%.3g±%.3g", val, unc);
    }
    else {
        if (vf) {
            g_snprintf(buf, sizeof(buf), "%.*f",
                       vf->precision, val/vf->magnitude);
        }
        else
            g_snprintf(buf, sizeof(buf), "%.3g", val);

    }
    g_object_set(renderer, "text", buf, NULL);
}

static void
gwy_tool_distance_save(GwyToolDistance *tool)
{
    gchar *text;

    text = gwy_tool_distance_create_report(tool);
    gwy_save_auxiliary_data(_("Save Table"),
                            GTK_WINDOW(GWY_TOOL(tool)->dialog), -1, text);
    g_free(text);
}

static void
gwy_tool_distance_copy(GwyToolDistance *tool)
{
    GtkClipboard *clipboard;
    GdkDisplay *display;
    gchar *text;

    text = gwy_tool_distance_create_report(tool);
    display = gtk_widget_get_display(GTK_WIDGET(GWY_TOOL(tool)->dialog));
    clipboard = gtk_clipboard_get_for_display(display, GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    g_free(text);
}

static gchar*
gwy_tool_distance_create_report(GwyToolDistance *tool)
{
    GwyResultsReportType report_style = tool->args.report_style;
    GwyPlainTool *plain_tool;
    gint n, x, y, i;
    gdouble line[4], dx, dy, r, phi, dz;
    GwySIUnitFormatStyle style = GWY_SI_UNIT_FORMAT_UNICODE;
    GwySIValueFormat *vf_dist, *vf_phi, *vf_dz;
    GwySIUnit *xyunit, *zunit;
    GString *text;
    gchar *retval, *dx_header, *dy_header, *phi_header, *r_header, *dz_header;

    plain_tool = GWY_PLAIN_TOOL(tool);
    text = g_string_new(NULL);
    xyunit = gwy_data_field_get_si_unit_xy(plain_tool->data_field);
    zunit = gwy_data_field_get_si_unit_z(plain_tool->data_field);
    if (!(report_style & GWY_RESULTS_REPORT_MACHINE)) {
        dx = gwy_data_field_get_dx(plain_tool->data_field);
        dy = gwy_data_field_get_dy(plain_tool->data_field);
        vf_dist = gwy_si_unit_get_format(xyunit, style, fmin(dx, dy), NULL);
        gwy_data_field_get_min_max(plain_tool->data_field, &dx, &dy);
        vf_dz = gwy_si_unit_get_format(zunit, style,
                                       fmax(fabs(dx), fabs(dy))/120.0, NULL);
        vf_phi = gwy_si_unit_value_format_new(G_PI/180.0, 0, _("deg"));
    }
    else {
        vf_dist = gwy_si_unit_get_format_for_power10(xyunit, style, 0, NULL);
        vf_dz = gwy_si_unit_get_format_for_power10(zunit, style, 0, NULL);
        vf_phi = gwy_si_unit_value_format_new(1.0, 0, "");
    }

    /* FIXME: This needs some helper function. */
    dx_header = g_strdup_printf("Δx [%s]", vf_dist->units);
    dy_header = g_strdup_printf("Δy [%s]", vf_dist->units);
    phi_header = g_strdup_printf("φ [%s]", vf_phi->units);
    r_header = g_strdup_printf("R [%s]", vf_dist->units);
    dz_header = g_strdup_printf("Δz [%s]", vf_dz->units);
    gwy_format_result_table_strings(text, report_style, 5,
                                    dx_header, dy_header,
                                    phi_header, r_header, dz_header);
    g_free(dx_header);
    g_free(dy_header);
    g_free(phi_header);
    g_free(r_header);
    g_free(dz_header);

    n = gwy_selection_get_data(plain_tool->selection, NULL);
    for (i = 0; i < n; i++) {
        gwy_selection_get_object(plain_tool->selection, i, line);

        dx = line[2] - line[0];
        dy = line[3] - line[1];
        r = hypot(line[2] - line[0], line[3] - line[1]);
        phi = atan2(line[1] - line[3], line[2] - line[0]);

        x = floor(gwy_data_field_rtoj(plain_tool->data_field, line[2]));
        y = floor(gwy_data_field_rtoi(plain_tool->data_field, line[3]));
        dz = gwy_data_field_get_val(plain_tool->data_field, x, y);
        x = floor(gwy_data_field_rtoj(plain_tool->data_field, line[0]));
        y = floor(gwy_data_field_rtoi(plain_tool->data_field, line[1]));
        dz -= gwy_data_field_get_val(plain_tool->data_field, x, y);

        gwy_format_result_table_row(text, report_style, 5,
                                    dx/vf_dist->magnitude,
                                    dy/vf_dist->magnitude,
                                    phi/vf_phi->magnitude,
                                    r/vf_dist->magnitude,
                                    dz/vf_dz->magnitude);
    }

    retval = text->str;
    g_string_free(text, FALSE);

    return retval;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
