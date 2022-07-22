/*
 *  $Id: profile.c 23307 2021-03-18 15:56:45Z yeti-dn $
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
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/datafield.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#include <libgwymodule/gwymodule-file.h>

#define GWY_TYPE_TOOL_PROFILE            (gwy_tool_profile_get_type())
#define GWY_TOOL_PROFILE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_PROFILE, GwyToolProfile))
#define GWY_IS_TOOL_PROFILE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_PROFILE))
#define GWY_TOOL_PROFILE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_PROFILE, GwyToolProfileClass))

typedef enum {
    GWY_CC_DISPLAY_NONE = 0,
    GWY_CC_DISPLAY_X_CORR  = 1,
    GWY_CC_DISPLAY_Y_CORR = 2,
    GWY_CC_DISPLAY_Z_CORR = 3,
    GWY_CC_DISPLAY_X_UNC = 4,
    GWY_CC_DISPLAY_Y_UNC = 5,
    GWY_CC_DISPLAY_Z_UNC = 6,
} GwyCCDisplayType;

enum {
    NLINES = 1024,
    MAX_THICKNESS = 128,
    MIN_RESOLUTION = 4,
    MAX_RESOLUTION = 16384
};

enum {
    COLUMN_I, COLUMN_X1, COLUMN_Y1, COLUMN_X2, COLUMN_Y2, NCOLUMNS
};

typedef struct _GwyToolProfile      GwyToolProfile;
typedef struct _GwyToolProfileClass GwyToolProfileClass;

typedef struct {
    gboolean options_visible;
    gint thickness;
    gint resolution;
    gboolean fixres;
    GwyInterpolationType interpolation;
    GwyMaskingType masking;
    gboolean separate;
    gboolean both;
    gboolean number_lines;
    GwyAppDataId target;
} ToolArgs;

struct _GwyToolProfile {
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
    GtkObject *thickness;
    GtkObject *resolution;
    GtkWidget *fixres;
    GtkWidget *interpolation;
    GtkWidget *number_lines;
    GtkWidget *separate;
    GtkWidget *apply;
    GtkWidget *menu_display;
    GtkWidget *callabel;
    GtkWidget *both;
    GtkWidget *target_graph;
    GtkWidget *masking;

    GwyDataField *xerr;
    GwyDataField *yerr;
    GwyDataField *zerr;
    GwyDataField *xunc;
    GwyDataField *yunc;
    GwyDataField *zunc;

    /*curves calculated and output*/
    GwyDataLine *line_xerr;
    GwyDataLine *line_yerr;
    GwyDataLine *line_zerr;
    GwyDataLine *line_xunc;
    GwyDataLine *line_yunc;
    GwyDataLine *line_zunc;

    gboolean has_calibration;
    GwyCCDisplayType display_type;

    /* potential class data */
    GwySIValueFormat *pixel_format;
    GType layer_type_line;
};

struct _GwyToolProfileClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType      gwy_tool_profile_get_type              (void)                      G_GNUC_CONST;
static void       gwy_tool_profile_finalize              (GObject *object);
static void       gwy_tool_profile_init_dialog           (GwyToolProfile *tool);
static void       gwy_tool_profile_data_switched         (GwyTool *gwytool,
                                                          GwyDataView *data_view);
static void       gwy_tool_profile_response              (GwyTool *tool,
                                                          gint response_id);
static void       gwy_tool_profile_data_changed          (GwyPlainTool *plain_tool);
static void       gwy_tool_profile_selection_changed     (GwyPlainTool *plain_tool,
                                                          gint hint);
static void       gwy_tool_profile_update_symm_sensitivty(GwyToolProfile *tool);
static void       gwy_tool_profile_update_curve          (GwyToolProfile *tool,
                                                          gint i);
static void       gwy_tool_profile_update_all_curves     (GwyToolProfile *tool);
static void       gwy_tool_profile_improve_all           (GwyToolProfile *tool);
static void       gwy_tool_profile_improve               (GwyToolProfile *tool);
static void       gwy_tool_profile_straighten_profile    (GwyToolProfile *tool,
                                                          gint id);
static void       gwy_tool_profile_render_cell           (GtkCellLayout *layout,
                                                          GtkCellRenderer *renderer,
                                                          GtkTreeModel *model,
                                                          GtkTreeIter *iter,
                                                          gpointer user_data);
static void       gwy_tool_profile_render_color          (GtkCellLayout *layout,
                                                          GtkCellRenderer *renderer,
                                                          GtkTreeModel *model,
                                                          GtkTreeIter *iter,
                                                          gpointer user_data);
static void       gwy_tool_profile_options_expanded      (GtkExpander *expander,
                                                          GParamSpec *pspec,
                                                          GwyToolProfile *tool);
static void       gwy_tool_profile_thickness_changed     (GwyToolProfile *tool,
                                                          GtkAdjustment *adj);
static void       gwy_tool_profile_resolution_changed    (GwyToolProfile *tool,
                                                          GtkAdjustment *adj);
static void       gwy_tool_profile_fixres_changed        (GtkToggleButton *check,
                                                          GwyToolProfile *tool);
static void       gwy_tool_profile_number_lines_changed  (GtkToggleButton *check,
                                                          GwyToolProfile *tool);
static void       gwy_tool_profile_separate_changed      (GtkToggleButton *check,
                                                          GwyToolProfile *tool);
static void       gwy_tool_profile_both_changed          (GtkToggleButton *check,
                                                          GwyToolProfile *tool);
static void       gwy_tool_profile_interpolation_changed (GtkComboBox *combo,
                                                          GwyToolProfile *tool);
static void       gwy_tool_profile_update_target_graphs  (GwyToolProfile *tool);
static gboolean   filter_target_graphs                   (GwyContainer *data,
                                                          gint id,
                                                          gpointer user_data);
static void       gwy_tool_profile_target_changed        (GwyToolProfile *tool);
static void       gwy_tool_profile_masking_changed       (GtkComboBox *combo,
                                                          GwyToolProfile *tool);
static void       gwy_tool_profile_apply                 (GwyToolProfile *tool);
static GtkWidget* menu_display                           (GCallback callback,
                                                          gpointer cbdata,
                                                          GwyCCDisplayType current);
static void       display_changed                        (GtkComboBox *combo,
                                                          GwyToolProfile *tool);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Profile tool, creates profile graphs from selected lines."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "4.3",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

static const gchar both_key[]            = "/module/profile/both";
static const gchar fixres_key[]          = "/module/profile/fixres";
static const gchar interpolation_key[]   = "/module/profile/interpolation";
static const gchar masking_key[]         = "/module/profile/masking";
static const gchar number_lines_key[]    = "/module/profile/number_lines";
static const gchar options_visible_key[] = "/module/profile/options_visible";
static const gchar resolution_key[]      = "/module/profile/resolution";
static const gchar separate_key[]        = "/module/profile/separate";
static const gchar thickness_key[]       = "/module/profile/thickness";

static const ToolArgs default_args = {
    FALSE,
    1,
    120,
    FALSE,
    GWY_INTERPOLATION_LINEAR,
    GWY_MASK_IGNORE,
    FALSE,
    TRUE,
    TRUE,
    GWY_APP_DATA_ID_NONE,
};

GWY_MODULE_QUERY2(module_info, profile)

G_DEFINE_TYPE(GwyToolProfile, gwy_tool_profile, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_PROFILE);

    return TRUE;
}

static void
gwy_tool_profile_class_init(GwyToolProfileClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_profile_finalize;

    tool_class->stock_id = GWY_STOCK_PROFILE;
    tool_class->title = _("Profiles");
    tool_class->tooltip = _("Extract profiles along arbitrary lines");
    tool_class->prefix = "/module/profile";
    tool_class->default_width = 640;
    tool_class->default_height = 400;
    tool_class->data_switched = gwy_tool_profile_data_switched;
    tool_class->response = gwy_tool_profile_response;

    ptool_class->data_changed = gwy_tool_profile_data_changed;
    ptool_class->selection_changed = gwy_tool_profile_selection_changed;
}

static void
gwy_tool_profile_finalize(GObject *object)
{
    GwyToolProfile *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_PROFILE(object);

    settings = gwy_app_settings_get();
    gwy_container_set_boolean_by_name(settings, options_visible_key,
                                      tool->args.options_visible);
    gwy_container_set_int32_by_name(settings, thickness_key,
                                    tool->args.thickness);
    gwy_container_set_int32_by_name(settings, resolution_key,
                                    tool->args.resolution);
    gwy_container_set_boolean_by_name(settings, fixres_key,
                                      tool->args.fixres);
    gwy_container_set_enum_by_name(settings, interpolation_key,
                                   tool->args.interpolation);
    gwy_container_set_enum_by_name(settings, masking_key,
                                   tool->args.masking);
    gwy_container_set_boolean_by_name(settings, separate_key,
                                      tool->args.separate);
    gwy_container_set_boolean_by_name(settings, both_key,
                                      tool->args.both);
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
    G_OBJECT_CLASS(gwy_tool_profile_parent_class)->finalize(object);
}

static void
gwy_tool_profile_init(GwyToolProfile *tool)
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
    gwy_container_gis_int32_by_name(settings, thickness_key,
                                    &tool->args.thickness);
    gwy_container_gis_int32_by_name(settings, resolution_key,
                                    &tool->args.resolution);
    gwy_container_gis_boolean_by_name(settings, fixres_key,
                                      &tool->args.fixres);
    gwy_container_gis_enum_by_name(settings, interpolation_key,
                                   &tool->args.interpolation);
    tool->args.interpolation
        = gwy_enum_sanitize_value(tool->args.interpolation,
                                  GWY_TYPE_INTERPOLATION_TYPE);
    gwy_container_gis_enum_by_name(settings, masking_key,
                                   &tool->args.masking);
    tool->args.masking = gwy_enum_sanitize_value(tool->args.masking,
                                                GWY_TYPE_MASKING_TYPE);
    gwy_container_gis_boolean_by_name(settings, separate_key,
                                      &tool->args.separate);
    gwy_container_gis_boolean_by_name(settings, both_key,
                                      &tool->args.both);
    gwy_container_gis_boolean_by_name(settings, number_lines_key,
                                      &tool->args.number_lines);

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    height |= 1;
    tool->colorpixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                                       height, height);

    tool->pixel_format = gwy_si_unit_value_format_new(1.0, 0, _("px"));
    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_line,
                                     "line");

    gwy_tool_profile_init_dialog(tool);
}

static void
gwy_tool_profile_init_dialog(GwyToolProfile *tool)
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
                             G_CALLBACK(gwy_tool_profile_update_symm_sensitivty),
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
                                           gwy_tool_profile_render_cell, tool,
                                           NULL);
        if (i == COLUMN_I) {
            renderer = gtk_cell_renderer_pixbuf_new();
            g_object_set(renderer, "pixbuf", tool->colorpixbuf, NULL);
            gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column),
                                       renderer, FALSE);
            gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column),
                                               renderer,
                                               gwy_tool_profile_render_color,
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
                     G_CALLBACK(gwy_tool_profile_options_expanded), tool);
    gtk_box_pack_start(GTK_BOX(vbox), tool->options, FALSE, FALSE, 0);

    table = GTK_TABLE(gtk_table_new(8, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_container_add(GTK_CONTAINER(tool->options), GTK_WIDGET(table));
    row = 0;

    hbox2 = gtk_hbox_new(FALSE, 2);
    gtk_table_attach(table, hbox2, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    tool->improve_all = gtk_button_new_with_mnemonic(_("Improve _All"));
    gtk_box_pack_end(GTK_BOX(hbox2), tool->improve_all, FALSE, FALSE, 0);
    g_signal_connect_swapped(tool->improve_all, "clicked",
                             G_CALLBACK(gwy_tool_profile_improve_all), tool);
    tool->improve = gtk_button_new_with_mnemonic(_("Improve _Direction"));
    gtk_box_pack_end(GTK_BOX(hbox2), tool->improve, FALSE, FALSE, 0);
    g_signal_connect_swapped(tool->improve, "clicked",
                             G_CALLBACK(gwy_tool_profile_improve), tool);
    row++;

    tool->thickness = gtk_adjustment_new(tool->args.thickness,
                                         1, MAX_THICKNESS, 1, 10, 0);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Thickness:"), _("px"),
                            tool->thickness, GWY_HSCALE_SQRT | GWY_HSCALE_SNAP);
    g_signal_connect_swapped(tool->thickness, "value-changed",
                             G_CALLBACK(gwy_tool_profile_thickness_changed),
                             tool);
    row++;

    tool->resolution = gtk_adjustment_new(tool->args.resolution,
                                          MIN_RESOLUTION, MAX_RESOLUTION,
                                          1, 10, 0);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row,
                            _("_Fixed resolution:"), NULL,
                            tool->resolution, GWY_HSCALE_CHECK);
    g_signal_connect_swapped(tool->resolution, "value-changed",
                             G_CALLBACK(gwy_tool_profile_resolution_changed),
                             tool);
    tool->fixres = gwy_table_hscale_get_check(tool->resolution);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->fixres),
                                 tool->args.fixres);
    g_signal_connect(tool->fixres, "toggled",
                     G_CALLBACK(gwy_tool_profile_fixres_changed), tool);
    row++;

    tool->number_lines
        = gtk_check_button_new_with_mnemonic(_("_Number lines"));
    gtk_table_attach(table, tool->number_lines,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->number_lines),
                                 tool->args.number_lines);
    g_signal_connect(tool->number_lines, "toggled",
                     G_CALLBACK(gwy_tool_profile_number_lines_changed), tool);
    row++;

    tool->separate
        = gtk_check_button_new_with_mnemonic(_("_Separate profiles"));
    gtk_table_attach(table, tool->separate,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->separate),
                                 tool->args.separate);
    g_signal_connect(tool->separate, "toggled",
                     G_CALLBACK(gwy_tool_profile_separate_changed), tool);
    row++;

    tool->interpolation = gwy_enum_combo_box_new
                            (gwy_interpolation_type_get_enum(), -1,
                             G_CALLBACK(gwy_tool_profile_interpolation_changed),
                             tool,
                             tool->args.interpolation, TRUE);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row,
                            _("_Interpolation type:"), NULL,
                            GTK_OBJECT(tool->interpolation),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    tool->masking = gwy_enum_combo_box_new
                            (gwy_masking_type_get_enum(), -1,
                             G_CALLBACK(gwy_tool_profile_masking_changed), tool,
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
                             G_CALLBACK(gwy_tool_profile_target_changed),
                             tool);
    row++;

    tool->display_type = 0;
    tool->menu_display = menu_display(G_CALLBACK(display_changed),
                                      tool,
                                      tool->display_type);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row,
                            _("_Calibration data:"), NULL,
                            GTK_OBJECT(tool->menu_display),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    tool->callabel = gwy_table_hscale_get_label(GTK_OBJECT(tool->menu_display));
    row++;

    tool->both = gtk_check_button_new_with_mnemonic(_("_Show profile"));
    gtk_table_attach(table, tool->both, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->both),
                                 tool->args.both);
    g_signal_connect(tool->both, "toggled",
                     G_CALLBACK(gwy_tool_profile_both_changed), tool);
    row++;

    tool->gmodel = gwy_graph_model_new();
    g_object_set(tool->gmodel, "title", _("Profiles"), NULL);

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
gwy_tool_profile_data_switched(GwyTool *gwytool,
                               GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolProfile *tool;
    gboolean ignore;
    gchar xekey[24], yekey[24], zekey[24], xukey[24], yukey[24], zukey[24];

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_profile_parent_class)->data_switched(gwytool,
                                                                 data_view);

    if (ignore || plain_tool->init_failed)
        return;

    tool = GWY_TOOL_PROFILE(gwytool);
    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_line,
                                "line-numbers", tool->args.number_lines,
                                "thickness", tool->args.thickness,
                                "center-tick", FALSE,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, NLINES);

        g_snprintf(xekey, sizeof(xekey), "/%d/data/cal_xerr", plain_tool->id);
        g_snprintf(yekey, sizeof(yekey), "/%d/data/cal_yerr", plain_tool->id);
        g_snprintf(zekey, sizeof(zekey), "/%d/data/cal_zerr", plain_tool->id);
        g_snprintf(xukey, sizeof(xukey), "/%d/data/cal_xunc", plain_tool->id);
        g_snprintf(yukey, sizeof(yukey), "/%d/data/cal_yunc", plain_tool->id);
        g_snprintf(zukey, sizeof(zukey), "/%d/data/cal_zunc", plain_tool->id);

        if (gwy_container_gis_object_by_name(plain_tool->container, xekey,
                                             &(tool->xerr))
            && gwy_container_gis_object_by_name(plain_tool->container, yekey,
                                                &(tool->yerr))
            && gwy_container_gis_object_by_name(plain_tool->container, zekey,
                                                &(tool->zerr))
            && gwy_container_gis_object_by_name(plain_tool->container, xukey,
                                                &(tool->xunc))
            && gwy_container_gis_object_by_name(plain_tool->container, yukey,
                                                &(tool->yunc))
            && gwy_container_gis_object_by_name(plain_tool->container, zukey,
                                                &(tool->zunc))) {
            gint xres = gwy_data_field_get_xres(plain_tool->data_field);
            gint xreal = gwy_data_field_get_xreal(plain_tool->data_field);
            tool->has_calibration = TRUE;
            tool->line_xerr = gwy_data_line_new(xres, xreal, FALSE);
            gtk_widget_show(tool->menu_display);
            gtk_widget_show(tool->callabel);
            gtk_widget_show(tool->both);
        }
        else {
            tool->has_calibration = FALSE;
            gtk_widget_hide(tool->menu_display);
            gtk_widget_hide(tool->callabel);
            gtk_widget_hide(tool->both);
        }
    }

    gwy_graph_model_remove_all_curves(tool->gmodel);
    gwy_tool_profile_update_all_curves(tool);
    gwy_tool_profile_update_target_graphs(tool);
}

static void
gwy_tool_profile_response(GwyTool *tool,
                          gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_profile_parent_class)->response(tool, response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_profile_apply(GWY_TOOL_PROFILE(tool));
}

static void
gwy_tool_profile_data_changed(GwyPlainTool *plain_tool)
{
    gwy_tool_profile_update_all_curves(GWY_TOOL_PROFILE(plain_tool));
    gwy_tool_profile_update_target_graphs(GWY_TOOL_PROFILE(plain_tool));
}

static void
gwy_tool_profile_selection_changed(GwyPlainTool *plain_tool,
                                   gint hint)
{
    GwyToolProfile *tool = GWY_TOOL_PROFILE(plain_tool);
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
        gwy_tool_profile_update_all_curves(tool);
    }
    else {
        GtkTreeSelection *selection;
        GtkTreePath *path;
        GtkTreeIter iter;

        if (hint < n)
            gwy_null_store_row_changed(store, hint);
        else
            gwy_null_store_set_n_rows(store, n+1);
        gwy_tool_profile_update_curve(tool, hint);
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
gwy_tool_profile_update_symm_sensitivty(GwyToolProfile *tool)
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

static void
gwy_data_line_sum(GwyDataLine *a, GwyDataLine *b)
{
    gint i;
    g_return_if_fail(GWY_IS_DATA_LINE(a));
    g_return_if_fail(GWY_IS_DATA_LINE(b));
    g_return_if_fail(a->res == b->res);

    for (i = 0; i < a->res; i++)
        a->data[i] += b->data[i];
}

static void
gwy_data_line_subtract(GwyDataLine *a, GwyDataLine *b)
{
    gint i;
    g_return_if_fail(GWY_IS_DATA_LINE(a));
    g_return_if_fail(GWY_IS_DATA_LINE(b));
    g_return_if_fail(a->res == b->res);

    for (i = 0; i < a->res; i++)
        a->data[i] -= b->data[i];
}

static void
add_hidden_curve(GwyToolProfile *tool, GwyDataLine *line,
                 gchar *str, const GwyRGBA *color, gboolean hidden)
{
    GwyGraphCurveModel *gcmodel;

    gcmodel = gwy_graph_curve_model_new();
    if (hidden)
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_HIDDEN,
                     "description", str,
                     "color", color,
                     "line-style", GDK_LINE_ON_OFF_DASH,
                     NULL);
    else
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description", str,
                     "color", color,
                     "line-style", GDK_LINE_ON_OFF_DASH,
                     NULL);
    gwy_graph_curve_model_set_data_from_dataline(gcmodel, line, 0, 0);
    gwy_graph_model_add_curve(tool->gmodel, gcmodel);
    g_object_unref(gcmodel);
}

static void
add_hidden_unc_curves(GwyToolProfile *tool, gint i, const GwyRGBA *color,
                      GwyDataLine *upunc, GwyDataLine *lowunc)
{
    gchar *desc;

    desc = g_strdup_printf(_("X error %d"), i);
    add_hidden_curve(tool, tool->line_xerr, desc, color,
                     !(tool->display_type==1));
    g_free(desc);
    desc = g_strdup_printf(_("Y error %d"), i);
    add_hidden_curve(tool, tool->line_yerr, desc, color,
                     !(tool->display_type==2));
    g_free(desc);
    desc = g_strdup_printf(_("Z error %d"), i);
    add_hidden_curve(tool, tool->line_zerr, desc, color,
                     !(tool->display_type==3));
    g_free(desc);
    desc = g_strdup_printf(_("X uncertainty %d"), i);
    add_hidden_curve(tool, tool->line_xunc, desc, color,
                     !(tool->display_type==4));
    g_free(desc);
    desc = g_strdup_printf(_("Y uncertainty %d"), i);
    add_hidden_curve(tool, tool->line_yunc, desc, color,
                     !(tool->display_type==5));
    g_free(desc);
    desc = g_strdup_printf(_("Z uncertainty %d"), i);
    add_hidden_curve(tool, tool->line_zunc, desc, color,
                     TRUE);
    g_free(desc);

    desc = g_strdup_printf(_("Zunc up bound %d"), i);
    add_hidden_curve(tool, upunc, desc, color,
                     !(tool->display_type==6));
    g_free(desc);

    desc = g_strdup_printf(_("Zunc low bound %d"), i);
    add_hidden_curve(tool, lowunc, desc, color,
                     !(tool->display_type==6));
    g_free(desc);
}

static void
get_profile_uncs(GwyToolProfile *tool,
                 gint xl1, gint yl1, gint xl2, gint yl2,
                 gint lineres)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyDataField *data_field = plain_tool->data_field;
    gdouble calxratio = ((gdouble)gwy_data_field_get_xres(tool->xerr)
                         /gwy_data_field_get_xres(data_field));
    gdouble calyratio = ((gdouble)gwy_data_field_get_yres(tool->xerr)
                         /gwy_data_field_get_yres(data_field));

    tool->line_xerr = gwy_data_field_get_profile(tool->xerr, tool->line_xerr,
                                                 xl1*calxratio, yl1*calyratio,
                                                 xl2*calxratio, yl2*calyratio,
                                                 lineres,
                                                 tool->args.thickness,
                                                 tool->args.interpolation);
    tool->line_yerr = gwy_data_field_get_profile(tool->yerr, tool->line_yerr,
                                                 xl1*calxratio, yl1*calyratio,
                                                 xl2*calxratio, yl2*calyratio,
                                                 lineres,
                                                 tool->args.thickness,
                                                 tool->args.interpolation);
    tool->line_zerr = gwy_data_field_get_profile(tool->zerr, tool->line_zerr,
                                                 xl1*calxratio, yl1*calyratio,
                                                 xl2*calxratio, yl2*calyratio,
                                                 lineres,
                                                 tool->args.thickness,
                                                 tool->args.interpolation);

    tool->line_xunc = gwy_data_field_get_profile(tool->xunc, tool->line_xunc,
                                                 xl1*calxratio, yl1*calyratio,
                                                 xl2*calxratio, yl2*calyratio,
                                                 lineres,
                                                 tool->args.thickness,
                                                 tool->args.interpolation);
    tool->line_yunc = gwy_data_field_get_profile(tool->yunc, tool->line_yunc,
                                                 xl1*calxratio, yl1*calyratio,
                                                 xl2*calxratio, yl2*calyratio,
                                                 lineres,
                                                 tool->args.thickness,
                                                 tool->args.interpolation);
    tool->line_zunc = gwy_data_field_get_profile(tool->zunc, tool->line_zunc,
                                                 xl1*calxratio, yl1*calyratio,
                                                 xl2*calxratio, yl2*calyratio,
                                                 lineres,
                                                 tool->args.thickness,
                                                 tool->args.interpolation);
}

static void
set_unc_gcmodel_data(GwyToolProfile *tool, gint i,
                     GwyDataLine *upunc, GwyDataLine *lowunc)
{
    GwyGraphCurveModel *cmodel;

    cmodel = gwy_graph_model_get_curve(tool->gmodel, i+1);
    gwy_graph_curve_model_set_data_from_dataline(cmodel, tool->line_xerr, 0, 0);

    cmodel = gwy_graph_model_get_curve(tool->gmodel, i+2);
    gwy_graph_curve_model_set_data_from_dataline(cmodel, tool->line_yerr, 0, 0);

    cmodel = gwy_graph_model_get_curve(tool->gmodel, i+3);
    gwy_graph_curve_model_set_data_from_dataline(cmodel, tool->line_zerr, 0, 0);

    cmodel = gwy_graph_model_get_curve(tool->gmodel, i+4);
    gwy_graph_curve_model_set_data_from_dataline(cmodel, tool->line_xunc, 0, 0);

    cmodel = gwy_graph_model_get_curve(tool->gmodel, i+5);
    gwy_graph_curve_model_set_data_from_dataline(cmodel, tool->line_yunc, 0, 0);

    cmodel = gwy_graph_model_get_curve(tool->gmodel, i+6);
    gwy_graph_curve_model_set_data_from_dataline(cmodel, tool->line_zunc, 0, 0);

    cmodel = gwy_graph_model_get_curve(tool->gmodel, i+7);
    gwy_graph_curve_model_set_data_from_dataline(cmodel, upunc, 0, 0);

    cmodel = gwy_graph_model_get_curve(tool->gmodel, i+8);
    gwy_graph_curve_model_set_data_from_dataline(cmodel, lowunc, 0, 0);
}

static void
gwy_tool_profile_update_curve(GwyToolProfile *tool,
                              gint i)
{
    GwyPlainTool *plain_tool;
    GwyGraphCurveModel *gcmodel;
    gdouble line[4];
    gint xl1, yl1, xl2, yl2;
    gint n, lineres, multpos;
    gchar *desc;
    const GwyRGBA *color;
    gboolean has_calibration, is_masking;
    GwyXY *xydata = NULL;
    GwyDataField *data_field, *mask;
    GwyDataLine *upunc = NULL, *lowunc = NULL;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);
    g_return_if_fail(gwy_selection_get_object(plain_tool->selection, i, line));
    data_field = plain_tool->data_field;
    mask = plain_tool->mask_field;

    is_masking = (mask && tool->args.masking != GWY_MASK_IGNORE);
    has_calibration = tool->has_calibration && !is_masking;

    multpos = has_calibration ? 9 : 1;
    i *= multpos;

    xl1 = floor(gwy_data_field_rtoj(data_field, line[0]));
    yl1 = floor(gwy_data_field_rtoi(data_field, line[1]));
    xl2 = floor(gwy_data_field_rtoj(data_field, line[2]));
    yl2 = floor(gwy_data_field_rtoi(data_field, line[3]));
    if (!tool->args.fixres) {
        lineres = GWY_ROUND(hypot(abs(xl1 - xl2) + 1, abs(yl1 - yl2) + 1));
        lineres = MAX(lineres, MIN_RESOLUTION);
    }
    else
        lineres = tool->args.resolution;

    if (has_calibration) {
        /* Use non-masking profiles with calibration. */
        tool->line = gwy_data_field_get_profile(data_field, tool->line,
                                                xl1, yl1, xl2, yl2,
                                                lineres,
                                                tool->args.thickness,
                                                tool->args.interpolation);
    }
    else {
        xydata = gwy_data_field_get_profile_mask(data_field, &lineres, mask,
                                                 tool->args.masking,
                                                 line[0], line[1],
                                                 line[2], line[3],
                                                 lineres,
                                                 tool->args.thickness,
                                                 tool->args.interpolation);
        if (!xydata) {
            xydata = g_new(GwyXY, 1);
            xydata[0].x = 0.0;
            xydata[0].y = gwy_data_field_get_dval_real(data_field,
                                                       0.5*(line[0] + line[2]),
                                                       0.5*(line[1] + line[3]),
                                                       GWY_INTERPOLATION_ROUND);
            lineres = 1;
        }
    }

    if (has_calibration) {
        get_profile_uncs(tool, xl1, yl1, xl2, yl2, lineres);

        upunc = gwy_data_line_new_alike(tool->line, FALSE);
        gwy_data_line_copy(tool->line, upunc);
        gwy_data_line_sum(upunc, tool->line_xerr);

        lowunc = gwy_data_line_new_alike(tool->line, FALSE);
        gwy_data_line_copy(tool->line, lowunc);
        gwy_data_line_subtract(lowunc, tool->line_xerr);
    }

    n = gwy_graph_model_get_n_curves(tool->gmodel);
    if (i < n) {
        gcmodel = gwy_graph_model_get_curve(tool->gmodel, i);
        if (xydata) {
            gwy_graph_curve_model_set_data_interleaved(gcmodel,
                                                       (gdouble*)xydata,
                                                       lineres);
        }
        else {
            gwy_graph_curve_model_set_data_from_dataline(gcmodel,
                                                         tool->line, 0, 0);
        }

        if (has_calibration)
            set_unc_gcmodel_data(tool, i, upunc, lowunc);
    }
    else {
        gcmodel = gwy_graph_curve_model_new();
        desc = g_strdup_printf(_("Profile %d"), i/multpos+1);
        color = gwy_graph_get_preset_color(i);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description", desc,
                     "color", color,
                     NULL);
        g_free(desc);
        if (xydata) {
            gwy_graph_curve_model_set_data_interleaved(gcmodel,
                                                       (gdouble*)xydata,
                                                       lineres);
        }
        else {
            gwy_graph_curve_model_set_data_from_dataline(gcmodel,
                                                         tool->line, 0, 0);
        }
        gwy_graph_model_add_curve(tool->gmodel, gcmodel);
        g_object_unref(gcmodel);

        if (i == 0) {
            gwy_graph_model_set_units_from_data_field(tool->gmodel, data_field,
                                                      1, 0, 0, 1);
            gwy_tool_profile_update_target_graphs(tool);
        }

        if (has_calibration)
            add_hidden_unc_curves(tool, i/multpos+1, color, upunc, lowunc);
    }

    g_free(xydata);
}

static void
gwy_tool_profile_improve(GwyToolProfile *tool)
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
    gwy_tool_profile_straighten_profile(tool, indices[0]);
    gtk_tree_path_free(path);
}

static void
gwy_tool_profile_improve_all(GwyToolProfile *tool)
{
    GwyPlainTool *plain_tool;
    gint n, i;

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (!plain_tool->selection
        || !(n = gwy_selection_get_data(plain_tool->selection, NULL)))
        return;

    for (i = 0; i < n; i++)
        gwy_tool_profile_straighten_profile(tool, i);
}

static void
gwy_tool_profile_update_all_curves(GwyToolProfile *tool)
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
        gwy_tool_profile_update_curve(tool, i);
}

static gdouble
estimate_orthogonal_variation(GwyDataField *dfield,
                              const gdouble *line, gint thickness)
{
    gdouble lx, ly, l, dx, dy, h, xreal, yreal;
    gdouble xfrom0, xfrom1, yfrom0, yfrom1, xto0, xto1, yto0, yto1;
    gdouble variation = 0.0;
    gint ir, res, i, j, n;

    /* Ignore offsets here we do not call any function that uses them. */
    lx = line[2] - line[0];
    ly = line[3] - line[1];
    l = hypot(lx, ly);

    dx = gwy_data_field_get_dx(dfield);
    dy = gwy_data_field_get_dy(dfield);
    h = 2.0*dx*dy/(dx + dy);

    /* First orthogonal profile is (xfrom0,yfrom0)--(xto0,yto0),
     * the last is (xfrom1,yfrom1)--(xto1,yto1), between them we interpolate. */
    xfrom0 = line[0] + ly/l*thickness*h;
    xto0 = line[0] - ly/l*thickness*h;
    yfrom0 = line[1] - lx/l*thickness*h;
    yto0 = line[1] + lx/l*thickness*h;
    xfrom1 = line[2] + ly/l*thickness*h;
    xto1 = line[2] - ly/l*thickness*h;
    yfrom1 = line[3] - lx/l*thickness*h;
    yto1 = line[3] + lx/l*thickness*h;

    xreal = gwy_data_field_get_xreal(dfield);
    yreal = gwy_data_field_get_yreal(dfield);
    ir = pow(l/h + 1.0, 2.0/3.0);
    res = thickness+1;
    n = 0;

    for (i = 0; i <= ir; i++) {
        gdouble t = i/(gdouble)ir;
        gdouble xl1 = xfrom0*(1.0 - t) + xfrom1*t;
        gdouble yl1 = yfrom0*(1.0 - t) + yfrom1*t;
        gdouble xl2 = xto0*(1.0 - t) + xto1*t;
        gdouble yl2 = yto0*(1.0 - t) + yto1*t;
        gdouble mu = 0.0;
        gint nxy;
        GwyXY *xy;

        if (xl1 < 0.5*dx || xl1 > xreal - 0.5*dx)
            continue;
        if (yl1 < 0.5*dy || yl1 > yreal - 0.5*dy)
            continue;
        if (xl2 < 0.5*dx || xl2 > xreal - 0.5*dx)
            continue;
        if (yl2 < 0.5*dy || yl2 > yreal - 0.5*dy)
            continue;

        xy = gwy_data_field_get_profile_mask(dfield, &nxy,
                                             NULL, GWY_MASK_IGNORE,
                                             xl1, yl1, xl2, yl2,
                                             res, 1, GWY_INTERPOLATION_LINEAR);
        if (!xy)
            continue;

        for (j = 0; j < nxy; j++)
            mu += xy[j].y;
        mu /= nxy;

        for (j = 0; j < nxy; j++)
            variation += (xy[j].y - mu)*(xy[j].y - mu);
        n += nxy;

        g_free(xy);
    }

    return variation/n;
}

static void
straighten_at_scale(GwyDataField *dfield, gdouble *line,
                    gint thickness, gdouble phistep, gint n)
{
    gdouble r, xc, yc, phi0, phi, cphi, sphi, t;
    gdouble *var;
    gint i, besti;

    xc = 0.5*(line[0] + line[2]);
    yc = 0.5*(line[1] + line[3]);
    r = 0.5*hypot(line[2] - line[0], line[3] - line[1]);
    phi0 = atan2(line[3] - line[1], line[2] - line[0]);

    var = g_new(gdouble, 2*n + 1);
    for (i = -n; i <= n; i++) {
        phi = i*phistep + phi0;
        cphi = cos(phi);
        sphi = sin(phi);
        line[0] = xc + cphi*r;
        line[1] = yc + sphi*r;
        line[2] = xc - cphi*r;
        line[3] = yc - sphi*r;
        var[n + i] = estimate_orthogonal_variation(dfield, line, thickness);
        gwy_debug("%g %g", phi, var[n + i]);
    }

    besti = 0;
    for (i = -n; i <= n; i++) {
        if (var[n + i] < var[n + besti])
            besti = i;
    }

    phi0 += besti*phistep;
    if (ABS(besti) < n) {
        gwy_math_refine_maximum_1d(var + (n + besti - 1), &t);
        phi0 += t*phistep;
    }
    cphi = cos(phi0);
    sphi = sin(phi0);
    line[0] = xc + cphi*r;
    line[1] = yc + sphi*r;
    line[2] = xc - cphi*r;
    line[3] = yc - sphi*r;

    g_free(var);
}

static void
gwy_tool_profile_straighten_profile(GwyToolProfile *tool, gint id)
{
    GwyPlainTool *plain_tool;
    GwyDataField *dfield;
    gdouble line[4];
    gdouble dx, dy;
    gint thickness = tool->args.thickness;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);
    g_return_if_fail(gwy_selection_get_object(plain_tool->selection, id, line));
    dfield = plain_tool->data_field;
    dx = gwy_data_field_get_dx(dfield);
    dy = gwy_data_field_get_dy(dfield);
    thickness = MAX((thickness + 1)/2, 4);

    /* Don't attempt to optimise very short lines. It would end up in tears. */
    if (hypot((line[2] - line[0])/dx, (line[3] - line[1])/dy) < 4.0)
        return;

    straighten_at_scale(dfield, line, thickness, 0.02, 15);
    straighten_at_scale(dfield, line, thickness, 0.002, 12);

    gwy_selection_set_object(plain_tool->selection, id, line);
}

static void
gwy_tool_profile_render_cell(GtkCellLayout *layout,
                             GtkCellRenderer *renderer,
                             GtkTreeModel *model,
                             GtkTreeIter *iter,
                             gpointer user_data)
{
    GwyToolProfile *tool = (GwyToolProfile*)user_data;
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
gwy_tool_profile_render_color(G_GNUC_UNUSED GtkCellLayout *layout,
                              G_GNUC_UNUSED GtkCellRenderer *renderer,
                              GtkTreeModel *model,
                              GtkTreeIter *iter,
                              gpointer user_data)
{
    GwyToolProfile *tool = (GwyToolProfile*)user_data;
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
gwy_tool_profile_options_expanded(GtkExpander *expander,
                                  G_GNUC_UNUSED GParamSpec *pspec,
                                  GwyToolProfile *tool)
{
    tool->args.options_visible = gtk_expander_get_expanded(expander);
}

static void
gwy_tool_profile_thickness_changed(GwyToolProfile *tool,
                                   GtkAdjustment *adj)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    tool->args.thickness = gwy_adjustment_get_int(adj);
    if (plain_tool->layer) {
        g_object_set(plain_tool->layer,
                     "thickness", tool->args.thickness,
                     NULL);
    }
    gwy_tool_profile_update_all_curves(tool);
}

static void
gwy_tool_profile_resolution_changed(GwyToolProfile *tool,
                                    GtkAdjustment *adj)
{
    tool->args.resolution = gwy_adjustment_get_int(adj);
    /* Resolution can be changed only when fixres == TRUE */
    gwy_tool_profile_update_all_curves(tool);
}

static void
gwy_tool_profile_fixres_changed(GtkToggleButton *check,
                                GwyToolProfile *tool)
{
    tool->args.fixres = gtk_toggle_button_get_active(check);
    gwy_tool_profile_update_all_curves(tool);
}

static void
gwy_tool_profile_number_lines_changed(GtkToggleButton *check,
                                      GwyToolProfile *tool)
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
gwy_tool_profile_separate_changed(GtkToggleButton *check,
                                  GwyToolProfile *tool)
{
    tool->args.separate = gtk_toggle_button_get_active(check);
    gwy_table_hscale_set_sensitive(GTK_OBJECT(tool->target_graph),
                                   !tool->args.separate);
    if (tool->args.separate)
        gwy_data_chooser_set_active(GWY_DATA_CHOOSER(tool->target_graph),
                                    NULL, -1);
}

static void
gwy_tool_profile_both_changed(GtkToggleButton *check,
                              GwyToolProfile *tool)
{
    tool->args.both = gtk_toggle_button_get_active(check);
    display_changed(NULL, tool);
}

static void
gwy_tool_profile_interpolation_changed(GtkComboBox *combo,
                                       GwyToolProfile *tool)
{
    tool->args.interpolation = gwy_enum_combo_box_get_active(combo);
    gwy_tool_profile_update_all_curves(tool);
}

static void
gwy_tool_profile_update_target_graphs(GwyToolProfile *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_refilter(chooser);
}

static gboolean
filter_target_graphs(GwyContainer *data, gint id, gpointer user_data)
{
    GwyToolProfile *tool = (GwyToolProfile*)user_data;
    GwyGraphModel *gmodel, *targetgmodel;
    GQuark quark = gwy_app_get_graph_key_for_id(id);

    return ((gmodel = tool->gmodel)
            && gwy_container_gis_object(data, quark, (GObject**)&targetgmodel)
            && gwy_graph_model_units_are_compatible(gmodel, targetgmodel));
}

static void
gwy_tool_profile_target_changed(GwyToolProfile *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_get_active_id(chooser, &tool->args.target);
}

static void
gwy_tool_profile_masking_changed(GtkComboBox *combo,
                                 GwyToolProfile *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->args.masking = gwy_enum_combo_box_get_active(combo);
    if (plain_tool->data_field && plain_tool->mask_field)
        gwy_tool_profile_update_all_curves(tool);
}

static void
gwy_tool_profile_apply(GwyToolProfile *tool)
{
    GwyPlainTool *plain_tool;
    GwyGraphCurveModel *gcmodel, *gcm;
    GwyCurveCalibrationData *ccdata;
    GwyGraphModel *gmodel;
    gchar *s;
    gint i, n, multpos, size;
    gboolean has_calibration, is_masking;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);
    n = gwy_selection_get_data(plain_tool->selection, NULL);
    g_return_if_fail(n);

    is_masking = (plain_tool->mask_field
                  && tool->args.masking != GWY_MASK_IGNORE);
    has_calibration = tool->has_calibration && !is_masking;

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

    multpos = has_calibration ? 9 : 1;
    for (i = 0; i < n*multpos; i += multpos) {
        gmodel = gwy_graph_model_new_alike(tool->gmodel);
        g_object_set(gmodel, "label-visible", TRUE, NULL);
        gcmodel = gwy_graph_model_get_curve(tool->gmodel, i);
        gcmodel = gwy_graph_curve_model_duplicate(gcmodel);

        /*add calibration data to the curve*/
        if (has_calibration) {
            ccdata = g_new(GwyCurveCalibrationData, 1);
            size = gwy_graph_curve_model_get_ndata(gcmodel) * sizeof(gdouble);
            gcm = gwy_graph_model_get_curve(tool->gmodel, i+1);
            ccdata->xerr = g_memdup(gwy_graph_curve_model_get_ydata(gcm), size);
            gcm = gwy_graph_model_get_curve(tool->gmodel, i+2);
            ccdata->yerr = g_memdup(gwy_graph_curve_model_get_ydata(gcm), size);
            gcm = gwy_graph_model_get_curve(tool->gmodel, i+3);
            ccdata->zerr = g_memdup(gwy_graph_curve_model_get_ydata(gcm), size);
            gcm = gwy_graph_model_get_curve(tool->gmodel, i+4);
            ccdata->xunc = g_memdup(gwy_graph_curve_model_get_ydata(gcm), size);
            gcm = gwy_graph_model_get_curve(tool->gmodel, i+5);
            ccdata->yunc = g_memdup(gwy_graph_curve_model_get_ydata(gcm), size);
            gcm = gwy_graph_model_get_curve(tool->gmodel, i+6);
            ccdata->zunc = g_memdup(gwy_graph_curve_model_get_ydata(gcm), size);
            gwy_graph_curve_model_set_calibration_data(gcmodel, ccdata);
        }

        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
        g_object_get(gcmodel, "description", &s, NULL);
        g_object_set(gmodel, "title", s, NULL);
        g_free(s);
        gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container,
                                             TRUE);
        g_object_unref(gmodel);


        if (tool->display_type > 0) {
            gmodel = gwy_graph_model_new_alike(tool->gmodel);
            g_object_set(gmodel, "label-visible", TRUE, NULL);
            gcmodel = gwy_graph_model_get_curve(tool->gmodel, i+tool->display_type);
            gcmodel = gwy_graph_curve_model_duplicate(gcmodel);
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);
            g_object_get(gcmodel, "description", &s, NULL);
            g_object_set(gmodel, "title", s, NULL);
            g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
            g_free(s);
            gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container,
                                             TRUE);
         }
    }
}

static GtkWidget*
menu_display(GCallback callback, gpointer cbdata,
             GwyCCDisplayType current)
{
    static const GwyEnum entries[] = {
        { N_("calib-data|None"), GWY_CC_DISPLAY_NONE,   },
        { N_("X correction"),    GWY_CC_DISPLAY_X_CORR, },
        { N_("Y correction"),    GWY_CC_DISPLAY_Y_CORR, },
        { N_("Z correction"),    GWY_CC_DISPLAY_Z_CORR, },
        { N_("X uncertainty"),   GWY_CC_DISPLAY_X_UNC,  },
        { N_("Y uncertainty"),   GWY_CC_DISPLAY_Y_UNC,  },
        { N_("Z uncertainty"),   GWY_CC_DISPLAY_Z_UNC,  },

    };
    return gwy_enum_combo_box_new(entries, G_N_ELEMENTS(entries),
                                  callback, cbdata, current, TRUE);
}

static void
display_changed(G_GNUC_UNUSED GtkComboBox *combo, GwyToolProfile *tool)
{
    GwyPlainTool *plain_tool;
    GwyGraphCurveModel *gcmodel;
    gint i, n;
    gint multpos = 9;

    if (!tool->has_calibration)
        return;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);
    n = gwy_selection_get_data(plain_tool->selection, NULL);
    if (!n)
        return;

    tool->display_type = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(tool->menu_display));

    /*change the visibility of all the affected curves*/
    for (i = 0; i < n*multpos; i++) {
        gcmodel = gwy_graph_model_get_curve(tool->gmodel, i);

        if (i % multpos == 0) {
            if (tool->args.both)
                g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
            else
                g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_HIDDEN, NULL);
        }
        else if ((tool->display_type <= 5
                  && (i - (int)tool->display_type) >= 0
                  && (i - (int)tool->display_type) % multpos == 0)
                 || (tool->display_type == 6
                     && ((i-7) % multpos == 0
                         || (i-8) % multpos == 0))) {
            g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
        }
        else {
            g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_HIDDEN, NULL);
        }
    }
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
