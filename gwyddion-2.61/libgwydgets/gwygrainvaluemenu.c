/*
 *  $Id: gwygrainvaluemenu.c 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2007 David Necas (Yeti).
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
#include <stdarg.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/gwygrainvalue.h>
#include <libgwydgets/gwygrainvaluemenu.h>

typedef enum {
    GROUP_STATE_EMPTY = 0,
    GROUP_STATE_OFF = 1 << 0,
    GROUP_STATE_ON =  1 << 1,
    GROUP_STATE_INCONSISTENT = GROUP_STATE_OFF | GROUP_STATE_ON
} GroupState;

static void
gwy_grain_value_tree_view_expand_enabled(GtkTreeView *treeview);

static gboolean      count_enabled                 (GtkTreeModel *model,
                                                    GtkTreePath *path,
                                                    GtkTreeIter *iter,
                                                    gpointer user_data);
static gboolean      gather_enabled                (GtkTreeModel *model,
                                                    GtkTreePath *path,
                                                    GtkTreeIter *iter,
                                                    gpointer user_data);
static gboolean      restore_enabled               (GtkTreeModel *model,
                                                    GtkTreePath *path,
                                                    GtkTreeIter *iter,
                                                    gpointer user_data);
static void          update_group_states           (GtkTreeModel *model);
static gboolean      expand_enabled                (GtkTreeModel *model,
                                                    GtkTreePath *path,
                                                    GtkTreeIter *iter,
                                                    gpointer user_data);
static void          render_name                   (GtkTreeViewColumn *column,
                                                    GtkCellRenderer *renderer,
                                                    GtkTreeModel *model,
                                                    GtkTreeIter *iter,
                                                    gpointer user_data);
static void          render_symbol                 (GtkTreeViewColumn *column,
                                                    GtkCellRenderer *renderer,
                                                    GtkTreeModel *model,
                                                    GtkTreeIter *iter,
                                                    gpointer user_data);
static void          render_symbol_markup          (GtkTreeViewColumn *column,
                                                    GtkCellRenderer *renderer,
                                                    GtkTreeModel *model,
                                                    GtkTreeIter *iter,
                                                    gpointer user_data);
static void          text_color                    (GtkTreeView *treeview,
                                                    GwyGrainValue *gvalue,
                                                    GdkColor *color);
static void          render_enabled                (GtkTreeViewColumn *column,
                                                    GtkCellRenderer *renderer,
                                                    GtkTreeModel *model,
                                                    GtkTreeIter *iter,
                                                    gpointer user_data);
static void          enabled_activated             (GtkCellRendererToggle *renderer,
                                                    gchar *strpath,
                                                    GtkTreeModel *model);
static gboolean      selection_allowed             (GtkTreeSelection *selection,
                                                    GtkTreeModel *model,
                                                    GtkTreePath *path,
                                                    gboolean path_currently_selected,
                                                    gpointer user_data);
static gboolean      units_are_good                (GtkTreeView *treeview,
                                                    GwyGrainValue *gvalue);
static GtkTreeModel* gwy_grain_value_tree_model_new(gboolean show_id);
static void          inventory_item_updated        (GwyInventory *inventory,
                                                    guint pos,
                                                    GtkTreeStore *store);
static void          inventory_item_inserted       (GwyInventory *inventory,
                                                    guint pos,
                                                    GtkTreeStore *store);
static void          inventory_item_deleted        (GwyInventory *inventory,
                                                    guint pos,
                                                    GtkTreeStore *store);
static gboolean      find_grain_value              (GtkTreeModel *model,
                                                    GwyGrainValue *gvalue,
                                                    GtkTreeIter *iter);
static gboolean      find_grain_group(GtkTreeModel *model,
                 GwyGrainValueGroup group,
                 GtkTreeIter *iter);
static void          grain_value_store_finalized   (gpointer inventory,
                                                    GObject *store);

typedef struct {
    gboolean same_units;
    gint count;  /* temporary counter */
} GrainValueViewPrivate;

typedef struct {
    GroupState group_states[GWY_GRAIN_VALUE_GROUP_USER+1];
    GtkTreeIter user_group_iter;
    guint user_start_pos;
} GrainValueStorePrivate;

static GQuark priv_quark = 0;

/**
 * gwy_grain_value_tree_view_new:
 * @show_id: %TRUE to include grain id number among the values, %FALSE to
 *           exclude it.
 * @first_column: The first column to show (may be %NULL for no columns).
 * @...: %NULL-terminated list of columns to show.
 *
 * Creates a new tree view selector of grain values.
 *
 * Possible column names are <literal>"name"</literal> for the grain value
 * name, <literal>"symbol_markup"</literal> for the rich text symbol,
 * <literal>"symbol"</literal> for identifier-style symbol and
 * <literal>"enabled"</literal> for a checkbox column.
 *
 * The tree view selection is set to %GTK_SELECTION_BROWSE mode and it is
 * allowed only on leaves.
 *
 * Returns: A new tree view with grain values.
 *
 * Since: 2.8
 **/
GtkWidget*
gwy_grain_value_tree_view_new(gboolean show_id,
                              const gchar *first_column,
                              ...)
{
    GrainValueViewPrivate *priv;
    GtkTreeView *treeview;
    GtkTreeSelection *selection;
    GtkWidget *widget;
    GtkTreeModel *model;
    va_list ap;

    model = gwy_grain_value_tree_model_new(show_id);
    widget = gtk_tree_view_new_with_model(model);
    treeview = GTK_TREE_VIEW(widget);
    g_object_unref(model);

    priv = g_new0(GrainValueViewPrivate, 1);
    priv->same_units = TRUE;
    g_object_set_qdata_full(G_OBJECT(treeview), priv_quark, priv, g_free);

    va_start(ap, first_column);
    while (first_column) {
        GtkTreeViewColumn *column;
        GtkCellRenderer *renderer;
        gboolean expand;
        const gchar *title;

        column = gtk_tree_view_column_new();
        expand = FALSE;
        if (gwy_strequal(first_column, "name")) {
            renderer = gtk_cell_renderer_text_new();
            gtk_tree_view_column_pack_start(column, renderer, TRUE);
            g_object_set(renderer,
                         "ellipsize-set", TRUE,
                         "weight-set", TRUE,
                         "foreground-set", TRUE,
                         NULL);
            gtk_tree_view_column_set_cell_data_func(column, renderer,
                                                    render_name, treeview,
                                                    NULL);
            title = _("Quantity");
            expand = TRUE;
        }
        else if (gwy_strequal(first_column, "symbol_markup")) {
            renderer = gtk_cell_renderer_text_new();
            gtk_tree_view_column_pack_start(column, renderer, TRUE);
            gtk_tree_view_column_set_cell_data_func(column, renderer,
                                                    render_symbol_markup,
                                                    treeview,
                                                    NULL);
            title =_("Symbol");
        }
        else if (gwy_strequal(first_column, "symbol")) {
            renderer = gtk_cell_renderer_text_new();
            g_object_set(renderer,
                         "family", "monospace",
                         "family-set", TRUE,
                         "foreground-set", TRUE,
                         NULL);
            gtk_tree_view_column_pack_start(column, renderer, TRUE);
            gtk_tree_view_column_set_cell_data_func(column, renderer,
                                                    render_symbol, NULL,
                                                    NULL);
            title = _("Symbol");
        }
        else if (gwy_strequal(first_column, "enabled")) {
            renderer = gtk_cell_renderer_toggle_new();
            gtk_tree_view_column_pack_start(column, renderer, TRUE);
            gtk_tree_view_column_set_cell_data_func(column, renderer,
                                                    render_enabled, treeview,
                                                    NULL);
            g_signal_connect(renderer, "toggled",
                             G_CALLBACK(enabled_activated), model);
            title = _("Enabled");
        }
        else {
            g_warning("Unknown column `%s'", first_column);
            title = "Unknown";
        }

        gtk_tree_view_column_set_title(column, title);
        gtk_tree_view_column_set_alignment(column, 0.5);
        gtk_tree_view_column_set_expand(column, expand);
        gtk_tree_view_append_column(treeview, column);

        first_column = va_arg(ap, const gchar*);
    }
    va_end(ap);

    selection = gtk_tree_view_get_selection(treeview);
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    gtk_tree_selection_set_select_function(selection,
                                           selection_allowed, treeview, NULL);
    gtk_tree_view_collapse_all(treeview);

    return widget;
}

/**
 * gwy_grain_value_tree_view_set_expanded_groups:
 * @treeview: A tree view with grain values.
 * @expanded_bits: Integer with bits of #GwyGrainValueGroup set if the
 *                 corresponding group should be expanded.  Typically this
 *                 is either zero or a value previously obtained from
 *                 gwy_grain_value_tree_view_get_expanded_groups().
 *
 * Restores a grain value tree view group expansion state.
 *
 * Since: 2.8
 **/
void
gwy_grain_value_tree_view_set_expanded_groups(GtkTreeView *treeview,
                                              guint expanded_bits)
{
    GtkTreeModel *model;
    GtkTreeIter siter;

    g_return_if_fail(GTK_IS_TREE_VIEW(treeview));
    g_return_if_fail(priv_quark
                     && g_object_get_qdata(G_OBJECT(treeview), priv_quark));

    model = gtk_tree_view_get_model(treeview);
    if (!gtk_tree_model_get_iter_first(model, &siter)) {
        g_warning("Grain value tree view is empty?!");
        return;
    }

    do {
        GtkTreePath *path;
        GwyGrainValueGroup group;

        gtk_tree_model_get(model, &siter,
                           GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, &group,
                           -1);
        path = gtk_tree_model_get_path(model, &siter);
        if (expanded_bits & (1 << group))
            gtk_tree_view_expand_row(treeview, path, TRUE);
        else
            gtk_tree_view_collapse_row(treeview, path);
        gtk_tree_path_free(path);
    } while (gtk_tree_model_iter_next(model, &siter));
}

/**
 * gwy_grain_value_tree_view_get_expanded_groups:
 * @treeview: A tree view with grain values.
 *
 * Obtains the group expansion state of a grain value tree view.
 *
 * Returns: The expansion state, see
 *          gwy_grain_value_tree_view_set_expanded_groups() for details.
 *
 * Since: 2.8
 **/
guint
gwy_grain_value_tree_view_get_expanded_groups(GtkTreeView *treeview)
{
    GtkTreeModel *model;
    GtkTreeIter siter;
    guint expanded_bits = 0;

    g_return_val_if_fail(GTK_IS_TREE_VIEW(treeview), 0);
    g_return_val_if_fail(priv_quark
                         && g_object_get_qdata(G_OBJECT(treeview), priv_quark),
                         0);

    model = gtk_tree_view_get_model(treeview);
    if (!gtk_tree_model_get_iter_first(model, &siter)) {
        g_warning("Grain value tree view is empty?!");
        return 0;
    }

    do {
        GwyGrainValueGroup group;
        GtkTreePath *path;

        gtk_tree_model_get(model, &siter,
                           GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, &group,
                           -1);
        path = gtk_tree_model_get_path(model, &siter);
        if (gtk_tree_view_row_expanded(treeview, path))
            expanded_bits |= (1 << group);
        gtk_tree_path_free(path);
    } while (gtk_tree_model_iter_next(model, &siter));

    return expanded_bits;
}

/**
 * gwy_grain_value_tree_view_n_enabled:
 * @treeview: A tree view with grain values.
 *
 * Gets the number of enabled values in a grain value tree view.
 *
 * Enabled values are those with %GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED column
 * set to %TRUE in the model.
 *
 * Returns: The number of enabled values.
 *
 * Since: 2.8
 **/
gint
gwy_grain_value_tree_view_n_enabled(GtkTreeView *treeview)
{
    GtkTreeModel *model;
    GrainValueViewPrivate *priv;

    g_return_val_if_fail(GTK_IS_TREE_VIEW(treeview), 0);
    g_return_val_if_fail(priv_quark
                         && g_object_get_qdata(G_OBJECT(treeview), priv_quark),
                         0);

    priv = g_object_get_qdata(G_OBJECT(treeview), priv_quark);
    model = gtk_tree_view_get_model(treeview);
    priv->count = 0;
    gtk_tree_model_foreach(model, count_enabled, treeview);

    return priv->count;
}

static gboolean
count_enabled(GtkTreeModel *model,
              G_GNUC_UNUSED GtkTreePath *path,
              GtkTreeIter *iter,
              gpointer user_data)
{
    GtkTreeView *treeview = (GtkTreeView*)user_data;
    GwyGrainValue *gvalue;
    gboolean enabled;

    gtk_tree_model_get(model, iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED, &enabled,
                       -1);

    if (gvalue) {
        if (enabled) {
            GrainValueViewPrivate *priv;

            priv = g_object_get_qdata(G_OBJECT(treeview), priv_quark);
            if (units_are_good(treeview, gvalue))
                priv->count++;
        }
        g_object_unref(gvalue);
    }

    return FALSE;
}

/**
 * gwy_grain_value_tree_view_get_enabled:
 * @treeview: A tree view with grain values.
 *
 * Obtains the list of enabled values in a grain value tree view.
 *
 * Returns: The list of grain value names.  The list must be freed by the
 *          caller, the strings are however owned by the individual grain
 *          values and must not be freed.
 *
 * Since: 2.8
 **/
const gchar**
gwy_grain_value_tree_view_get_enabled(GtkTreeView *treeview)
{
    GtkTreeModel *model;
    GPtrArray *names;
    const gchar **retval;

    g_return_val_if_fail(GTK_IS_TREE_VIEW(treeview), NULL);
    g_return_val_if_fail(priv_quark
                         && g_object_get_qdata(G_OBJECT(treeview), priv_quark),
                         NULL);

    model = gtk_tree_view_get_model(treeview);
    names = g_ptr_array_new();
    gtk_tree_model_foreach(model, gather_enabled, names);
    g_ptr_array_add(names, NULL);
    retval = (const gchar**)names->pdata;
    g_ptr_array_free(names, FALSE);

    return retval;
}

static gboolean
gather_enabled(GtkTreeModel *model,
               G_GNUC_UNUSED GtkTreePath *path,
               GtkTreeIter *iter,
               gpointer user_data)
{
    GPtrArray *names = (GPtrArray*)user_data;
    GwyGrainValue *gvalue;
    gboolean enabled;

    gtk_tree_model_get(model, iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED, &enabled,
                       -1);

    if (gvalue) {
        if (enabled) {
            const gchar *name = gwy_resource_get_name(GWY_RESOURCE(gvalue));

            g_ptr_array_add(names, (gpointer)name);
        }
        g_object_unref(gvalue);
    }

    return FALSE;
}

/**
 * gwy_grain_value_tree_view_set_enabled:
 * @treeview: A tree view with grain values.
 * @names: Array of grain value names to enables.  All grain values not present
 *         here are disabled.
 *
 * Sets the set of enabled values in a grain value tree view.
 *
 * The tree is possibly expanded so that all enabled values are visible.
 *
 * Since: 2.8
 **/
void
gwy_grain_value_tree_view_set_enabled(GtkTreeView *treeview,
                                      gchar **names)
{
    GtkTreeModel *model;

    g_return_if_fail(GTK_IS_TREE_VIEW(treeview));
    g_return_if_fail(priv_quark
                     && g_object_get_qdata(G_OBJECT(treeview), priv_quark));

    model = gtk_tree_view_get_model(treeview);
    gtk_tree_model_foreach(model, restore_enabled, names);
    update_group_states(model);
    gwy_grain_value_tree_view_expand_enabled(treeview);
}

static gboolean
restore_enabled(GtkTreeModel *model,
                G_GNUC_UNUSED GtkTreePath *path,
                GtkTreeIter *iter,
                gpointer user_data)
{
    const gchar **names = (const gchar**)user_data;
    GwyGrainValue *gvalue;

    if (!names) {
        gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                           GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED, FALSE,
                           -1);
        return FALSE;
    }

    gtk_tree_model_get(model, iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       -1);
    if (gvalue) {
        const gchar *name = gwy_resource_get_name(GWY_RESOURCE(gvalue));

        while (*names && !gwy_strequal(name, *names))
            names++;

        gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                           GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED, !!*names,
                           -1);
        g_object_unref(gvalue);
    }

    return FALSE;
}

static gboolean
update_state(GtkTreeModel *model,
             G_GNUC_UNUSED GtkTreePath *path,
             GtkTreeIter *iter,
             gpointer user_data)
{
    GwyGrainValueGroup group;
    GwyGrainValue *gvalue;
    gboolean enabled;
    GroupState *states = (GroupState*)user_data;

    gtk_tree_model_get(model, iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, &group,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED, &enabled,
                       -1);
    if (gvalue) {
        if (enabled)
            states[group] |= GROUP_STATE_ON;
        else
            states[group] |= GROUP_STATE_OFF;
        g_object_unref(gvalue);
    }

    return FALSE;
}

/* This is called only explicitly as we always know the enabled state changes
 * because we always do it and otherwise we would have to prevent recursion. */
static void
update_group_states(GtkTreeModel *model)
{
    GroupState group_states[GWY_GRAIN_VALUE_GROUP_USER+1];
    GrainValueStorePrivate *priv;
    GtkTreeIter iter;

    gwy_clear(group_states, GWY_GRAIN_VALUE_GROUP_USER+1);

    gtk_tree_model_foreach(model, update_state, group_states);
    if (!gtk_tree_model_get_iter_first(model, &iter))
        return;

    priv = g_object_get_qdata(G_OBJECT(model), priv_quark);
    do {
        GwyGrainValueGroup group;
        GtkTreePath *path;

        gtk_tree_model_get(model, &iter,
                           GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, &group,
                           -1);
        if (group_states[group] != priv->group_states[group]) {
            priv->group_states[group] = group_states[group];
            path = gtk_tree_model_get_path(model, &iter);
            gtk_tree_model_row_changed(model, path, &iter);
            gtk_tree_path_free(path);
        }
    } while (gtk_tree_model_iter_next(model, &iter));
}

/* This was meant to be public, but we would prefer to inhibit the expansion of
 * enabled groups altogether. */
static void
gwy_grain_value_tree_view_expand_enabled(GtkTreeView *treeview)
{
    GtkTreeModel *model;

    g_return_if_fail(GTK_IS_TREE_VIEW(treeview));
    g_return_if_fail(priv_quark
                     && g_object_get_qdata(G_OBJECT(treeview), priv_quark));

    model = gtk_tree_view_get_model(treeview);
    gtk_tree_model_foreach(model, expand_enabled, treeview);
}

static gboolean
expand_enabled(GtkTreeModel *model,
               GtkTreePath *path,
               G_GNUC_UNUSED GtkTreeIter *iter,
               gpointer user_data)
{
    GtkTreeView *treeview = (GtkTreeView*)user_data;
    GwyGrainValue *gvalue;
    gboolean enabled;

    gtk_tree_model_get(model, iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED, &enabled,
                       -1);
    if (gvalue) {
        if (enabled)
            gtk_tree_view_expand_to_path(treeview, path);
        g_object_unref(gvalue);
    }

    return FALSE;
}

/**
 * gwy_grain_value_tree_view_select:
 * @treeview: A tree view with grain values.
 * @gvalue: The grain value to select.
 *
 * Selects a particular grain value in a grain value tree view.
 *
 * If the @gvalue group is currently unexpanded, it will be expanded to
 * show it, and the tree view may scroll to make it visible.
 *
 * Since: 2.8
 **/
void
gwy_grain_value_tree_view_select(GtkTreeView *treeview,
                                 GwyGrainValue *gvalue)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeIter iter;

    g_return_if_fail(GTK_IS_TREE_VIEW(treeview));
    g_return_if_fail(GWY_IS_GRAIN_VALUE(gvalue));
    g_return_if_fail(priv_quark
                     && g_object_get_qdata(G_OBJECT(treeview), priv_quark));

    model = gtk_tree_view_get_model(treeview);
    if (!find_grain_value(model, gvalue, &iter)) {
        g_warning("Grain value not in tree model.");
        return;
    }

    path = gtk_tree_model_get_path(model, &iter);
    gtk_tree_view_expand_to_path(treeview, path);
    gtk_tree_view_scroll_to_cell(treeview, path, NULL, FALSE, 0.0, 0.0);
    gtk_tree_path_free(path);

    selection = gtk_tree_view_get_selection(treeview);
    gtk_tree_selection_select_iter(selection, &iter);
}

/**
 * gwy_grain_value_tree_view_set_same_units:
 * @treeview: A tree view with grain values.
 * @same_units: %TRUE if the lateral and value units match and therefore all
 *              grain values are calculable, %FALSE if they don't match and
 *              values that require same units are disabled.
 *
 * Sets the availability of grain values that require the same lateral and
 * value units.
 *
 * This @same_units is %FALSE, grain values requiring matching units will be
 * disabled.  This means they will not be selectable, names and symbols will
 * be displayed greyed out, checkboxes will be made non-activatable (if they
 * are currently checked, they will not be unchecked but they will be displayed
 * as inconsistent).
 *
 * By default @same_units is %TRUE.
 *
 * Since: 2.8
 **/
void
gwy_grain_value_tree_view_set_same_units(GtkTreeView *treeview,
                                         gboolean same_units)
{
    GrainValueViewPrivate *priv;

    g_return_if_fail(GTK_IS_TREE_VIEW(treeview));
    g_return_if_fail(priv_quark);
    priv = g_object_get_qdata(G_OBJECT(treeview), priv_quark);
    g_return_if_fail(priv);

    same_units = !!same_units;
    if (same_units == priv->same_units)
        return;

    priv->same_units = same_units;
    if (GTK_WIDGET_DRAWABLE(treeview))
        gtk_widget_queue_draw(GTK_WIDGET(treeview));

    /* FIXME: What about if selection becomes disallowed, does GtkTreeView
     * handle this itself? */
}

static void
render_name(G_GNUC_UNUSED GtkTreeViewColumn *column,
            GtkCellRenderer *renderer,
            GtkTreeModel *model,
            GtkTreeIter *iter,
            gpointer user_data)
{
    GtkTreeView *treeview = (GtkTreeView*)user_data;
    PangoEllipsizeMode ellipsize;
    PangoWeight weight;
    GwyGrainValue *gvalue;
    GwyGrainValueGroup group;
    const gchar *name;
    GdkColor color;

    gtk_tree_model_get(model, iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, &group,
                       -1);
    ellipsize = gvalue ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE;
    weight = gvalue ? PANGO_WEIGHT_NORMAL : PANGO_WEIGHT_BOLD;
    text_color(treeview, gvalue, &color);
    if (gvalue) {
        name = gwy_resource_get_name(GWY_RESOURCE(gvalue));
        if (group != GWY_GRAIN_VALUE_GROUP_USER)
            name = gettext(name);
        g_object_unref(gvalue);
    }
    else
        name = gettext(gwy_grain_value_group_name(group));

    g_object_set(renderer,
                 "ellipsize", ellipsize,
                 "weight", weight,
                 "markup", name,
                 "foreground-gdk", &color,
                 NULL);
}

static void
render_symbol(G_GNUC_UNUSED GtkTreeViewColumn *column,
              GtkCellRenderer *renderer,
              GtkTreeModel *model,
              GtkTreeIter *iter,
              gpointer user_data)
{
    GtkTreeView *treeview = (GtkTreeView*)user_data;
    GwyGrainValue *gvalue;
    GdkColor color;

    gtk_tree_model_get(model, iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       -1);

    if (gvalue) {
        text_color(treeview, gvalue, &color);
        g_object_set(renderer,
                     "text", gwy_grain_value_get_symbol(gvalue),
                     "foreground-gdk", &color,
                     NULL);
        g_object_unref(gvalue);
    }
    else
        g_object_set(renderer, "text", "", NULL);
}

static void
render_symbol_markup(G_GNUC_UNUSED GtkTreeViewColumn *column,
                     GtkCellRenderer *renderer,
                     GtkTreeModel *model,
                     GtkTreeIter *iter,
                     gpointer user_data)
{
    GtkTreeView *treeview = (GtkTreeView*)user_data;
    GwyGrainValue *gvalue;
    GdkColor color;

    gtk_tree_model_get(model, iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       -1);

    if (gvalue) {
        text_color(treeview, gvalue, &color);
        g_object_set(renderer,
                     "markup", gwy_grain_value_get_symbol_markup(gvalue),
                     "foreground-gdk", &color,
                     NULL);
        g_object_unref(gvalue);
    }
    else
        g_object_set(renderer, "text", "", NULL);
}

static void
text_color(GtkTreeView *treeview,
           GwyGrainValue *gvalue,
           GdkColor *color)
{
    GtkStateType state;
    gboolean good_units;

    good_units = !gvalue || units_are_good(treeview, gvalue);
    state = good_units ? GTK_STATE_NORMAL : GTK_STATE_INSENSITIVE;
    *color = GTK_WIDGET(treeview)->style->text[state];
}

static void
render_enabled(G_GNUC_UNUSED GtkTreeViewColumn *column,
               GtkCellRenderer *renderer,
               GtkTreeModel *model,
               GtkTreeIter *iter,
               gpointer user_data)
{
    GtkTreeView *treeview = (GtkTreeView*)user_data;
    GwyGrainValue *gvalue;
    GwyGrainValueGroup group;
    gboolean enabled, good_units;

    gtk_tree_model_get(model, iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, &group,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED, &enabled,
                       -1);
    if (!gvalue) {
        GroupState state;
        GrainValueStorePrivate *priv;

        priv = g_object_get_qdata(G_OBJECT(model), priv_quark);
        state = priv->group_states[group];
        if (state == GROUP_STATE_EMPTY || state == GROUP_STATE_OFF)
            g_object_set(renderer,
                         "activatable", TRUE,
                         "active", FALSE,
                         "inconsistent", FALSE,
                         NULL);
        else
            g_object_set(renderer,
                         "activatable", TRUE,
                         "active", TRUE,
                         "inconsistent", state & GROUP_STATE_OFF,
                         NULL);
        return;
    }

    good_units = units_are_good(treeview, gvalue);
    g_object_set(renderer,
                 "active", enabled,
                 "sensitive", good_units,
                 "activatable", good_units,
                 "inconsistent", enabled && !good_units,
                 NULL);
    g_object_unref(gvalue);
}

static void
enabled_activated(GtkCellRendererToggle *renderer,
                  gchar *strpath,
                  GtkTreeModel *model)
{
    GtkTreeStore *store;
    GwyGrainValue *gvalue;
    GtkTreePath *path;
    GwyGrainValueGroup group;
    GtkTreeIter iter;
    gboolean enabled;

    path = gtk_tree_path_new_from_string(strpath);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

    gtk_tree_model_get(model, &iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, &group,
                       -1);

    g_object_get(renderer, "active", &enabled, NULL);
    store = GTK_TREE_STORE(model);

    if (gvalue) {
        gtk_tree_store_set(store, &iter,
                           GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED, !enabled,
                           -1);
        g_object_unref(gvalue);
    }
    else {
        GtkTreeIter siter;

        if (!find_grain_group(model, group, &siter)
            || !gtk_tree_model_iter_children(model, &iter, &siter))
            return;

        do {
            gtk_tree_store_set(store, &iter,
                               GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED, !enabled,
                               -1);
        } while (gtk_tree_model_iter_next(model, &iter));
    }
    update_group_states(model);
}

static gboolean
selection_allowed(G_GNUC_UNUSED GtkTreeSelection *selection,
                  GtkTreeModel *model,
                  GtkTreePath *path,
                  G_GNUC_UNUSED gboolean path_currently_selected,
                  gpointer user_data)
{
    GtkTreeView *treeview = (GtkTreeView*)user_data;
    GtkTreeIter iter;
    GwyGrainValue *gvalue;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue,
                       -1);
    if (!gvalue)
        return FALSE;

    g_object_unref(gvalue);

    return units_are_good(treeview, gvalue);
}

static gboolean
units_are_good(GtkTreeView *treeview,
               GwyGrainValue *gvalue)
{
    GrainValueViewPrivate *priv;

    priv = g_object_get_qdata(G_OBJECT(treeview), priv_quark);
    if (priv->same_units)
        return TRUE;

    return !(gwy_grain_value_get_flags(gvalue) & GWY_GRAIN_VALUE_SAME_UNITS);
}

static GtkTreeModel*
gwy_grain_value_tree_model_new(gboolean show_id)
{
    GrainValueStorePrivate *priv;
    GwyInventory *inventory;
    GtkTreeStore *store;
    GtkTreeIter siter, iter;
    GwyGrainValue *gvalue;
    GwyGrainValueGroup group, lastgroup;
    guint i, j, n;

    if (!priv_quark)
        priv_quark = g_quark_from_static_string("gwy-grain-value-chooser-data");

    priv = g_new0(GrainValueStorePrivate, 1);
    store = gtk_tree_store_new(3,
                               GWY_TYPE_GRAIN_VALUE,
                               GWY_TYPE_GRAIN_VALUE_GROUP,
                               G_TYPE_BOOLEAN);
    g_object_set_qdata_full(G_OBJECT(store), priv_quark, priv, g_free);

    inventory = gwy_grain_values();
    n = gwy_inventory_get_n_items(inventory);
    lastgroup = -1;
    for (i = j = 0; i < n; i++) {
        gvalue = gwy_inventory_get_nth_item(inventory, i);
        group = gwy_grain_value_get_group(gvalue);
        if (!show_id && group == GWY_GRAIN_VALUE_GROUP_ID)
            continue;

        if (group != lastgroup) {
            gtk_tree_store_insert_after(store, &siter, NULL,
                                        lastgroup != (GwyGrainValueGroup)-1
                                        ? &siter
                                        : NULL);
            gtk_tree_store_set(store, &siter,
                               GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, group,
                               -1);
            if (group == GWY_GRAIN_VALUE_GROUP_USER) {
                priv->user_group_iter = siter;
                priv->user_start_pos = i;
            }
            lastgroup = group;
            j = 0;
        }
        gtk_tree_store_insert_after(store, &iter, &siter, j ? &iter : NULL);
        gtk_tree_store_set(store, &iter,
                           GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, gvalue,
                           GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, group,
                           -1);
        j++;
    }

    /* Ensure User branch is present, even if empty */
    if (lastgroup != GWY_GRAIN_VALUE_GROUP_USER) {
        group = GWY_GRAIN_VALUE_GROUP_USER;
        gtk_tree_store_insert_after(store, &siter, NULL, i ? &siter : NULL);
        gtk_tree_store_set(store, &siter,
                           GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, group,
                           -1);
        priv->user_group_iter = siter;
        priv->user_start_pos = i;
    }

    g_signal_connect(inventory, "item-updated",
                     G_CALLBACK(inventory_item_updated), store);
    g_signal_connect(inventory, "item-inserted",
                     G_CALLBACK(inventory_item_inserted), store);
    g_signal_connect(inventory, "item-deleted",
                     G_CALLBACK(inventory_item_deleted), store);
    g_object_weak_ref(G_OBJECT(store), grain_value_store_finalized, inventory);

    return GTK_TREE_MODEL(store);
}

static void
inventory_item_updated(G_GNUC_UNUSED GwyInventory *inventory,
                       guint pos,
                       GtkTreeStore *store)
{
    GrainValueStorePrivate *priv;
    GtkTreeModel *model;
    GtkTreeIter siter, iter;
    GtkTreePath *path;

    priv = g_object_get_qdata(G_OBJECT(store), priv_quark);
    g_return_if_fail(pos >= priv->user_start_pos);
    siter = priv->user_group_iter;

    model = GTK_TREE_MODEL(store);
    gtk_tree_model_iter_nth_child(model, &iter, &siter,
                                  pos - priv->user_start_pos);
    path = gtk_tree_model_get_path(model, &iter);
    gtk_tree_model_row_changed(model, path, &iter);
    gtk_tree_path_free(path);
}

static void
inventory_item_inserted(GwyInventory *inventory,
                        guint pos,
                        GtkTreeStore *store)
{
    GrainValueStorePrivate *priv;
    GwyGrainValue *gvalue;
    GwyGrainValueGroup group;
    GtkTreeIter siter, iter;

    priv = g_object_get_qdata(G_OBJECT(store), priv_quark);
    g_return_if_fail(pos >= priv->user_start_pos);
    siter = priv->user_group_iter;

    gvalue = gwy_inventory_get_nth_item(inventory, pos);
    g_return_if_fail(GWY_IS_GRAIN_VALUE(gvalue));
    group = gwy_grain_value_get_group(gvalue);
    g_return_if_fail(group == GWY_GRAIN_VALUE_GROUP_USER);

    gtk_tree_store_insert(store, &iter, &siter, pos - priv->user_start_pos);
    gtk_tree_store_set(store, &iter,
                       GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, gvalue,
                       GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, group,
                       -1);
}

static void
inventory_item_deleted(G_GNUC_UNUSED GwyInventory *inventory,
                       guint pos,
                       GtkTreeStore *store)
{
    GrainValueStorePrivate *priv;
    GtkTreeIter siter, iter;

    priv = g_object_get_qdata(G_OBJECT(store), priv_quark);
    g_return_if_fail(pos >= priv->user_start_pos);
    siter = priv->user_group_iter;

    gtk_tree_store_insert(store, &iter, &siter, pos - priv->user_start_pos);
    gtk_tree_store_remove(store, &iter);
}

static gboolean
find_grain_group(GtkTreeModel *model,
                 GwyGrainValueGroup group,
                 GtkTreeIter *iter)
{
    GwyGrainValueGroup igroup;

    if (!gtk_tree_model_get_iter_first(model, iter))
        return FALSE;

    do {
        gtk_tree_model_get(model, iter,
                           GWY_GRAIN_VALUE_STORE_COLUMN_GROUP, &igroup,
                           -1);
        if (igroup == group)
            return TRUE;
    } while (gtk_tree_model_iter_next(model, iter));

    return FALSE;
}

static gboolean
find_grain_value(GtkTreeModel *model,
                 GwyGrainValue *gvalue,
                 GtkTreeIter *iter)
{
    GwyGrainValue *igvalue;
    GtkTreeIter siter;

    if (!find_grain_group(model, gwy_grain_value_get_group(gvalue), &siter)
        || !gtk_tree_model_iter_children(model, iter, &siter))
        return FALSE;

    do {
        gtk_tree_model_get(model, iter,
                           GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &igvalue,
                           -1);
        g_object_unref(igvalue);
        if (gvalue == igvalue)
            return TRUE;

    } while (gtk_tree_model_iter_next(model, iter));

    return FALSE;
}

static void
grain_value_store_finalized(gpointer inventory,
                            GObject *store)
{
    g_signal_handlers_disconnect_by_func(inventory,
                                         inventory_item_updated, store);
    g_signal_handlers_disconnect_by_func(inventory,
                                         inventory_item_inserted, store);
    g_signal_handlers_disconnect_by_func(inventory,
                                         inventory_item_deleted, store);
}

/************************** Documentation ****************************/

/**
 * SECTION:gwygrainvaluemenu
 * @title: gwygrainvaluemenu
 * @short_description: Grain value display/selector
 **/

/**
 * GwyGrainValueStoreColumn:
 * @GWY_GRAIN_VALUE_STORE_COLUMN_ITEM: Grain value itself (%NULL for
 *                                     non-leaves), the column type is
 *                                     #GwyGrainValue.
 * @GWY_GRAIN_VALUE_STORE_COLUMN_GROUP: Grain value group, useful namely for
 *                                      non-leaves (identical to the value
 *                                      group for leaves), the column type is
 *                                      #GwyGrainValueGroup.
 * @GWY_GRAIN_VALUE_STORE_COLUMN_ENABLED: Enabled/disabled state (meaning is
 *                                        undefined for non-leaves and
 *                                        reserved for future use), the column
 *                                        type is #gboolean.
 *
 * Columns of the grain value tree view #GtkTreeStore model.
 *
 * It must not be assumed these are the only columns in the tree store.
 *
 * Since: 2.8
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
