/*
 *  $Id: maskedit.c 24706 2022-03-21 17:00:25Z yeti-dn $
 *  Copyright (C) 2003-2017 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/elliptic.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>

#define GWY_TYPE_TOOL_MASK_EDITOR            (gwy_tool_mask_editor_get_type())
#define GWY_TOOL_MASK_EDITOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_MASK_EDITOR, GwyToolMaskEditor))
#define GWY_IS_TOOL_MASK_EDITOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_MASK_EDITOR))
#define GWY_TOOL_MASK_EDITOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_MASK_EDITOR, GwyToolMaskEditorClass))

enum {
    SENS_DATA   = 1 << 0,
    SENS_MASK   = 1 << 1,
};

typedef enum {
    MASK_EDIT_STYLE_SHAPES  = 0,
    MASK_EDIT_STYLE_DRAWING = 1,
    MASK_NSTYLES
} MaskEditStyle;

typedef enum {
    MASK_EDIT_SET       = 0,
    MASK_EDIT_ADD       = 1,
    MASK_EDIT_REMOVE    = 2,
    MASK_EDIT_INTERSECT = 3,
    MASK_NMODES
} MaskEditMode;

typedef enum {
    MASK_SHAPE_RECTANGLE  = 0,
    MASK_SHAPE_ELLIPSE    = 1,
    MASK_SHAPE_LINE       = 2,
    MASK_NSHAPES
} MaskEditShape;

typedef enum {
    MASK_TOOL_PAINT_DRAW  = 0,
    MASK_TOOL_PAINT_ERASE = 1,
    MASK_TOOL_FILL_DRAW   = 2,
    MASK_TOOL_FILL_ERASE  = 3,
    MASK_NTOOLS
} MaskEditTool;

typedef struct _GwyToolMaskEditor      GwyToolMaskEditor;
typedef struct _GwyToolMaskEditorClass GwyToolMaskEditorClass;

typedef void (*FieldFillFunc)(GwyDataField*, gint, gint, gint, gint, gdouble);

typedef struct {
    MaskEditStyle style;
    MaskEditMode mode;
    MaskEditShape shape;
    MaskEditTool tool;
    GwyDistanceTransformType dist_type;
    gint32 gsamount;
    gint32 radius;
    gboolean from_border;
    gboolean prevent_merge;
    gboolean fill_nonsimple;
} ToolArgs;

struct _GwyToolMaskEditor {
    GwyPlainTool parent_instance;

    ToolArgs args;

    GwySensitivityGroup *sensgroup;
    GSList *style;
    GSList *mode;
    GSList *shape;
    GSList *tool;

    GtkObject *radius;
    GtkObject *gsamount;
    GtkWidget *dist_type;
    GtkWidget *from_border;
    GtkWidget *prevent_merge;
    GtkWidget *fill_nonsimple;

    gboolean in_setup;

    /* paintrbrush only */
    gboolean drawing_started;
    gint oldisel[2];

    /* potential class data */
    GType layer_types[MASK_NSHAPES];
    GType layer_type_point;
};

struct _GwyToolMaskEditorClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType gwy_tool_mask_editor_get_type              (void)                      G_GNUC_CONST;
static void  gwy_tool_mask_editor_finalize              (GObject *object);
static void  gwy_tool_mask_editor_init_dialog           (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_data_switched         (GwyTool *gwytool,
                                                         GwyDataView *data_view);
static void  gwy_tool_mask_editor_mask_changed          (GwyPlainTool *plain_tool);
static void  gwy_tool_mask_editor_style_changed         (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_mode_changed          (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_shape_changed         (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_tool_changed          (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_setup_layer           (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_radius_changed        (GtkAdjustment *adj,
                                                         GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_gsamount_changed      (GtkAdjustment *adj,
                                                         GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_dist_type_changed     (GtkComboBox *combo,
                                                         GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_from_border_changed   (GtkToggleButton *toggle,
                                                         GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_prevent_merge_changed (GtkToggleButton *toggle,
                                                         GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_fill_nonsimple_changed(GtkToggleButton *toggle,
                                                         GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_invert                (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_remove                (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_fill                  (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_grow                  (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_shrink                (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_fill_voids            (GwyToolMaskEditor *tool);
static void  gwy_tool_mask_editor_selection_finished    (GwyPlainTool *plain_tool);
static void  gwy_tool_mask_editor_bucket_fill           (GwyToolMaskEditor *tool,
                                                         gint j,
                                                         gint i);
static void  gwy_tool_mask_editor_selection_changed     (GwyPlainTool *plain_tool,
                                                         gint hint);
static void  gwy_tool_mask_editor_save_args             (GwyToolMaskEditor *tool);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Mask editor tool, allows interactive modification of parts "
       "of the mask."),
    "Yeti <yeti@gwyddion.net>",
    "3.13",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

static const gchar *const shape_selection_names[MASK_NSHAPES] = {
    "rectangle", "ellipse", "line"
};

static const gchar dist_type_key[]      = "/module/maskeditor/dist_type";
static const gchar fill_nonsimple_key[] = "/module/maskeditor/fill_nonsimple";
static const gchar from_border_key[]    = "/module/maskeditor/from_border";
static const gchar gsamount_key[]       = "/module/maskeditor/gsamount";
static const gchar mode_key[]           = "/module/maskeditor/mode";
static const gchar prevent_merge_key[]  = "/module/maskeditor/prevent_merge";
static const gchar radius_key[]         = "/module/maskeditor/radius";
static const gchar shape_key[]          = "/module/maskeditor/shape";
static const gchar style_key[]          = "/module/maskeditor/style";
static const gchar tool_key[]           = "/module/maskeditor/tool";

static const ToolArgs default_args = {
    MASK_EDIT_STYLE_SHAPES,
    MASK_EDIT_SET,
    MASK_SHAPE_RECTANGLE,
    MASK_TOOL_PAINT_DRAW,
    GWY_DISTANCE_TRANSFORM_EUCLIDEAN,
    5,
    1,
    FALSE,
    TRUE,
    FALSE,
};

GWY_MODULE_QUERY2(module_info, maskedit)

G_DEFINE_TYPE(GwyToolMaskEditor, gwy_tool_mask_editor, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_MASK_EDITOR);

    return TRUE;
}

static void
gwy_tool_mask_editor_class_init(GwyToolMaskEditorClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_mask_editor_finalize;

    tool_class->stock_id = GWY_STOCK_MASK_EDITOR;
    tool_class->title = _("Mask Editor");
    tool_class->tooltip = _("Edit mask");
    tool_class->prefix = "/module/maskeditor";
    tool_class->data_switched = gwy_tool_mask_editor_data_switched;

    ptool_class->mask_changed = gwy_tool_mask_editor_mask_changed;
    ptool_class->selection_changed = gwy_tool_mask_editor_selection_changed;
    ptool_class->selection_finished = gwy_tool_mask_editor_selection_finished;
}

static void
gwy_tool_mask_editor_finalize(GObject *object)
{
    gwy_tool_mask_editor_save_args(GWY_TOOL_MASK_EDITOR(object));
    G_OBJECT_CLASS(gwy_tool_mask_editor_parent_class)->finalize(object);
}

static void
gwy_tool_mask_editor_init(GwyToolMaskEditor *tool)
{
    static const gchar *const shape_layer_types[MASK_NSHAPES] = {
        "GwyLayerRectangle", "GwyLayerEllipse", "GwyLayerLine",
    };

    GwyPlainTool *plain_tool;
    GwyContainer *settings;
    guint i;

    tool->in_setup = TRUE;

    plain_tool = GWY_PLAIN_TOOL(tool);
    for (i = 0; i < MASK_NSHAPES; i++) {
        tool->layer_types[i]
            = gwy_plain_tool_check_layer_type(plain_tool,
                                              shape_layer_types[i]);
        if (!tool->layer_types[i])
            return;
    }
    tool->layer_type_point = gwy_plain_tool_check_layer_type(plain_tool,
                                                             "GwyLayerPoint");
    if (!tool->layer_type_point)
        return;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_enum_by_name(settings, style_key,
                                   &tool->args.style);
    gwy_container_gis_enum_by_name(settings, mode_key,
                                   &tool->args.mode);
    gwy_container_gis_enum_by_name(settings, shape_key,
                                   &tool->args.shape);
    gwy_container_gis_enum_by_name(settings, tool_key,
                                   &tool->args.tool);
    gwy_container_gis_enum_by_name(settings, dist_type_key,
                                   &tool->args.dist_type);
    gwy_container_gis_int32_by_name(settings, radius_key,
                                    &tool->args.radius);
    gwy_container_gis_int32_by_name(settings, gsamount_key,
                                    &tool->args.gsamount);
    gwy_container_gis_boolean_by_name(settings, from_border_key,
                                      &tool->args.from_border);
    gwy_container_gis_boolean_by_name(settings, prevent_merge_key,
                                      &tool->args.prevent_merge);
    gwy_container_gis_boolean_by_name(settings, fill_nonsimple_key,
                                      &tool->args.fill_nonsimple);

    tool->args.style = MIN(tool->args.style, MASK_NSTYLES-1);
    tool->args.mode = MIN(tool->args.mode, MASK_NMODES-1);
    tool->args.shape = MIN(tool->args.shape, MASK_NSHAPES-1);
    tool->args.tool = MIN(tool->args.tool, MASK_NTOOLS-1);
    tool->args.dist_type
        = gwy_enum_sanitize_value(tool->args.dist_type,
                                  GWY_TYPE_DISTANCE_TRANSFORM_TYPE);

    if (tool->args.style == MASK_EDIT_STYLE_SHAPES)
        gwy_plain_tool_connect_selection(plain_tool,
                                         tool->layer_types[tool->args.shape],
                                         shape_selection_names[tool->args.shape]);
    else
        gwy_plain_tool_connect_selection(plain_tool,
                                         tool->layer_type_point, "pointer");

    gwy_tool_mask_editor_init_dialog(tool);
    tool->in_setup = FALSE;
}

static void
gwy_tool_mask_editor_init_dialog(GwyToolMaskEditor *tool)
{
    static struct {
        guint type;
        const gchar *stock_id;
        const gchar *text;
    }
    const modes[] = {
        {
            MASK_EDIT_SET,
            GWY_STOCK_MASK_SET,
            N_("Set mask to selection"),
        },
        {
            MASK_EDIT_ADD,
            GWY_STOCK_MASK_ADD,
            N_("Add selection to mask"),
        },
        {
            MASK_EDIT_REMOVE,
            GWY_STOCK_MASK_SUBTRACT,
            N_("Subtract selection from mask"),
        },
        {
            MASK_EDIT_INTERSECT,
            GWY_STOCK_MASK_INTERSECT,
            N_("Intersect selection with mask"),
        },
    },
    shapes[] = {
        {
            MASK_SHAPE_RECTANGLE,
            GWY_STOCK_MASK,
            N_("Rectangular shapes"),
        },
        {
            MASK_SHAPE_ELLIPSE,
            GWY_STOCK_MASK_CIRCLE,
            N_("Elliptic shapes"),
        },
        {
            MASK_SHAPE_LINE,
            GWY_STOCK_MASK_LINE,
            N_("Thin lines"),
        },
    },
    tools[] = {
        {
            MASK_TOOL_PAINT_DRAW,
            GWY_STOCK_MASK_PAINT_DRAW,
            N_("Freehand mask drawing"),
        },
        {
            MASK_TOOL_PAINT_ERASE,
            GWY_STOCK_MASK_PAINT_ERASE,
            N_("Freehand mask erasing"),
        },
        {
            MASK_TOOL_FILL_DRAW,
            GWY_STOCK_MASK_FILL_DRAW,
            N_("Fill continuous empty areas with mask"),
        },
        {
            MASK_TOOL_FILL_ERASE,
            GWY_STOCK_MASK_FILL_ERASE,
            N_("Erase continuous parts of mask"),
        },
    };

    GtkRadioButton *group;
    GtkSizeGroup *sizegroup, *labelsize;
    GtkDialog *dialog;
    GtkTable *table;
    GtkWidget *image, *label, *button;
    GtkBox *hbox;
    gint i, row;
    MaskEditStyle style = tool->args.style;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);
    sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
    labelsize = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    tool->sensgroup = gwy_sensitivity_group_new();

    table = GTK_TABLE(gtk_table_new(15, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), GTK_WIDGET(table),
                       FALSE, FALSE, 0);
    row = 0;

    /* Editor */
    label = gwy_label_new_header(_("Editor"));
    gtk_table_attach(table, label, 0, 4, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    /* Shapes */
    button = gtk_radio_button_new_with_mnemonic(NULL, _("_Shapes"));
    tool->style = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    gwy_radio_button_set_value(button, MASK_EDIT_STYLE_SHAPES);
    gtk_table_attach(table, button, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_tool_mask_editor_style_changed),
                             tool);
    row++;

    /* Mode */
    hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_table_attach(table, GTK_WIDGET(hbox),
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Mode:"));
    gtk_size_group_add_widget(labelsize, label);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(hbox, label, FALSE, TRUE, 4);

    group = NULL;
    for (i = 0; i < G_N_ELEMENTS(modes); i++) {
        button = gtk_radio_button_new_from_widget(group);
        g_object_set(button, "draw-indicator", FALSE, NULL);
        image = gtk_image_new_from_stock(modes[i].stock_id,
                                         GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add(GTK_CONTAINER(button), image);
        gwy_radio_button_set_value(button, modes[i].type);
        gtk_box_pack_start(hbox, button, FALSE, FALSE, 0);
        gtk_widget_set_tooltip_text(button, gettext(modes[i].text));
        g_signal_connect_swapped(button, "clicked",
                                 G_CALLBACK(gwy_tool_mask_editor_mode_changed),
                                 tool);
        if (!group)
            group = GTK_RADIO_BUTTON(button);
    }
    tool->mode = gtk_radio_button_get_group(group);
    gwy_radio_buttons_set_current(tool->mode, tool->args.mode);
    row++;

    /* Shape */
    hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_table_attach(table, GTK_WIDGET(hbox),
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Shape:"));
    gtk_size_group_add_widget(labelsize, label);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(hbox, label, FALSE, TRUE, 4);

    group = NULL;
    for (i = 0; i < G_N_ELEMENTS(shapes); i++) {
        button = gtk_radio_button_new_from_widget(group);
        g_object_set(button, "draw-indicator", FALSE, NULL);
        image = gtk_image_new_from_stock(shapes[i].stock_id,
                                         GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add(GTK_CONTAINER(button), image);
        gwy_radio_button_set_value(button, shapes[i].type);
        gtk_box_pack_start(hbox, button, FALSE, FALSE, 0);
        gtk_widget_set_tooltip_text(button, gettext(shapes[i].text));
        g_signal_connect_swapped(button, "clicked",
                                 G_CALLBACK(gwy_tool_mask_editor_shape_changed),
                                 tool);
        if (!group)
            group = GTK_RADIO_BUTTON(button);
    }
    tool->shape = gtk_radio_button_get_group(group);
    gwy_radio_buttons_set_current(tool->shape, tool->args.shape);
    row++;

    /* Drawing Tools */
    gtk_table_set_row_spacing(table, row-1, 8);
    button = gtk_radio_button_new_with_mnemonic(tool->style,
                                                _("_Drawing Tools"));
    tool->style = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    gwy_radio_button_set_value(button, MASK_EDIT_STYLE_DRAWING);
    gtk_table_attach(table, button, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_tool_mask_editor_style_changed),
                             tool);
    row++;

    /* Tool */
    hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_table_attach(table, GTK_WIDGET(hbox),
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Tool:"));
    gtk_size_group_add_widget(labelsize, label);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(hbox, label, FALSE, TRUE, 4);

    group = NULL;
    for (i = 0; i < G_N_ELEMENTS(tools); i++) {
        button = gtk_radio_button_new_from_widget(group);
        g_object_set(button, "draw-indicator", FALSE, NULL);
        image = gtk_image_new_from_stock(tools[i].stock_id,
                                         GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add(GTK_CONTAINER(button), image);
        gwy_radio_button_set_value(button, tools[i].type);
        gtk_box_pack_start(hbox, button, FALSE, FALSE, 0);
        gtk_widget_set_tooltip_text(button, gettext(tools[i].text));
        g_signal_connect_swapped(button, "clicked",
                                 G_CALLBACK(gwy_tool_mask_editor_tool_changed),
                                 tool);
        if (!group)
            group = GTK_RADIO_BUTTON(button);
    }
    tool->tool = gtk_radio_button_get_group(group);
    gwy_radio_buttons_set_current(tool->tool, tool->args.tool);
    row++;

    /* Radius */
    tool->radius = gtk_adjustment_new(tool->args.radius, 1.0, 15.0,
                                      1.0, 1.0, 0.0);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Radius:"), _("px"),
                            tool->radius, GWY_HSCALE_LINEAR | GWY_HSCALE_SNAP);
    gtk_size_group_add_widget(labelsize,
                              gwy_table_hscale_get_label(tool->radius));
    g_signal_connect(tool->radius, "value-changed",
                     G_CALLBACK(gwy_tool_mask_editor_radius_changed), tool);
    row++;

    /* Actions */
    gtk_table_set_row_spacing(table, row-1, 8);
    label = gwy_label_new_header(_("Actions"));
    gtk_table_attach(table, label, 0, 4, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_table_attach(table, GTK_WIDGET(hbox),
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);

    button = gwy_stock_like_button_new(_("_Invert"), GWY_STOCK_MASK_INVERT);
    gtk_size_group_add_widget(sizegroup, button);
    gwy_sensitivity_group_add_widget(tool->sensgroup, button,
                                     SENS_DATA | SENS_MASK);
    gtk_box_pack_start(hbox, button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_tool_mask_editor_invert), tool);

    button = gwy_stock_like_button_new(_("_Remove"), GWY_STOCK_MASK_REMOVE);
    gtk_size_group_add_widget(sizegroup, button);
    gwy_sensitivity_group_add_widget(tool->sensgroup, button,
                                     SENS_DATA | SENS_MASK);
    gtk_box_pack_start(hbox, button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_tool_mask_editor_remove), tool);

    button = gwy_stock_like_button_new(_("_Fill"), GWY_STOCK_MASK);
    gtk_size_group_add_widget(sizegroup, button);
    gwy_sensitivity_group_add_widget(tool->sensgroup, button, SENS_DATA);
    gtk_box_pack_start(hbox, button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_tool_mask_editor_fill), tool);

    label = gtk_label_new(NULL);
    gtk_box_pack_start(hbox, label, TRUE, TRUE, 0);
    row++;

    hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_table_attach(table, GTK_WIDGET(hbox),
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);

    button = gtk_button_new_with_mnemonic(_("Fill _Voids"));
    gtk_size_group_add_widget(sizegroup, button);
    gwy_sensitivity_group_add_widget(tool->sensgroup, button,
                                     SENS_DATA | SENS_MASK);
    gtk_box_pack_start(hbox, button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_tool_mask_editor_fill_voids), tool);

    tool->fill_nonsimple
        = gtk_check_button_new_with_mnemonic(_("Fill non-simple-connected"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->fill_nonsimple),
                                 tool->args.fill_nonsimple);
    gtk_box_pack_start(hbox, tool->fill_nonsimple, TRUE, TRUE, 0);
    g_signal_connect(tool->fill_nonsimple, "toggled",
                     G_CALLBACK(gwy_tool_mask_editor_fill_nonsimple_changed),
                     tool);
    gtk_table_set_row_spacing(table, row, 8);
    row++;

    /* Grow/Shrink */
    label = gwy_label_new_header(_("Grow/Shrink"));
    gtk_table_attach(table, label, 0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    /* Buttons */
    hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_table_attach(table, GTK_WIDGET(hbox),
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);

    button = gwy_stock_like_button_new(_("_Grow"), GWY_STOCK_MASK_GROW);
    gtk_size_group_add_widget(sizegroup, button);
    gwy_sensitivity_group_add_widget(tool->sensgroup, button,
                                     SENS_DATA | SENS_MASK);
    gtk_box_pack_start(hbox, button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_tool_mask_editor_grow), tool);

    button = gwy_stock_like_button_new(_("Shrin_k"), GWY_STOCK_MASK_SHRINK);
    gtk_size_group_add_widget(sizegroup, button);
    gwy_sensitivity_group_add_widget(tool->sensgroup, button,
                                     SENS_DATA | SENS_MASK);
    gtk_box_pack_start(hbox, button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_tool_mask_editor_shrink), tool);

    label = gtk_label_new(NULL);
    gtk_box_pack_start(hbox, label, TRUE, TRUE, 0);
    row++;

    /* Options */
    tool->gsamount = gtk_adjustment_new(tool->args.gsamount, 1.0, 256.0,
                                        1.0, 10.0, 0.0);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Amount:"), _("px"),
                            tool->gsamount, GWY_HSCALE_SQRT | GWY_HSCALE_SNAP);
    g_signal_connect(tool->gsamount, "value-changed",
                     G_CALLBACK(gwy_tool_mask_editor_gsamount_changed), tool);
    row++;

    tool->dist_type = gwy_enum_combo_box_new
                           (gwy_distance_transform_type_get_enum(), -1,
                            G_CALLBACK(gwy_tool_mask_editor_dist_type_changed),
                            tool, tool->args.dist_type, TRUE);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row++,
                            _("_Distance type:"), NULL,
                            GTK_OBJECT(tool->dist_type),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    tool->from_border
        = gtk_check_button_new_with_mnemonic(_("Shrink from _border"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->from_border),
                                 tool->args.from_border);
    gtk_table_attach(table, tool->from_border,
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(tool->from_border, "toggled",
                     G_CALLBACK(gwy_tool_mask_editor_from_border_changed),
                     tool);
    row++;

    tool->prevent_merge
        = gtk_check_button_new_with_mnemonic(_("_Prevent grain merging "
                                               "by growing"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->prevent_merge),
                                 tool->args.prevent_merge);
    gtk_table_attach(table, tool->prevent_merge,
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(tool->prevent_merge, "toggled",
                     G_CALLBACK(gwy_tool_mask_editor_prevent_merge_changed),
                     tool);
    row++;

    gwy_tool_add_hide_button(GWY_TOOL(tool), TRUE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);
    gwy_radio_buttons_set_current(tool->style, style);

    g_object_unref(sizegroup);
    g_object_unref(labelsize);
    g_object_unref(tool->sensgroup);
    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_mask_editor_data_switched(GwyTool *gwytool,
                                   GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolMaskEditor *tool;
    gboolean ignore;

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);

    tool = GWY_TOOL_MASK_EDITOR(gwytool);
    tool->in_setup = TRUE;
    GWY_TOOL_CLASS(gwy_tool_mask_editor_parent_class)->data_switched(gwytool,
                                                                     data_view);
    tool->in_setup = FALSE;

    if (ignore || plain_tool->init_failed)
        return;

    tool->in_setup = TRUE;
    gwy_tool_mask_editor_style_changed(tool);
    gwy_sensitivity_group_set_state(tool->sensgroup, SENS_DATA,
                                    data_view ? SENS_DATA : 0);
    gwy_tool_mask_editor_mask_changed(plain_tool);
    tool->in_setup = FALSE;
}

static void
gwy_tool_mask_editor_mask_changed(GwyPlainTool *plain_tool)
{
    GwyToolMaskEditor *tool;
    guint state = 0;

    tool = GWY_TOOL_MASK_EDITOR(plain_tool);
    if (plain_tool->mask_field) {
        gwy_debug("mask field exists");
        if (gwy_data_field_get_max(plain_tool->mask_field) > 0) {
            gwy_debug("mask field is nonempty");
            state = SENS_MASK;
        }
    }

    gwy_sensitivity_group_set_state(tool->sensgroup, SENS_MASK, state);
}

static void
gwy_tool_mask_editor_style_changed(GwyToolMaskEditor *tool)
{
    MaskEditStyle style;

    style = tool->args.style = gwy_radio_buttons_get_current(tool->style);
    if (style == MASK_EDIT_STYLE_SHAPES) {
        /* Force layer setup */
        tool->args.shape = -1;
        gwy_tool_mask_editor_shape_changed(tool);
    }
    else {
        tool->in_setup = TRUE;
        gwy_plain_tool_connect_selection(GWY_PLAIN_TOOL(tool),
                                         tool->layer_type_point, "pointer");
        if (GWY_PLAIN_TOOL(tool)->selection)
            gwy_selection_clear(GWY_PLAIN_TOOL(tool)->selection);
        tool->in_setup = FALSE;
        gwy_tool_mask_editor_setup_layer(tool);
    }
}

static void
gwy_tool_mask_editor_mode_changed(GwyToolMaskEditor *tool)
{
    tool->args.mode = gwy_radio_buttons_get_current(tool->mode);
    gwy_radio_buttons_set_current(tool->style, MASK_EDIT_STYLE_SHAPES);
}

static void
gwy_tool_mask_editor_shape_changed(GwyToolMaskEditor *tool)
{
    MaskEditShape shape;

    shape = gwy_radio_buttons_get_current(tool->shape);
    tool->args.shape = shape;
    gwy_radio_buttons_set_current(tool->style, MASK_EDIT_STYLE_SHAPES);
    gwy_plain_tool_connect_selection(GWY_PLAIN_TOOL(tool),
                                     tool->layer_types[tool->args.shape],
                                     shape_selection_names[tool->args.shape]);
    gwy_tool_mask_editor_setup_layer(tool);
}

static void
gwy_tool_mask_editor_setup_layer(GwyToolMaskEditor *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (!plain_tool->data_view)
        return;

    if (tool->args.style == MASK_EDIT_STYLE_SHAPES) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_types[tool->args.shape],
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        if (tool->args.shape == MASK_SHAPE_LINE)
            g_object_set(plain_tool->layer,
                         "line-numbers", FALSE,
                         "thickness", 1,
                         NULL);
    }
    else {
        gwy_object_set_or_reset(plain_tool->layer, tool->layer_type_point,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        if (tool->args.tool == MASK_TOOL_PAINT_DRAW
            || tool->args.tool == MASK_TOOL_PAINT_ERASE)
            g_object_set(plain_tool->layer,
                         "marker-radius", tool->args.radius,
                         NULL);
        else
            g_object_set(plain_tool->layer,
                         "draw-marker", FALSE,
                         NULL);
    }

    gwy_selection_set_max_objects(plain_tool->selection, 1);
}

static void
gwy_tool_mask_editor_tool_changed(GwyToolMaskEditor *tool)
{
    tool->args.tool = gwy_radio_buttons_get_current(tool->tool);
    gwy_radio_buttons_set_current(tool->style, MASK_EDIT_STYLE_DRAWING);
    gwy_tool_mask_editor_setup_layer(tool);
}

static void
gwy_tool_mask_editor_radius_changed(GtkAdjustment *adj,
                                    GwyToolMaskEditor *tool)
{
    tool->args.radius = gwy_adjustment_get_int(adj);
    gwy_radio_buttons_set_current(tool->style, MASK_EDIT_STYLE_DRAWING);
    if (tool->args.style == MASK_EDIT_STYLE_DRAWING
        && (tool->args.tool == MASK_TOOL_PAINT_DRAW
            || tool->args.tool == MASK_TOOL_PAINT_ERASE)) {
        GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

        if (plain_tool->data_view && plain_tool->layer) {
            g_object_set(plain_tool->layer,
                         "marker-radius", tool->args.radius,
                         NULL);
        }
    }
}

static void
gwy_tool_mask_editor_gsamount_changed(GtkAdjustment *adj,
                                      GwyToolMaskEditor *tool)
{
    tool->args.gsamount = gwy_adjustment_get_int(adj);
}

static void
gwy_tool_mask_editor_dist_type_changed(GtkComboBox *combo,
                                       GwyToolMaskEditor *tool)
{
    tool->args.dist_type = gwy_enum_combo_box_get_active(combo);
}

static void
gwy_tool_mask_editor_from_border_changed(GtkToggleButton *toggle,
                                         GwyToolMaskEditor *tool)
{
    tool->args.from_border = gtk_toggle_button_get_active(toggle);
}

static void
gwy_tool_mask_editor_prevent_merge_changed(GtkToggleButton *toggle,
                                           GwyToolMaskEditor *tool)
{
    tool->args.prevent_merge = gtk_toggle_button_get_active(toggle);
}

static void
gwy_tool_mask_editor_fill_nonsimple_changed(GtkToggleButton *toggle,
                                            GwyToolMaskEditor *tool)
{
    tool->args.fill_nonsimple = gtk_toggle_button_get_active(toggle);
}

static GwyDataField*
gwy_tool_mask_editor_maybe_add_mask(GwyPlainTool *plain_tool,
                                    GQuark quark)
{
    GwyDataField *mfield;

    if (!(mfield = plain_tool->mask_field)) {
        mfield = gwy_data_field_new_alike(plain_tool->data_field, TRUE);
        gwy_container_set_object(plain_tool->container, quark, mfield);
        g_object_unref(mfield);
    }
    return mfield;
}

static void
gwy_tool_mask_editor_invert(GwyToolMaskEditor *tool)
{
    GwyPlainTool *plain_tool;
    GQuark quark;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->mask_field);

    quark = gwy_app_get_mask_key_for_id(plain_tool->id);
    gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
    gwy_data_field_grains_invert(plain_tool->mask_field);
    gwy_data_field_data_changed(plain_tool->mask_field);
    gwy_tool_mask_editor_save_args(tool);
    gwy_plain_tool_log_add(plain_tool);
}

static void
gwy_tool_mask_editor_remove(GwyToolMaskEditor *tool)
{
    GwyPlainTool *plain_tool;
    GQuark quark;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->mask_field);

    quark = gwy_app_get_mask_key_for_id(plain_tool->id);
    gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
    gwy_container_remove(plain_tool->container, quark);
    gwy_tool_mask_editor_save_args(tool);
    gwy_plain_tool_log_add(plain_tool);
}

static void
gwy_tool_mask_editor_fill(GwyToolMaskEditor *tool)
{
    GwyPlainTool *plain_tool;
    GwyDataField *mfield;
    GQuark quark;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->data_field);

    quark = gwy_app_get_mask_key_for_id(plain_tool->id);
    gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
    mfield = gwy_tool_mask_editor_maybe_add_mask(plain_tool, quark);
    gwy_data_field_fill(mfield, 1.0);
    gwy_data_field_data_changed(mfield);
    gwy_tool_mask_editor_save_args(tool);
    gwy_plain_tool_log_add(plain_tool);
}

static void
gwy_tool_mask_editor_grow(GwyToolMaskEditor *tool)
{
    GwyPlainTool *plain_tool;
    GQuark quark;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->mask_field);

    quark = gwy_app_get_mask_key_for_id(plain_tool->id);
    gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
    gwy_data_field_grains_grow(plain_tool->mask_field, tool->args.gsamount,
                               tool->args.dist_type, tool->args.prevent_merge);
    gwy_data_field_data_changed(plain_tool->mask_field);
    gwy_tool_mask_editor_save_args(tool);
    gwy_plain_tool_log_add(plain_tool);
}

static void
gwy_tool_mask_editor_shrink(GwyToolMaskEditor *tool)
{
    GwyPlainTool *plain_tool;
    GQuark quark;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->mask_field);

    quark = gwy_app_get_mask_key_for_id(plain_tool->id);
    gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
    gwy_data_field_grains_shrink(plain_tool->mask_field, tool->args.gsamount,
                                 tool->args.dist_type, tool->args.from_border);
    gwy_data_field_data_changed(plain_tool->mask_field);
    gwy_tool_mask_editor_save_args(tool);
    gwy_plain_tool_log_add(plain_tool);
}

static void
gwy_tool_mask_editor_fill_voids(GwyToolMaskEditor *tool)
{
    GwyPlainTool *plain_tool;
    GQuark quark;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->mask_field);

    quark = gwy_app_get_mask_key_for_id(plain_tool->id);
    gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
    gwy_data_field_fill_voids(plain_tool->mask_field,
                              tool->args.fill_nonsimple);
    gwy_data_field_data_changed(plain_tool->mask_field);
    gwy_tool_mask_editor_save_args(tool);
    gwy_plain_tool_log_add(plain_tool);
}

static void
gwy_data_field_linear_area_fill(GwyDataField *dfield,
                                gint col, gint row,
                                gint width, gint height,
                                gdouble value)
{
    gint i, q, xres;
    gdouble *d;

    xres = gwy_data_field_get_xres(dfield);
    d = gwy_data_field_get_data(dfield);
    if (ABS(height) >= width) {
        q = width/2;
        if (height > 0) {
            for (i = 0; i < height; i++) {
                d[(row + i)*xres + col + q/height] = value;
                q += width;
            }
        }
        else {
            height = ABS(height);
            for (i = 0; i < height; i++) {
                d[(row - i)*xres + col + q/height] = value;
                q += width;
            }
        }
    }
    else {
        q = height/2;
        for (i = 0; i < width; i++) {
            d[(row + q/width)*xres + col + i] = value;
            q += height;
        }
    }
    gwy_data_field_invalidate(dfield);
}

static gint
gwy_data_field_get_linear_area_size(gint width, gint height)
{
    return MAX(width, ABS(height));
}

static gint
gwy_data_field_linear_area_extract(GwyDataField *dfield,
                                   gint col, gint row,
                                   gint width, gint height,
                                   gdouble *data)
{
    gint i, n, q, xres;
    gdouble *d;

    /* FIXME: We do not handle lines sticking out, nor wide lines */
    xres = gwy_data_field_get_xres(dfield);
    d = gwy_data_field_get_data(dfield);
    n = 0;
    if (ABS(height) >= width) {
        q = width/2;
        if (height > 0) {
            for (i = 0; i < height; i++) {
                data[n++] = d[(row + i)*xres + col + q/height];
                q += width;
            }
        }
        else {
            height = ABS(height);
            for (i = 0; i < height; i++) {
                data[n++] = d[(row - i)*xres + col + q/height];
                q += width;
            }
        }
    }
    else {
        q = height/2;
        for (i = 0; i < width; i++) {
            data[n++] = d[(row + q/width)*xres + col + i];
            q += height;
        }
    }

    return n;
}

static void
gwy_data_field_linear_area_unextract(GwyDataField *dfield,
                                     gint col, gint row,
                                     gint width, gint height,
                                     gdouble *data)
{
    gint i, n, q, xres;
    gdouble *d;

    /* FIXME: We do not handle lines sticking out, nor wide lines */
    xres = gwy_data_field_get_xres(dfield);
    d = gwy_data_field_get_data(dfield);
    n = 0;
    if (ABS(height) >= width) {
        q = width/2;
        if (height > 0) {
            for (i = 0; i < height; i++) {
                d[(row + i)*xres + col + q/height] = data[n++];
                q += width;
            }
        }
        else {
            height = ABS(height);
            for (i = 0; i < height; i++) {
                d[(row - i)*xres + col + q/height] = data[n++];
                q += width;
            }
        }
    }
    else {
        q = height/2;
        for (i = 0; i < width; i++) {
            d[(row + q/width)*xres + col + i] = data[n++];
            q += height;
        }
    }
}

static void
gwy_tool_mask_editor_selection_finished(GwyPlainTool *plain_tool)
{
    GwyToolMaskEditor *tool;
    GwyDataField *mfield = NULL;
    FieldFillFunc fill_func;
    GQuark quark;
    gdouble sel[4];
    gint isel[4];

    g_return_if_fail(plain_tool->data_field);

    tool = GWY_TOOL_MASK_EDITOR(plain_tool);
    tool->drawing_started = FALSE;
    if (!gwy_selection_get_object(plain_tool->selection, 0, sel))
        return;

    isel[0] = floor(gwy_data_field_rtoj(plain_tool->data_field, sel[0]));
    isel[1] = floor(gwy_data_field_rtoi(plain_tool->data_field, sel[1]));

    if (tool->args.style == MASK_EDIT_STYLE_DRAWING) {
        if (tool->args.tool == MASK_TOOL_PAINT_DRAW
            || tool->args.tool == MASK_TOOL_PAINT_ERASE) {
            gwy_plain_tool_log_add(plain_tool);
            /* The mask has been already modified. */
            gwy_selection_clear(plain_tool->selection);
            return;
        }
        gwy_tool_mask_editor_bucket_fill(tool, isel[0], isel[1]);
        if (plain_tool->mask_field)
            gwy_data_field_data_changed(plain_tool->mask_field);
        return;
    }

    isel[2] = floor(gwy_data_field_rtoj(plain_tool->data_field, sel[2]));
    isel[3] = floor(gwy_data_field_rtoi(plain_tool->data_field, sel[3]));
    if (tool->args.shape == MASK_SHAPE_LINE) {
        if (isel[2] < isel[0]) {
            GWY_SWAP(gdouble, isel[0], isel[2]);
            GWY_SWAP(gdouble, isel[1], isel[3]);
        }
    }
    else {
        if (isel[2] < isel[0])
            GWY_SWAP(gdouble, isel[0], isel[2]);
        if (isel[3] < isel[1])
            GWY_SWAP(gdouble, isel[1], isel[3]);
    }
    gwy_debug("(%d,%d) (%d,%d)", isel[0], isel[1], isel[2], isel[3]);
    isel[2] -= isel[0] - 1;
    isel[3] -= isel[1] - 1;

    switch (tool->args.shape) {
        case MASK_SHAPE_RECTANGLE:
        fill_func = &gwy_data_field_area_fill;
        break;

        case MASK_SHAPE_ELLIPSE:
        fill_func = (FieldFillFunc)&gwy_data_field_elliptic_area_fill;
        break;

        case MASK_SHAPE_LINE:
        fill_func = (FieldFillFunc)&gwy_data_field_linear_area_fill;
        break;

        default:
        g_return_if_reached();
        break;
    }

    quark = gwy_app_get_mask_key_for_id(plain_tool->id);
    switch (tool->args.mode) {
        case MASK_EDIT_SET:
        gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
        mfield = gwy_tool_mask_editor_maybe_add_mask(plain_tool, quark);
        gwy_data_field_clear(mfield);
        fill_func(mfield, isel[0], isel[1], isel[2], isel[3], 1.0);
        break;

        case MASK_EDIT_ADD:
        gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
        mfield = gwy_tool_mask_editor_maybe_add_mask(plain_tool, quark);
        fill_func(mfield, isel[0], isel[1], isel[2], isel[3], 1.0);
        break;

        case MASK_EDIT_REMOVE:
        if (plain_tool->mask_field) {
            gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
            mfield = plain_tool->mask_field;
            fill_func(mfield, isel[0], isel[1], isel[2], isel[3], 0.0);
            if (!(gwy_data_field_get_max(mfield) > 0.0)) {
                gwy_container_remove(plain_tool->container, quark);
                mfield = NULL;
            }
        }
        break;

        case MASK_EDIT_INTERSECT:
        if (plain_tool->mask_field) {
            gdouble *data;
            gint n;

            gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
            mfield = plain_tool->mask_field;
            gwy_data_field_clamp(mfield, 0.0, 1.0);
            switch (tool->args.shape) {
                case MASK_SHAPE_RECTANGLE:
                gwy_data_field_area_add(mfield,
                                        isel[0], isel[1], isel[2], isel[3],
                                        1.0);
                break;

                case MASK_SHAPE_ELLIPSE:
                n = gwy_data_field_get_elliptic_area_size(isel[2], isel[3]);
                data = g_new(gdouble, n);
                gwy_data_field_elliptic_area_extract(mfield,
                                                     isel[0], isel[1],
                                                     isel[2], isel[3],
                                                     data);
                while (n)
                    data[--n] += 1.0;
                gwy_data_field_elliptic_area_unextract(mfield,
                                                       isel[0], isel[1],
                                                       isel[2], isel[3],
                                                       data);
                g_free(data);
                break;

                case MASK_SHAPE_LINE:
                n = gwy_data_field_get_linear_area_size(isel[2], isel[3]);
                data = g_new(gdouble, n);
                gwy_data_field_linear_area_extract(mfield,
                                                   isel[0], isel[1],
                                                   isel[2], isel[3],
                                                   data);
                while (n)
                    data[--n] += 1.0;
                gwy_data_field_linear_area_unextract(mfield,
                                                     isel[0], isel[1],
                                                     isel[2], isel[3],
                                                     data);
                g_free(data);
                break;

                default:
                break;
            }
            gwy_data_field_add(mfield, -1.0);
            gwy_data_field_clamp(mfield, 0.0, 1.0);
            if (!(gwy_data_field_get_max(mfield) > 0.0)) {
                gwy_container_remove(plain_tool->container, quark);
                mfield = NULL;
            }
        }
        break;

        default:
        break;
    }
    gwy_selection_clear(plain_tool->selection);
    if (mfield) {
        gwy_data_field_data_changed(mfield);
        gwy_tool_mask_editor_save_args(tool);
        gwy_plain_tool_log_add(plain_tool);
    }
}

static void
gwy_tool_mask_editor_bucket_fill(GwyToolMaskEditor *tool,
                                 gint j, gint i)
{
    GwyPlainTool *plain_tool;
    GwyDataField *mfield;
    gint xres, yres, k, gno;
    gint *g, *grains = NULL;
    gboolean draw;
    gdouble *data;
    GQuark quark;

    plain_tool = GWY_PLAIN_TOOL(tool);
    mfield = plain_tool->mask_field;
    if (!mfield) {
        if (tool->args.tool == MASK_TOOL_FILL_DRAW)
            gwy_tool_mask_editor_fill(tool);
        return;
    }

    xres = gwy_data_field_get_xres(mfield);
    yres = gwy_data_field_get_yres(mfield);
    if (i < 0 || i >= yres || j < 0 || j >= xres)
        return;

    if (tool->args.tool == MASK_TOOL_FILL_DRAW)
        draw = TRUE;
    else if (tool->args.tool == MASK_TOOL_FILL_ERASE)
        draw = FALSE;
    else {
        g_return_if_reached();
    }

    data = gwy_data_field_get_data(mfield);
    if ((data[i*xres + j] && draw) || (!data[i*xres + j] && !draw))
        return;

    quark = gwy_app_get_mask_key_for_id(plain_tool->id);
    gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);

    g = grains = g_new0(gint, xres*yres);
    if (draw)
        gwy_data_field_grains_invert(mfield);
    gwy_data_field_number_grains(mfield, grains);
    gno = grains[i*xres + j];

    for (k = xres*yres; k; k--, data++, g++) {
        if (*g == gno)
            *data = 0.0;
    }
    if (draw)
        gwy_data_field_grains_invert(mfield);

    g_free(grains);
    gwy_plain_tool_log_add(plain_tool);
}

/* FIXME: This is woefully inefficient. */
static void
gwy_data_field_paint_wide_line(GwyDataField *dfield,
                               gint col, gint row,
                               gint width, gint height,
                               gdouble radius, gdouble value)
{
    gint i, q;

    if (!width && !height) {
        gwy_data_field_circular_area_fill(dfield, col, row, radius, value);
        return;
    }

    if (ABS(height) >= width) {
        q = width/2;
        if (height > 0) {
            for (i = 0; i <= height; i++) {
                gwy_data_field_circular_area_fill(dfield,
                                                  col + q/height, row + i,
                                                  radius, value);
                q += width;
            }
        }
        else {
            height = ABS(height);
            for (i = 0; i <= height; i++) {
                gwy_data_field_circular_area_fill(dfield,
                                                  col + q/height, row - i,
                                                  radius, value);
                q += width;
            }
        }
    }
    else {
        q = height/2;
        for (i = 0; i <= width; i++) {
            gwy_data_field_circular_area_fill(dfield,
                                              col+i, row + q/width,
                                              radius, value);
            q += height;
        }
    }
}

static void
gwy_tool_mask_editor_selection_changed(GwyPlainTool *plain_tool,
                                       G_GNUC_UNUSED gint hint)
{
    GwyDataField *mfield;
    GwyToolMaskEditor *tool;
    GQuark quark;
    gint xres, yres;
    gdouble sel[2];
    gdouble fillvalue, r;
    gint isel[2];

    tool = GWY_TOOL_MASK_EDITOR(plain_tool);
    if (tool->in_setup || tool->args.style != MASK_EDIT_STYLE_DRAWING)
        return;

    if (tool->args.tool == MASK_TOOL_PAINT_DRAW)
        fillvalue = 1.0;
    else if (tool->args.tool == MASK_TOOL_PAINT_ERASE)
        fillvalue = 0.0;
    else
        return;

    /* Apparently this gets called also during the tool destruction. */
    if (!plain_tool->data_field || !plain_tool->selection
        || !gwy_selection_get_object(plain_tool->selection, 0, sel)) {
        tool->drawing_started = FALSE;
        return;
    }

    isel[0] = floor(gwy_data_field_rtoj(plain_tool->data_field, sel[0]));
    isel[1] = floor(gwy_data_field_rtoi(plain_tool->data_field, sel[1]));

    quark = gwy_app_get_mask_key_for_id(plain_tool->id);

    mfield = gwy_tool_mask_editor_maybe_add_mask(plain_tool, quark);
    xres = gwy_data_field_get_xres(mfield);
    yres = gwy_data_field_get_yres(mfield);
    r = tool->args.radius - 0.5;
    if (isel[0] >= 0 && isel[0] < xres && isel[1] >= 0 && isel[1] < yres) {
        if (!tool->drawing_started) {
            gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
            gwy_data_field_circular_area_fill(mfield, isel[0], isel[1],
                                              r, fillvalue);
        }
        else {
            gint xy[4];

            xy[0] = tool->oldisel[0];
            xy[1] = tool->oldisel[1];
            xy[2] = isel[0];
            xy[3] = isel[1];
            if (xy[2] < xy[0]) {
                GWY_SWAP(gdouble, xy[0], xy[2]);
                GWY_SWAP(gdouble, xy[1], xy[3]);
            }
            xy[2] -= xy[0];
            xy[3] -= xy[1];
            gwy_data_field_paint_wide_line(mfield, xy[0], xy[1], xy[2], xy[3],
                                           r, fillvalue);
        }
        gwy_data_field_data_changed(mfield);
        tool->oldisel[0] = isel[0];
        tool->oldisel[1] = isel[1];
        tool->drawing_started = TRUE;
    }
}

static void
gwy_tool_mask_editor_save_args(GwyToolMaskEditor *tool)
{
    GwyContainer *settings;

    settings = gwy_app_settings_get();
    gwy_container_set_enum_by_name(settings, style_key,
                                   tool->args.style);
    gwy_container_set_enum_by_name(settings, mode_key,
                                   tool->args.mode);
    gwy_container_set_enum_by_name(settings, shape_key,
                                   tool->args.shape);
    gwy_container_set_enum_by_name(settings, tool_key,
                                   tool->args.tool);
    gwy_container_set_enum_by_name(settings, dist_type_key,
                                   tool->args.dist_type);
    gwy_container_set_int32_by_name(settings, radius_key,
                                    tool->args.radius);
    gwy_container_set_int32_by_name(settings, gsamount_key,
                                    tool->args.gsamount);
    gwy_container_set_boolean_by_name(settings, from_border_key,
                                      tool->args.from_border);
    gwy_container_set_boolean_by_name(settings, prevent_merge_key,
                                      tool->args.prevent_merge);
    gwy_container_set_boolean_by_name(settings, fill_nonsimple_key,
                                      tool->args.fill_nonsimple);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

