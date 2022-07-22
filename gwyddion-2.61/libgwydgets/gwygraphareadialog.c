/*
 *  $Id: gwygraphareadialog.c 22209 2019-07-10 08:16:03Z yeti-dn $
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyoptionmenus.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwyscitext.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycolorbutton.h>
#include <libgwydgets/gwygraphareadialog.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwygraphcurvemodel.h>

enum {
    COLUMN_VALUE,
    COLUMN_NAME,
    COLUMN_PIXBUF
};

static void       gwy_graph_area_dialog_destroy (GtkObject *object);
static gboolean   gwy_graph_area_dialog_delete  (GtkWidget *widget,
                                                 GdkEventAny *event);
static void       gwy_graph_area_dialog_response(GtkDialog *gtkdialog,
                                                 gint response_id);
static GtkWidget* gwy_graph_combo_box_new       (GwyGraphAreaDialog *dialog,
                                                 const gchar *property,
                                                 gboolean labels,
                                                 GCallback model_creator,
                                                 gint current);
static void       color_change                  (GtkWidget *color_button,
                                                 GwyGraphAreaDialog *dialog);
static void       colorsel_response             (GtkWidget *selector,
                                                 gint response,
                                                 GwyGraphAreaDialog *dialog);
static void       colorsel_changed              (GtkColorSelection *colorsel,
                                                 GwyGraphAreaDialog *dialog);
static void       color_selected                (GtkComboBox *combo,
                                                 GwyGraphAreaDialog *dialog);
static void       label_change                  (GwySciText *sci_text,
                                                 GwyGraphAreaDialog *dialog);
static void       combo_realized                (GtkWidget *parent,
                                                 GtkWidget *combo);
static void       refresh                       (GwyGraphAreaDialog *dialog);
static void       curvetype_changed             (GtkWidget *combo,
                                                 GwyGraphAreaDialog *dialog);
static void       thickness_changed             (GtkAdjustment *adj,
                                                 GwyGraphAreaDialog *dialog);
static void       pointsize_changed             (GtkAdjustment *adj,
                                                 GwyGraphAreaDialog *dialog);
static GtkWidget* gwy_graph_color_combo_new     (void);
static void       gwy_graph_color_combo_select  (GtkComboBox *combo,
                                                 const GwyRGBA *color);
static void       update_switcher_sensitivity   (GwyGraphAreaDialog *dialog);

G_DEFINE_TYPE(GwyGraphAreaDialog, _gwy_graph_area_dialog, GTK_TYPE_DIALOG)

static void
_gwy_graph_area_dialog_class_init(GwyGraphAreaDialogClass *klass)
{
    GtkObjectClass *object_class = GTK_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    GtkDialogClass *dialog_class = GTK_DIALOG_CLASS(klass);

    object_class->destroy = gwy_graph_area_dialog_destroy;
    widget_class->delete_event = gwy_graph_area_dialog_delete;
    dialog_class->response = gwy_graph_area_dialog_response;
}

static void
_gwy_graph_area_dialog_init(GwyGraphAreaDialog *dialog)
{
    GtkWidget *table, *label, *hbox;
    GwySciText *scitext;
    gint row;

    gtk_window_set_title(GTK_WINDOW(dialog), _("Curve Properties"));
    dialog->prev = gwy_stock_like_button_new(_("Pre_v"), GWY_STOCK_PREVIOUS);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), dialog->prev,
                                 GWY_GRAPH_AREA_DIALOG_RESPONSE_PREV);
    dialog->next = gwy_stock_like_button_new(_("_Next"), GWY_STOCK_NEXT);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), dialog->next,
                                 GWY_GRAPH_AREA_DIALOG_RESPONSE_NEXT);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    update_switcher_sensitivity(dialog);

    table = gtk_table_new(7, 3, FALSE);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);
    row = 0;

    dialog->curvetype_menu
        = gwy_enum_combo_box_new(gwy_graph_curve_type_get_enum(), -1,
                                 G_CALLBACK(curvetype_changed), dialog,
                                 0, TRUE);
    gwy_table_attach_adjbar(table, row, _("Plot _style:"), NULL,
                            GTK_OBJECT(dialog->curvetype_menu),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    dialog->color_button = gwy_color_button_new();
    gwy_color_button_set_use_alpha(GWY_COLOR_BUTTON(dialog->color_button),
                                   FALSE);
    gwy_table_attach_adjbar(table, row, _("Pl_ot color:"), NULL,
                            GTK_OBJECT(dialog->color_button),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    g_signal_connect(dialog->color_button, "clicked",
                     G_CALLBACK(color_change), dialog);

    hbox = gtk_widget_get_parent(dialog->color_button);
    dialog->color_selector = gwy_graph_color_combo_new();
    gtk_box_pack_end(GTK_BOX(hbox), dialog->color_selector, FALSE, FALSE, 0);
    gtk_container_child_set(GTK_CONTAINER(hbox), dialog->color_selector,
                            "position", 1,
                            NULL);
    g_signal_connect(dialog->color_selector, "changed",
                     G_CALLBACK(color_selected), dialog);
    row++;

    dialog->pointtype_menu
        = gwy_graph_combo_box_new(dialog, "point-type", TRUE,
                                  G_CALLBACK(_gwy_graph_get_point_type_store),
                                  GWY_GRAPH_POINT_SQUARE);
    gwy_table_attach_adjbar(table, row, _("Point _type:"), NULL,
                            GTK_OBJECT(dialog->pointtype_menu),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    dialog->pointsize = gtk_adjustment_new(6, 1, 50, 1, 5, 0);
    gwy_table_attach_adjbar(table, row, _("_Point size:"), _("px"),
                            dialog->pointsize,
                            GWY_HSCALE_SQRT | GWY_HSCALE_SNAP);
    g_signal_connect(dialog->pointsize, "value-changed",
                     G_CALLBACK(pointsize_changed), dialog);
    row++;

    dialog->linestyle_menu
        = gwy_graph_combo_box_new(dialog, "line-style", FALSE,
                                  G_CALLBACK(_gwy_graph_get_line_style_store),
                                  GDK_LINE_SOLID);
    gwy_table_attach_adjbar(table, row, _("_Line type:"), NULL,
                            GTK_OBJECT(dialog->linestyle_menu),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    dialog->thickness = gtk_adjustment_new(6, 1, 50, 1, 5, 0);
    gwy_table_attach_adjbar(table, row, _("Line t_hickness:"), _("px"),
                            dialog->thickness,
                            GWY_HSCALE_SQRT | GWY_HSCALE_SNAP);
    g_signal_connect(dialog->thickness, "value-changed",
                     G_CALLBACK(thickness_changed), dialog);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    label = gtk_label_new_with_mnemonic(_("<b>Label Te_xt</b>"));
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    dialog->sci_text = gwy_sci_text_new();
    scitext = GWY_SCI_TEXT(dialog->sci_text);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label),
                                  gwy_sci_text_get_entry(scitext));
    gtk_container_set_border_width(GTK_CONTAINER(dialog->sci_text), 4);
    g_signal_connect(dialog->sci_text, "edited",
                     G_CALLBACK(label_change), dialog);

    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
                      dialog->sci_text);
    gtk_widget_show_all(dialog->sci_text);
}

static gboolean
gwy_graph_area_dialog_delete(GtkWidget *widget,
                             G_GNUC_UNUSED GdkEventAny *event)
{
    GwyGraphAreaDialog *dialog;

    dialog = GWY_GRAPH_AREA_DIALOG(widget);
    if (dialog->color_dialog)
        gtk_widget_hide(dialog->color_dialog);
    gtk_widget_hide(widget);

    return TRUE;
}

static void
gwy_graph_area_dialog_response(GtkDialog *gtkdialog,
                               G_GNUC_UNUSED gint response_id)
{
    GwyGraphAreaDialog *dialog = GWY_GRAPH_AREA_DIALOG(gtkdialog);

    if (dialog->color_dialog)
        gtk_widget_hide(dialog->color_dialog);
}

GtkWidget*
_gwy_graph_area_dialog_new()
{
    return GTK_WIDGET(g_object_new(GWY_TYPE_GRAPH_AREA_DIALOG, NULL));
}

static void
gwy_graph_area_dialog_destroy(GtkObject *object)
{
    GwyGraphAreaDialog *dialog;

    dialog = GWY_GRAPH_AREA_DIALOG(object);
    if (dialog->color_dialog) {
        gtk_widget_destroy(dialog->color_dialog);
        dialog->color_dialog = NULL;
    }

    GTK_OBJECT_CLASS(_gwy_graph_area_dialog_parent_class)->destroy(object);
}

static void
combo_changed(GtkWidget *combo,
              GwyGraphAreaDialog *dialog)
{
    const gchar *property;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint active;

    if (!dialog->curve_model
        || !gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter))
        return;

    property = g_object_get_data(G_OBJECT(combo), "property-name");
    g_return_if_fail(property);
    model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
    gtk_tree_model_get(model, &iter, COLUMN_VALUE, &active, -1);
    g_object_set(dialog->curve_model, property, active, NULL);
}

static GtkWidget*
gwy_graph_combo_box_new(GwyGraphAreaDialog *dialog,
                        const gchar *property,
                        gboolean labels,
                        GCallback model_creator,
                        gint current)
{
    GtkWidget *combo;
    GtkCellRenderer *renderer;

    combo = gtk_combo_box_new();
    gtk_combo_box_set_wrap_width(GTK_COMBO_BOX(combo), 1);
    g_object_set_data(G_OBJECT(combo), "initial-value",
                      GINT_TO_POINTER(current));
    g_object_set_data(G_OBJECT(combo), "model-creator", model_creator);
    g_object_set_data(G_OBJECT(combo), "property-name", (gpointer)property);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, FALSE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(combo), renderer,
                                  "pixbuf", COLUMN_PIXBUF);
    if (labels) {
        renderer = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, FALSE);
        gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(combo), renderer,
                                      "text", COLUMN_NAME);
    }

    g_signal_connect(dialog, "realize", G_CALLBACK(combo_realized), combo);
    g_signal_connect(combo, "changed", G_CALLBACK(combo_changed), dialog);

    return combo;
}

static void
combo_set_current(GtkWidget *combo,
                  gint value)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint v;

    model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
    if (!model) {
        g_object_set_data(G_OBJECT(combo),
                          "initial-value", GINT_TO_POINTER(value));
        return;
    }

    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gtk_tree_model_get(model, &iter, COLUMN_VALUE, &v, -1);
            if (v == value) {
                gtk_combo_box_set_active_iter(GTK_COMBO_BOX(combo), &iter);
                break;
            }
        } while (gtk_tree_model_iter_next(model, &iter));
    }
}

static void
combo_realized(GtkWidget *parent,
               GtkWidget *combo)
{
    GtkTreeModel* (*model_creator)(GtkWidget *widget);
    GtkTreeModel *model;
    gint value;

    model_creator = g_object_get_data(G_OBJECT(combo), "model-creator");
    g_return_if_fail(model_creator);
    model = model_creator(parent);
    g_return_if_fail(GTK_IS_TREE_MODEL(model));

    gtk_combo_box_set_model(GTK_COMBO_BOX(combo), model);
    value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(combo),
                                              "initial-value"));
    combo_set_current(combo, value);
}

static void
refresh(GwyGraphAreaDialog *dialog)
{
    GwyGraphCurveModel *cmodel;
    GtkTreeModel *model;
    GwyRGBA *rgba;

    model = gtk_combo_box_get_model(GTK_COMBO_BOX(dialog->color_selector));
    gwy_null_store_set_model(GWY_NULL_STORE(model), dialog->curve_model, NULL);
    gwy_null_store_row_changed(GWY_NULL_STORE(model), 0);
    update_switcher_sensitivity(dialog);

    if (dialog->curve_model == NULL)
        return;

    cmodel = GWY_GRAPH_CURVE_MODEL(dialog->curve_model);

    g_object_get(cmodel, "color", &rgba, NULL);
    gwy_graph_color_combo_select(GTK_COMBO_BOX(dialog->color_selector), rgba);
    gwy_rgba_free(rgba);
    gwy_color_button_set_color(GWY_COLOR_BUTTON(dialog->color_button),
                               &cmodel->color);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(dialog->curvetype_menu),
                                  cmodel->mode);

    combo_set_current(dialog->pointtype_menu, cmodel->point_type);
    combo_set_current(dialog->linestyle_menu, cmodel->line_style);

    gtk_adjustment_set_value(GTK_ADJUSTMENT(dialog->pointsize),
                             cmodel->point_size);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(dialog->thickness),
                             cmodel->line_width);

    gwy_sci_text_set_text(GWY_SCI_TEXT((dialog->sci_text)),
                          cmodel->description->str);
}

static void
color_change(G_GNUC_UNUSED GtkWidget *color_button,
             GwyGraphAreaDialog *dialog)
{
    GdkColor gcl;
    GwyGraphCurveModel *cmodel;
    GtkWidget *selector, *colorsel;

    if (!dialog->curve_model)
        return;

    if (dialog->color_dialog) {
        gtk_window_present(GTK_WINDOW(dialog->color_dialog));
        return;
    }

    cmodel = GWY_GRAPH_CURVE_MODEL(dialog->curve_model);
    selector = gtk_color_selection_dialog_new(_("Set Curve Color"));
    dialog->color_dialog = selector;
    dialog->old_color = cmodel->color;

    colorsel = GTK_COLOR_SELECTION_DIALOG(selector)->colorsel;
    g_signal_connect(selector, "response",
                     G_CALLBACK(colorsel_response), dialog);
    g_signal_connect(colorsel, "color-changed",
                     G_CALLBACK(colorsel_changed), dialog);
    gwy_rgba_to_gdk_color(&cmodel->color, &gcl);
    gtk_color_selection_set_current_color(GTK_COLOR_SELECTION(colorsel), &gcl);
    gtk_widget_show(selector);
}

static void
colorsel_response(GtkWidget *selector,
                  gint response,
                  GwyGraphAreaDialog *dialog)
{
    GwyGraphCurveModel *cmodel;

    if (!dialog->curve_model)
        return;

    /* The parent graph area will handle this. */
    if (response == GWY_GRAPH_AREA_DIALOG_RESPONSE_PREV
        || response == GWY_GRAPH_AREA_DIALOG_RESPONSE_NEXT)
        return;

    cmodel = GWY_GRAPH_CURVE_MODEL(dialog->curve_model);
    if (response == GTK_RESPONSE_CANCEL) {
        g_object_set(cmodel, "color", &dialog->old_color, NULL);
        refresh(dialog);
    }
    gtk_widget_destroy(selector);
    dialog->color_dialog = NULL;
}

static void
colorsel_changed(GtkColorSelection *colorsel,
                 GwyGraphAreaDialog *dialog)
{
    GwyGraphCurveModel *cmodel;
    GwyRGBA rgba;
    GdkColor gcl;

    if (!dialog->curve_model)
        return;

    cmodel = GWY_GRAPH_CURVE_MODEL(dialog->curve_model);
    gtk_color_selection_get_current_color(colorsel, &gcl);
    gwy_rgba_from_gdk_color(&rgba, &gcl);
    rgba.a = 1.0;
    g_object_set(cmodel, "color", &rgba, NULL);
    refresh(dialog);
}

static void
color_selected(GtkComboBox *combo,
                  GwyGraphAreaDialog *dialog)
{
    GwyGraphCurveModel *cmodel;
    const GwyRGBA *rgba;
    gint i;

    i = gtk_combo_box_get_active(combo);
    if (!i || !dialog->curve_model)
        return;

    cmodel = GWY_GRAPH_CURVE_MODEL(dialog->curve_model);
    rgba = gwy_graph_get_preset_color(i-1);
    g_object_set(cmodel, "color", rgba, NULL);
    refresh(dialog);
}

static void
label_change(GwySciText *sci_text, GwyGraphAreaDialog *dialog)
{
    GwyGraphCurveModel *cmodel;

    if (dialog->curve_model == NULL)
        return;
    cmodel = GWY_GRAPH_CURVE_MODEL(dialog->curve_model);
    g_object_set(cmodel,
                 "description", gwy_sci_text_get_text(GWY_SCI_TEXT(sci_text)),
                 NULL);
}

void
_gwy_graph_area_dialog_set_curve_data(GtkWidget *dialog,
                                      GwyGraphCurveModel *cmodel)
{
    GwyGraphAreaDialog *gadialog = GWY_GRAPH_AREA_DIALOG(dialog);

    gadialog->curve_model = cmodel;
    if (gadialog->color_dialog) {
        GtkWidget *colorsel;
        GdkColor gcl;

        gadialog->old_color = cmodel->color;
        colorsel = GTK_COLOR_SELECTION_DIALOG(gadialog->color_dialog)->colorsel;
        gwy_rgba_to_gdk_color(&cmodel->color, &gcl);
        gtk_color_selection_set_previous_color(GTK_COLOR_SELECTION(colorsel),
                                               &gcl);
        gtk_color_selection_set_current_color(GTK_COLOR_SELECTION(colorsel),
                                              &gcl);
    }
    refresh(gadialog);
}

static void
curvetype_changed(GtkWidget *combo, GwyGraphAreaDialog *dialog)
{
    GwyGraphCurveModel *cmodel;
    GwyGraphCurveType ctype;

    if (dialog->curve_model == NULL)
        return;

    cmodel = GWY_GRAPH_CURVE_MODEL(dialog->curve_model);
    ctype = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    g_object_set(cmodel, "mode", ctype, NULL);
}

static void
thickness_changed(GtkAdjustment *adj, GwyGraphAreaDialog *dialog)
{
    GwyGraphCurveModel *cmodel;

    if (dialog->curve_model == NULL)
        return;

    cmodel = GWY_GRAPH_CURVE_MODEL(dialog->curve_model);
    g_object_set(cmodel, "line-width", gwy_adjustment_get_int(adj), NULL);
}

static void
pointsize_changed(GtkAdjustment *adj, GwyGraphAreaDialog *dialog)
{
    GwyGraphCurveModel *cmodel;

    if (dialog->curve_model == NULL)
        return;

    cmodel = GWY_GRAPH_CURVE_MODEL(dialog->curve_model);
    g_object_set(cmodel, "point-size", gwy_adjustment_get_int(adj), NULL);
}

GtkTreeModel*
_gwy_graph_get_point_type_store(GtkWidget *widget)
{
    static const GwyEnum point_types[] = {
        { N_("Square"),              GWY_GRAPH_POINT_SQUARE,                },
        { N_("Circle"),              GWY_GRAPH_POINT_CIRCLE,                },
        { N_("Diamond"),             GWY_GRAPH_POINT_DIAMOND,               },
        { N_("Cross"),               GWY_GRAPH_POINT_CROSS,                 },
        { N_("Diagonal cross"),      GWY_GRAPH_POINT_TIMES,                 },
        { N_("Asterisk"),            GWY_GRAPH_POINT_ASTERISK,              },
        { N_("Star"),                GWY_GRAPH_POINT_STAR,                  },
        { N_("Triangle up"),         GWY_GRAPH_POINT_TRIANGLE_UP,           },
        { N_("Triangle down"),       GWY_GRAPH_POINT_TRIANGLE_DOWN,         },
        { N_("Triangle left"),       GWY_GRAPH_POINT_TRIANGLE_LEFT,         },
        { N_("Triangle right"),      GWY_GRAPH_POINT_TRIANGLE_RIGHT,        },
        { N_("Full square"),         GWY_GRAPH_POINT_FILLED_SQUARE,         },
        { N_("Disc"),                GWY_GRAPH_POINT_FILLED_CIRCLE,         },
        { N_("Full diamond"),        GWY_GRAPH_POINT_FILLED_DIAMOND,        },
        { N_("Full triangle up"),    GWY_GRAPH_POINT_FILLED_TRIANGLE_UP,    },
        { N_("Full triangle down"),  GWY_GRAPH_POINT_FILLED_TRIANGLE_DOWN,  },
        { N_("Full triangle left"),  GWY_GRAPH_POINT_FILLED_TRIANGLE_LEFT,  },
        { N_("Full triangle right"), GWY_GRAPH_POINT_FILLED_TRIANGLE_RIGHT, },
    };
    static const GwyRGBA fg = { 0.0, 0.0, 0.0, 1.0 };
    static GtkListStore *store = NULL;

    GdkColor bg = { -1, 0xffff, 0xffff, 0xffff };
    GdkPixbuf *pixbuf, *tmpixbuf;
    GtkTreeIter iter;
    GdkPixmap *pixmap;
    GdkGC *gc;
    gint width, height, size;
    guint i;

    if (store)
        return GTK_TREE_MODEL(store);

    g_return_val_if_fail(GTK_IS_WIDGET(widget) && GTK_WIDGET_REALIZED(widget),
                         NULL);

    store = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_STRING, GDK_TYPE_PIXBUF);
    g_object_add_weak_pointer(G_OBJECT(store), (gpointer*)&store);

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    width |= 1;
    height |= 1;
    size = 3*height | 1;
    gc = gdk_gc_new(widget->window);
    pixmap = gdk_pixmap_new(widget->window, 4*width + 1, 4*height + 1, -1);

    pixbuf = NULL;
    for (i = 0; i < G_N_ELEMENTS(point_types); i++) {
        gdk_gc_set_rgb_fg_color(gc, &bg);
        gdk_draw_rectangle(pixmap, gc, TRUE, 0, 0, 4*width + 1, 4*height + 1);
        gwy_graph_draw_point(pixmap, gc, 2*width, 2*height,
                             point_types[i].value, size, &fg);

        pixbuf = gdk_pixbuf_get_from_drawable(pixbuf, GDK_DRAWABLE(pixmap),
                                              NULL,
                                              0, 0, 0, 0,
                                              4*width + 1, 4*height + 1);
        tmpixbuf = gdk_pixbuf_scale_simple(pixbuf, width, height,
                                           GDK_INTERP_HYPER);
        gtk_list_store_insert_with_values(store, &iter, G_MAXINT,
                                          COLUMN_VALUE, point_types[i].value,
                                          COLUMN_PIXBUF, tmpixbuf,
                                          COLUMN_NAME,
                                          gwy_sgettext(point_types[i].name),
                                          -1);
        g_object_unref(tmpixbuf);
    }
    g_object_unref(pixbuf);

    g_object_unref(pixmap);
    g_object_unref(gc);

    return GTK_TREE_MODEL(store);
}

GtkTreeModel*
_gwy_graph_get_line_style_store(GtkWidget *widget)
{
    static const GwyEnum line_styles[] = {
        { N_("line-style|Solid"), GDK_LINE_SOLID,       },
        { N_("line-style|Dash"),  GDK_LINE_ON_OFF_DASH, },
    };
    static const GwyRGBA fg = { 0.0, 0.0, 0.0, 1.0 };
    static GtkListStore *store = NULL;

    GdkColor bg = { -1, 0xffff, 0xffff, 0xffff };
    GdkPixbuf *pixbuf;
    GdkPixmap *pixmap;
    GtkTreeIter iter;
    GdkGC *gc;
    gint width, height;
    guint i;

    if (store)
        return GTK_TREE_MODEL(store);

    g_return_val_if_fail(GTK_IS_WIDGET(widget) && GTK_WIDGET_REALIZED(widget),
                         NULL);

    store = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_STRING, GDK_TYPE_PIXBUF);
    g_object_add_weak_pointer(G_OBJECT(store), (gpointer*)&store);

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    width = 5*height;
    height |= 1;
    gc = gdk_gc_new(widget->window);
    pixmap = gdk_pixmap_new(widget->window, width, height, -1);

    for (i = 0; i < G_N_ELEMENTS(line_styles); i++) {
        gdk_gc_set_rgb_fg_color(gc, &bg);
        gdk_draw_rectangle(pixmap, gc, TRUE, 0, 0, width, height);
        gwy_graph_draw_line(pixmap, gc, 1, height/2, width - 1, height/2,
                            line_styles[i].value, /* line width */ 3, &fg);

        pixbuf = gdk_pixbuf_get_from_drawable(NULL, GDK_DRAWABLE(pixmap), NULL,
                                              0, 0, 0, 0, width, height);
        gtk_list_store_insert_with_values(store, &iter, G_MAXINT,
                                          COLUMN_VALUE, line_styles[i].value,
                                          COLUMN_PIXBUF, pixbuf,
                                          COLUMN_NAME,
                                          gettext(line_styles[i].name),
                                          -1);
        g_object_unref(pixbuf);
    }

    g_object_unref(pixmap);
    g_object_unref(gc);

    return GTK_TREE_MODEL(store);
}

static void
render_graph_color(G_GNUC_UNUSED GtkCellLayout *cell_layout,
                   GtkCellRenderer *renderer,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   gpointer user_data)
{
    GdkPixbuf *pixbuf = (GdkPixbuf*)user_data;
    GwyRGBA rgba;
    guint32 pixel = 1;
    gint row;

    gtk_tree_model_get(model, iter, 0, &row, -1);
    if (row)
        rgba = *gwy_graph_get_preset_color(row-1);
    else {
        GwyGraphCurveModel *curve_model;

        curve_model = gwy_null_store_get_model(GWY_NULL_STORE(model));
        if (curve_model) {
            GwyRGBA *this_is_silly;

            g_object_get(curve_model, "color", &this_is_silly, NULL);
            rgba = *this_is_silly;
            gwy_rgba_free(this_is_silly);
        }
        else
            pixel = 0;
    }
    if (pixel)
        pixel = 0xff | gwy_rgba_to_pixbuf_pixel(&rgba);
    gdk_pixbuf_fill(pixbuf, pixel);
    g_object_set(renderer, "pixbuf", pixbuf, NULL);
}

static GtkWidget*
gwy_graph_color_combo_new(void)
{
    GtkCellRenderer *renderer;
    GwyNullStore *store;
    GdkPixbuf *pixbuf;
    GtkWidget *combo;
    gint width, height;

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    height |= 1;
    width = MAX(width, (gint)(1.618*height)) | 1;
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);

    store = gwy_null_store_new(gwy_graph_get_n_preset_colors() + 1);
    g_object_weak_ref(G_OBJECT(store), (GWeakNotify)g_object_unref, pixbuf);

    combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_combo_box_set_wrap_width(GTK_COMBO_BOX(combo), 4);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, FALSE);
    gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(combo), renderer,
                                       render_graph_color, pixbuf, NULL);

    return combo;
}

static void
gwy_graph_color_combo_select(GtkComboBox *combo,
                             const GwyRGBA *color)
{
    const GwyRGBA *preset;
    guint i, n;

    n = gwy_graph_get_n_preset_colors();
    for (i = 0; i < n; i++) {
        preset = gwy_graph_get_preset_color(i);
        if (fabs(color->r - preset->r)
            + fabs(color->g - preset->g)
            + fabs(color->b - preset->b)
            < 1e-5)
            break;
    }
    if (i < n)
        gtk_combo_box_set_active(combo, i+1);
    else
        gtk_combo_box_set_active(combo, 0);
}

void
_gwy_graph_area_dialog_set_switching(GtkWidget *dialog,
                                     gboolean prev_possible,
                                     gboolean next_possible)
{
    GwyGraphAreaDialog *gadialog = GWY_GRAPH_AREA_DIALOG(dialog);

    gadialog->prev_possible = prev_possible;
    gadialog->next_possible = next_possible;
    update_switcher_sensitivity(gadialog);
}

static void
update_switcher_sensitivity(GwyGraphAreaDialog *dialog)
{
    gboolean prev_sens = dialog->prev_possible;
    gboolean next_sens = dialog->next_possible;

    if (dialog->curve_model == NULL)
        prev_sens = next_sens = FALSE;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),
                                      GWY_GRAPH_AREA_DIALOG_RESPONSE_PREV,
                                      prev_sens);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),
                                      GWY_GRAPH_AREA_DIALOG_RESPONSE_NEXT,
                                      next_sens);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
