/*
 *  $Id: icolorange.c 24706 2022-03-21 17:00:25Z yeti-dn $
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwycontainer.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwylayer-basic.h>
#include <app/gwyapp.h>
#include <libgwydgets/gwydgetutils.h>

#define APP_RANGE_KEY "/app/default-range-type"

#define GWY_TYPE_TOOL_COLOR_RANGE            (gwy_tool_color_range_get_type())
#define GWY_TOOL_COLOR_RANGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_COLOR_RANGE, GwyToolColorRange))
#define GWY_IS_TOOL_COLOR_RANGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_COLOR_RANGE))
#define GWY_TOOL_COLOR_RANGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_COLOR_RANGE, GwyToolColorRangeClass))

typedef enum {
    USE_SELECTION = 0,
    USE_HISTOGRAM
} ColorRangeSource;

typedef struct _GwyToolColorRange      GwyToolColorRange;
typedef struct _GwyToolColorRangeClass GwyToolColorRangeClass;

struct _GwyToolColorRange {
    GwyPlainTool parent_instance;

    GwyRectSelectionLabels *rlabels;

    GwyGraph *histogram;
    GwyGraphModel *histogram_model;
    GwyDataLine *heightdist;
    GwySelection *graph_selection;
    gint isel[4];
    gdouble rsel[4];

    GtkWidget *is_default;
    GtkLabel *min;
    GtkWidget *spinmin;
    GtkWidget *spinmax;
    GtkLabel *max;
    GtkLabel *datamin;
    GtkLabel *datamax;
    GtkWidget *set_to_masked;
    GtkWidget *set_to_unmasked;
    GtkWidget *invert;

    ColorRangeSource range_source;
    gboolean programmatic_update;
    gboolean data_switch;
    GSList *modelist;

    GQuark key_min;
    GQuark key_max;

    /* potential class data */
    GType layer_type_rect;
};

struct _GwyToolColorRangeClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType  gwy_tool_color_range_get_type         (void) G_GNUC_CONST;
static void   gwy_tool_color_range_finalize         (GObject *object);
static void   gwy_tool_color_range_init_dialog      (GwyToolColorRange *tool);
static void   gwy_tool_color_range_data_switched    (GwyTool *gwytool,
                                                     GwyDataView *data_view);
static void   gwy_tool_color_range_make_keys        (GwyToolColorRange *tool,
                                                     GwyDataView *data_view);
static void   gwy_tool_color_range_data_changed     (GwyPlainTool *plain_tool);
static void   gwy_tool_color_range_mask_changed     (GwyPlainTool *plain_tool);
static void   gwy_tool_color_range_selection_changed(GwyPlainTool *plain_tool,
                                                     gint hint);
static void   gwy_tool_color_range_xsel_changed     (GwySelection *selection,
                                                     gint hint,
                                                     GwyToolColorRange *tool);
static void   gwy_tool_color_range_type_changed     (GtkWidget *radio,
                                                     GwyToolColorRange *tool);
static void   gwy_tool_color_range_set_default_mode (GtkToggleButton *check,
                                                     GwyToolColorRange *tool);
static GwyLayerBasicRangeType gwy_tool_color_range_get_range_type(GwyToolColorRange *tool);
static void   gwy_tool_color_range_set_range_type   (GwyToolColorRange *tool,
                                                     GwyLayerBasicRangeType range_type);
static void   gwy_tool_color_range_get_min_max      (GwyToolColorRange *tool,
                                                     gdouble *selection);
static void   gwy_tool_color_range_set_min_max      (GwyToolColorRange *tool);
static void   gwy_tool_color_range_update_fullrange (GwyToolColorRange *tool);
static void   gwy_tool_color_range_update_histogram (GwyToolColorRange *tool);
static void   gwy_tool_color_range_spin_changed     (GwyToolColorRange *tool);
static void   set_range_to_masked                   (GwyToolColorRange *tool);
static void   set_range_to_unmasked                 (GwyToolColorRange *tool);
static void   invert_mapping                        (GwyToolColorRange *tool);
static void   update_selected_rectangle             (GwyToolColorRange *tool);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Interactive color range tool, allows selecting the data range false "
       "color scale should map to, either on data or on height distribution "
       "histogram."),
    "Yeti <yeti@gwyddion.net>",
    "3.21",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, icolorange)

G_DEFINE_TYPE(GwyToolColorRange, gwy_tool_color_range, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_COLOR_RANGE);

    return TRUE;
}

static void
gwy_tool_color_range_class_init(GwyToolColorRangeClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_color_range_finalize;

    tool_class->stock_id = GWY_STOCK_COLOR_RANGE;
    tool_class->title = _("Color Range");
    tool_class->tooltip = _("Stretch color range to part of data");
    tool_class->prefix = "/module/colorrange";
    tool_class->data_switched = gwy_tool_color_range_data_switched;

    ptool_class->data_changed = gwy_tool_color_range_data_changed;
    ptool_class->mask_changed = gwy_tool_color_range_mask_changed;
    ptool_class->selection_changed = gwy_tool_color_range_selection_changed;
}

static void
gwy_tool_color_range_finalize(GObject *object)
{
    GwyToolColorRange *tool;

    tool = GWY_TOOL_COLOR_RANGE(object);
    GWY_OBJECT_UNREF(tool->heightdist);

    G_OBJECT_CLASS(gwy_tool_color_range_parent_class)->finalize(object);
}

static void
gwy_tool_color_range_init(GwyToolColorRange *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_rect = gwy_plain_tool_check_layer_type(plain_tool,
                                                           "GwyLayerRectangle");
    if (!tool->layer_type_rect)
        return;

    settings = gwy_app_settings_get();
    if (!gwy_container_contains_by_name(settings, APP_RANGE_KEY))
        gwy_container_set_enum_by_name(settings, APP_RANGE_KEY,
                                       GWY_LAYER_BASIC_RANGE_FULL);

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_VFMARKUP;

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_rect,
                                     "rectangle");

    gwy_tool_color_range_init_dialog(tool);
}

static void
gwy_tool_crop_rect_updated(GwyToolColorRange *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_rect_selection_labels_select(tool->rlabels,
                                     plain_tool->selection,
                                     plain_tool->data_field);
}

static void
gwy_tool_color_range_init_dialog(GwyToolColorRange *tool)
{
    static struct {
        guint type;
        const gchar *stock_id;
        const gchar *text;
    }
    const range_types[] = {
        {
            GWY_LAYER_BASIC_RANGE_FULL,
            GWY_STOCK_COLOR_RANGE_FULL,
            N_("Full color range from minimum to maximum"),
        },
        {
            GWY_LAYER_BASIC_RANGE_FIXED,
            GWY_STOCK_COLOR_RANGE_FIXED,
            N_("Explicitly set fixed color range"),
        },
        {
            GWY_LAYER_BASIC_RANGE_AUTO,
            GWY_STOCK_COLOR_RANGE_AUTO,
            N_("Automatic color range with tails cut off"),
        },
        {
            GWY_LAYER_BASIC_RANGE_ADAPT,
            GWY_STOCK_COLOR_RANGE_ADAPTIVE,
            N_("Adaptive nonlinear color mapping"),
        },
    };

    GtkWidget *label, *hbox, *button, *image, *hbox_spin_min, *hbox_spin_max;
    GtkRadioButton *group;
    GtkTable *table;
    GtkDialog *dialog;
    GwyGraphCurveModel *cmodel;
    GwyGraphArea *garea;
    GtkSizeGroup *sizegroup;
    GwyLayerBasicRangeType range_type = GWY_LAYER_BASIC_RANGE_FULL;
    gint row, i;
    GtkObject *spin_adj;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    /* Mode switch */
    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, FALSE, FALSE, 0);

    group = NULL;
    for (i = 0; i < G_N_ELEMENTS(range_types); i++) {
        button = gtk_radio_button_new_from_widget(group);
        g_object_set(button, "draw-indicator", FALSE, NULL);
        image = gtk_image_new_from_stock(range_types[i].stock_id,
                                         GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add(GTK_CONTAINER(button), image);
        gwy_radio_button_set_value(button, range_types[i].type);
        gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
        gtk_widget_set_tooltip_text(button, gettext(range_types[i].text));
        g_signal_connect(button, "clicked",
                         G_CALLBACK(gwy_tool_color_range_type_changed), tool);
        if (!group)
            group = GTK_RADIO_BUTTON(button);
    }
    tool->modelist = gtk_radio_button_get_group(group);

    /* Is default */
    tool->is_default = gtk_check_button_new_with_mnemonic(_("_default"));
    gtk_box_pack_start(GTK_BOX(hbox), tool->is_default, FALSE, FALSE, 4);
    g_signal_connect(tool->is_default, "toggled",
                     G_CALLBACK(gwy_tool_color_range_set_default_mode), tool);

    /* Height distribution */
    tool->heightdist = gwy_data_line_new(1.0, 1.0, TRUE);
    cmodel = gwy_graph_curve_model_new();
    g_object_set(cmodel,
                 "description", _("Height histogram"),
                 "mode", GWY_GRAPH_CURVE_LINE,
                 NULL);

    tool->histogram_model = gwy_graph_model_new();
    gwy_graph_model_add_curve(tool->histogram_model, cmodel);
    tool->histogram = GWY_GRAPH(gwy_graph_new(tool->histogram_model));
    gwy_graph_set_status(tool->histogram, GWY_GRAPH_STATUS_XSEL);
    garea = GWY_GRAPH_AREA(gwy_graph_get_area(tool->histogram));
    gtk_widget_set_size_request(GTK_WIDGET(garea), -1, 48);
    tool->graph_selection = gwy_graph_area_get_selection(garea,
                                                         GWY_GRAPH_STATUS_XSEL);
    g_return_if_fail(GWY_IS_SELECTION_GRAPH_1DAREA(tool->graph_selection));
    gwy_selection_set_max_objects(tool->graph_selection, 1);
    g_signal_connect(tool->graph_selection, "changed",
                     G_CALLBACK(gwy_tool_color_range_xsel_changed), tool);

    g_object_set(tool->histogram_model, "label-visible", FALSE, NULL);
    gwy_graph_set_axis_visible(tool->histogram, GTK_POS_TOP, FALSE);
    gwy_graph_set_axis_visible(tool->histogram, GTK_POS_BOTTOM, FALSE);
    gwy_graph_set_axis_visible(tool->histogram, GTK_POS_LEFT, FALSE);
    gwy_graph_set_axis_visible(tool->histogram, GTK_POS_RIGHT, FALSE);
    gwy_graph_enable_user_input(tool->histogram, FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(tool->histogram), FALSE);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), GTK_WIDGET(tool->histogram),
                       TRUE, TRUE, 2);

    /* Data ranges */
    table = GTK_TABLE(gtk_table_new(8, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), GTK_WIDGET(table),
                       FALSE, FALSE, 0);
    row = 0;

    label = gwy_label_new_header(_("Color Mapping"));
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Start"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    spin_adj = gtk_adjustment_new(1, -1e6, 1e6, 0.01, 0.5, 0);
    tool->spinmin = gtk_spin_button_new(GTK_ADJUSTMENT(spin_adj), 0.0, 3);
    gtk_widget_set_sensitive(GTK_WIDGET(tool->spinmin), FALSE);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(tool->spinmin), TRUE);
    g_signal_connect_swapped(spin_adj, "value-changed",
                             G_CALLBACK(gwy_tool_color_range_spin_changed),
                             tool);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), GTK_WIDGET(tool->spinmin));

    tool->min = GTK_LABEL(gtk_label_new(NULL));
    gtk_misc_set_alignment(GTK_MISC(tool->min), 1.0, 0.5);

    hbox_spin_min = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_spin_min),
                       GTK_WIDGET(tool->min), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_spin_min), tool->spinmin, FALSE, FALSE, 0);
    gtk_table_attach(table, hbox_spin_min,
                     1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_End"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    spin_adj = gtk_adjustment_new(1, -1e6, 1e6, 0.01, 0.5, 0);
    tool->spinmax = gtk_spin_button_new(GTK_ADJUSTMENT(spin_adj), 0.0, 3);
    gtk_widget_set_sensitive(GTK_WIDGET(tool->spinmax), FALSE);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(tool->spinmax), TRUE);
    g_signal_connect_swapped(spin_adj, "value-changed",
                             G_CALLBACK(gwy_tool_color_range_spin_changed),
                             tool);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), GTK_WIDGET(tool->spinmax));

    tool->max = GTK_LABEL(gtk_label_new(NULL));
    gtk_misc_set_alignment(GTK_MISC(tool->max), 1.0, 0.5);

    hbox_spin_max = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_spin_max),
                       GTK_WIDGET(tool->max), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox_spin_max), tool->spinmax, FALSE, FALSE, 0);
    gtk_table_attach(table, hbox_spin_max,
                     1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_table_attach(table, hbox,
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

    button = gtk_button_new_with_mnemonic(_("Set to _Unmasked"));
    tool->set_to_unmasked = button;
    gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, TRUE, 0);
    gtk_size_group_add_widget(sizegroup, button);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(set_range_to_unmasked), tool);

    button = gtk_button_new_with_mnemonic(_("Set to _Masked"));
    tool->set_to_masked = button;
    gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, TRUE, 0);
    gtk_size_group_add_widget(sizegroup, button);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(set_range_to_masked), tool);
    row++;

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_table_attach(table, hbox,
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    button = gtk_button_new_with_mnemonic(_("_Invert Mapping"));
    tool->invert = button;
    gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, TRUE, 0);
    gtk_size_group_add_widget(sizegroup, button);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(invert_mapping), tool);
    g_object_unref(sizegroup);
    row++;

    gtk_table_set_row_spacing(table, row-1, 8);
    label = gwy_label_new_header(_("Data Range"));
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new(_("Minimum"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    tool->datamin = GTK_LABEL(gtk_label_new(NULL));
    gtk_misc_set_alignment(GTK_MISC(tool->datamin), 1.0, 0.5);
    gtk_table_attach(table, GTK_WIDGET(tool->datamin),
                     1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new(_("Maximum"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    tool->datamax = GTK_LABEL(gtk_label_new(NULL));
    gtk_misc_set_alignment(GTK_MISC(tool->datamax), 1.0, 0.5);
    gtk_table_attach(table, GTK_WIDGET(tool->datamax),
                     1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    gtk_table_set_row_spacing(table, row, 8);
    row++;

    /* Selection info */
    tool->rlabels = gwy_rect_selection_labels_new
                         (TRUE, G_CALLBACK(gwy_tool_crop_rect_updated), tool);
    gtk_box_pack_start(GTK_BOX(dialog->vbox),
                       gwy_rect_selection_labels_get_table(tool->rlabels),
                       FALSE, FALSE, 0);

    gwy_tool_add_hide_button(GWY_TOOL(tool), TRUE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    /* Switch to the default */
    gwy_container_gis_enum_by_name(gwy_app_settings_get(),
                                   APP_RANGE_KEY, &range_type);
    gwy_radio_buttons_set_current(tool->modelist, range_type);
    gwy_tool_color_range_type_changed(NULL, tool);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_color_range_data_switched(GwyTool *gwytool,
                                   GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolColorRange *tool;
    GwyLayerBasicRangeType range_type;
    gboolean ignore;

    tool = GWY_TOOL_COLOR_RANGE(gwytool);
    gwy_tool_color_range_make_keys(tool, data_view);
    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);

    gwy_debug("A");
    tool->data_switch = TRUE;
    GWY_TOOL_CLASS(gwy_tool_color_range_parent_class)->data_switched(gwytool,
                                                                     data_view);
    tool->data_switch = FALSE;
    gwy_debug("B");

    if (plain_tool->init_failed)
        return;

    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_rect,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);
    }
    else {
        gtk_widget_set_sensitive(GTK_WIDGET(tool->histogram), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(tool->spinmin), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(tool->spinmax), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(tool->invert), FALSE);
        gwy_selection_clear(tool->graph_selection);
    }

    tool = GWY_TOOL_COLOR_RANGE(gwytool);
    gwy_tool_color_range_update_histogram(tool);

    if (ignore)
        return;

    range_type = gwy_tool_color_range_get_range_type(tool);
    if (data_view) {
        if (range_type == GWY_LAYER_BASIC_RANGE_FIXED) {
            gdouble sel[2];

            gwy_tool_color_range_get_min_max(tool, sel);
            gwy_debug("[%g, %g]", sel[0], sel[1]);
            gwy_selection_set_data(tool->graph_selection, 1, sel);
        }
        else
            gwy_selection_clear(tool->graph_selection);

        tool->programmatic_update = TRUE;
        gwy_tool_color_range_type_changed(NULL, tool);
        tool->programmatic_update = FALSE;
    }
    gwy_radio_buttons_set_current(tool->modelist, range_type);
    gwy_tool_color_range_update_fullrange(tool);
    gwy_tool_color_range_mask_changed(plain_tool);
    gwy_debug("set min max after data switch");
    gwy_tool_color_range_set_min_max(tool);
}

static void
gwy_tool_color_range_make_keys(GwyToolColorRange *tool,
                               GwyDataView *data_view)
{
    GwyPixmapLayer *layer;
    const gchar *dkey;
    gchar key[32];
    gint id;

    if (!data_view) {
        tool->key_min = tool->key_max = 0;
        return;
    }

    layer = gwy_data_view_get_base_layer(data_view);
    g_return_if_fail(GWY_IS_PIXMAP_LAYER(layer));
    dkey = gwy_pixmap_layer_get_data_key(layer);
    g_return_if_fail(dkey && dkey[0] == '/' && g_ascii_isdigit(dkey[1]));
    id = atoi(dkey + 1);

    g_snprintf(key, sizeof(key), "/%d/base/min", id);
    tool->key_min = g_quark_from_string(key);
    g_snprintf(key, sizeof(key), "/%d/base/max", id);
    tool->key_max = g_quark_from_string(key);
}

static void
gwy_tool_color_range_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolColorRange *tool = GWY_TOOL_COLOR_RANGE(plain_tool);

    update_selected_rectangle(tool);
    gwy_tool_color_range_update_histogram(tool);
}

static void
gwy_tool_color_range_mask_changed(GwyPlainTool *plain_tool)
{
    GwyToolColorRange *tool = GWY_TOOL_COLOR_RANGE(plain_tool);
    gboolean have_mask = !!plain_tool->mask_field;
    gboolean range_type = gwy_tool_color_range_get_range_type(tool);
    gboolean msens = (have_mask && range_type == GWY_LAYER_BASIC_RANGE_FIXED);
    gtk_widget_set_sensitive(tool->set_to_masked, msens);
    gtk_widget_set_sensitive(tool->set_to_unmasked, msens);
}

static void
gwy_tool_color_range_selection_changed(GwyPlainTool *plain_tool,
                                       gint hint)
{
    GwyToolColorRange *tool = GWY_TOOL_COLOR_RANGE(plain_tool);
    GwyLayerBasicRangeType range_type;
    gboolean is_selected = FALSE;
    gdouble range[4];

    g_return_if_fail(hint <= 0);

    update_selected_rectangle(tool);
    if (plain_tool->selection)
        is_selected = gwy_selection_get_data(plain_tool->selection, NULL);

    range_type = gwy_tool_color_range_get_range_type(tool);
    if (range_type != GWY_LAYER_BASIC_RANGE_FIXED)
        return;

    if (!tool->programmatic_update)
        tool->range_source = USE_SELECTION;

    gwy_debug("set min max after area selection");
    gwy_tool_color_range_set_min_max(tool);
    if (!tool->programmatic_update) {
        tool->programmatic_update = TRUE;
        if (is_selected) {
            gwy_tool_color_range_get_min_max(tool, range);
            gwy_selection_set_object(tool->graph_selection, 0, range);
        }
        else
            gwy_selection_clear(tool->graph_selection);
        tool->programmatic_update = FALSE;
    }
}

static void
gwy_tool_color_range_xsel_changed(GwySelection *selection,
                                  gint hint,
                                  GwyToolColorRange *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    g_return_if_fail(hint <= 0);

    if (tool->programmatic_update)
        return;

    if (gwy_selection_get_data(selection, NULL)) {
        tool->range_source = USE_HISTOGRAM;
        gwy_debug("set min max after histogram selection");
        gwy_tool_color_range_set_min_max(tool);

        /* when user begins a selection on the histogram, the selection on the
           image is now invalid, and should be removed. */
        tool->programmatic_update = TRUE;
        gwy_selection_clear(plain_tool->selection);
        tool->programmatic_update = FALSE;
    }
    else {
        tool->range_source = USE_SELECTION;
        tool->programmatic_update = TRUE;
        gwy_tool_color_range_selection_changed(GWY_PLAIN_TOOL(tool), -1);
        tool->programmatic_update = FALSE;
    }
}

/* TODO: this is not enough, we need to restore range from container.
 * add USE_CONTAINER source type? */
static void
gwy_tool_color_range_type_changed(GtkWidget *radio,
                                  GwyToolColorRange *tool)
{
    GwyLayerBasicRangeType range_type, old_mode;
    GwyPlainTool *plain_tool;
    gboolean fixed_sens = FALSE;

    old_mode = gwy_tool_color_range_get_range_type(tool);
    if (radio) {
        range_type = gwy_radio_button_get_value(radio);
        if (old_mode == range_type)
            return;
    }
    else
        range_type = old_mode;  /* Initialization */

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (plain_tool->container) {
        fixed_sens = (range_type == GWY_LAYER_BASIC_RANGE_FIXED);
        gwy_tool_color_range_set_range_type(tool, range_type);
        if (fixed_sens && !tool->data_switch) {
            gwy_debug("set min max after range type change");
            gwy_tool_color_range_set_min_max(tool);
        }
    }
    gtk_widget_set_sensitive(GTK_WIDGET(tool->histogram), fixed_sens);
    gtk_widget_set_sensitive(GTK_WIDGET(tool->spinmin), fixed_sens);
    gtk_widget_set_sensitive(GTK_WIDGET(tool->spinmax), fixed_sens);
    gtk_widget_set_sensitive(GTK_WIDGET(tool->invert), fixed_sens);

    old_mode = -1;
    gwy_container_gis_enum_by_name(gwy_app_settings_get(),
                                   APP_RANGE_KEY, &old_mode);
    gtk_widget_set_sensitive(tool->is_default, old_mode != range_type);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->is_default),
                                 old_mode == range_type);
    gwy_tool_color_range_mask_changed(plain_tool);
}

static void
gwy_tool_color_range_set_default_mode(GtkToggleButton *check,
                                      GwyToolColorRange *tool)
{
    if (!gtk_toggle_button_get_active(check))
        return;

    gwy_container_set_enum_by_name(gwy_app_settings_get(), APP_RANGE_KEY,
                                   gwy_tool_color_range_get_range_type(tool));
    /* This might be a bit silly.  However unchecking the check box has not
     * defined meaning, so just don't allow it. */
    gtk_widget_set_sensitive(tool->is_default, FALSE);
}

static GwyLayerBasicRangeType
gwy_tool_color_range_get_range_type(GwyToolColorRange *tool)
{
    GwyLayerBasicRangeType range_type = GWY_LAYER_BASIC_RANGE_FULL;
    GwyPlainTool *plain_tool;
    GwyPixmapLayer *layer;

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (plain_tool->data_view) {
        layer = gwy_data_view_get_base_layer(plain_tool->data_view);
        range_type = gwy_layer_basic_get_range_type(GWY_LAYER_BASIC(layer));
    }
    else {
        gwy_container_gis_enum_by_name(gwy_app_settings_get(),
                                       APP_RANGE_KEY, &range_type);
    }

    return range_type;
}

static void
gwy_tool_color_range_set_range_type(GwyToolColorRange *tool,
                                    GwyLayerBasicRangeType range_type)
{
    GwyPlainTool *plain_tool;
    GwyPixmapLayer *layer;
    const gchar *key;
    gchar buf[32];

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->data_view);

    layer = gwy_data_view_get_base_layer(plain_tool->data_view);
    key = gwy_layer_basic_get_range_type_key(GWY_LAYER_BASIC(layer));
    if (!key) {
        g_warning("Setting range type key.  This should be done by the app.");

        g_snprintf(buf, sizeof(buf), "/%d/base", plain_tool->id);
        gwy_layer_basic_set_min_max_key(GWY_LAYER_BASIC(layer), buf);
        strncat(buf, "/range-type", sizeof(buf)-1);
        gwy_layer_basic_set_range_type_key(GWY_LAYER_BASIC(layer), buf);
        key = buf;
    }
    gwy_container_set_enum_by_name(plain_tool->container, key, range_type);
}

static void
gwy_tool_color_range_get_min_max(GwyToolColorRange *tool,
                                 gdouble *selection)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->data_view && plain_tool->data_field);

    selection[0] = gwy_data_field_get_min(plain_tool->data_field);
    gwy_container_gis_double(plain_tool->container,
                             tool->key_min, &selection[0]);

    selection[1] = gwy_data_field_get_max(plain_tool->data_field);
    gwy_container_gis_double(plain_tool->container,
                             tool->key_max, &selection[1]);
}

static void
gwy_tool_color_range_set_min_max(GwyToolColorRange *tool)
{
    GwyPlainTool *plain_tool;
    const GwySIValueFormat *vf;
    gboolean clear = FALSE;
    gdouble sel[2];
    gint col, row, w, h;
    gchar buf[64];

    if (tool->data_switch)
        return;

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (!plain_tool->container) {
        gtk_label_set_text(tool->min, NULL);
        gtk_label_set_text(tool->max, NULL);
        return;
    }

    switch (tool->range_source) {
        case USE_SELECTION:
        gwy_debug("source: area selection");
        col = tool->isel[0];
        row = tool->isel[1];
        w = tool->isel[2]+1 - tool->isel[0];
        h = tool->isel[3]+1 - tool->isel[1];
        if (!plain_tool->selection
            || !gwy_selection_get_data(plain_tool->selection, NULL)
            || w <= 1 || h <= 1) {
            clear = TRUE;
            break;
        }
        gwy_data_field_area_get_min_max_mask(plain_tool->data_field,
                                             NULL, GWY_MASK_IGNORE,
                                             col, row, w, h,
                                             &sel[0], &sel[1]);
        break;

        case USE_HISTOGRAM:
        gwy_debug("source: histogram");
        if (!gwy_selection_get_object(tool->graph_selection, 0, sel)
            || sel[0] == sel[1])
            clear = TRUE;
        break;

        default:
        g_return_if_reached();
        break;
    }
    gwy_debug("[%g, %g]", sel[0], sel[1]);

    if (clear) {
        gwy_container_remove(plain_tool->container, tool->key_min);
        gwy_container_remove(plain_tool->container, tool->key_max);
        gwy_data_field_get_min_max(plain_tool->data_field, &sel[0], &sel[1]);
    }
    else {
        gwy_container_set_double(plain_tool->container, tool->key_min, sel[0]);
        gwy_container_set_double(plain_tool->container, tool->key_max, sel[1]);
    }

    if (!tool->programmatic_update) {
        tool->programmatic_update = TRUE;
        vf = plain_tool->value_format;
        g_snprintf(buf, sizeof(buf), "%s%s",
                   *vf->units ? " " : "", vf->units);
        gtk_label_set_markup(tool->min, buf);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(tool->spinmin),
                                  sel[0]/vf->magnitude);

        g_snprintf(buf, sizeof(buf), "%s%s",
                   *vf->units ? " " : "", vf->units);
        gtk_label_set_markup(tool->max, buf);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(tool->spinmax),
                                  sel[1]/vf->magnitude);
        tool->programmatic_update = FALSE;
    }
}

static void
gwy_tool_color_range_update_fullrange(GwyToolColorRange *tool)
{
    GwyPlainTool *plain_tool;
    const GwySIValueFormat *vf;
    gdouble min, max;
    gchar buf[64];

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (!plain_tool->container) {
        gtk_label_set_text(tool->datamin, NULL);
        gtk_label_set_text(tool->datamax, NULL);
        return;
    }

    gwy_data_field_get_min_max(plain_tool->data_field, &min, &max);

    vf = plain_tool->value_format;
    g_snprintf(buf, sizeof(buf), "%.*f%s%s",
               vf->precision, min/vf->magnitude,
               *vf->units ? " " : "", vf->units);
    gtk_label_set_markup(tool->datamin, buf);
    g_snprintf(buf, sizeof(buf), "%.*f%s%s",
               vf->precision, max/vf->magnitude,
               *vf->units ? " " : "", vf->units);
    gtk_label_set_markup(tool->datamax, buf);
}

static void
gwy_tool_color_range_update_histogram(GwyToolColorRange *tool)
{
    GwyPlainTool *plain_tool;
    GwyGraphCurveModel *cmodel;

    plain_tool = GWY_PLAIN_TOOL(tool);
    cmodel = gwy_graph_model_get_curve(tool->histogram_model, 0);
    if (!plain_tool->data_field) {
        gdouble data[2] = { 0.0, 0.0 };

        gwy_graph_curve_model_set_data(cmodel, data, data, G_N_ELEMENTS(data));
        return;
    }

    gwy_data_field_dh(plain_tool->data_field, tool->heightdist, 0);
    /* rescale to sqrt to make more readable  */
    gwy_data_line_sqrt(tool->heightdist);

    gwy_graph_curve_model_set_data_from_dataline(cmodel, tool->heightdist,
                                                 0, 0);
}

static void
gwy_tool_color_range_spin_changed(GwyToolColorRange *tool)
{
    GwyPlainTool *plain_tool;
    gdouble sel[2];
    const GwySIValueFormat *vf;

    if (tool->programmatic_update)
        return;

    plain_tool = GWY_PLAIN_TOOL(tool);
    vf = plain_tool->value_format;

    sel[0] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(tool->spinmin));
    sel[0] *= vf->magnitude;
    gwy_container_set_double(plain_tool->container,
                             tool->key_min, sel[0]);

    sel[1] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(tool->spinmax));
    sel[1] *= vf->magnitude;
    gwy_container_set_double(plain_tool->container,
                             tool->key_max, sel[1]);

    tool->programmatic_update = TRUE;
    gwy_selection_set_data(tool->graph_selection, 1, sel);
    tool->programmatic_update = FALSE;
}

static void
set_range_using_mask(GwyToolColorRange *tool, GwyMaskingType masking)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyDataField *dfield = plain_tool->data_field,
                 *mask = plain_tool->mask_field;
    gdouble sel[2];

    if (!dfield || !mask)
        return;

    gwy_data_field_area_get_min_max_mask(dfield, mask, masking,
                                         0, 0, dfield->xres, dfield->yres,
                                         &sel[0], &sel[1]);
    /* No valid pixels? */
    if (sel[1] < sel[0])
        gwy_data_field_get_min_max(dfield, &sel[0], &sel[1]);

    gwy_container_set_double(plain_tool->container, tool->key_min, sel[0]);
    gwy_container_set_double(plain_tool->container, tool->key_max, sel[1]);

    tool->programmatic_update = TRUE;
    gwy_selection_set_data(tool->graph_selection, 1, sel);
    tool->programmatic_update = FALSE;
}

static void
set_range_to_masked(GwyToolColorRange *tool)
{
    set_range_using_mask(tool, GWY_MASK_INCLUDE);
}

static void
set_range_to_unmasked(GwyToolColorRange *tool)
{
    set_range_using_mask(tool, GWY_MASK_EXCLUDE);
}

static void
invert_mapping(GwyToolColorRange *tool)
{
    gdouble min, max;

    if (!GWY_PLAIN_TOOL(tool)->data_field)
        return;

    min = gtk_spin_button_get_value(GTK_SPIN_BUTTON(tool->spinmin));
    max = gtk_spin_button_get_value(GTK_SPIN_BUTTON(tool->spinmax));

    tool->programmatic_update = TRUE;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tool->spinmin), max);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tool->spinmax), min);
    tool->programmatic_update = FALSE;

    gwy_tool_color_range_spin_changed(tool);
}

static void
update_selected_rectangle(GwyToolColorRange *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwySelection *selection = plain_tool->selection;
    GwyDataField *field = plain_tool->data_field;
    gint n;

    n = selection ? gwy_selection_get_data(selection, NULL) : 0;
    gwy_rect_selection_labels_fill(tool->rlabels, n == 1 ? selection : NULL,
                                   field, NULL, tool->isel);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

