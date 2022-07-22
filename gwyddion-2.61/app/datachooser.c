/*
 *  $Id: datachooser.c 24088 2021-09-07 12:12:49Z yeti-dn $
 *  Copyright (C) 2006-2021 David Necas (Yeti), Petr Klapetek.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwycontainer.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/data-browser.h>
#include <app/datachooser.h>
#include "gwyappinternal.h"

enum {
    ICON_SIZE = 20
};

/*****************************************************************************
 *
 * Declaration.  Do not make it public yet.
 *
 *****************************************************************************/

struct _GwyDataChooser {
    GtkComboBox parent_instance;

    GtkTreeModel *filter;
    GtkListStore *store;

    GwyDataChooserFilterFunc filter_func;
    gpointer filter_data;
    GtkDestroyNotify filter_destroy;

    GtkTreeIter none_iter;
    gchar *none_label;

    GList *events;
    gulong watcher_id;
    guint update_id;

    GwyAppPage kind;
    gint* (*get_ids)(GwyContainer *container);
    gchar* (*get_title)(GwyContainer *container, gint id);
    GdkPixbuf* (*get_thumbnail)(GwyContainer *container, gint id,
                                gint width, gint height);
    void (*remove_watch)(gulong id);
};

struct _GwyDataChooserClass {
    GtkComboBoxClass parent_class;
};

/*****************************************************************************
 *
 * Implementation.
 *
 *****************************************************************************/

typedef struct {
    GwyContainer *container;
    gint id;
    GwyDataWatchEventType event_type;
} GwyDataChooserEvent;

/* To avoid "row-changed" when values are actually filled in.  Filling rows inside a cell renderer causes an obscure
 * Gtk+ crash. */
typedef struct {
    GdkPixbuf *thumb;
    gchar *name;
    gboolean is_none;
} Proxy;

enum {
    MODEL_COLUMN_CONTAINER,
    MODEL_COLUMN_ID,
    MODEL_COLUMN_PROXY,
    MODEL_NCOLUMNS
};

static void     gwy_data_chooser_finalize       (GObject *object);
static void     gwy_data_chooser_destroy        (GtkObject *object);
static gboolean gwy_data_chooser_is_visible     (GtkTreeModel *model,
                                                 GtkTreeIter *iter,
                                                 gpointer data);
static void     gwy_data_chooser_choose_whatever(GwyDataChooser *chooser);
static void     gwy_data_chooser_remove_events  (GwyDataChooser *chooser);

G_DEFINE_TYPE(GwyDataChooser, gwy_data_chooser, GTK_TYPE_COMBO_BOX)

static void
gwy_data_chooser_class_init(GwyDataChooserClass *klass)
{
    GtkObjectClass *object_class = GTK_OBJECT_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    object_class->destroy = gwy_data_chooser_destroy;

    gobject_class->finalize = gwy_data_chooser_finalize;
}

static void
gwy_data_chooser_finalize(GObject *object)
{
    GwyDataChooser *chooser;

    chooser = GWY_DATA_CHOOSER(object);

    g_free(chooser->none_label);
    if (chooser->filter_destroy)
        chooser->filter_destroy(chooser->filter_data);

    G_OBJECT_CLASS(gwy_data_chooser_parent_class)->finalize(object);
}

GType
gwy_app_data_id_get_type(void)
{
    static GType dataid_type = 0;

    if (G_UNLIKELY(!dataid_type)) {
        dataid_type = g_boxed_type_register_static("GwyAppDataId",
                                                   (GBoxedCopyFunc)gwy_app_data_id_copy,
                                                   (GBoxedFreeFunc)gwy_app_data_id_free);
    }

    return dataid_type;
}

/**
 * gwy_app_data_id_new:
 * @datano: Numeric identifier of data container.  Zero is used for none.
 * @id: Numeric identifier of a specific data item, such as channel or graph number.  Value -1 is used for none.
 *
 * Creates a new data identifier on heap.
 *
 * This is mostly useful for language bindings.
 *
 * Returns: Newly allocated data id.  It must be freed with gwy_app_data_id_free().
 *
 * Since: 2.47
 **/
GwyAppDataId*
gwy_app_data_id_new(gint datano,
                    gint id)
{
    GwyAppDataId *dataid = g_slice_new(GwyAppDataId);
    dataid->datano = datano;
    dataid->id = id;
    return dataid;
}

/**
 * gwy_app_data_id_copy:
 * @dataid: Data identifier.
 *
 * Creates a copy of data identifier.
 *
 * This is mostly useful for language bindings.
 *
 * Returns: Newly allocated data id.  It must be freed with gwy_app_data_id_free().
 *
 * Since: 2.47
 **/
GwyAppDataId*
gwy_app_data_id_copy(GwyAppDataId *dataid)
{
    return g_slice_dup(GwyAppDataId, dataid);
}

/**
 * gwy_app_data_id_free:
 * @dataid: Data identifier.
 *
 * Frees a data identifier.
 *
 * This is mostly useful for language bindings.
 *
 * Since: 2.47
 **/
void
gwy_app_data_id_free(GwyAppDataId *dataid)
{
    g_slice_free(GwyAppDataId, dataid);
}

static void
proxy_free(Proxy *proxy)
{
    GWY_OBJECT_UNREF(proxy->thumb);
    g_free(proxy->name);
    g_free(proxy);
}

static void
gwy_data_chooser_free_proxies(GtkListStore *store)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    Proxy *proxy;

    model = GTK_TREE_MODEL(store);
    if (!gtk_tree_model_get_iter_first(model, &iter))
        return;

    do {
        gtk_tree_model_get(model, &iter, MODEL_COLUMN_PROXY, &proxy, -1);
        proxy_free(proxy);
    } while (gtk_tree_model_iter_next(model, &iter));
}

static void
gwy_data_chooser_destroy(GtkObject *object)
{
    GwyDataChooser *chooser;
    GtkComboBox *combo;
    GtkTreeModel *model;

    chooser = GWY_DATA_CHOOSER(object);
    combo = GTK_COMBO_BOX(object);
    model = gtk_combo_box_get_model(combo);
    if (model) {
        gwy_data_chooser_free_proxies(chooser->store);
        gtk_combo_box_set_model(combo, NULL);
        GWY_OBJECT_UNREF(chooser->filter);
        GWY_OBJECT_UNREF(chooser->store);
    }
    if (chooser->watcher_id) {
        if (chooser->remove_watch)
            chooser->remove_watch(chooser->watcher_id);
        else {
            g_warning("Watcher removal function missing?");
        }
        chooser->watcher_id = 0;
    }
    if (chooser->update_id)
        g_source_remove(chooser->update_id);

    GTK_OBJECT_CLASS(gwy_data_chooser_parent_class)->destroy(object);
}

static void
gwy_data_chooser_init(GwyDataChooser *chooser)
{
    GtkTreeModelFilter *filter;
    GtkComboBox *combo;
    GtkTreeIter iter;
    Proxy *proxy;

    chooser->store = gtk_list_store_new(MODEL_NCOLUMNS, GWY_TYPE_CONTAINER, G_TYPE_INT, G_TYPE_POINTER);
    chooser->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(chooser->store), NULL);
    filter = GTK_TREE_MODEL_FILTER(chooser->filter);
    gtk_tree_model_filter_set_visible_func(filter, gwy_data_chooser_is_visible, chooser, NULL);

    /* Create `none' row */
    proxy = g_new0(Proxy, 1);
    /* XXX: size */
    proxy->thumb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 20, 20);
    gdk_pixbuf_fill(proxy->thumb, 0x00000000);
    proxy->name = g_strdup(gwy_sgettext("channel|None"));
    proxy->is_none = TRUE;
    gtk_list_store_insert_with_values(chooser->store, &iter, 0,
                                      MODEL_COLUMN_ID, -1,
                                      MODEL_COLUMN_PROXY, proxy,
                                      -1);

    combo = GTK_COMBO_BOX(chooser);
    gtk_combo_box_set_model(combo, chooser->filter);
    gtk_combo_box_set_wrap_width(combo, 1);
}

/**
 * gwy_data_chooser_set_active:
 * @chooser: A data chooser.
 * @data: Container to select, %NULL to select none (if the chooser contains `none' item).
 * @id: Id of particular data to select in @data.
 *
 * Selects a data in a data chooser.
 *
 * Returns: %TRUE if selected item was set.
 **/
gboolean
gwy_data_chooser_set_active(GwyDataChooser *chooser,
                            GwyContainer *data,
                            gint id)
{
    GwyContainer *container;
    GtkComboBox *combo;
    GtkTreeIter iter;
    gint dataid;

    g_return_val_if_fail(GWY_IS_DATA_CHOOSER(chooser), FALSE);

    if (!gtk_tree_model_get_iter_first(chooser->filter, &iter))
        return FALSE;

    combo = GTK_COMBO_BOX(chooser);
    if (!data) {
        if (chooser->none_label) {
            /* Rely on none being always the first. */
            gtk_combo_box_set_active_iter(combo, &iter);
            return TRUE;
        }
        return FALSE;
    }

    do {
        gtk_tree_model_get(chooser->filter, &iter,
                           MODEL_COLUMN_CONTAINER, &container,
                           MODEL_COLUMN_ID, &dataid,
                           -1);
        if (container)
            g_object_unref(container);
        if (container == data && dataid == id) {
            gtk_combo_box_set_active_iter(combo, &iter);
            return TRUE;
        }
    } while (gtk_tree_model_iter_next(chooser->filter, &iter));

    return FALSE;
}

/**
 * gwy_data_chooser_get_active:
 * @chooser: A data chooser.
 * @id: Location to store selected data id to (may be %NULL).
 *
 * Gets the selected item in a data chooser.
 *
 * Returns: The container selected data lies in, %NULL if nothing is selected or `none' item is selected.
 **/
GwyContainer*
gwy_data_chooser_get_active(GwyDataChooser *chooser,
                            gint *id)
{
    GwyContainer *container;
    GtkComboBox *combo;
    GtkTreeIter iter;
    gint dataid;

    g_return_val_if_fail(GWY_IS_DATA_CHOOSER(chooser), NULL);

    combo = GTK_COMBO_BOX(chooser);
    if (!gtk_combo_box_get_active_iter(combo, &iter))
        return NULL;

    gtk_tree_model_get(chooser->filter, &iter,
                       MODEL_COLUMN_CONTAINER, &container,
                       MODEL_COLUMN_ID, &dataid,
                       -1);
    if (container)
        g_object_unref(container);

    if (id)
        *id = dataid;

    return container;
}

/**
 * gwy_data_chooser_set_active_id:
 * @chooser: A data chooser.
 * @id: Data item to select.
 *
 * Selects a data in a data chooser using numerical identifiers.
 *
 * Passing %NULL as @id is permitted as a request to select the ‘none’ item.
 *
 * Returns: %TRUE if selected item was set.
 *
 * Since: 2.41
 **/
gboolean
gwy_data_chooser_set_active_id(GwyDataChooser *chooser,
                               const GwyAppDataId *id)
{
    GwyContainer *data;

    if (!id)
        return gwy_data_chooser_set_active(chooser, NULL, -1);

    data = gwy_app_data_browser_get(id->datano);
    return gwy_data_chooser_set_active(chooser, data, data ? id->id : -1);
}

/**
 * gwy_data_chooser_get_active_id:
 * @chooser: A data chooser.
 * @id: Location for the id selected data item.
 *
 * Gets the selected item in a data chooser as numerical identifiers.
 *
 * Returns: %TRUE if any actual data item is selected.  %FALSE is nothing is selected or the ‘none’ item is selected.
 *
 * Since: 2.41
 **/
gboolean
gwy_data_chooser_get_active_id(GwyDataChooser *chooser,
                               GwyAppDataId *id)
{
    GwyContainer *data;
    gint itemid;

    data = gwy_data_chooser_get_active(chooser, &itemid);
    if (!id)
        return !!data;

    if (!data) {
        id->datano = 0;
        id->id = -1;
        return FALSE;
    }

    id->datano = gwy_app_data_browser_get_number(data);
    id->id = itemid;
    return TRUE;
}

static gboolean
gwy_data_chooser_is_visible(GtkTreeModel *model,
                            GtkTreeIter *iter,
                            gpointer data)
{
    GwyDataChooser *chooser = (GwyDataChooser*)data;
    GwyContainer *container;
    guint id;

    gtk_tree_model_get(model, iter,
                       MODEL_COLUMN_CONTAINER, &container,
                       MODEL_COLUMN_ID, &id,
                       -1);

    /* Handle `none' explicitly */
    if (!container)
        return chooser->none_label != NULL;

    g_object_unref(container);
    if (!chooser->filter_func)
        return TRUE;

    return chooser->filter_func(container, id, chooser->filter_data);
}

/**
 * gwy_data_chooser_set_filter:
 * @chooser: A data chooser.
 * @filter: The filter function.
 * @user_data: The data passed to @filter.
 * @destroy: Destroy notifier of @user_data or %NULL.
 *
 * Sets the filter applied to a data chooser.
 *
 * The display of an item corresponding to no data is controlled by gwy_data_chooser_set_none(), @filter function is
 * only called for real data.
 *
 * Use gwy_data_chooser_refilter() to update the list if the filter depends on external state and that changes.
 **/
void
gwy_data_chooser_set_filter(GwyDataChooser *chooser,
                            GwyDataChooserFilterFunc filter,
                            gpointer user_data,
                            GtkDestroyNotify destroy)
{
    g_return_if_fail(GWY_IS_DATA_CHOOSER(chooser));

    if (chooser->filter_destroy)
        chooser->filter_destroy(chooser->filter_data);

    chooser->filter_func = filter;
    chooser->filter_data = user_data;
    chooser->filter_destroy = destroy;
    gwy_data_chooser_refilter(chooser);
}

/**
 * gwy_data_chooser_get_filter:
 * @chooser: A data chooser.
 *
 * Gets the tree model filter used in a data chooser.
 *
 * In general, you should not access the filter directly.  An exception being gtk_tree_model_filter_refilter() when
 * the filtering functions given in gwy_data_chooser_set_filter() depends on external state and that state changes.
 * However, gwy_data_chooser_refilter() is usually more useful.
 *
 * Returns: The #GtkTreeModelFilter object used by the chooser.
 **/
GtkTreeModel*
gwy_data_chooser_get_filter(GwyDataChooser *chooser)
{
    g_return_val_if_fail(GWY_IS_DATA_CHOOSER(chooser), NULL);
    return chooser->filter;
}

/**
 * gwy_data_chooser_refilter:
 * @chooser: A data chooser.
 *
 * Reruns the filter function of a data chooser.
 *
 * This function is useful when the filtering functions given in gwy_data_chooser_set_filter() depends on external
 * state and that state changes.
 *
 * If the currently selected item becomes filtered out the chooser selects the no-data item if enabled.  If the
 * no-data item is disabled but the list contains some items then an arbitrary item is selected.  At present, this
 * means the first item in the list.
 *
 * Since: 2.41
 **/
void
gwy_data_chooser_refilter(GwyDataChooser *chooser)
{
    GtkTreeModel *model;
    GwyContainer *data;
    gint id;

    g_return_if_fail(GWY_IS_DATA_CHOOSER(chooser));
    data = gwy_data_chooser_get_active(chooser, &id);
    model = g_object_ref(chooser->filter);
    gtk_combo_box_set_model(GTK_COMBO_BOX(chooser), NULL);
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(chooser->filter));
    gtk_combo_box_set_model(GTK_COMBO_BOX(chooser), model);
    g_object_unref(model);
    gwy_data_chooser_set_active(chooser, data, id);
    gwy_data_chooser_choose_whatever(chooser);
}

/**
 * gwy_data_chooser_get_none:
 * @chooser: A data chooser.
 *
 * Gets the label of the item corresponding to no data.
 *
 * Returns: The label corresponding to no data, an empty string for the default label and %NULL if the chooser does
 *          not display the no-data item.
 **/
const gchar*
gwy_data_chooser_get_none(GwyDataChooser *chooser)
{
    g_return_val_if_fail(GWY_IS_DATA_CHOOSER(chooser), NULL);

    return chooser->none_label;
}

/**
 * gwy_data_chooser_set_none:
 * @chooser: A data chooser.
 * @none: Label to use for item corresponding to no data. Passing %NULL, disables such an item, an empty string
 *        enables it with the default label.
 *
 * Sets the label of the item corresponding to no data.
 **/
void
gwy_data_chooser_set_none(GwyDataChooser *chooser,
                          const gchar *none)
{
    GtkTreeIter iter;
    Proxy *proxy;

    g_return_if_fail(GWY_IS_DATA_CHOOSER(chooser));
    gwy_assign_string(&chooser->none_label, none);

    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(chooser->store), &iter);
    gtk_tree_model_get(GTK_TREE_MODEL(chooser->store), &iter, MODEL_COLUMN_PROXY, &proxy, -1);
    gwy_assign_string(&proxy->name,
                      chooser->none_label && *chooser->none_label
                      ? chooser->none_label
                      : gwy_sgettext("channel|None"));
    gwy_list_store_row_changed(chooser->store, &iter, NULL, 0);

    gwy_data_chooser_choose_whatever(chooser);
}

/**
 * gwy_data_chooser_choose_whatever:
 * @chooser: A data chooser.
 *
 * Choose arbitrary item if none is active.
 **/
static void
gwy_data_chooser_choose_whatever(GwyDataChooser *chooser)
{
    GtkComboBox *combo;
    GtkTreeIter iter;

    combo = GTK_COMBO_BOX(chooser);
    if (gtk_combo_box_get_active_iter(combo, &iter))
        return;

    /* The ‘none’ item is always first.  So we choose that if enabled. */
    if (gtk_tree_model_get_iter_first(chooser->filter, &iter))
        gtk_combo_box_set_active_iter(combo, &iter);
}

/* Used as destroy_func of gwy_data_chooser_process_events() */
static void
gwy_data_chooser_remove_events(GwyDataChooser *chooser)
{
    while (chooser->events) {
        g_free(chooser->events->data);
        chooser->events = g_list_delete_link(chooser->events, chooser->events);
    }
    chooser->update_id = 0;
}

/* Returns TRUE if we find the item exactly, otherwise it finds the position where to insert the item at */
static gboolean
gwy_data_chooser_find_data(GwyDataChooser *chooser,
                           GwyContainer *container,
                           gint id,
                           gint *position)
{
    GtkTreeModel *model = GTK_TREE_MODEL(chooser->store);
    GtkTreeIter iter;

    *position = 0;
    if (!gtk_tree_model_get_iter_first(model, &iter))
        return FALSE;

    do {
        GwyContainer *container2;
        gint id2;

        gtk_tree_model_get(model, &iter,
                           MODEL_COLUMN_CONTAINER, &container2,
                           MODEL_COLUMN_ID, &id2,
                           -1);
        if (container2)
            g_object_unref(container2);
        if (container2 == container && id2 == id)
            return TRUE;
        if (container2 > container || (container2 == container && id2 > id))
            return FALSE;
        (*position)++;
    } while (gtk_tree_model_iter_next(model, &iter));

    return FALSE;
}

static gboolean
gwy_data_chooser_process_events(gpointer user_data)
{
    GwyDataChooser *chooser = (GwyDataChooser*)user_data;
    GtkTreeModel *model = GTK_TREE_MODEL(chooser->store);
    GtkTreeIter iter;
    GList *item;

    chooser->events = g_list_reverse(chooser->events);
    for (item = chooser->events; item; item = g_list_next(item)) {
        GwyDataChooserEvent *event = (GwyDataChooserEvent*)item->data;
        gboolean found;
        gint position;
        Proxy *proxy;

        found = gwy_data_chooser_find_data(chooser, event->container, event->id, &position);
        gwy_debug("id %u, type %u, container %p, found %d", event->id, event->event_type, event->container, found);

        if (found) {
            if (event->event_type == GWY_DATA_WATCH_EVENT_ADDED) {
                g_warning("Attempted to add an item already present %p, %d.", event->container, event->id);
                event->event_type = GWY_DATA_WATCH_EVENT_CHANGED;
            }
        }
        else {
            if (event->event_type == GWY_DATA_WATCH_EVENT_CHANGED) {
                g_warning("Attempted to change an item not present yet %p, %d.", event->container, event->id);
                event->event_type = GWY_DATA_WATCH_EVENT_ADDED;
            }
            if (event->event_type == GWY_DATA_WATCH_EVENT_REMOVED) {
                g_warning("Attempted to remove a nonexistent item %p, %d.", event->container, event->id);
                continue;
            }
        }

        if (event->event_type == GWY_DATA_WATCH_EVENT_ADDED) {
            proxy = g_new0(Proxy, 1);
            gtk_list_store_insert_with_values(chooser->store, &iter, position,
                                              MODEL_COLUMN_CONTAINER, event->container,
                                              MODEL_COLUMN_ID, event->id,
                                              MODEL_COLUMN_PROXY, proxy,
                                              -1);
        }
        else if (event->event_type == GWY_DATA_WATCH_EVENT_CHANGED) {
            found = gtk_tree_model_iter_nth_child(model, &iter, NULL, position);
            gtk_tree_model_get(model, &iter, MODEL_COLUMN_PROXY, &proxy, -1);
            GWY_OBJECT_UNREF(proxy->thumb);
            g_free(proxy->name);
            proxy->name = NULL;
            /* XXX XXX XXX: We cannot emit "row-changed" on the tree model here for some reason.  If we do, we get
             * CRITICAL message
             *
             * gtk_menu_attach: assertion 'left_attach < right_attach' failed
             *
             * because property "right_attach" of the combo box menu is -1 now (like unattached?), it gets typecast to
             * guint as G_MAXUINT and things only go downhill from there.
             *
             * This does not occur in Fedora, but does in Ubuntu.  No RedHat patch seems relevant, so who knows...
             *
             * Work around by pretending the row did not change (we can do that because the tree model only contains
             * a pointer) and asking the widget to redraw itself. */
            gtk_widget_queue_draw(GTK_WIDGET(chooser));
        }
        else if (event->event_type == GWY_DATA_WATCH_EVENT_REMOVED) {
            gtk_tree_model_iter_nth_child(model, &iter, NULL, position);
            gtk_tree_model_get(model, &iter, MODEL_COLUMN_PROXY, &proxy, -1);
            proxy_free(proxy);
            gtk_list_store_remove(chooser->store, &iter);
        }
        else {
            g_assert_not_reached();
        }
    }

    gwy_data_chooser_choose_whatever(chooser);
    return FALSE;
}

static void
gwy_data_chooser_receive_event(GwyContainer *data,
                               gint id,
                               GwyDataWatchEventType event_type,
                               gpointer user_data)
{
    GwyDataChooser *chooser = (GwyDataChooser*)user_data;
    GwyDataChooserEvent *event = NULL;
    GList *item;

    /* Find if we already have an event on the same object. */
    for (item = chooser->events; item; item = g_list_next(item)) {
        GwyDataChooserEvent *thisevent = (GwyDataChooserEvent*)item->data;
        if (thisevent->container == data && thisevent->id == id) {
            event = thisevent;
            break;
        }
    }

    if (event) {
        if (event_type == GWY_DATA_WATCH_EVENT_REMOVED) {
            if (event->event_type == GWY_DATA_WATCH_EVENT_REMOVED)
                g_warning("Got event REMOVED twice on %p, %d.", data, id);
            else if (event->event_type == GWY_DATA_WATCH_EVENT_ADDED) {
                /* Get rid of the data altogether, as we do not display it. */
                chooser->events = g_list_delete_link(chooser->events, item);
                return;
            }
            else
                event->event_type = event_type;
        }
        else if (event_type == GWY_DATA_WATCH_EVENT_ADDED) {
            g_warning("Got event ADDED twice on %p, %d.", data, id);
        }
        else if (event_type == GWY_DATA_WATCH_EVENT_CHANGED) {
            if (event->event_type == GWY_DATA_WATCH_EVENT_REMOVED)
                g_warning("Got event CHANGED after REMOVED on %p, %d.", data, id);
            /* Keep the item type.   ADDED is as good as CHANGED for the update processing and it permits removal when
             * a later REMOVED comes. */
        }
        else {
            g_assert_not_reached();
        }
    }
    else {
        event = g_new(GwyDataChooserEvent, 1);
        event->container = data;
        event->id = id;
        event->event_type = event_type;
        /* Use g_list_prepend() for efficiency; we revert the order when we process the items. */
        chooser->events = g_list_prepend(chooser->events, event);
    }

    if (chooser->events && !chooser->update_id) {
        guint sid;
        sid = g_idle_add_full(G_PRIORITY_HIGH_IDLE,
                              gwy_data_chooser_process_events, chooser,
                              (GDestroyNotify)&gwy_data_chooser_remove_events);
        chooser->update_id = sid;
    }
}

static void
gwy_data_chooser_fill(GwyContainer *data,
                      gpointer user_data)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(user_data);
    GtkListStore *store;
    GtkTreeIter iter;
    Proxy *proxy;
    gint *ids;
    gint i;

    store = chooser->store;
    ids = chooser->get_ids(data);
    for (i = 0; ids[i] >= 0; i++) {
        gwy_debug("inserting %p %d", data, ids[i]);
        proxy = g_new0(Proxy, 1);
        gtk_list_store_insert_with_values(store, &iter, G_MAXINT,
                                          MODEL_COLUMN_CONTAINER, data,
                                          MODEL_COLUMN_ID, ids[i],
                                          MODEL_COLUMN_PROXY, proxy,
                                          -1);
    }
    g_free(ids);
}

static void
gwy_data_chooser_render_name(G_GNUC_UNUSED GtkCellLayout *layout,
                             GtkCellRenderer *renderer,
                             GtkTreeModel *model,
                             GtkTreeIter *iter,
                             gpointer user_data)
{
    GwyDataChooser *chooser = (GwyDataChooser*)user_data;
    GwyContainer *container;
    Proxy *proxy;
    gint id;

    gtk_tree_model_get(model, iter, MODEL_COLUMN_PROXY, &proxy, -1);
    if (!proxy->name) {
        gtk_tree_model_get(model, iter,
                           MODEL_COLUMN_CONTAINER, &container,
                           MODEL_COLUMN_ID, &id,
                           -1);
        proxy->name = chooser->get_title(container, id);
        g_object_unref(container);
    }
    g_object_set(renderer,
                 "text", proxy->name,
                 "style", proxy->is_none ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL,
                 NULL);
}

static void
gwy_data_chooser_render_icon(G_GNUC_UNUSED GtkCellLayout *layout,
                             GtkCellRenderer *renderer,
                             GtkTreeModel *model,
                             GtkTreeIter *iter,
                             gpointer user_data)
{
    GwyDataChooser *chooser = (GwyDataChooser*)user_data;
    GwyContainer *container;
    gint id;
    Proxy *proxy;

    gtk_tree_model_get(model, iter, MODEL_COLUMN_PROXY, &proxy, -1);
    if (!proxy->thumb) {
        gtk_tree_model_get(model, iter,
                           MODEL_COLUMN_CONTAINER, &container,
                           MODEL_COLUMN_ID, &id,
                           -1);
        proxy->thumb = chooser->get_thumbnail(container, id, ICON_SIZE, ICON_SIZE);
        g_object_unref(container);
    }
    g_object_set(renderer, "pixbuf", proxy->thumb, NULL);
}

/*****************************************************************************
 *
 * Channels.
 *
 *****************************************************************************/

static void
gwy_data_chooser_channels_setup_watcher(GwyDataChooser *chooser)
{
    gint id;

    id = gwy_app_data_browser_add_channel_watch(gwy_data_chooser_receive_event, chooser);
    chooser->watcher_id = id;
}

/**
 * gwy_data_chooser_new_channels:
 *
 * Creates a data chooser for data channels.
 *
 * Returns: A new channel chooser.  Nothing may be assumed about the type and properties of the returned widget as
 *          they can change in the future.
 **/
GtkWidget*
gwy_data_chooser_new_channels(void)
{
    GwyDataChooser *chooser;
    GtkCellRenderer *renderer;
    GtkCellLayout *layout;

    chooser = (GwyDataChooser*)g_object_new(GWY_TYPE_DATA_CHOOSER, NULL);
    chooser->kind = GWY_PAGE_CHANNELS;
    chooser->get_ids = gwy_app_data_browser_get_data_ids;
    chooser->get_title = gwy_app_get_data_field_title;
    chooser->get_thumbnail = gwy_app_get_channel_thumbnail;
    chooser->remove_watch = gwy_app_data_browser_remove_channel_watch;
    gwy_app_data_browser_foreach(gwy_data_chooser_fill, chooser);
    layout = GTK_CELL_LAYOUT(chooser);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(layout, renderer, FALSE);
    gtk_cell_layout_set_cell_data_func(layout, renderer, gwy_data_chooser_render_icon, chooser, NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, "style-set", TRUE, NULL);
    gtk_cell_layout_pack_start(layout, renderer, TRUE);
    gtk_cell_layout_set_cell_data_func(layout, renderer, gwy_data_chooser_render_name, chooser, NULL);

    gwy_data_chooser_choose_whatever(chooser);
    gwy_data_chooser_channels_setup_watcher(chooser);

    return (GtkWidget*)chooser;
}

/*****************************************************************************
 *
 * Volume data.
 *
 *****************************************************************************/

/**
 * gwy_data_chooser_new_volumes:
 *
 * Creates a data chooser for volume data.
 *
 * Returns: A new volume chooser.  Nothing may be assumed about the type and properties of the returned widget as they
 *          can change in the future.
 *
 * Since: 2.33
 **/
GtkWidget*
gwy_data_chooser_new_volumes(void)
{
    GwyDataChooser *chooser;
    GtkCellRenderer *renderer;
    GtkCellLayout *layout;

    chooser = (GwyDataChooser*)g_object_new(GWY_TYPE_DATA_CHOOSER, NULL);
    chooser->kind = GWY_PAGE_VOLUMES;
    chooser->get_title = gwy_app_get_brick_title;
    chooser->get_thumbnail = gwy_app_get_volume_thumbnail;
    chooser->get_ids = gwy_app_data_browser_get_volume_ids;
    gwy_app_data_browser_foreach(gwy_data_chooser_fill, chooser);
    layout = GTK_CELL_LAYOUT(chooser);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(layout, renderer, FALSE);
    gtk_cell_layout_set_cell_data_func(layout, renderer, gwy_data_chooser_render_icon, chooser, NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, "style-set", TRUE, NULL);
    gtk_cell_layout_pack_start(layout, renderer, TRUE);
    gtk_cell_layout_set_cell_data_func(layout, renderer, gwy_data_chooser_render_name, chooser, NULL);

    gwy_data_chooser_choose_whatever(chooser);
    /* FIXME: Needs data browser support. */
    /* gwy_data_chooser_volumes_setup_watcher(chooser); */

    return (GtkWidget*)chooser;
}

/*****************************************************************************
 *
 * Graphs.
 *
 *****************************************************************************/

static gchar*
get_graph_title(GwyContainer *data, gint id)
{
    GQuark quark = gwy_app_get_graph_key_for_id(id);
    GwyGraphModel *gmodel = (GwyGraphModel*)gwy_container_get_object(data, quark);
    gchar *s, *title;

    g_return_val_if_fail(GWY_IS_GRAPH_MODEL(gmodel), NULL);
    g_object_get(gmodel, "title", &s, NULL);
    title = g_strdup_printf("%s (%d)", s, gwy_graph_model_get_n_curves(gmodel));
    g_free(s);

    return title;
}

static void
gwy_data_chooser_graphs_setup_watcher(GwyDataChooser *chooser)
{
    gint id;

    id = gwy_app_data_browser_add_graph_watch(gwy_data_chooser_receive_event, chooser);
    chooser->watcher_id = id;
}

/**
 * gwy_data_chooser_new_graphs:
 *
 * Creates a data chooser for graphs.
 *
 * Returns: A new graph chooser.  Nothing may be assumed about the type and properties of the returned widget as they
 *          can change in the future.
 *
 * Since: 2.41
 **/
GtkWidget*
gwy_data_chooser_new_graphs(void)
{
    GwyDataChooser *chooser;
    GtkCellRenderer *renderer;
    GtkCellLayout *layout;

    chooser = (GwyDataChooser*)g_object_new(GWY_TYPE_DATA_CHOOSER, NULL);
    chooser->kind = GWY_PAGE_GRAPHS;
    chooser->get_ids = gwy_app_data_browser_get_graph_ids;
    chooser->get_title = get_graph_title;
    chooser->get_thumbnail = gwy_app_get_graph_thumbnail;
    chooser->remove_watch = gwy_app_data_browser_remove_graph_watch;
    gwy_app_data_browser_foreach(gwy_data_chooser_fill, chooser);
    layout = GTK_CELL_LAYOUT(chooser);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(layout, renderer, FALSE);
    gtk_cell_layout_set_cell_data_func(layout, renderer, gwy_data_chooser_render_icon, chooser, NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, "style-set", TRUE, NULL);
    gtk_cell_layout_pack_start(layout, renderer, TRUE);
    gtk_cell_layout_set_cell_data_func(layout, renderer, gwy_data_chooser_render_name, chooser, NULL);

    gwy_data_chooser_choose_whatever(chooser);
    gwy_data_chooser_graphs_setup_watcher(chooser);

    return (GtkWidget*)chooser;
}

/*****************************************************************************
 *
 * Surfaces.
 *
 *****************************************************************************/

/**
 * gwy_data_chooser_new_xyzs:
 *
 * Creates a data chooser for XYZ data.
 *
 * Returns: A new XYZ data chooser.  Nothing may be assumed about the type and properties of the returned widget as
 *          they can change in the future.
 *
 * Since: 2.45
 **/
GtkWidget*
gwy_data_chooser_new_xyzs(void)
{
    GwyDataChooser *chooser;
    GtkCellRenderer *renderer;
    GtkCellLayout *layout;

    chooser = (GwyDataChooser*)g_object_new(GWY_TYPE_DATA_CHOOSER, NULL);
    chooser->kind = GWY_PAGE_XYZS;
    chooser->get_ids = gwy_app_data_browser_get_xyz_ids;
    chooser->get_title = gwy_app_get_surface_title;
    chooser->get_thumbnail = gwy_app_get_xyz_thumbnail;
    gwy_app_data_browser_foreach(gwy_data_chooser_fill, chooser);
    layout = GTK_CELL_LAYOUT(chooser);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(layout, renderer, FALSE);
    gtk_cell_layout_set_cell_data_func(layout, renderer, gwy_data_chooser_render_icon, chooser, NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, "style-set", TRUE, NULL);
    gtk_cell_layout_pack_start(layout, renderer, TRUE);
    gtk_cell_layout_set_cell_data_func(layout, renderer, gwy_data_chooser_render_name, chooser, NULL);

    gwy_data_chooser_choose_whatever(chooser);
    /* FIXME: Needs data browser support. */
    /* gwy_data_chooser_xyzs_setup_watcher(chooser); */

    return (GtkWidget*)chooser;
}

/*****************************************************************************
 *
 * Lawns.
 *
 *****************************************************************************/

/**
 * gwy_data_chooser_new_curve_maps:
 *
 * Creates a data chooser for curve map data.
 *
 * Returns: A new curve map data chooser.  Nothing may be assumed about the type and properties of the returned widget
 *          as they can change in the future.
 *
 * Since: 2.60
 **/
GtkWidget*
gwy_data_chooser_new_curve_maps(void)
{
    GwyDataChooser *chooser;
    GtkCellRenderer *renderer;
    GtkCellLayout *layout;

    chooser = (GwyDataChooser*)g_object_new(GWY_TYPE_DATA_CHOOSER, NULL);
    chooser->kind = GWY_PAGE_CURVE_MAPS;
    chooser->get_ids = gwy_app_data_browser_get_curve_map_ids;
    chooser->get_title = gwy_app_get_lawn_title;
    chooser->get_thumbnail = gwy_app_get_curve_map_thumbnail;
    gwy_app_data_browser_foreach(gwy_data_chooser_fill, chooser);
    layout = GTK_CELL_LAYOUT(chooser);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(layout, renderer, FALSE);
    gtk_cell_layout_set_cell_data_func(layout, renderer, gwy_data_chooser_render_icon, chooser, NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, "style-set", TRUE, NULL);
    gtk_cell_layout_pack_start(layout, renderer, TRUE);
    gtk_cell_layout_set_cell_data_func(layout, renderer, gwy_data_chooser_render_name, chooser, NULL);

    gwy_data_chooser_choose_whatever(chooser);
    /* FIXME: Needs data browser support. */
    /* gwy_data_chooser_curve_maps_setup_watcher(chooser); */

    return (GtkWidget*)chooser;
}

/************************** Documentation ****************************/

/**
 * SECTION:datachooser
 * @title: GwyDataChooser
 * @short_description: Data object choosers
 *
 * #GwyDataChooser is an base data object chooser class.  Choosers for particular data objects can be created with
 * functions like gwy_data_chooser_new_channels() or gwy_data_chooser_new_volumes() and then manipulated through
 * #GwyDataChooser interface.
 *
 * The widget type used to implement choosers is not a part of the interface and may be subject of future changes.  In
 * any case #GwyDataChooser has a <code>"changed"</code> signal emitted when the selected item changes.
 *
 * It is possible to offer only data objects matching some criteria.  For example to offer only data fields compatible
 * with another data field, one can use:
 * <informalexample><programlisting>
 * GtkWidget *chooser;
 * GwyDataField *model;
 * <!-- Hello, gtk-doc! -->
 * model = ...;
 * chooser = gwy_data_chooser_new_channels(<!-- Hello, gtk-doc! -->);
 * gwy_data_chooser_set_filter(GWY_DATA_CHOOSER(chooser), compatible_field_filter, model, NULL);
 * </programlisting></informalexample>
 * where the filter function looks like
 * <informalexample><programlisting>
 * static gboolean
 * compatible_field_filter(GwyContainer *data,
 *                         gint id,
 *                         gpointer user_data)
 * {
 *     GwyDataField *model, *data_field;
 *     GQuark quark;
 *     <!-- Hello, gtk-doc! -->
 *     quark = gwy_app_get_data_key_for_id(id);
 *     data_field = gwy_container_get_object(data, quark);
 *     model = GWY_DATA_FIELD(user_data);
 *     return !gwy_data_field_check_compatibility(data_field, model,
 *                                                GWY_DATA_COMPATIBILITY_RES
 *                                                | GWY_DATA_COMPATIBILITY_REAL
 *                                                | GWY_DATA_COMPATIBILITY_LATERAL
 *                                                | GWY_DATA_COMPATIBILITY_VALUE);
 * }
 * </programlisting></informalexample>
 **/

/**
 * GwyDataChooserFilterFunc:
 * @data: Data container.
 * @id: Id of particular data in @data.
 * @user_data: Data passed to gwy_data_chooser_set_filter().
 *
 * The type of data chooser filter function.
 *
 * Returns: %TRUE to display this data in the chooser, %FALSE to omit it.
 **/

/**
 * GwyAppDataId:
 * @datano: Numeric identifier of data container.  Zero is used for none.
 * @id: Numeric identifier of a specific data item, such as channel or graph number.  Value -1 is used for none.
 *
 * Auxiliary structure representing one data item in an open file.
 *
 * The data container number can be obtained with gwy_app_data_browser_get() and used to look up the container with
 * gwy_app_data_browser_get_number().
 *
 * Since: 2.41
 **/

/**
 * GWY_APP_DATA_ID_NONE:
 *
 * Initialiser for #GwyAppDataId that corresponds to no data.
 *
 * The macro would be typically used in initialisation as
 * |[
 * GwyAppDataId dataid = GWY_APP_DATA_ID_NONE;
 * ]|
 *
 * Since: 2.41
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
