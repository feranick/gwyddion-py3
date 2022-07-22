/*
 *  $Id: rprofile.c 23307 2021-03-18 15:56:45Z yeti-dn $
 *  Copyright (C) 2003-2019 David Necas (Yeti), Petr Klapetek.
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/datafield.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"

#define GWY_TYPE_TOOL_RPROFILE            (gwy_tool_rprofile_get_type())
#define GWY_TOOL_RPROFILE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_RPROFILE, GwyToolRprofile))
#define GWY_IS_TOOL_RPROFILE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_RPROFILE))
#define GWY_TOOL_RPROFILE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_RPROFILE, GwyToolRprofileClass))

enum {
    NLINES = 1024,
    MIN_RESOLUTION = 4,
    MAX_RESOLUTION = 16384
};

enum {
    COLUMN_I, COLUMN_X1, COLUMN_Y1, COLUMN_X2, COLUMN_Y2, NCOLUMNS
};

typedef struct _GwyToolRprofile      GwyToolRprofile;
typedef struct _GwyToolRprofileClass GwyToolRprofileClass;

typedef struct {
    gboolean options_visible;
    gint resolution;
    gboolean fixres;
    GwyMaskingType masking;
    gboolean separate;
    gboolean number_lines;
    GwyAppDataId target;
} ToolArgs;

struct _GwyToolRprofile {
    GwyPlainTool parent_instance;

    ToolArgs args;

    GtkTreeView *treeview;
    GtkTreeModel *model;

    GwyDataLine *line;
    GtkWidget *graph;
    GwyGraphModel *gmodel;
    GdkPixbuf *colorpixbuf;

    GtkWidget *options;
    GtkWidget *improve;
    GtkWidget *improve_all;
    GtkObject *resolution;
    GtkWidget *fixres;
    GtkWidget *number_lines;
    GtkWidget *separate;
    GtkWidget *apply;
    GtkWidget *target_graph;
    GtkWidget *masking;

    /* potential class data */
    GwySIValueFormat *pixel_format;
    GType layer_type_line;
};

struct _GwyToolRprofileClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType    gwy_tool_rprofile_get_type              (void)                      G_GNUC_CONST;
static void     gwy_tool_rprofile_finalize              (GObject *object);
static void     gwy_tool_rprofile_init_dialog           (GwyToolRprofile *tool);
static void     gwy_tool_rprofile_data_switched         (GwyTool *gwytool,
                                                         GwyDataView *data_view);
static void     gwy_tool_rprofile_response              (GwyTool *tool,
                                                         gint response_id);
static void     gwy_tool_rprofile_data_changed          (GwyPlainTool *plain_tool);
static void     gwy_tool_rprofile_selection_changed     (GwyPlainTool *plain_tool,
                                                         gint hint);
static void     gwy_tool_rprofile_update_symm_sensitivty(GwyToolRprofile *tool);
static void     gwy_tool_rprofile_update_curve          (GwyToolRprofile *tool,
                                                         gint i);
static void     gwy_tool_rprofile_update_all_curves     (GwyToolRprofile *tool);
static void     gwy_tool_rprofile_improve_all           (GwyToolRprofile *tool);
static void     gwy_tool_rprofile_improve               (GwyToolRprofile *tool);
static void     gwy_tool_rprofile_symmetrize_profile    (GwyToolRprofile *tool,
                                                         gint id);
static void     gwy_tool_rprofile_render_cell           (GtkCellLayout *layout,
                                                         GtkCellRenderer *renderer,
                                                         GtkTreeModel *model,
                                                         GtkTreeIter *iter,
                                                         gpointer user_data);
static void     gwy_tool_rprofile_render_color          (GtkCellLayout *layout,
                                                         GtkCellRenderer *renderer,
                                                         GtkTreeModel *model,
                                                         GtkTreeIter *iter,
                                                         gpointer user_data);
static void     gwy_tool_rprofile_options_expanded      (GtkExpander *expander,
                                                         GParamSpec *pspec,
                                                         GwyToolRprofile *tool);
static void     gwy_tool_rprofile_resolution_changed    (GwyToolRprofile *tool,
                                                         GtkAdjustment *adj);
static void     gwy_tool_rprofile_fixres_changed        (GtkToggleButton *check,
                                                         GwyToolRprofile *tool);
static void     gwy_tool_rprofile_number_lines_changed  (GtkToggleButton *check,
                                                         GwyToolRprofile *tool);
static void     gwy_tool_rprofile_separate_changed      (GtkToggleButton *check,
                                                         GwyToolRprofile *tool);
static void     gwy_tool_rprofile_update_target_graphs  (GwyToolRprofile *tool);
static gboolean filter_target_graphs                    (GwyContainer *data,
                                                         gint id,
                                                         gpointer user_data);
static void     gwy_tool_rprofile_target_changed        (GwyToolRprofile *tool);
static void     gwy_tool_rprofile_masking_changed       (GtkComboBox *combo,
                                                         GwyToolRprofile *tool);
static void     gwy_tool_rprofile_apply                 (GwyToolRprofile *tool);

static gdouble
angular_average_mismatch(GwyDataField *data_field,
                         GwyDataField *mask,
                         GwyMaskingType masking,
                         gdouble x,
                         gdouble y,
                         gdouble r,
                         gint nstats);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Creates angularly averaged profile graphs."),
    "Yeti <yeti@gwyddion.net>",
    "1.4",
    "David Nečas (Yeti) & Petr Klapetek",
    "2018",
};

static const gchar fixres_key[]          = "/module/rprofile/fixres";
static const gchar masking_key[]         = "/module/rprofile/masking";
static const gchar number_lines_key[]    = "/module/rprofile/number_lines";
static const gchar options_visible_key[] = "/module/rprofile/options_visible";
static const gchar resolution_key[]      = "/module/rprofile/resolution";
static const gchar separate_key[]        = "/module/rprofile/separate";

static const ToolArgs default_args = {
    FALSE,
    120,
    FALSE,
    GWY_MASK_IGNORE,
    FALSE,
    TRUE,
    GWY_APP_DATA_ID_NONE,
};

GWY_MODULE_QUERY2(module_info, rprofile)

G_DEFINE_TYPE(GwyToolRprofile, gwy_tool_rprofile, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_RPROFILE);

    return TRUE;
}

static void
gwy_tool_rprofile_class_init(GwyToolRprofileClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_rprofile_finalize;

    tool_class->stock_id = GWY_STOCK_RADIAL_PROFILE;
    tool_class->title = _("Radial Profiles");
    tool_class->tooltip = _("Extract angularly averaged profiles");
    tool_class->prefix = "/module/rprofile";
    tool_class->default_width = 640;
    tool_class->default_height = 400;
    tool_class->data_switched = gwy_tool_rprofile_data_switched;
    tool_class->response = gwy_tool_rprofile_response;

    ptool_class->data_changed = gwy_tool_rprofile_data_changed;
    ptool_class->selection_changed = gwy_tool_rprofile_selection_changed;
}

static void
gwy_tool_rprofile_finalize(GObject *object)
{
    GwyToolRprofile *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_RPROFILE(object);

    settings = gwy_app_settings_get();
    gwy_container_set_boolean_by_name(settings, options_visible_key,
                                      tool->args.options_visible);
    gwy_container_set_int32_by_name(settings, resolution_key,
                                    tool->args.resolution);
    gwy_container_set_boolean_by_name(settings, fixres_key,
                                      tool->args.fixres);
    gwy_container_set_enum_by_name(settings, masking_key,
                                   tool->args.masking);
    gwy_container_set_boolean_by_name(settings, separate_key,
                                      tool->args.separate);
    gwy_container_set_boolean_by_name(settings, number_lines_key,
                                      tool->args.number_lines);

    GWY_OBJECT_UNREF(tool->line);
    if (tool->model) {
        gtk_tree_view_set_model(tool->treeview, NULL);
        GWY_OBJECT_UNREF(tool->model);
    }
    GWY_OBJECT_UNREF(tool->colorpixbuf);
    GWY_OBJECT_UNREF(tool->gmodel);
    GWY_SI_VALUE_FORMAT_FREE(tool->pixel_format);
    G_OBJECT_CLASS(gwy_tool_rprofile_parent_class)->finalize(object);
}

static void
gwy_tool_rprofile_init(GwyToolRprofile *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;
    gint width, height;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_line = gwy_plain_tool_check_layer_type(plain_tool,
                                                            "GwyLayerLine");
    if (!tool->layer_type_line)
        return;

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_MARKUP;
    plain_tool->lazy_updates = TRUE;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_boolean_by_name(settings, options_visible_key,
                                      &tool->args.options_visible);
    gwy_container_gis_int32_by_name(settings, resolution_key,
                                    &tool->args.resolution);
    gwy_container_gis_boolean_by_name(settings, fixres_key,
                                      &tool->args.fixres);
    gwy_container_gis_enum_by_name(settings, masking_key,
                                   &tool->args.masking);
    tool->args.masking = gwy_enum_sanitize_value(tool->args.masking,
                                                 GWY_TYPE_MASKING_TYPE);
    gwy_container_gis_boolean_by_name(settings, separate_key,
                                      &tool->args.separate);
    gwy_container_gis_boolean_by_name(settings, number_lines_key,
                                      &tool->args.number_lines);

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    height |= 1;
    tool->colorpixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                                       height, height);

    tool->pixel_format = gwy_si_unit_value_format_new(1.0, 0, _("px"));
    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_line,
                                     "line");

    gwy_tool_rprofile_init_dialog(tool);
}

static void
gwy_tool_rprofile_init_dialog(GwyToolRprofile *tool)
{
    static const gchar *column_titles[] = {
        "<b>n</b>",
        "<b>x<sub>1</sub></b>",
        "<b>y<sub>1</sub></b>",
        "<b>x<sub>2</sub></b>",
        "<b>y<sub>2</sub></b>",
    };
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;
    GtkCellRenderer *renderer;
    GtkDialog *dialog;
    GtkWidget *scwin, *label, *hbox, *vbox, *hbox2;
    GtkTable *table;
    GwyNullStore *store;
    guint i, row;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, TRUE, TRUE, 0);

    /* Left pane */
    vbox = gtk_vbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    /* Line coordinates */
    store = gwy_null_store_new(0);
    tool->model = GTK_TREE_MODEL(store);
    tool->treeview = GTK_TREE_VIEW(gtk_tree_view_new_with_model(tool->model));
    gwy_plain_tool_enable_object_deletion(GWY_PLAIN_TOOL(tool), tool->treeview);

    selection = gtk_tree_view_get_selection(tool->treeview);
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(gwy_tool_rprofile_update_symm_sensitivty),
                             tool);

    for (i = 0; i < NCOLUMNS; i++) {
        column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_expand(column, TRUE);
        gtk_tree_view_column_set_alignment(column, 0.5);
        g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(i));
        renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "xalign", 1.0, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column), renderer,
                                           gwy_tool_rprofile_render_cell, tool,
                                           NULL);
        if (i == COLUMN_I) {
            renderer = gtk_cell_renderer_pixbuf_new();
            g_object_set(renderer, "pixbuf", tool->colorpixbuf, NULL);
            gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column),
                                       renderer, FALSE);
            gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column),
                                               renderer,
                                               gwy_tool_rprofile_render_color,
                                               tool,
                                               NULL);
        }

        label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), column_titles[i]);
        gtk_tree_view_column_set_widget(column, label);
        gtk_widget_show(label);
        gtk_tree_view_append_column(tool->treeview, column);
    }

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scwin), GTK_WIDGET(tool->treeview));
    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    /* Options */
    tool->options = gtk_expander_new(_("<b>Options</b>"));
    gtk_expander_set_use_markup(GTK_EXPANDER(tool->options), TRUE);
    gtk_expander_set_expanded(GTK_EXPANDER(tool->options),
                              tool->args.options_visible);
    g_signal_connect(tool->options, "notify::expanded",
                     G_CALLBACK(gwy_tool_rprofile_options_expanded), tool);
    gtk_box_pack_start(GTK_BOX(vbox), tool->options, FALSE, FALSE, 0);

    table = GTK_TABLE(gtk_table_new(6, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_container_add(GTK_CONTAINER(tool->options), GTK_WIDGET(table));
    row = 0;

    hbox2 = gtk_hbox_new(FALSE, 2);
    gtk_table_attach(table, hbox2, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    tool->improve_all = gtk_button_new_with_mnemonic(_("Symmetrize _All"));
    gtk_box_pack_end(GTK_BOX(hbox2), tool->improve_all, FALSE, FALSE, 0);
    g_signal_connect_swapped(tool->improve_all, "clicked",
                             G_CALLBACK(gwy_tool_rprofile_improve_all), tool);
    tool->improve = gtk_button_new_with_mnemonic(_("S_ymmetrize"));
    gtk_box_pack_end(GTK_BOX(hbox2), tool->improve, FALSE, FALSE, 0);
    g_signal_connect_swapped(tool->improve, "clicked",
                             G_CALLBACK(gwy_tool_rprofile_improve), tool);
    row++;

    tool->resolution = gtk_adjustment_new(tool->args.resolution,
                                          MIN_RESOLUTION, MAX_RESOLUTION,
                                          1, 10, 0);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row,
                            _("_Fixed resolution:"), NULL,
                            tool->resolution, GWY_HSCALE_CHECK);
    g_signal_connect_swapped(tool->resolution, "value-changed",
                             G_CALLBACK(gwy_tool_rprofile_resolution_changed),
                             tool);
    tool->fixres = gwy_table_hscale_get_check(tool->resolution);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->fixres),
                                 tool->args.fixres);
    g_signal_connect(tool->fixres, "toggled",
                     G_CALLBACK(gwy_tool_rprofile_fixres_changed), tool);
    row++;

    tool->number_lines
        = gtk_check_button_new_with_mnemonic(_("_Number lines"));
    gtk_table_attach(table, tool->number_lines,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->number_lines),
                                 tool->args.number_lines);
    g_signal_connect(tool->number_lines, "toggled",
                     G_CALLBACK(gwy_tool_rprofile_number_lines_changed), tool);
    row++;

    tool->separate
        = gtk_check_button_new_with_mnemonic(_("_Separate profiles"));
    gtk_table_attach(table, tool->separate,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->separate),
                                 tool->args.separate);
    g_signal_connect(tool->separate, "toggled",
                     G_CALLBACK(gwy_tool_rprofile_separate_changed), tool);
    row++;

    tool->masking = gwy_enum_combo_box_new
                            (gwy_masking_type_get_enum(), -1,
                             G_CALLBACK(gwy_tool_rprofile_masking_changed),
                             tool,
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
                             G_CALLBACK(gwy_tool_rprofile_target_changed),
                             tool);
    row++;

    tool->gmodel = gwy_graph_model_new();
    g_object_set(tool->gmodel, "title", _("Radial profiles"), NULL);

    tool->graph = gwy_graph_new(tool->gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(tool->graph), FALSE);
    g_object_set(tool->gmodel, "label-visible", FALSE, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), tool->graph, TRUE, TRUE, 2);

    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), FALSE);
    tool->apply = gtk_dialog_add_button(dialog, GTK_STOCK_APPLY,
                                        GTK_RESPONSE_APPLY);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_rprofile_data_switched(GwyTool *gwytool, GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolRprofile *tool;
    gboolean ignore;

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_rprofile_parent_class)->data_switched(gwytool,
                                                                 data_view);

    if (ignore || plain_tool->init_failed)
        return;

    tool = GWY_TOOL_RPROFILE(gwytool);
    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_line,
                                "line-numbers", tool->args.number_lines,
                                "thickness", 1,
                                "center-tick", TRUE,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, NLINES);
    }

    gwy_graph_model_remove_all_curves(tool->gmodel);
    gwy_tool_rprofile_update_all_curves(tool);
    gwy_tool_rprofile_update_target_graphs(tool);
}

static void
gwy_tool_rprofile_response(GwyTool *tool,
                           gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_rprofile_parent_class)->response(tool, response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_rprofile_apply(GWY_TOOL_RPROFILE(tool));
}

static void
gwy_tool_rprofile_data_changed(GwyPlainTool *plain_tool)
{
    gwy_tool_rprofile_update_all_curves(GWY_TOOL_RPROFILE(plain_tool));
    gwy_tool_rprofile_update_target_graphs(GWY_TOOL_RPROFILE(plain_tool));
}

static void
gwy_tool_rprofile_selection_changed(GwyPlainTool *plain_tool,
                                    gint hint)
{
    GwyToolRprofile *tool = GWY_TOOL_RPROFILE(plain_tool);
    GtkDialog *dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);
    GwyNullStore *store;
    gint n;

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
        gwy_graph_model_remove_all_curves(tool->gmodel);
        gwy_tool_rprofile_update_all_curves(tool);
    }
    else {
        GtkTreeSelection *selection;
        GtkTreePath *path;
        GtkTreeIter iter;

        if (hint < n)
            gwy_null_store_row_changed(store, hint);
        else
            gwy_null_store_set_n_rows(store, n+1);
        gwy_tool_rprofile_update_curve(tool, hint);
        n++;

        gtk_tree_model_iter_nth_child(tool->model, &iter, NULL, hint);
        path = gtk_tree_model_get_path(tool->model, &iter);
        selection = gtk_tree_view_get_selection(tool->treeview);
        gtk_tree_selection_select_iter(selection, &iter);
        gtk_tree_view_scroll_to_cell(tool->treeview, path, NULL,
                                     FALSE, 0.0, 0.0);
        gtk_tree_path_free(path);
    }

    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, n > 0);
}

static void
gwy_tool_rprofile_update_symm_sensitivty(GwyToolRprofile *tool)
{
    GtkTreeSelection *selection;
    gboolean is_selected, has_lines;
    GtkTreeModel *model;
    GtkTreeIter iter;

    selection = gtk_tree_view_get_selection(tool->treeview);
    is_selected = gtk_tree_selection_get_selected(selection, &model, &iter);
    has_lines = (model && gtk_tree_model_iter_n_children(model, NULL) > 0);

    gtk_widget_set_sensitive(tool->improve, is_selected);
    gtk_widget_set_sensitive(tool->improve_all, has_lines);
}

static gint
calculate_lineres(GwyToolRprofile *tool, const gdouble *line)
{
    GwyDataField *dfield;
    gint xl1, xl2, yl1, yl2, lineres;

    if (tool->args.fixres)
        return tool->args.resolution;

    dfield = GWY_PLAIN_TOOL(tool)->data_field;
    xl1 = floor(gwy_data_field_rtoj(dfield, line[0]));
    yl1 = floor(gwy_data_field_rtoi(dfield, line[1]));
    xl2 = floor(gwy_data_field_rtoj(dfield, line[2]));
    yl2 = floor(gwy_data_field_rtoi(dfield, line[3]));
    lineres = GWY_ROUND(hypot(abs(xl1 - xl2) + 1, abs(yl1 - yl2) + 1));
    lineres = MAX(lineres, MIN_RESOLUTION);

    return lineres;
}

static void
gwy_tool_rprofile_update_curve(GwyToolRprofile *tool, gint i)
{
    GwyPlainTool *plain_tool;
    GwyGraphCurveModel *gcmodel;
    gdouble xc, yc, r, line[4];
    gint n, lineres;
    gchar *desc;
    const GwyRGBA *color;
    GwyDataField *data_field, *mask;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);
    g_return_if_fail(gwy_selection_get_object(plain_tool->selection, i, line));
    data_field = plain_tool->data_field;
    mask = plain_tool->mask_field;
    lineres = calculate_lineres(tool, line);

    xc = 0.5*(line[0] + line[2]) + data_field->xoff;
    yc = 0.5*(line[1] + line[3]) + data_field->yoff;
    r = 0.5*hypot(line[2] - line[0], line[3] - line[1]);

    /* Just create some line when there is none. */
    if (!tool->line)
        tool->line = gwy_data_line_new(1, 1.0, FALSE);
    r = MAX(r, hypot(gwy_data_field_get_dx(data_field),
                     gwy_data_field_get_dy(data_field)));
    gwy_data_field_angular_average(data_field, tool->line, mask,
                                   tool->args.masking,
                                   xc, yc, r, lineres);

    n = gwy_graph_model_get_n_curves(tool->gmodel);
    if (i < n) {
        gcmodel = gwy_graph_model_get_curve(tool->gmodel, i);
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, tool->line, 0, 0);
    }
    else {
        gcmodel = gwy_graph_curve_model_new();
        desc = g_strdup_printf(_("Radial profile %d"), i+1);
        color = gwy_graph_get_preset_color(i);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description", desc,
                     "color", color,
                     NULL);
        g_free(desc);
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, tool->line, 0, 0);
        gwy_graph_model_add_curve(tool->gmodel, gcmodel);
        g_object_unref(gcmodel);

        if (i == 0) {
            gwy_graph_model_set_units_from_data_field(tool->gmodel, data_field,
                                                      1, 0, 0, 1);
            gwy_tool_rprofile_update_target_graphs(tool);
        }
    }
}

static void
gwy_tool_rprofile_improve(GwyToolRprofile *tool)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeIter iter;
    const gint *indices;

    selection = gtk_tree_view_get_selection(tool->treeview);
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    path = gtk_tree_model_get_path(model, &iter);
    indices = gtk_tree_path_get_indices(path);
    gwy_app_wait_cursor_start(GTK_WINDOW(GWY_TOOL(tool)->dialog));
    gwy_tool_rprofile_symmetrize_profile(tool, indices[0]);
    gwy_app_wait_cursor_finish(GTK_WINDOW(GWY_TOOL(tool)->dialog));
    gtk_tree_path_free(path);
}

static void
gwy_tool_rprofile_improve_all(GwyToolRprofile *tool)
{
    GwyPlainTool *plain_tool;
    gint n, i;

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (!plain_tool->selection
        || !(n = gwy_selection_get_data(plain_tool->selection, NULL)))
        return;

    gwy_app_wait_cursor_start(GTK_WINDOW(GWY_TOOL(tool)->dialog));
    for (i = 0; i < n; i++)
        gwy_tool_rprofile_symmetrize_profile(tool, i);
    gwy_app_wait_cursor_finish(GTK_WINDOW(GWY_TOOL(tool)->dialog));
}

static void
gwy_tool_rprofile_update_all_curves(GwyToolRprofile *tool)
{
    GwyPlainTool *plain_tool;
    gint n, i;

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (!plain_tool->selection
        || !(n = gwy_selection_get_data(plain_tool->selection, NULL))) {
        gwy_graph_model_remove_all_curves(tool->gmodel);
        return;
    }

    for (i = 0; i < n; i++)
        gwy_tool_rprofile_update_curve(tool, i);
}

static gdouble
angular_average_mismatch(GwyDataField *data_field,
                         GwyDataField *mask,
                         GwyMaskingType masking,
                         gdouble x,
                         gdouble y,
                         gdouble r,
                         gint nstats)
{
    gint ifrom, ito, jfrom, jto, k, xres, yres, i, j;
    gdouble xreal, yreal, dx, dy, xoff, yoff, h, mismatch;
    const gdouble *d, *m;
    gdouble *target, *weight, *sum2;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), G_MAXDOUBLE);
    g_return_val_if_fail(r >= 0.0, G_MAXDOUBLE);
    xres = data_field->xres;
    yres = data_field->yres;
    if (masking == GWY_MASK_IGNORE)
        mask = NULL;
    else if (!mask)
        masking = GWY_MASK_IGNORE;

    if (mask) {
        g_return_val_if_fail(GWY_IS_DATA_FIELD(mask), G_MAXDOUBLE);
        g_return_val_if_fail(mask->xres == xres, G_MAXDOUBLE);
        g_return_val_if_fail(mask->yres == yres, G_MAXDOUBLE);
    }

    xreal = data_field->xreal;
    yreal = data_field->yreal;
    xoff = data_field->xoff;
    yoff = data_field->yoff;
    g_return_val_if_fail(x >= xoff && x <= xoff + xreal, G_MAXDOUBLE);
    g_return_val_if_fail(y >= yoff && y <= yoff + yreal, G_MAXDOUBLE);
    /* Just for integer overflow; we limit i and j ranges explicitly later. */
    r = MIN(r, hypot(xreal, yreal));
    x -= xoff;
    y -= yoff;

    dx = xreal/xres;
    dy = yreal/yres;

    /* Prefer sampling close to the shorter step. */
    if (nstats < 1) {
        h = 2.0*dx*dy/(dx + dy);
        nstats = GWY_ROUND(r/h);
        nstats = MAX(nstats, 1);
    }
    h = r/nstats;

    d = data_field->data;
    m = mask ? mask->data : NULL;

    /* Just return something for single-point lines. */
    if (nstats < 2 || r == 0.0)
        return G_MAXDOUBLE;

    ifrom = (gint)floor(gwy_data_field_rtoi(data_field, y - r));
    ifrom = MAX(ifrom, 0);
    ito = (gint)ceil(gwy_data_field_rtoi(data_field, y + r));
    ito = MIN(ito, yres-1);

    jfrom = (gint)floor(gwy_data_field_rtoj(data_field, x - r));
    jfrom = MAX(jfrom, 0);
    jto = (gint)ceil(gwy_data_field_rtoj(data_field, x + r));
    jto = MIN(jto, xres-1);

    sum2 = g_new0(gdouble, 3*nstats);
    target = sum2 + 1;
    weight = target + 1;
    for (i = ifrom; i < ito; i++) {
        gdouble yy = (i + 0.5)*dy - y;
        for (j = jfrom; j <= jto; j++) {
            gdouble xx = (j + 0.5)*dx - x;
            gdouble v = d[i*xres + j];
            gdouble rr;
            gint kk;

            if ((masking == GWY_MASK_INCLUDE && m[i*xres + j] <= 0.0)
                || (masking == GWY_MASK_EXCLUDE && m[i*xres + j] >= 1.0))
                continue;

            rr = sqrt(xx*xx + yy*yy)/h;
            kk = floor(rr);
            if (kk+1 >= nstats) {
                if (kk+1 == nstats) {
                    sum2[3*kk + 0] += v*v;
                    sum2[3*kk + 1] += v;
                    sum2[3*kk + 2] += 1.0;
                }
                continue;
            }

            rr -= kk;
            if (rr <= 0.5)
                rr = 2.0*rr*rr;
            else
                rr = 1.0 - 2.0*(1.0 - rr)*(1.0 - rr);

            sum2[3*kk + 0] += (1.0 - rr)*v*v;
            sum2[3*kk + 1] += (1.0 - rr)*v;
            sum2[3*kk + 2] += 1.0 - rr;
            sum2[3*kk + 3] += rr*v*v;
            sum2[3*kk + 4] += rr*v;
            sum2[3*kk + 5] += rr;
        }
    }

    mismatch = 0.0;
    for (k = 0; k < nstats; k++) {
        gdouble wk = weight[3*k];
        if (wk)
            mismatch += sum2[3*k]/wk - gwy_powi(target[3*k]/wk, 2);
    }

    g_free(sum2);

    return mismatch;
}

static gdouble
calculate_angular_mismatch(GwyDataField *dfield,
                           GwyDataField *mask, GwyMaskingType masking,
                           const gdouble *line, gint lineres)
{
    gdouble xc, yc, r;

    xc = 0.5*(line[0] + line[2]) + dfield->xoff;
    yc = 0.5*(line[1] + line[3]) + dfield->yoff;
    r = 0.5*hypot(line[2] - line[0], line[3] - line[1]);

    return angular_average_mismatch(dfield, mask, masking, xc, yc, r, lineres);
}

static gboolean
optimize_profile_at_scale(GwyDataField *dfield,
                          GwyDataField *mask,
                          GwyMaskingType masking,
                          gdouble r,
                          gdouble *line,
                          gint lineres,
                          gdouble *mismatch)
{
    enum { GRID_H = 3, GRID_N = 2*GRID_H + 1, GRID_NN = GRID_N*GRID_N };

    gdouble xreal = dfield->xreal, yreal = dfield->yreal;
    gint ij, besti = 0, bestj = 0;
    gdouble dx = gwy_data_field_get_dx(dfield);
    gdouble dy = gwy_data_field_get_dy(dfield);
    gdouble allvar[GRID_NN];
    gdouble bestvar = G_MAXDOUBLE;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(allvar,line,dfield,mask,masking,r,xreal,yreal,lineres) \
            private(ij)
#endif
    {
        gint ijfrom = gwy_omp_chunk_start(GRID_NN);
        gint ijto = gwy_omp_chunk_end(GRID_NN);

        for (ij = ijfrom; ij < ijto; ij++) {
            gint i = ij/GRID_N - GRID_H, j = ij % GRID_N - GRID_H;
            gdouble offline[4] = {
                line[0] + j*r, line[1] + i*r, line[2] + j*r, line[3] + i*r,
            };

            allvar[ij] = G_MAXDOUBLE;
            if (i*i + j*j > 2*2 + 3*3)
                continue;

            if (offline[0] < 0.0 || offline[2] > xreal
                || offline[1] < 0.0 || offline[3] > yreal)
                continue;

            allvar[ij] = calculate_angular_mismatch(dfield, mask, masking,
                                                    offline, lineres);
        }
    }

    for (ij = 0; ij < GRID_NN; ij++) {
        gint i = ij/GRID_N - GRID_H, j = ij % GRID_N - GRID_H;

        if (i*i + j*j <= 2*2 + 3*3 && allvar[ij] < bestvar) {
            besti = i;
            bestj = j;
            bestvar = allvar[ij];
        }
    }

    line[0] += bestj*r;
    line[1] += besti*r;
    line[2] += bestj*r;
    line[3] += besti*r;
    *mismatch = bestvar;

    gwy_debug("symmetrize at scale profiles %g: (%d,%d)", r, bestj, besti);
    return r <= 0.05 * 2*dx*dy/(dx + dy);
}

static void
gwy_tool_rprofile_symmetrize_profile(GwyToolRprofile *tool, gint id)
{
    GwyPlainTool *plain_tool;
    GwyDataField *dfield, *mask;
    GwyMaskingType masking;
    gdouble line_coarse[4], line_fine[4];
    gdouble r, dx, dy, h, mismatch_fine, mismatch_coarse;
    gint lineres;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);
    g_return_if_fail(gwy_selection_get_object(plain_tool->selection, id,
                                              line_fine));
    gwy_assign(line_coarse, line_fine, G_N_ELEMENTS(line_fine));
    dfield = plain_tool->data_field;
    mask = plain_tool->mask_field;
    masking = tool->args.masking;
    dx = gwy_data_field_get_dx(dfield);
    dy = gwy_data_field_get_dy(dfield);
    lineres = calculate_lineres(tool, line_fine);

    /* Don't attempt to optimise very short lines. It would end up in tears. */
    if (hypot((line_fine[2] - line_fine[0])/dx,
              (line_fine[3] - line_fine[1])/dy) < 4.0)
        return;
    h = hypot(line_fine[2] - line_fine[0], line_fine[3] - line_fine[1]);

    r = 0.07*h;
    while (!optimize_profile_at_scale(dfield, mask, masking, r, line_coarse,
                                      lineres, &mismatch_coarse))
        r *= 0.25;

    r = 0.015*h;
    while (!optimize_profile_at_scale(dfield, mask, masking, r, line_fine,
                                      lineres, &mismatch_fine))
        r *= 0.25;

    if (mismatch_fine <= 1.1*mismatch_coarse)
        gwy_selection_set_object(plain_tool->selection, id, line_fine);
    else
        gwy_selection_set_object(plain_tool->selection, id, line_coarse);
}

static void
gwy_tool_rprofile_render_cell(GtkCellLayout *layout,
                              GtkCellRenderer *renderer,
                              GtkTreeModel *model,
                              GtkTreeIter *iter,
                              gpointer user_data)
{
    GwyToolRprofile *tool = (GwyToolRprofile*)user_data;
    GwyPlainTool *plain_tool;
    const GwySIValueFormat *vf;
    gchar buf[32];
    gdouble line[4];
    gdouble val;
    guint idx, id;

    id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(layout), "id"));
    gtk_tree_model_get(model, iter, 0, &idx, -1);
    if (id == COLUMN_I) {
        g_snprintf(buf, sizeof(buf), "%d", idx + 1);
        g_object_set(renderer, "text", buf, NULL);
        return;
    }

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_selection_get_object(plain_tool->selection, idx, line);

    vf = tool->pixel_format;
    switch (id) {
        case COLUMN_X1:
        val = floor(gwy_data_field_rtoj(plain_tool->data_field, line[0]));
        break;

        case COLUMN_Y1:
        val = floor(gwy_data_field_rtoi(plain_tool->data_field, line[1]));
        break;

        case COLUMN_X2:
        val = floor(gwy_data_field_rtoj(plain_tool->data_field, line[2]));
        break;

        case COLUMN_Y2:
        val = floor(gwy_data_field_rtoi(plain_tool->data_field, line[3]));
        break;

        default:
        g_return_if_reached();
        break;
    }

    if (vf)
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, val/vf->magnitude);
    else
        g_snprintf(buf, sizeof(buf), "%.3g", val);

    g_object_set(renderer, "text", buf, NULL);
}

static void
gwy_tool_rprofile_render_color(G_GNUC_UNUSED GtkCellLayout *layout,
                               G_GNUC_UNUSED GtkCellRenderer *renderer,
                               GtkTreeModel *model,
                               GtkTreeIter *iter,
                               gpointer user_data)
{
    GwyToolRprofile *tool = (GwyToolRprofile*)user_data;
    GwyGraphCurveModel *gcmodel;
    GwyRGBA *rgba;
    guint idx, pixel;

    gtk_tree_model_get(model, iter, 0, &idx, -1);
    gcmodel = gwy_graph_model_get_curve(tool->gmodel, idx);
    g_object_get(gcmodel, "color", &rgba, NULL);
    pixel = 0xff | gwy_rgba_to_pixbuf_pixel(rgba);
    gwy_rgba_free(rgba);
    gdk_pixbuf_fill(tool->colorpixbuf, pixel);
}

static void
gwy_tool_rprofile_options_expanded(GtkExpander *expander,
                                   G_GNUC_UNUSED GParamSpec *pspec,
                                   GwyToolRprofile *tool)
{
    tool->args.options_visible = gtk_expander_get_expanded(expander);
}

static void
gwy_tool_rprofile_resolution_changed(GwyToolRprofile *tool,
                                     GtkAdjustment *adj)
{
    tool->args.resolution = gwy_adjustment_get_int(adj);
    /* Resolution can be changed only when fixres == TRUE */
    gwy_tool_rprofile_update_all_curves(tool);
}

static void
gwy_tool_rprofile_fixres_changed(GtkToggleButton *check,
                                 GwyToolRprofile *tool)
{
    tool->args.fixres = gtk_toggle_button_get_active(check);
    gwy_tool_rprofile_update_all_curves(tool);
}

static void
gwy_tool_rprofile_number_lines_changed(GtkToggleButton *check,
                                       GwyToolRprofile *tool)
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
gwy_tool_rprofile_separate_changed(GtkToggleButton *check,
                                   GwyToolRprofile *tool)
{
    tool->args.separate = gtk_toggle_button_get_active(check);
    gwy_table_hscale_set_sensitive(GTK_OBJECT(tool->target_graph),
                                   !tool->args.separate);
    if (tool->args.separate)
        gwy_data_chooser_set_active(GWY_DATA_CHOOSER(tool->target_graph),
                                    NULL, -1);
}

static void
gwy_tool_rprofile_update_target_graphs(GwyToolRprofile *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_refilter(chooser);
}

static gboolean
filter_target_graphs(GwyContainer *data, gint id, gpointer user_data)
{
    GwyToolRprofile *tool = (GwyToolRprofile*)user_data;
    GwyGraphModel *gmodel, *targetgmodel;
    GQuark quark = gwy_app_get_graph_key_for_id(id);

    return ((gmodel = tool->gmodel)
            && gwy_container_gis_object(data, quark, (GObject**)&targetgmodel)
            && gwy_graph_model_units_are_compatible(gmodel, targetgmodel));
}

static void
gwy_tool_rprofile_target_changed(GwyToolRprofile *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_get_active_id(chooser, &tool->args.target);
}

static void
gwy_tool_rprofile_masking_changed(GtkComboBox *combo,
                                  GwyToolRprofile *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->args.masking = gwy_enum_combo_box_get_active(combo);
    if (plain_tool->data_field && plain_tool->mask_field)
        gwy_tool_rprofile_update_all_curves(tool);
}

static void
gwy_tool_rprofile_apply(GwyToolRprofile *tool)
{
    GwyPlainTool *plain_tool;
    GwyGraphCurveModel *gcmodel;
    GwyGraphModel *gmodel;
    gchar *s;
    gint i, n;

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

    if (!tool->args.separate) {
        gmodel = gwy_graph_model_duplicate(tool->gmodel);
        g_object_set(gmodel, "label-visible", TRUE, NULL);
        gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container,
                                             TRUE);
        g_object_unref(gmodel);
        return;
    }

    for (i = 0; i < n; i++) {
        gmodel = gwy_graph_model_new_alike(tool->gmodel);
        g_object_set(gmodel, "label-visible", TRUE, NULL);
        gcmodel = gwy_graph_model_get_curve(tool->gmodel, i);
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
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
