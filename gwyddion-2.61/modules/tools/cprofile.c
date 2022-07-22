/*
 *  $Id: cprofile.c 23307 2021-03-18 15:56:45Z yeti-dn $
 *  Copyright (C) 2019 David Necas (Yeti).
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/datafield.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>

#define GWY_TYPE_TOOL_CPROFILE            (gwy_tool_cprofile_get_type())
#define GWY_TOOL_CPROFILE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_CPROFILE, GwyToolCprofile))
#define GWY_IS_TOOL_CPROFILE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_CPROFILE))
#define GWY_TOOL_CPROFILE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_CPROFILE, GwyToolCprofileClass))

enum {
    NLINES = 1024,
    MAX_THICKNESS = 128,
};

enum {
    COLUMN_I, COLUMN_X, COLUMN_Y, NCOLUMNS
};

typedef enum {
    CPROFILE_MODE_CROSS      = 0,
    CPROFILE_MODE_HORIZONTAL = 1,
    CPROFILE_MODE_VERTICAL   = 2,
    CPROFILE_NMODES,
} CprofileMode;

typedef struct _GwyToolCprofile      GwyToolCprofile;
typedef struct _GwyToolCprofileClass GwyToolCprofileClass;

typedef struct {
    gint thickness;
    GwyMaskingType masking;
    CprofileMode mode;
    gboolean options_visible;
    gboolean zero_cross;
    GwyAppDataId target;
} ToolArgs;

struct _GwyToolCprofile {
    GwyPlainTool parent_instance;

    ToolArgs args;

    GtkTreeView *treeview;
    GtkTreeModel *model;

    GArray *xydata;
    GtkWidget *graph;
    GwyGraphModel *gmodel;
    GdkPixbuf *colorpixbuf;

    GtkWidget *options;
    GtkWidget *mode;
    GtkObject *thickness;
    GtkWidget *target_graph;
    GtkWidget *zero_cross;
    GtkWidget *masking;
    GtkWidget *apply;

    /* potential class data */
    GwySIValueFormat *pixel_format;
    GType layer_type_cross;
};

struct _GwyToolCprofileClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType    gwy_tool_cprofile_get_type            (void)                      G_GNUC_CONST;
static void     gwy_tool_cprofile_finalize            (GObject *object);
static void     gwy_tool_cprofile_init_dialog         (GwyToolCprofile *tool);
static void     gwy_tool_cprofile_render_cell         (GtkCellLayout *layout,
                                                       GtkCellRenderer *renderer,
                                                       GtkTreeModel *model,
                                                       GtkTreeIter *iter,
                                                       gpointer user_data);
static void     gwy_tool_cprofile_render_color        (GtkCellLayout *layout,
                                                       GtkCellRenderer *renderer,
                                                       GtkTreeModel *model,
                                                       GtkTreeIter *iter,
                                                       gpointer user_data);
static void     gwy_tool_cprofile_data_switched       (GwyTool *gwytool,
                                                       GwyDataView *data_view);
static void     gwy_tool_cprofile_update_curve        (GwyToolCprofile *tool,
                                                       gint i);
static void     gwy_tool_cprofile_update_all_curves   (GwyToolCprofile *tool);
static void     gwy_tool_cprofile_data_changed        (GwyPlainTool *plain_tool);
static void     gwy_tool_cprofile_response            (GwyTool *tool,
                                                       gint response_id);
static void     gwy_tool_cprofile_selection_changed   (GwyPlainTool *plain_tool,
                                                       gint hint);
static void     gwy_tool_cprofile_mode_changed        (GtkComboBox *combo,
                                                       GwyToolCprofile *tool);
static void     gwy_tool_cprofile_thickness_changed   (GwyToolCprofile *tool);
static void     gwy_tool_cprofile_zero_cross_changed  (GtkToggleButton *toggle,
                                                       GwyToolCprofile *tool);
static void     gwy_tool_cprofile_masking_changed     (GtkComboBox *combo,
                                                       GwyToolCprofile *tool);
static void     gwy_tool_cprofile_options_expanded    (GtkExpander *expander,
                                                       GParamSpec *pspec,
                                                       GwyToolCprofile *tool);
static void     gwy_tool_cprofile_update_target_graphs(GwyToolCprofile *tool);
static gboolean filter_target_graphs                  (GwyContainer *data,
                                                       gint id,
                                                       gpointer user_data);
static void     gwy_tool_cprofile_target_changed      (GwyToolCprofile *tool);
static void     gwy_tool_cprofile_apply               (GwyToolCprofile *tool);

static const gchar masking_key[]         = "/module/cprofile/masking";
static const gchar mode_key[]            = "/module/cprofile/mode";
static const gchar options_visible_key[] = "/module/cprofile/options_visible";
static const gchar thickness_key[]       = "/module/cprofile/thickness";
static const gchar zero_cross_key[]      = "/module/cprofile/zero_cross";

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Profile tool which reads horizontal and/or vertical scan lines."),
    "Yeti <yeti@gwyddion.net>",
    "1.4",
    "David Nečas (Yeti)",
    "2019",
};

static const ToolArgs default_args = {
    1, GWY_MASK_IGNORE, CPROFILE_MODE_CROSS,
    FALSE, TRUE,
    GWY_APP_DATA_ID_NONE,
};

GWY_MODULE_QUERY2(module_info, cprofile)

G_DEFINE_TYPE(GwyToolCprofile, gwy_tool_cprofile, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_CPROFILE);

    return TRUE;
}

static void
gwy_tool_cprofile_class_init(GwyToolCprofileClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_cprofile_finalize;

    tool_class->stock_id = GWY_STOCK_CROSS_PROFILE;
    tool_class->title = _("Profiles Along Axes");
    tool_class->tooltip = _("Read horizontal and/or vertical profiles");
    tool_class->prefix = "/module/cprofile";
    tool_class->default_width = 640;
    tool_class->default_height = 400;
    tool_class->data_switched = gwy_tool_cprofile_data_switched;
    tool_class->response = gwy_tool_cprofile_response;

    ptool_class->data_changed = gwy_tool_cprofile_data_changed;
    ptool_class->selection_changed = gwy_tool_cprofile_selection_changed;
}

static void
gwy_tool_cprofile_finalize(GObject *object)
{
    GwyToolCprofile *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_CPROFILE(object);

    settings = gwy_app_settings_get();
    gwy_container_set_boolean_by_name(settings, options_visible_key,
                                      tool->args.options_visible);
    gwy_container_set_boolean_by_name(settings, zero_cross_key,
                                      tool->args.zero_cross);
    gwy_container_set_int32_by_name(settings, thickness_key,
                                    tool->args.thickness);
    gwy_container_set_enum_by_name(settings, masking_key, tool->args.masking);
    gwy_container_set_enum_by_name(settings, mode_key, tool->args.mode);

    if (tool->xydata)
        g_array_free(tool->xydata, TRUE);
    if (tool->model) {
        gtk_tree_view_set_model(tool->treeview, NULL);
        GWY_OBJECT_UNREF(tool->model);
    }
    GWY_OBJECT_UNREF(tool->gmodel);
    GWY_OBJECT_UNREF(tool->colorpixbuf);
    GWY_SI_VALUE_FORMAT_FREE(tool->pixel_format);
    G_OBJECT_CLASS(gwy_tool_cprofile_parent_class)->finalize(object);
}

static void
gwy_tool_cprofile_init(GwyToolCprofile *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;
    gint width, height;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_cross = gwy_plain_tool_check_layer_type(plain_tool,
                                                             "GwyLayerCross");
    if (!tool->layer_type_cross)
        return;

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_MARKUP;
    plain_tool->lazy_updates = TRUE;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_boolean_by_name(settings, options_visible_key,
                                      &tool->args.options_visible);
    gwy_container_gis_boolean_by_name(settings, zero_cross_key,
                                      &tool->args.zero_cross);
    gwy_container_gis_int32_by_name(settings, thickness_key,
                                    &tool->args.thickness);
    gwy_container_gis_enum_by_name(settings, masking_key, &tool->args.masking);
    tool->args.masking = gwy_enum_sanitize_value(tool->args.masking,
                                                 GWY_TYPE_MASKING_TYPE);
    gwy_container_gis_enum_by_name(settings, mode_key, &tool->args.mode);
    tool->args.mode = CLAMP(tool->args.mode, 0, CPROFILE_NMODES-1);

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    height |= 1;
    tool->colorpixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                                       height, height);

    tool->pixel_format = gwy_si_unit_value_format_new(1.0, 0, _("px"));
    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_cross,
                                     "cross");

    gwy_tool_cprofile_init_dialog(tool);
}

static void
gwy_tool_cprofile_init_dialog(GwyToolCprofile *tool)
{
    static const gchar *column_titles[] = {
        "<b>n</b>",
        "<b>x</b>",
        "<b>y</b>",
    };
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkDialog *dialog;
    GtkWidget *scwin, *label, *hbox, *vbox;
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

    for (i = 0; i < NCOLUMNS; i++) {
        column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_expand(column, TRUE);
        gtk_tree_view_column_set_alignment(column, 0.5);
        g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(i));
        renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "xalign", 1.0, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column), renderer,
                                           gwy_tool_cprofile_render_cell, tool,
                                           NULL);
        if (i == COLUMN_I) {
            renderer = gtk_cell_renderer_pixbuf_new();
            g_object_set(renderer, "pixbuf", tool->colorpixbuf, NULL);
            gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column),
                                       renderer, FALSE);
            gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column),
                                               renderer,
                                               gwy_tool_cprofile_render_color,
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
                     G_CALLBACK(gwy_tool_cprofile_options_expanded), tool);
    gtk_box_pack_start(GTK_BOX(vbox), tool->options, FALSE, FALSE, 0);

    table = GTK_TABLE(gtk_table_new(5, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_container_add(GTK_CONTAINER(tool->options), GTK_WIDGET(table));
    row = 0;

    tool->mode
        = gwy_enum_combo_box_newl(G_CALLBACK(gwy_tool_cprofile_mode_changed),
                                  tool, tool->args.mode,
                                  _("Cross"), CPROFILE_MODE_CROSS,
                                  _("Horizontal"), CPROFILE_MODE_HORIZONTAL,
                                  _("Vertical"), CPROFILE_MODE_VERTICAL,
                                  NULL);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Mode:"), NULL,
                            GTK_OBJECT(tool->mode),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    tool->masking = gwy_enum_combo_box_new
                            (gwy_masking_type_get_enum(), -1,
                             G_CALLBACK(gwy_tool_cprofile_masking_changed),
                             tool,
                             tool->args.masking, TRUE);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Masking:"), NULL,
                            GTK_OBJECT(tool->masking),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    tool->thickness = gtk_adjustment_new(tool->args.thickness,
                                         1, MAX_THICKNESS, 1, 10, 0);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Thickness:"), _("px"),
                            tool->thickness, GWY_HSCALE_SQRT | GWY_HSCALE_SNAP);
    g_signal_connect_swapped(tool->thickness, "value-changed",
                             G_CALLBACK(gwy_tool_cprofile_thickness_changed),
                             tool);
    row++;

    tool->zero_cross = gtk_check_button_new_with_mnemonic(_("Cross at _zero"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->zero_cross),
                                 tool->args.zero_cross);
    gtk_table_attach(GTK_TABLE(table), tool->zero_cross,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(tool->zero_cross, "toggled",
                     G_CALLBACK(gwy_tool_cprofile_zero_cross_changed), tool);
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
                             G_CALLBACK(gwy_tool_cprofile_target_changed),
                             tool);
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
gwy_tool_cprofile_render_cell(GtkCellLayout *layout,
                              GtkCellRenderer *renderer,
                              GtkTreeModel *model,
                              GtkTreeIter *iter,
                              gpointer user_data)
{
    GwyToolCprofile *tool = (GwyToolCprofile*)user_data;
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
        case COLUMN_X:
        val = floor(gwy_data_field_rtoj(plain_tool->data_field, line[0]));
        break;

        case COLUMN_Y:
        val = floor(gwy_data_field_rtoi(plain_tool->data_field, line[1]));
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
fill_pixbuf_triangular(GdkPixbuf *pixbuf,
                       const GwyRGBA *ulcolor, const GwyRGBA *brcolor)
{
    gint width, height, bpp, rowstride, i, ir, j, jto;
    guchar *pixels, *row;
    guint ulpixel, brpixel;
    guchar ulsamples[3], brsamples[3], mixsamples[3];
    gboolean mixme;

    width = gdk_pixbuf_get_width(pixbuf);
    height = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    bpp = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_if_fail(bpp == 3 || bpp == 4);
    pixels = gdk_pixbuf_get_pixels(pixbuf);

    ulpixel = gwy_rgba_to_pixbuf_pixel(ulcolor);
    brpixel = gwy_rgba_to_pixbuf_pixel(brcolor);
    brsamples[2] = (brpixel >> 8) & 0xff;
    brsamples[1] = (brpixel >> 16) & 0xff;
    brsamples[0] = (brpixel >> 24) & 0xff;

    for (i = 0; i < 3; i++) {
        ulpixel >>= 8;
        ulsamples[2-i] = ulpixel & 0xff;
        brpixel >>= 8;
        brsamples[2-i] = brpixel & 0xff;
        mixsamples[2-i] = (ulsamples[2-i] + brsamples[2-i] + 1)/2;
    }

    for (i = 0; i < height; i++) {
        row = pixels + i*rowstride;
        ir = height-1 - i;
        jto = ((2*ir + 1)*width - height)/(2*height);
        jto = MIN(jto, width);
        mixme = ((2*jto + 1)*height == (2*ir + 1)*width);
        if (bpp == 4) {
            for (j = 0; j < jto; j++) {
                *(row++) = ulsamples[0];
                *(row++) = ulsamples[1];
                *(row++) = ulsamples[2];
                *(row++) = 0xff;
            }
            if (mixme) {
                *(row++) = mixsamples[0];
                *(row++) = mixsamples[1];
                *(row++) = mixsamples[2];
                *(row++) = 0xff;
                jto++;
            }
            for (j = jto; j < width; j++) {
                *(row++) = brsamples[0];
                *(row++) = brsamples[1];
                *(row++) = brsamples[2];
                *(row++) = 0xff;
            }
        }
        else {
            for (j = 0; j < jto; j++) {
                *(row++) = ulsamples[0];
                *(row++) = ulsamples[1];
                *(row++) = ulsamples[2];
            }
            if (mixme) {
                *(row++) = mixsamples[0];
                *(row++) = mixsamples[1];
                *(row++) = mixsamples[2];
                jto++;
            }
            for (j = jto; j < width; j++) {
                *(row++) = brsamples[0];
                *(row++) = brsamples[1];
                *(row++) = brsamples[2];
            }
        }
    }
}

static void
gwy_tool_cprofile_render_color(G_GNUC_UNUSED GtkCellLayout *layout,
                               G_GNUC_UNUSED GtkCellRenderer *renderer,
                               GtkTreeModel *model,
                               GtkTreeIter *iter,
                               gpointer user_data)
{
    GwyToolCprofile *tool = (GwyToolCprofile*)user_data;
    GwyGraphCurveModel *gcmodel;
    GwyRGBA *rgba, *rgba2;
    guint idx, pixel;

    gtk_tree_model_get(model, iter, 0, &idx, -1);

    if (tool->args.mode != CPROFILE_MODE_CROSS) {
        gcmodel = gwy_graph_model_get_curve(tool->gmodel, idx);
        g_object_get(gcmodel, "color", &rgba, NULL);
        pixel = 0xff | gwy_rgba_to_pixbuf_pixel(rgba);
        gwy_rgba_free(rgba);
        gdk_pixbuf_fill(tool->colorpixbuf, pixel);
        return;
    }

    gcmodel = gwy_graph_model_get_curve(tool->gmodel, 2*idx);
    g_object_get(gcmodel, "color", &rgba, NULL);
    gcmodel = gwy_graph_model_get_curve(tool->gmodel, 2*idx + 1);
    g_object_get(gcmodel, "color", &rgba2, NULL);
    fill_pixbuf_triangular(tool->colorpixbuf, rgba, rgba2);
    gwy_rgba_free(rgba2);
    gwy_rgba_free(rgba);
}

static void
gwy_tool_cprofile_data_switched(GwyTool *gwytool,
                                GwyDataView *data_view)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(gwytool);
    GwyToolCprofile *tool;
    gboolean ignore;

    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_cprofile_parent_class)->data_switched(gwytool,
                                                                  data_view);

    if (ignore || plain_tool->init_failed)
        return;

    tool = GWY_TOOL_CPROFILE(gwytool);
    if (data_view) {
        gboolean is_horiz = (tool->args.mode == CPROFILE_MODE_CROSS
                             || tool->args.mode == CPROFILE_MODE_HORIZONTAL);
        gboolean is_vert = (tool->args.mode == CPROFILE_MODE_CROSS
                            || tool->args.mode == CPROFILE_MODE_VERTICAL);
        gwy_object_set_or_reset(plain_tool->layer, tool->layer_type_cross,
                                "draw-horizontal", is_horiz,
                                "draw-vertical", is_vert,
                                "thickness", tool->args.thickness,
                                "editable", TRUE,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, NLINES);
    }

    gwy_graph_model_remove_all_curves(tool->gmodel);
    gwy_tool_cprofile_update_all_curves(tool);
    gwy_tool_cprofile_update_target_graphs(tool);
}

static void
gwy_tool_cprofile_data_changed(GwyPlainTool *plain_tool)
{
    gwy_tool_cprofile_update_all_curves(GWY_TOOL_CPROFILE(plain_tool));
    gwy_tool_cprofile_update_target_graphs(GWY_TOOL_CPROFILE(plain_tool));
}

static void
gwy_tool_cprofile_response(GwyTool *tool,
                           gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_cprofile_parent_class)->response(tool, response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_cprofile_apply(GWY_TOOL_CPROFILE(tool));
}

static void
gwy_tool_cprofile_selection_changed(GwyPlainTool *plain_tool,
                                    gint hint)
{
    GwyToolCprofile *tool = GWY_TOOL_CPROFILE(plain_tool);
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
        gwy_tool_cprofile_update_all_curves(tool);
    }
    else {
        GtkTreeSelection *selection;
        GtkTreePath *path;
        GtkTreeIter iter;

        if (hint < n)
            gwy_null_store_row_changed(store, hint);
        else
            gwy_null_store_set_n_rows(store, n+1);
        gwy_tool_cprofile_update_curve(tool, hint);
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
gwy_tool_cprofile_mode_changed(GtkComboBox *combo, GwyToolCprofile *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    tool->args.mode = gwy_enum_combo_box_get_active(combo);
    if (plain_tool->layer) {
        gboolean is_horiz = (tool->args.mode == CPROFILE_MODE_CROSS
                             || tool->args.mode == CPROFILE_MODE_HORIZONTAL);
        gboolean is_vert = (tool->args.mode == CPROFILE_MODE_CROSS
                            || tool->args.mode == CPROFILE_MODE_VERTICAL);
        g_object_set(plain_tool->layer,
                     "draw-horizontal", is_horiz,
                     "draw-vertical", is_vert,
                     NULL);
    }
    gwy_graph_model_remove_all_curves(tool->gmodel);
    gwy_tool_cprofile_update_all_curves(tool);
}

static void
gwy_tool_cprofile_thickness_changed(GwyToolCprofile *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    tool->args.thickness = gwy_adjustment_get_int(tool->thickness);
    if (plain_tool->layer) {
        g_object_set(plain_tool->layer,
                     "thickness", tool->args.thickness,
                     NULL);
    }
    gwy_tool_cprofile_update_all_curves(tool);
}

static void
gwy_tool_cprofile_zero_cross_changed(GtkToggleButton *toggle,
                                     GwyToolCprofile *tool)
{
    tool->args.zero_cross = gtk_toggle_button_get_active(toggle);
    gwy_tool_cprofile_update_all_curves(tool);
}

static void
gwy_tool_cprofile_masking_changed(GtkComboBox *combo,
                                  GwyToolCprofile *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->args.masking = gwy_enum_combo_box_get_active(combo);
    if (plain_tool->data_field && plain_tool->mask_field)
        gwy_tool_cprofile_update_all_curves(tool);
}

static void
gwy_tool_cprofile_options_expanded(GtkExpander *expander,
                                   G_GNUC_UNUSED GParamSpec *pspec,
                                   GwyToolCprofile *tool)
{
    tool->args.options_visible = gtk_expander_get_expanded(expander);
}

static void
gwy_tool_cprofile_update_target_graphs(GwyToolCprofile *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_refilter(chooser);
}

static gboolean
filter_target_graphs(GwyContainer *data, gint id, gpointer user_data)
{
    GwyToolCprofile *tool = (GwyToolCprofile*)user_data;
    GwyGraphModel *gmodel, *targetgmodel;
    GQuark quark = gwy_app_get_graph_key_for_id(id);

    return ((gmodel = tool->gmodel)
            && gwy_container_gis_object(data, quark, (GObject**)&targetgmodel)
            && gwy_graph_model_units_are_compatible(gmodel, targetgmodel));
}

static void
gwy_tool_cprofile_target_changed(GwyToolCprofile *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_get_active_id(chooser, &tool->args.target);
}

static void
gwy_tool_cprofile_update_all_curves(GwyToolCprofile *tool)
{
    GwyPlainTool *plain_tool;
    GwyNullStore *store;
    gint n, i, nstore;

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (!plain_tool->selection
        || !(n = gwy_selection_get_data(plain_tool->selection, NULL))) {
        gwy_graph_model_remove_all_curves(tool->gmodel);
        return;
    }

    store = GWY_NULL_STORE(tool->model);
    nstore = gwy_null_store_get_n_rows(store);
    for (i = 0; i < n; i++) {
        gwy_tool_cprofile_update_curve(tool, i);
        if (i < nstore)
            gwy_null_store_row_changed(store, i);
    }
    gwy_null_store_set_n_rows(store, n);
}

static void
extract_column_profile(GwyDataField *dfield,
                       GwyDataField *mask, GwyMaskingType masking,
                       GArray *xydata, gint col, gint thickness)
{
    gint xres, yres, jfrom, jto, i, j, count;
    const gdouble *d, *drow, *m, *mrow;
    gdouble dy, z;
    GwyXY xy;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    dy = gwy_data_field_get_dy(dfield);
    d = gwy_data_field_get_data_const(dfield);

    g_array_set_size(xydata, 0);

    jfrom = col - (thickness - 1)/2;
    jfrom = MAX(jfrom, 0);
    jto = col + thickness/2 + 1;
    jto = MIN(jto, xres);

    if (mask && masking != GWY_MASK_IGNORE)
        m = gwy_data_field_get_data_const(mask);
    else
        m = NULL;

    for (i = 0; i < yres; i++) {
        drow = d + i*xres + jfrom;
        z = 0.0;
        if (m) {
            mrow = m + i*xres + jfrom;
            count = 0;
            if (masking == GWY_MASK_INCLUDE) {
                for (j = 0; j < jto - jfrom; j++) {
                    if (mrow[j] > 0.0) {
                        z += drow[j];
                        count++;
                    }
                }
            }
            else {
                for (j = 0; j < jto - jfrom; j++) {
                    if (mrow[j] <= 0.0) {
                        z += drow[j];
                        count++;
                    }
                }
            }
        }
        else {
            count = jto - jfrom;
            for (j = 0; j < count; j++)
                z += drow[j];
        }

        if (count) {
            xy.x = dy*i;
            xy.y = z/count;
            g_array_append_val(xydata, xy);
        }
    }
}

static void
extract_row_profile(GwyDataField *dfield,
                    GwyDataField *mask, GwyMaskingType masking,
                    GArray *xydata, gint row, gint thickness)
{
    gint xres, yres, ifrom, ito, i, j;
    const gdouble *d, *drow, *m, *mrow;
    gdouble dx;
    GwyXY *xy;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    dx = gwy_data_field_get_dx(dfield);
    d = gwy_data_field_get_data_const(dfield);

    g_array_set_size(xydata, xres);
    xy = &g_array_index(xydata, GwyXY, 0);
    gwy_clear(xy, xres);

    ifrom = row - (thickness - 1)/2;
    ifrom = MAX(ifrom, 0);
    ito = row + thickness/2 + 1;
    ito = MIN(ito, yres);

    if (mask && masking != GWY_MASK_IGNORE)
        m = gwy_data_field_get_data_const(mask);
    else {
        m = NULL;
        for (j = 0; j < xres; j++)
            xy[j].x = ito - ifrom;
    }

    for (i = ifrom; i < ito; i++) {
        drow = d + i*xres;
        if (m) {
            mrow = m + i*xres;
            if (masking == GWY_MASK_INCLUDE) {
                for (j = 0; j < xres; j++) {
                    if (mrow[j] > 0.0) {
                        xy[j].y += drow[j];
                        xy[j].x += 1.0;
                    }
                }
            }
            else {
                for (j = 0; j < xres; j++) {
                    if (mrow[j] <= 0.0) {
                        xy[j].y += drow[j];
                        xy[j].x += 1.0;
                    }
                }
            }
        }
        else {
            for (j = 0; j < xres; j++)
                xy[j].y += drow[j];
        }
    }

    for (i = j = 0; j < xres; j++) {
        if (xy[j].x > 0.0) {
            xy[i].y = xy[j].y/xy[j].x;
            xy[i].x = dx*j;
            i++;
        }
    }
    g_array_set_size(xydata, i);
}

static void
add_x_offset(GArray *xydata, gdouble offset)
{
    guint i, n;

    n = xydata->len;
    for (i = 0; i < n; i++)
        g_array_index(xydata, GwyXY, i).x += offset;
}

static void
update_one_curve(GwyToolCprofile *tool, gint i, gint id, gboolean is_vert)
{
    GwyGraphCurveModel *gcmodel;
    GwyDataField *data_field;
    const GwyRGBA *color;
    GArray *xydata;
    gchar *desc;
    gint n;

    xydata = tool->xydata;

    n = gwy_graph_model_get_n_curves(tool->gmodel);
    if (i < n) {
        gcmodel = gwy_graph_model_get_curve(tool->gmodel, i);
        gwy_graph_curve_model_set_data_interleaved(gcmodel,
                                                   (gdouble*)xydata->data,
                                                   xydata->len);
        return;
    }

    gcmodel = gwy_graph_curve_model_new();
    if (is_vert)
        desc = g_strdup_printf(_("Vertical profile %d"), id);
    else
        desc = g_strdup_printf(_("Horizontal profile %d"), id);
    color = gwy_graph_get_preset_color(i);
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "description", desc,
                 "color", color,
                 NULL);
    g_free(desc);
    gwy_graph_model_add_curve(tool->gmodel, gcmodel);
    gwy_graph_curve_model_set_data_interleaved(gcmodel,
                                               (gdouble*)xydata->data,
                                               xydata->len);
    g_object_unref(gcmodel);

    if (i == 0) {
        data_field = GWY_PLAIN_TOOL(tool)->data_field;
        gwy_graph_model_set_units_from_data_field(tool->gmodel, data_field,
                                                  1, 0, 0, 1);
        gwy_tool_cprofile_update_target_graphs(tool);
    }
}

static void
gwy_tool_cprofile_update_curve(GwyToolCprofile *tool,
                               gint i)
{
    GwyPlainTool *plain_tool;
    gdouble xy[2];
    gint col, row;
    GwyDataField *data_field, *mask;
    GArray *xydata;
    gdouble offset;
    gboolean is_horiz = (tool->args.mode == CPROFILE_MODE_CROSS
                         || tool->args.mode == CPROFILE_MODE_HORIZONTAL);
    gboolean is_vert = (tool->args.mode == CPROFILE_MODE_CROSS
                        || tool->args.mode == CPROFILE_MODE_VERTICAL);

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);
    g_return_if_fail(gwy_selection_get_object(plain_tool->selection, i, xy));
    data_field = plain_tool->data_field;
    mask = plain_tool->mask_field;
    if (!tool->xydata)
        tool->xydata = g_array_new(FALSE, FALSE, sizeof(GwyXY));
    xydata = tool->xydata;

    col = gwy_data_field_rtoj(data_field, xy[0]);
    row = gwy_data_field_rtoi(data_field, xy[1]);
    if (is_horiz) {
        extract_row_profile(data_field, mask, tool->args.masking,
                            xydata, row, tool->args.thickness);
        if (tool->args.zero_cross)
            offset = -xy[0];
        else
            offset = gwy_data_field_get_xoffset(data_field);

        add_x_offset(xydata, offset);
        update_one_curve(tool, is_vert ? 2*i + 0 : i, i+1, FALSE);
    }

    if (is_vert) {
        extract_column_profile(data_field, mask, tool->args.masking,
                               xydata, col, tool->args.thickness);
        if (tool->args.zero_cross)
            offset = -xy[1];
        else
            offset = gwy_data_field_get_yoffset(data_field);

        add_x_offset(xydata, offset);
        update_one_curve(tool, is_horiz ? 2*i + 1 : i, i+1, TRUE);
    }
}

static void
gwy_tool_cprofile_apply(GwyToolCprofile *tool)
{
    GwyPlainTool *plain_tool;
    GwyGraphModel *gmodel;
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

    gmodel = gwy_graph_model_duplicate(tool->gmodel);
    g_object_set(gmodel, "label-visible", TRUE, NULL);
    gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container,
                                         TRUE);
    g_object_unref(gmodel);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
