/*
 *  $Id: gwycombobox.c 24329 2021-10-11 15:08:54Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwydgets/gwyinventorystore.h>
#include <libgwydgets/gwycombobox.h>

static void     cell_translate_func                (GtkCellLayout *cell_layout,
                                                    GtkCellRenderer *renderer,
                                                    GtkTreeModel *tree_model,
                                                    GtkTreeIter *iter,
                                                    gpointer data);
static gboolean gwy_enum_combo_box_try_set_active  (GtkComboBox *combo,
                                                    gint active);
static gboolean gwy_enum_combo_box_find_value      (gpointer key,
                                                    const GwyEnum *item,
                                                    gint *i);
static GwyEnum* gwy_combo_box_metric_unit_make_enum(gint from,
                                                    gint to,
                                                    GwySIUnit *unit,
                                                    gint *nentries);

static GQuark enum_quark = 0;

/**
 * gwy_enum_combo_box_new:
 * @entries: An enum with choices.
 * @nentries: The number of items in @entries, may be -1 when @entries is terminated with %NULL enum name.
 * @callback: A callback called when a new choice is selected (may be %NULL). If you want to just update an integer,
 *            you can use gwy_enum_combo_box_update_int() here.
 * @cbdata: User data passed to the callback.
 * @active: The enum value to show as currently selected.  If it isn't equal to any @entries value, first item is
 *          selected.
 * @translate: Whether to apply translation function (gwy_sgettext()) to item names.
 *
 * Creates a combo box with choices from a enum.
 *
 * The array @entries must exist during the whole lifetime of the combo box because it is used directly as the model.
 *
 * Returns: A newly created combo box as #GtkWidget.
 **/
GtkWidget*
gwy_enum_combo_box_new(const GwyEnum *entries,
                       gint nentries,
                       GCallback callback,
                       gpointer cbdata,
                       gint active,
                       gboolean translate)
{
    GtkWidget *combo;
    GwyInventory *inventory;
    GwyInventoryStore *store;
    GtkCellRenderer *renderer;
    GtkCellLayout *layout;
    GtkTreeModel *model;

    inventory = gwy_enum_inventory_new(entries, nentries);
    store = gwy_inventory_store_new(inventory);
    g_object_unref(inventory);
    model = GTK_TREE_MODEL(store);
    combo = gtk_combo_box_new_with_model(model);
    gtk_combo_box_set_wrap_width(GTK_COMBO_BOX(combo), 1);
    g_object_unref(store);

    g_assert(gwy_inventory_store_get_column_by_name(store, "name") == 1);
    g_assert(gwy_inventory_store_get_column_by_name(store, "value") == 2);

    layout = GTK_CELL_LAYOUT(combo);
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(layout, renderer, FALSE);
    if (translate)
        gtk_cell_layout_set_cell_data_func(layout, renderer, cell_translate_func, &gwy_sgettext, NULL);
    else
        gtk_cell_layout_add_attribute(layout, renderer, "markup", 1);

    if (!gwy_enum_combo_box_try_set_active(GTK_COMBO_BOX(combo), active))
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    if (callback)
        g_signal_connect(combo, "changed", callback, cbdata);

    return combo;
}

/**
 * gwy_enum_combo_box_newl:
 * @callback: A callback called when a new choice is selected (may be %NULL).
 * @cbdata: User data passed to the callback.
 * @active: The enum value to show as currently selected.  If it isn't equal to any @entries value, the first item is
 *          selected.
 * @...: First item label, first item value, second item label, second item value, etc.  Terminated with %NULL.
 *
 * Creates a combo box with choices from a list of label/value pairs.
 *
 * The string values passed as label texts must exist through the whole lifetime of the widget.
 *
 * Returns: A newly created combo box as #GtkWidget.
 *
 * Since: 2.5
 **/
GtkWidget*
gwy_enum_combo_box_newl(GCallback callback,
                        gpointer cbdata,
                        gint active,
                        ...)
{
    GtkWidget *widget;
    GwyEnum *entries;
    gint i, nentries;
    va_list ap;

    va_start(ap, active);
    nentries = 0;
    while (va_arg(ap, const gchar*)) {
        (void)va_arg(ap, gint);
        nentries++;
    }
    va_end(ap);

    entries = g_new(GwyEnum, nentries);

    va_start(ap, active);
    for (i = 0; i < nentries; i++) {
        entries[i].name = va_arg(ap, const gchar*);
        entries[i].value = va_arg(ap, gint);
    }
    va_end(ap);

    widget = gwy_enum_combo_box_new(entries, nentries, callback, cbdata, active, FALSE);
    g_signal_connect_swapped(widget, "destroy", G_CALLBACK(g_free), entries);

    return widget;
}

/**
 * gwy_enum_combo_box_set_active:
 * @combo: A combo box which was created with gwy_enum_combo_box_new().
 * @active: The enum value to show as currently selected.
 *
 * Sets the active combo box item by corresponding enum value.
 **/
void
gwy_enum_combo_box_set_active(GtkComboBox *combo,
                              gint active)
{
    if (!gwy_enum_combo_box_try_set_active(combo, active))
        g_warning("Enum value not between inventory enums");
}

/**
 * gwy_enum_combo_box_get_active:
 * @combo: A combo box which was created with gwy_enum_combo_box_new().
 *
 * Gets the enum value corresponding to currently active combo box item.
 *
 * Returns: The selected enum value.
 **/
gint
gwy_enum_combo_box_get_active(GtkComboBox *combo)
{
    GwyInventoryStore *store;
    const GwyEnum *item;
    gint i;

    i = gtk_combo_box_get_active(combo);
    store = GWY_INVENTORY_STORE(gtk_combo_box_get_model(combo));
    g_return_val_if_fail(GWY_IS_INVENTORY_STORE(store), -1);
    item = gwy_inventory_get_nth_item(gwy_inventory_store_get_inventory(store), i);
    g_return_val_if_fail(item, -1);

    return item->value;
}

/**
 * gwy_enum_combo_box_update_int:
 * @combo: A combo box which was created with gwy_enum_combo_box_new().
 * @integer: Pointer to an integer to update to selected enum value.
 *
 * Convenience callback keeping an integer synchronized with selected enum combo box value.
 **/
void
gwy_enum_combo_box_update_int(GtkComboBox *combo,
                              gint *integer)
{
    GwyInventoryStore *store;
    const GwyEnum *item;
    gint i;

    i = gtk_combo_box_get_active(combo);
    store = GWY_INVENTORY_STORE(gtk_combo_box_get_model(combo));
    g_return_if_fail(GWY_IS_INVENTORY_STORE(store));
    item = gwy_inventory_get_nth_item(gwy_inventory_store_get_inventory(store), i);
    g_return_if_fail(item);
    *integer = item->value;
}

static gboolean
gwy_enum_combo_box_try_set_active(GtkComboBox *combo,
                                  gint active)
{
    GwyInventoryStore *store;

    store = GWY_INVENTORY_STORE(gtk_combo_box_get_model(combo));
    g_return_val_if_fail(GWY_IS_INVENTORY_STORE(store), FALSE);
    if (!gwy_inventory_find(gwy_inventory_store_get_inventory(store), (GHRFunc)&gwy_enum_combo_box_find_value, &active))
        return FALSE;

    gtk_combo_box_set_active(combo, active);
    return TRUE;
}

/* Find an enum and translate its enum value to inventory position */
static gboolean
gwy_enum_combo_box_find_value(gpointer key,
                              const GwyEnum *item,
                              gint *i)
{
    if (item->value == *i) {
        *i = GPOINTER_TO_UINT(key);
        return TRUE;
    }
    return FALSE;
}

static void
cell_translate_func(G_GNUC_UNUSED GtkCellLayout *cell_layout,
                    GtkCellRenderer *renderer,
                    GtkTreeModel *tree_model,
                    GtkTreeIter *iter,
                    gpointer data)
{
    const gchar* (*method)(const gchar*) = data;
    const GwyEnum *enum_item;

    gtk_tree_model_get(tree_model, iter, 0, &enum_item, -1);
    g_object_set(renderer, "markup", method(enum_item->name), NULL);
}

static void
gwy_enum_combo_box_set_model(GtkComboBox *combo,
                             GwyEnum *newenum)
{
    GwyEnum *oldenum;
    gint active = -1;

    oldenum = g_object_get_qdata(G_OBJECT(combo), enum_quark);
    if (oldenum) {
        active = gwy_enum_combo_box_get_active(combo);
        gwy_enum_freev(oldenum);
        g_object_set_qdata(G_OBJECT(combo), enum_quark, NULL);
    }

    if (newenum) {
        GwyInventoryStore *store;
        GwyInventory *inventory;

        inventory = gwy_enum_inventory_new(newenum, -1);
        store = gwy_inventory_store_new(inventory);
        g_object_unref(inventory);
        gtk_combo_box_set_model(combo, GTK_TREE_MODEL(store));
        g_object_unref(store);
        g_object_set_qdata(G_OBJECT(combo), enum_quark, newenum);

        if (!oldenum || !gwy_enum_combo_box_try_set_active(combo, active))
            gtk_combo_box_set_active(combo, 0);
    }
    else
        gtk_combo_box_set_model(combo, NULL);
}

/**
 * gwy_combo_box_metric_unit_new:
 * @callback: A callback called when a new choice is selected (may be %NULL). If you want to just update an integer,
 *            you can use gwy_enum_combo_box_update_int() here.
 * @cbdata: User data passed to the callback.
 * @from: The exponent of 10 the menu should start at (a multiple of 3, will be rounded downward if isn't).
 * @to: The exponent of 10 the menu should end at (a multiple of 3, will be rounded upward if isn't).
 * @unit: The unit to be prefixed.
 * @active: The power of 10 to show as currently selected (a multiple of 3).
 *
 * Creates an enum combo box with SI power of 10 multiplies.
 *
 * The integer value is the power of 10.
 *
 * Returns: The newly created combo box as #GtkWidget.
 **/
GtkWidget*
gwy_combo_box_metric_unit_new(GCallback callback,
                              gpointer cbdata,
                              gint from,
                              gint to,
                              GwySIUnit *unit,
                              gint active)
{
    GtkWidget *combo;
    GwyEnum *entries;
    gint n;

    g_return_val_if_fail(GWY_IS_SI_UNIT(unit), NULL);

    if (!enum_quark)
        enum_quark = g_quark_from_static_string("gwy-metric-unit-combo-box-enum");

    entries = gwy_combo_box_metric_unit_make_enum(from, to, unit, &n);
    combo = gwy_enum_combo_box_new(entries, n, callback, cbdata, active, FALSE);
    g_object_set_qdata(G_OBJECT(combo), enum_quark, entries);
    g_signal_connect(combo, "destroy", G_CALLBACK(gwy_enum_combo_box_set_model), NULL);

    return combo;
}

/**
 * gwy_combo_box_metric_unit_set_unit:
 * @combo: A combo box which was created with gwy_combo_box_metric_unit_new().
 * @from: The exponent of 10 the menu should start at (a multiple of 3, will be rounded downward if isn't).
 * @to: The exponent of 10 the menu should end at (a multiple of 3, will be rounded upward if isn't).
 * @unit: The unit to be prefixed.
 *
 * Changes the unit selection displayed by a metric unit combo box.
 *
 * Since: 2.5
 **/
void
gwy_combo_box_metric_unit_set_unit(GtkComboBox *combo,
                                   gint from,
                                   gint to,
                                   GwySIUnit *unit)
{
    GwyEnum *entries;

    entries = gwy_combo_box_metric_unit_make_enum(from, to, unit, NULL);
    gwy_enum_combo_box_set_model(combo, entries);
}

static GwyEnum*
gwy_combo_box_metric_unit_make_enum(gint from,
                                    gint to,
                                    GwySIUnit *unit,
                                    gint *nentries)
{
    GwyEnum *entries;
    GwySIValueFormat *format = NULL;
    gint i, n;

    from = from/3;
    to = (to + 2)/3;
    if (to < from)
        GWY_SWAP(gint, from, to);

    n = (to - from) + 1;
    entries = g_new(GwyEnum, n + 1);
    for (i = from; i <= to; i++) {
        format = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_MARKUP, 3*i, format);
        if (*format->units)
            entries[i - from].name = g_strdup(format->units);
        else
            entries[i - from].name = g_strdup("1");
        entries[i - from].value = 3*i;
    }
    entries[n].name = NULL;
    gwy_si_unit_value_format_free(format);

    if (nentries)
        *nentries = n;

    return entries;
}

static void
free_curve_combo_box_data(GObject *object)
{
    GObject *pixbuf;

    gwy_enum_freev(g_object_get_data(object, "gwy-combo-enum"));
    g_object_unref(g_object_get_data(object, "gwy-combo-model"));
    if ((pixbuf = g_object_get_data(object, "gwy-combo-pixbuf")))
        g_object_unref(pixbuf);
}

static void
render_curve_colour(G_GNUC_UNUSED GtkCellLayout *layout,
                    GtkCellRenderer *renderer,
                    GtkTreeModel *model,
                    GtkTreeIter *iter,
                    G_GNUC_UNUSED gpointer data)
{
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GObject *object;
    GdkPixbuf *pixbuf;
    GwyRGBA *color;
    GwyEnum *item;
    guint32 pixel;

    object = G_OBJECT(renderer);
    pixbuf = g_object_get_data(object, "gwy-combo-pixbuf");
    g_return_if_fail(pixbuf);
    gmodel = g_object_get_data(object, "gwy-combo-model");
    g_return_if_fail(gmodel);

    gtk_tree_model_get(model, iter, 0, &item, -1);
    gcmodel = gwy_graph_model_get_curve(gmodel, item->value);
    g_return_if_fail(gcmodel);

    g_object_get(gcmodel, "color", &color, NULL);
    pixel = 0xff | gwy_rgba_to_pixbuf_pixel(color);
    gwy_rgba_free(color);
    gdk_pixbuf_fill(pixbuf, pixel);
}

/**
 * gwy_combo_box_graph_curve_new:
 * @callback: A callback called when a new choice is selected (may be %NULL). If you want to just update an integer,
 *            you can use gwy_enum_combo_box_update_int() here.
 * @cbdata: User data passed to the callback.
 * @gmodel: A graph model.
 * @current: Index of currently selected curve.
 *
 * Creates an enum combo box with curves from a graph model.
 *
 * This function is intended for selection of curves from static graphs in graph modules.  The graph model is not
 * permitted to change.
 *
 * Returns: The newly created combo box as #GtkWidget.
 *
 * Since: 2.45
 **/
GtkWidget*
gwy_combo_box_graph_curve_new(GCallback callback, gpointer cbdata,
                              GwyGraphModel *gmodel, gint current)
{
    GtkCellRenderer *renderer;
    GwyGraphCurveModel *curve;
    GObject *object;
    GtkWidget *combo;
    GwyEnum *curves;
    GdkPixbuf *pixbuf;
    gint ncurves, i, width, height;

    g_object_ref(gmodel);
    ncurves = gwy_graph_model_get_n_curves(gmodel);

    curves = g_new(GwyEnum, ncurves + 1);
    for (i = 0; i < ncurves; i++) {
        curve = gwy_graph_model_get_curve(gmodel, i);
        g_object_get(curve, "description", &curves[i].name, NULL);
        if (!curves[i].name || !*curves[i].name)
            curves[i].name = g_strdup_printf("%s %d", _("Untitled"), i+1);
        curves[i].value = i;
    }
    curves[ncurves].name = NULL;
    combo = gwy_enum_combo_box_new(curves, ncurves, callback, cbdata, current, FALSE);

    /* Color */
    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    width |= 1;
    height |= 1;
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, GWY_ROUND(1.618*height), height);

    renderer = gtk_cell_renderer_pixbuf_new();
    /* XXX: We set the object data on the renderer because the thing passed as the first argument to
     * render_curve_colour() is some internal GtkCellView, not the combo itself. */
    object = G_OBJECT(renderer);
    g_object_set_data(object, "gwy-combo-model", gmodel);
    g_object_set_data(object, "gwy-combo-enum", curves);
    g_object_set_data(object, "gwy-combo-pixbuf", pixbuf);
    g_signal_connect(object, "destroy", G_CALLBACK(free_curve_combo_box_data), NULL);

    g_object_set(renderer, "pixbuf", pixbuf, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, FALSE);
    gtk_cell_layout_reorder(GTK_CELL_LAYOUT(combo), renderer, 0);
    gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(combo), renderer, render_curve_colour, NULL, NULL);

    return combo;
}

/**
 * gwy_combo_box_lawn_curve_new:
 * @callback: A callback called when a new choice is selected (may be %NULL). If you want to just update an integer,
 *            you can use gwy_enum_combo_box_update_int() here.
 * @cbdata: User data passed to the callback.
 * @lawn: A lawn curve map object.
 * @current: Index of currently selected curve.
 *
 * Creates an enum combo box with curves from a lawn curve map object.
 *
 * This function is intended for selection of curves from static lawn objects.  The lawn object is not permitted to
 * change.
 *
 * Returns: The newly created combo box as #GtkWidget.
 *
 * Since: 2.60
 **/
GtkWidget*
gwy_combo_box_lawn_curve_new(GCallback callback, gpointer cbdata,
                             GwyLawn *lawn, gint current)
{
    GtkWidget *combo;
    GwyEnum *curves;
    gint ncurves, i;
    const gchar *label;

    g_object_ref(lawn);
    ncurves = gwy_lawn_get_n_curves(lawn);

    curves = g_new(GwyEnum, ncurves + 1);
    for (i = 0; i < ncurves; i++) {
        label = gwy_lawn_get_curve_label(lawn, i);
        curves[i].name = (label && *label ? g_strdup(label) : g_strdup_printf("%s %d", _("Untitled"), i+1));
        curves[i].value = i;
    }
    curves[ncurves].name = NULL;
    combo = gwy_enum_combo_box_new(curves, ncurves, callback, cbdata, current, FALSE);
    g_object_set_data(G_OBJECT(combo), "gwy-combo-model", lawn);
    g_object_set_data(G_OBJECT(combo), "gwy-combo-enum", curves);
    g_signal_connect(combo, "destroy", G_CALLBACK(free_curve_combo_box_data), NULL);

    return combo;
}

/**
 * gwy_combo_box_lawn_segment_new:
 * @callback: A callback called when a new choice is selected (may be %NULL). If you want to just update an integer,
 *            you can use gwy_enum_combo_box_update_int() here.
 * @cbdata: User data passed to the callback.
 * @lawn: A lawn curve map object.
 * @current: Index of currently selected segment.
 *
 * Creates an enum combo box with segments from a lawn curve map object.
 *
 * This function is intended for selection of segments from static lawn objects.  The lawn object is not permitted to
 * change.
 *
 * Returns: The newly created combo box as #GtkWidget.
 *
 * Since: 2.60
 **/
GtkWidget*
gwy_combo_box_lawn_segment_new(GCallback callback,
                               gpointer cbdata,
                               GwyLawn *lawn,
                               gint current)
{
    GtkWidget *combo;
    GwyEnum *segments;
    gint nsegments, i;
    const gchar *label;

    g_object_ref(lawn);
    nsegments = gwy_lawn_get_n_segments(lawn);

    segments = g_new(GwyEnum, nsegments + 1);
    for (i = 0; i < nsegments; i++) {
        label = gwy_lawn_get_segment_label(lawn, i);
        segments[i].name = (label && *label ? g_strdup(label) : g_strdup_printf("%s %d", _("Segment"), i+1));
        segments[i].value = i;
    }
    segments[nsegments].name = NULL;
    combo = gwy_enum_combo_box_new(segments, nsegments, callback, cbdata, current, FALSE);
    g_object_set_data(G_OBJECT(combo), "gwy-combo-model", lawn);
    g_object_set_data(G_OBJECT(combo), "gwy-combo-enum", segments);
    g_signal_connect(combo, "destroy", G_CALLBACK(free_curve_combo_box_data), NULL);

    return combo;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwycombobox
 * @title: gwycombobox
 * @short_description: Combo box constructors
 * @see_also: <link linkend="libgwydgets-gwyradiobuttons">gwyradiobuttons</link> -- radio button constructors
 *
 * Combo boxes can be easily constructed from #GwyEnum's with gwy_enum_combo_box_new().   Here's an example of
 * construction of a combo box with three items:
 * <informalexample><programlisting>
 * typedef enum {
 *     MY_ENUM_FOO, MY_ENUM_BAR, MY_ENUM_BAZ
 * } MyEnum;
 *
 * static GwyEnum my_enum_fields[] = {
 *     { N_("Foo"), MY_ENUM_FOO },
 *     { N_("Bar"), MY_ENUM_BAR },
 *     { N_("Baz"), MY_ENUM_BAZ },
 * };
 *
 * static void
 * menu_callback(GtkWidget *combo, gpointer cbdata)
 * {
 *     MyEnum value;
 *
 *     value = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
 *     ...
 * }
 *
 * static void
 * function(void) {
 *     GtkWidget *combo;
 *     ...
 *
 *     combo = gwy_enum_combo_box_new(fields, G_N_ELEMENTS(fields),
 *                                    G_CALLBACK(menu_callback), NULL,
 *                                    MY_ENUM_FOO, TRUE);
 *     ...
 * }
 * </programlisting></informalexample>
 *
 * Many common Gwyddion enumerations have companion function returning corresponding #GwyEnum, see for example <link
 * linkend="libgwyprocess-gwyprocessenums">gwyprocessenums</link>, making combo box construction even easier.
 *
 * For example, a combo box with possible interpolation types can be constructed:
 * <informalexample><programlisting>
 * combo = gwy_enum_combo_box_new(gwy_interpolation_type_get_enum(), -1,
 *                                G_CALLBACK(menu_callback), NULL,
 *                                GWY_INTERPOLATION_BILINEAR, TRUE);
 * </programlisting></informalexample>
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
