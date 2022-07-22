/*
 *  $Id: data-browser.c 24709 2022-03-21 17:31:45Z yeti-dn $
 *  Copyright (C) 2006-2021 David Necas (Yeti), Petr Klapetek, Chris Anderson
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, sidewinderasu@gmail.com.
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

/* XXX: The purpose of this file is to contain all ugliness from the rest of source files.  And indeed it has managed
 * to gather lots of it.  Part of it has been offloaded to data-browser-aux.c. */
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwycontainer.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libdraw/gwypixfield.h>
#include <libgwydgets/gwydgets.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "app/gwyappinternal.h"

/* Data browser window manager role */
#define GWY_DATABROWSER_WM_ROLE "gwyddion-databrowser"

enum {
    SURFACE_PREVIEW_SIZE = 512,
    PAGENO_SHIFT = 16,
};

enum {
    important_mods = (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_RELEASE_MASK),
};

/* Sensitivity flags */
enum {
    SENS_OBJECT = 1 << 0,
    SENS_FILE   = 1 << 1,
    SENS_MASK   = 0x07
};

/* Channel and graph tree store columns */
enum {
    MODEL_ID,
    MODEL_OBJECT,
    MODEL_WIDGET,
    MODEL_TIMESTAMP,
    MODEL_THUMBNAIL,
    MODEL_N_COLUMNS
};

typedef struct _GwyAppDataBrowser GwyAppDataBrowser;
typedef struct _GwyAppDataProxy   GwyAppDataProxy;

typedef gboolean (*SetVisibleFunc)(GwyAppDataProxy *proxy,
                                   GtkTreeIter *iter,
                                   gboolean visible);

/* Channel or graph list */
typedef struct {
    GtkListStore *store;
    gint last;  /* The id of last object, if no object is present, it is equal
                   to the smallest possible id minus 1 */
    gint active;
    gint visible_count;
} GwyAppDataList;

typedef struct {
    GObject *object;
    gint id;
} GwyAppDataAssociation;

typedef struct {
    GwyAppDataWatchFunc function;
    gpointer user_data;
    gulong id;
} GwyAppWatcherData;

/* The data browser */
struct _GwyAppDataBrowser {
    GList *proxy_list;
    struct _GwyAppDataProxy *current;
    GwyAppPage active_page;
    gint untitled_counter;
    gboolean doubleclick;
    gdouble edit_timestamp;
    GwySensitivityGroup *sensgroup;
    GtkWidget *window;
    GtkWidget *filename;
    GtkWidget *messages_button;
    GtkWidget *notebook;
    GtkWidget *lists[GWY_NPAGES];
};

/* The proxy associated with each Container (this is non-GUI object) */
struct _GwyAppDataProxy {
    guint finalize_id;
    gint untitled_no;
    gint data_no;
    gboolean keep_invisible;
    gboolean resetting_visibility;
    struct _GwyAppDataBrowser *parent;
    GwyContainer *container;
    GwyAppDataList lists[GWY_NPAGES];
    GList *associated_3d;             /* of a channel (crude) */
    GList *associated_mask;           /* of a channel */
    GList *associated_brick_preview;  /* of a volume */
    GList *associated_lawn_preview;   /* of a curve map */
    GList *associated_raster;         /* of a surface */
    GArray *messages;
    GtkTextBuffer *message_textbuf;
    GtkWidget *message_window;
    GLogLevelFlags log_levels_seen;
};

static GwyAppDataBrowser*     gwy_app_get_data_browser               (void);
static void                   gwy_app_data_browser_update_filename   (GwyAppDataProxy *proxy);
static GwyAppDataProxy*       gwy_app_data_browser_get_proxy         (GwyAppDataBrowser *browser,
                                                                      GwyContainer *data);
static gboolean               gwy_app_data_proxy_find_object         (GtkListStore *store,
                                                                      gint i,
                                                                      GtkTreeIter *iter);
static void                   gwy_app_data_browser_switch_data       (GwyContainer *data);
static void                   gwy_app_data_proxy_destroy_all_3d      (GwyAppDataProxy *proxy);
static void                   gwy_app_data_proxy_update_window_titles(GwyAppDataProxy *proxy);
static void                   gwy_app_update_data_window_title       (GwyDataView *data_view,
                                                                      gint id);
static void                   gwy_app_update_brick_window_title      (GwyDataView *data_view,
                                                                      gint id);
static void                   gwy_app_update_surface_window_title    (GwyDataView *data_view,
                                                                      gint id);
static void                   gwy_app_update_lawn_window_title       (GwyDataView *data_view,
                                                                      gint id);
static void                   replace_surface_preview                (GwyContainer *container,
                                                                      GtkTreeModel *model,
                                                                      GtkTreeIter *iter);
static void                   ensure_brick_previews                  (GwyAppDataProxy *proxy);
static void                   ensure_lawn_previews                   (GwyAppDataProxy *proxy);
static gboolean               gwy_app_data_proxy_channel_set_visible (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gboolean visible);
static gboolean               gwy_app_data_proxy_graph_set_visible   (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gboolean visible);
static gboolean               gwy_app_data_proxy_brick_set_visible   (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gboolean visible);
static gboolean               gwy_app_data_proxy_surface_set_visible (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gboolean visible);
static gboolean               gwy_app_data_proxy_lawn_set_visible    (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gboolean visible);
static void                   gwy_app_data_proxy_channel_name_edited (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gchar *title);
static void                   gwy_app_data_proxy_graph_name_edited   (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gchar *title);
static void                   gwy_app_data_proxy_spectra_name_edited (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gchar *title);
static void                   gwy_app_data_proxy_brick_name_edited   (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gchar *title);
static void                   gwy_app_data_proxy_surface_name_edited (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gchar *title);
static void                   gwy_app_data_proxy_lawn_name_edited    (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      gchar *title);
static GwyAppDataAssociation* gwy_app_data_assoc_find                (GList **assoclist,
                                                                      GObject *object);
static GwyAppDataAssociation* gwy_app_data_assoc_get                 (GList **assoclist,
                                                                      gint id);
static GList*                 gwy_app_data_proxy_find_3d             (GwyAppDataProxy *proxy,
                                                                      Gwy3DWindow *window3d);
static GList*                 gwy_app_data_proxy_get_3d              (GwyAppDataProxy *proxy,
                                                                      gint id);
static void                   update_all_sens                        (void);
static void                   update_message_button                  (void);
static void                   gwy_app_data_proxy_destroy_messages    (GwyAppDataProxy *proxy);
static void                   gwy_app_data_browser_show_hide_messages(GtkToggleButton *toggle,
                                                                      GwyAppDataBrowser *browser);
static void                   try_to_fix_data_window_size            (GwyAppDataProxy *proxy,
                                                                      GtkTreeIter *iter,
                                                                      GwyAppPage pageno);
static void                   gwy_app_data_browser_copy_object       (GwyAppDataProxy *srcproxy,
                                                                      GwyAppPage pageno,
                                                                      GtkTreeModel *model,
                                                                      GtkTreeIter *iter,
                                                                      GwyAppDataProxy *destproxy);
static void                   gwy_app_data_browser_copy_other        (GtkTreeModel *model,
                                                                      GtkTreeIter *iter,
                                                                      GtkWidget *window,
                                                                      GwyContainer *container);
static void                   gwy_app_data_browser_shoot_object      (GObject *button,
                                                                      GwyAppDataBrowser *browser);
static gboolean               gwy_app_data_browser_select_data_view2 (GwyDataView *data_view);
static gboolean               gwy_app_data_browser_select_graph2     (GwyGraph *graph);
static gboolean               gwy_app_data_browser_select_volume2    (GwyDataView *data_view);
static gboolean               gwy_app_data_browser_select_xyz2       (GwyDataView *data_view);
static gboolean               gwy_app_data_browser_select_curve_map2 (GwyDataView *data_view);
static void                   gwy_app_data_browser_show_real         (GwyAppDataBrowser *browser);
static void                   gwy_app_data_browser_hide_real         (GwyAppDataBrowser *browser);
static void                   gwy_app_data_browser_notify_watch      (GwyContainer *container,
                                                                      GwyAppPage pageno,
                                                                      gint id,
                                                                      GwyDataWatchEventType event);

static const GtkTargetEntry dnd_target_table[] = { GTK_TREE_MODEL_ROW };

static GQuark container_quark      = 0;
static GQuark own_key_quark        = 0;
static GQuark page_id_quark        = 0;  /* NB: data is pageno+1, not pageno */
static GQuark filename_quark       = 0;
static GQuark column_id_quark      = 0;
static GQuark graph_window_quark   = 0;
static GQuark surface_update_quark = 0;

/* The data browser */
static GwyAppDataBrowser *gwy_app_data_browser = NULL;
static gboolean gui_disabled = FALSE;
static gint last_data_number = 0;

static gulong watcher_id = 0;
static GList *data_watchers[GWY_NPAGES];

/* Use doubles for timestamps.  They have 53bit mantisa, which is sufficient
 * for microsecond precision. */
static inline gdouble
gwy_get_timestamp(void)
{
    GTimeVal timestamp;

    g_get_current_time(&timestamp);
    return timestamp.tv_sec + 1e-6*timestamp.tv_usec;
}

/**
 * gwy_app_data_proxy_compare_data:
 * @a: Pointer to a #GwyAppDataProxy.
 * @b: Pointer to a #GwyContainer.
 *
 * Compares two containers (one of them referenced by a data proxy).
 *
 * Returns: Zero if the containers are equal, nonzero otherwise.  This function is intended only for equality tests,
 *          not ordering.
 **/
static gint
gwy_app_data_proxy_compare_data(gconstpointer a,
                                gconstpointer b)
{
    GwyAppDataProxy *ua = (GwyAppDataProxy*)a;

    return (guchar*)ua->container - (guchar*)b;
}

/**
 * gwy_app_data_proxy_compare:
 * @a: Pointer to a #GwyAppDataProxy.
 * @b: Pointer to a #GwyAppDataProxy.
 *
 * Compares two data proxies using file name ordering.
 *
 * Returns: -1, 1 or 0 according to alphabetical order.
 **/
G_GNUC_UNUSED
static gint
gwy_app_data_proxy_compare(gconstpointer a,
                           gconstpointer b)
{
    GwyContainer *ua = ((GwyAppDataProxy*)a)->container;
    GwyContainer *ub = ((GwyAppDataProxy*)b)->container;
    const guchar *fa = NULL, *fb = NULL;

    gwy_container_gis_string(ua, filename_quark, &fa);
    gwy_container_gis_string(ub, filename_quark, &fb);
    if (!fa && !fb)
        return (guchar*)ua - (guchar*)ub;
    if (!fa)
        return -1;
    if (!fb)
        return 1;
    return g_utf8_collate(fa, fb);
}

/**
 * gwy_app_data_browser_set_file_present:
 * @browser: A data browser.
 * @present: %TRUE when a file is opened, %FALSE when no file is opened.
 *
 * Updates sensitivity groups according to file existence state.
 **/
static void
gwy_app_data_browser_set_file_present(GwyAppDataBrowser *browser,
                                      gboolean present)
{
    GwySensitivityGroup *sensgroup;

    if (browser->sensgroup) {
        if (present)
            gwy_sensitivity_group_set_state(browser->sensgroup, SENS_FILE, SENS_FILE);
        else
            gwy_sensitivity_group_set_state(browser->sensgroup, SENS_FILE | SENS_OBJECT, 0);
    }

    if ((sensgroup = _gwy_app_sensitivity_get_group()))
        gwy_sensitivity_group_set_state(sensgroup,
                                        GWY_MENU_FLAG_FILE,
                                        present ? GWY_MENU_FLAG_FILE : 0);
}

/**
 * gwy_app_data_proxy_add_object:
 * @list: A data proxy list.
 * @i: Object id.
 * @object: The object to add (data field, graph model, ...).
 *
 * Adds an object to data proxy list.
 **/
static void
gwy_app_data_proxy_add_object(GwyAppDataList *list,
                              gint i,
                              GtkTreeIter *iter,
                              GObject *object)
{
    gtk_list_store_insert_with_values(list->store, iter, G_MAXINT,
                                      MODEL_ID, i,
                                      MODEL_OBJECT, object,
                                      MODEL_WIDGET, NULL,
                                      MODEL_THUMBNAIL, NULL,
                                      -1);
    if (list->last < i)
        list->last = i;
}

/**
 * gwy_app_data_proxy_switch_object_data:
 * @proxy: Data proxy (not actually used except for sanity check).
 * @old: Old object.
 * @object: New object.
 *
 * Moves qdata set on data proxy object list objects from one object to another
 * one, unsetting them on the old object.
 **/
static void
gwy_app_data_proxy_switch_object_data(GwyAppDataProxy *proxy,
                                      GObject *old,
                                      GObject *object)
{
    gpointer old_container, old_own_key;

    old_container = g_object_get_qdata(old, container_quark);
    g_return_if_fail(old_container == proxy->container);

    old_own_key = g_object_get_qdata(old, own_key_quark);
    g_return_if_fail(own_key_quark);

    g_object_set_qdata(old, container_quark, NULL);
    g_object_set_qdata(old, own_key_quark, NULL);
    g_object_set_qdata(object, container_quark, old_container);
    g_object_set_qdata(object, own_key_quark, old_own_key);
}

static void
update_data_object_timestamp(GwyAppDataProxy *proxy, GwyAppPage page, gint id)
{
    GtkListStore *store = proxy->lists[page].store;
    GtkTreeIter iter;

    g_return_if_fail(id >= 0);
    if (gwy_app_data_proxy_find_object(store, id, &iter)) {
        gtk_list_store_set(store, &iter, MODEL_TIMESTAMP, gwy_get_timestamp(), -1);
        gwy_app_data_browser_notify_watch(proxy->container, page, id, GWY_DATA_WATCH_EVENT_CHANGED);
    }
}

/**
 * gwy_app_data_proxy_channel_changed:
 * @channel: The data field representing a channel.
 * @proxy: Data proxy.
 *
 * Updates channel display in the data browser when channel data change.
 **/
static void
gwy_app_data_proxy_channel_changed(GwyDataField *channel, GwyAppDataProxy *proxy)
{
    GwyAppKeyType type;
    GQuark quark;
    gint id;

    gwy_debug("proxy=%p channel=%p", proxy, channel);
    quark = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(channel), own_key_quark));
    g_return_if_fail(quark);
    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    update_data_object_timestamp(proxy, GWY_PAGE_CHANNELS, id);
}

/**
 * gwy_app_data_proxy_connect_channel:
 * @proxy: Data proxy.
 * @id: Channel id.
 * @object: The data field to add (passed as #GObject).
 *
 * Adds a data field as channel of specified id, setting qdata and connecting signals.
 **/
static void
gwy_app_data_proxy_connect_channel(GwyAppDataProxy *proxy, gint id, GtkTreeIter *iter, GObject *object)
{
    GQuark quark = gwy_app_get_data_key_for_id(id);

    gwy_app_data_proxy_add_object(&proxy->lists[GWY_PAGE_CHANNELS], id, iter, object);
    gwy_debug("%p: %d in %p", object, id, proxy->container);
    g_object_set_qdata(object, container_quark, proxy->container);
    g_object_set_qdata(object, own_key_quark, GUINT_TO_POINTER(quark));
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_channel_changed), proxy);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_CHANNELS, id, GWY_DATA_WATCH_EVENT_ADDED);
}

/**
 * gwy_app_data_proxy_disconnect_channel:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the channel in @proxy's list store.
 *
 * Disconnects signals from a channel data field, removes qdata and finally removes it from the data proxy list store.
 **/
static void
gwy_app_data_proxy_disconnect_channel(GwyAppDataProxy *proxy, GtkTreeIter *iter)
{
    GObject *object;
    gint id;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_CHANNELS].store), iter,
                       MODEL_OBJECT, &object,
                       MODEL_ID, &id,
                       -1);
    gwy_debug("%p: from %p", object, proxy->container);
    g_object_set_qdata(object, container_quark, NULL);
    g_object_set_qdata(object, own_key_quark, NULL);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_channel_changed, proxy);
    g_object_unref(object);
    gtk_list_store_remove(proxy->lists[GWY_PAGE_CHANNELS].store, iter);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_CHANNELS, id, GWY_DATA_WATCH_EVENT_REMOVED);
}

/**
 * gwy_app_data_proxy_reconnect_channel:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the channel in @proxy's list store.
 * @object: The data field representing the channel (passed as #GObject).
 *
 * Updates data proxy's list store when the data field representing a channel is switched for another data field.
 **/
static void
gwy_app_data_proxy_reconnect_channel(GwyAppDataProxy *proxy, GtkTreeIter *iter, GObject *object)
{
    GObject *old;
    gint id;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_CHANNELS].store), iter,
                       MODEL_OBJECT, &old,
                       MODEL_ID, &id,
                       -1);
    g_signal_handlers_disconnect_by_func(old, gwy_app_data_proxy_channel_changed, proxy);
    gwy_app_data_proxy_switch_object_data(proxy, old, object);
    gtk_list_store_set(proxy->lists[GWY_PAGE_CHANNELS].store, iter, MODEL_OBJECT, object, -1);
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_channel_changed), proxy);
    g_object_unref(old);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_CHANNELS, id, GWY_DATA_WATCH_EVENT_CHANGED);
}

static void
gwy_app_data_proxy_mask_changed(GObject *mask, GwyAppDataProxy *proxy)
{
    GwyAppKeyType type;
    GQuark quark;
    gint id;

    gwy_debug("proxy=%p mask=%p", proxy, mask);
    quark = GPOINTER_TO_UINT(g_object_get_qdata(mask, own_key_quark));
    g_return_if_fail(quark);
    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    g_return_if_fail(id >= 0 && type == KEY_IS_MASK);
    update_data_object_timestamp(proxy, GWY_PAGE_CHANNELS, id);
}

static void
gwy_app_data_proxy_connect_mask(GwyAppDataProxy *proxy, gint id, GObject *object)
{
    GwyAppDataAssociation *assoc;
    GQuark quark = gwy_app_get_mask_key_for_id(id);

    gwy_debug("%p: %d in %p", object, id, proxy->container);
    g_object_set_qdata(object, container_quark, proxy->container);
    g_object_set_qdata(object, own_key_quark, GUINT_TO_POINTER(quark));

    g_object_ref(object);
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_mask_changed), proxy);
    assoc = g_slice_new(GwyAppDataAssociation);
    assoc->object = object;
    assoc->id = id;
    proxy->associated_mask = g_list_prepend(proxy->associated_mask, assoc);
    update_data_object_timestamp(proxy, GWY_PAGE_CHANNELS, id);
}

static void
gwy_app_data_proxy_disconnect_mask(GwyAppDataProxy *proxy, gint id)
{
    GwyAppDataAssociation *assoc;
    GList *item;
    GObject *object;

    gwy_debug("%u: from %p", id, proxy->container);
    assoc = gwy_app_data_assoc_get(&proxy->associated_mask, id);
    g_return_if_fail(assoc);
    object = assoc->object;
    g_object_set_qdata(object, container_quark, NULL);
    g_object_set_qdata(object, own_key_quark, NULL);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_mask_changed, proxy);
    item = proxy->associated_mask;
    proxy->associated_mask = g_list_remove_link(proxy->associated_mask, item);
    g_slice_free(GwyAppDataAssociation, assoc);
    g_list_free_1(item);
    g_object_unref(object);
    update_data_object_timestamp(proxy, GWY_PAGE_CHANNELS, id);
}

static void
gwy_app_data_proxy_reconnect_mask(GwyAppDataProxy *proxy, gint id, GObject *object)
{
    GwyAppDataAssociation *assoc;
    GObject *old;

    gwy_debug("%p: %d in %p", object, id, proxy->container);
    assoc = gwy_app_data_assoc_get(&proxy->associated_mask, id);
    g_return_if_fail(assoc);
    old = G_OBJECT(assoc->object);
    g_object_ref(object);
    g_signal_handlers_disconnect_by_func(old, gwy_app_data_proxy_mask_changed, proxy);
    gwy_app_data_proxy_switch_object_data(proxy, old, object);
    assoc->object = object;
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_mask_changed), proxy);
    g_object_unref(old);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_CHANNELS, id, GWY_DATA_WATCH_EVENT_CHANGED);
}

static gint
graph_changed_common(GwyGraphModel *gmodel, GwyAppDataProxy *proxy)
{
    GwyAppKeyType type;
    GtkTreeIter iter;
    GQuark quark;
    gint id;

    if (!(quark = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(gmodel), own_key_quark))))
        return -1;
    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    gwy_debug("proxy=%p, gmodel=%p, curve=%d", proxy, gmodel, id);
    g_return_val_if_fail(type == KEY_IS_GRAPH, -1);
    if (!gwy_app_data_proxy_find_object(proxy->lists[GWY_PAGE_GRAPHS].store, id, &iter))
        return -1;
    gtk_list_store_set(proxy->lists[GWY_PAGE_GRAPHS].store, &iter, MODEL_TIMESTAMP, gwy_get_timestamp(), -1);
    return id;
}

/**
 * gwy_app_data_proxy_graph_changed:
 * @gmodel: The graph model representing a graph.
 * @pspec: Parameter spec for the changed property.
 * @proxy: Data proxy.
 *
 * Updates graph display in the data browser when graph property changes.
 **/
static void
gwy_app_data_proxy_graph_changed(GwyGraphModel *gmodel, GParamSpec *pspec, GwyAppDataProxy *proxy)
{
    gint id;

    gwy_debug("proxy=%p, gmodel=%p", proxy, gmodel);
    id = graph_changed_common(gmodel, proxy);
    if (id == -1)
        return;

    /* Respond to non-cosmetic changes.  The title and number of curves are
     * relevant metadata, units can be used for compatibility checks. */
    if (!gwy_stramong(pspec->name, "n-curves", "si-unit-x", "si-unit-y", "title", NULL))
        return;
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_GRAPHS, id, GWY_DATA_WATCH_EVENT_CHANGED);
}

/**
 * gwy_app_data_proxy_graph_curve_changed:
 * @gmodel: The graph model representing a graph.
 * @i: Curve index.
 * @proxy: Data proxy.
 *
 * Updates graph display in the data browser when graph curve data change.
 **/
static void
gwy_app_data_proxy_graph_curve_changed(GwyGraphModel *gmodel, G_GNUC_UNUSED gint i, GwyAppDataProxy *proxy)
{
    gwy_debug("proxy=%p, gmodel=%p, curve=%d", proxy, gmodel, i);
    graph_changed_common(gmodel, proxy);
}

/**
 * gwy_app_data_proxy_graph_curve_notify:
 * @gmodel: The graph model representing a graph.
 * @i: Curve index.
 * @pspec: Parameter spec for the changed property.
 * @proxy: Data proxy.
 *
 * Updates graph display in the data browser when graph curve property changes.
 **/
static void
gwy_app_data_proxy_graph_curve_notify(GwyGraphModel *gmodel,
                                      G_GNUC_UNUSED gint i,
                                      G_GNUC_UNUSED GParamSpec *pspec,
                                      GwyAppDataProxy *proxy)
{
    gwy_debug("proxy=%p, gmodel=%p, curve=%d", proxy, gmodel, i);
    graph_changed_common(gmodel, proxy);
}

/**
 * gwy_app_data_proxy_connect_graph:
 * @proxy: Data proxy.
 * @id: Graph id.
 * @object: The graph model to add (passed as #GObject).
 *
 * Adds a graph model as graph of specified id, setting qdata and connecting signals.
 **/
static void
gwy_app_data_proxy_connect_graph(GwyAppDataProxy *proxy, gint id, GtkTreeIter *iter, GObject *object)
{
    GQuark quark;

    gwy_app_data_proxy_add_object(&proxy->lists[GWY_PAGE_GRAPHS], id, iter, object);
    gwy_debug("%p: %d in %p", object, id, proxy->container);
    quark = gwy_app_get_graph_key_for_id(id);
    g_object_set_qdata(object, container_quark, proxy->container);
    g_object_set_qdata(object, own_key_quark, GUINT_TO_POINTER(quark));

    g_signal_connect(object, "notify", G_CALLBACK(gwy_app_data_proxy_graph_changed), proxy);
    g_signal_connect(object, "curve-notify", G_CALLBACK(gwy_app_data_proxy_graph_curve_notify), proxy);
    g_signal_connect(object, "curve-data-changed", G_CALLBACK(gwy_app_data_proxy_graph_curve_changed), proxy);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_GRAPHS, id, GWY_DATA_WATCH_EVENT_ADDED);
}

/**
 * gwy_app_data_proxy_disconnect_graph:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the graph in @proxy's list store.
 *
 * Disconnects signals from a graph graph model, removes qdata and finally removes it from the data proxy list store.
 **/
static void
gwy_app_data_proxy_disconnect_graph(GwyAppDataProxy *proxy, GtkTreeIter *iter)
{
    GObject *object;
    gint id;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_GRAPHS].store), iter,
                       MODEL_ID, &id,
                       MODEL_OBJECT, &object,
                       -1);
    gwy_debug("%p: from %p", object, proxy->container);
    g_object_set_qdata(object, container_quark, NULL);
    g_object_set_qdata(object, own_key_quark, NULL);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_graph_changed, proxy);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_graph_curve_notify, proxy);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_graph_curve_changed, proxy);
    g_object_unref(object);
    gtk_list_store_remove(proxy->lists[GWY_PAGE_GRAPHS].store, iter);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_GRAPHS, id, GWY_DATA_WATCH_EVENT_REMOVED);
}

/**
 * gwy_app_data_proxy_reconnect_graph:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the graph in @proxy's list store.
 * @object: The graph model representing the graph (passed as #GObject).
 *
 * Updates data proxy's list store when the graph model representing a graph
 * is switched for another graph model.
 **/
static void
gwy_app_data_proxy_reconnect_graph(GwyAppDataProxy *proxy, GtkTreeIter *iter, GObject *object)
{
    GwyGraph *graph;
    GObject *old;
    gint id;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_GRAPHS].store), iter,
                       MODEL_OBJECT, &old,
                       MODEL_WIDGET, &graph,
                       MODEL_ID, &id,
                       -1);
    g_signal_handlers_disconnect_by_func(old, gwy_app_data_proxy_graph_changed, proxy);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_graph_curve_notify, proxy);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_graph_curve_changed, proxy);
    gwy_app_data_proxy_switch_object_data(proxy, old, object);
    gtk_list_store_set(proxy->lists[GWY_PAGE_GRAPHS].store, iter, MODEL_OBJECT, object, -1);
    g_signal_connect(object, "notify", G_CALLBACK(gwy_app_data_proxy_graph_changed), proxy);
    g_signal_connect(object, "curve-notify", G_CALLBACK(gwy_app_data_proxy_graph_curve_notify), proxy);
    g_signal_connect(object, "curve-data-changed", G_CALLBACK(gwy_app_data_proxy_graph_curve_changed), proxy);
    if (graph) {
        gwy_graph_set_model(graph, GWY_GRAPH_MODEL(object));
        g_object_unref(graph);
    }
    g_object_unref(old);

    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_GRAPHS, id, GWY_DATA_WATCH_EVENT_CHANGED);
}

/**
 * gwy_app_data_proxy_spectra_changed:
 * @spectra: The spectra object.
 * @proxy: Data proxy.
 *
 * Updates spectra display in the data browser when spectra data change.
 **/
static void
gwy_app_data_proxy_spectra_changed(GwySpectra *spectra, GwyAppDataProxy *proxy)
{
    GwyAppKeyType type;
    GtkTreeIter iter;
    GQuark quark;
    gint id;

    gwy_debug("proxy=%p, spectra=%p", proxy, spectra);
    if (!(quark = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(spectra), own_key_quark))))
        return;
    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    g_return_if_fail(type == KEY_IS_SPECTRA);
    if (!gwy_app_data_proxy_find_object(proxy->lists[GWY_PAGE_SPECTRA].store, id, &iter))
        return;
    gwy_list_store_row_changed(proxy->lists[GWY_PAGE_SPECTRA].store, &iter, NULL, -1);
}

/**
 * gwy_app_data_proxy_connect_spectra:
 * @proxy: Data proxy.
 * @i: Channel id.
 * @object: The spectra to add (passed as #GObject).
 *
 * Adds a spectra object of specified id, setting qdata and connecting signals.
 **/
static void
gwy_app_data_proxy_connect_spectra(GwyAppDataProxy *proxy, gint i, GtkTreeIter *iter, GObject *object)
{
    GQuark quark;

    gwy_app_data_proxy_add_object(&proxy->lists[GWY_PAGE_SPECTRA], i, iter, object);
    gwy_debug("%p: %d in %p", object, i, proxy->container);
    quark = gwy_app_get_spectra_key_for_id(i);
    g_object_set_qdata(object, container_quark, proxy->container);
    g_object_set_qdata(object, own_key_quark, GUINT_TO_POINTER(quark));
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_spectra_changed), proxy);
}

/**
 * gwy_app_data_proxy_disconnect_spectra:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the spectra in @proxy's list store.
 *
 * Disconnects signals from a spectra object, removes qdata and finally removes it from the data proxy list store.
 **/
static void
gwy_app_data_proxy_disconnect_spectra(GwyAppDataProxy *proxy, GtkTreeIter *iter)
{
    GObject *object;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_SPECTRA].store), iter, MODEL_OBJECT, &object, -1);
    gwy_debug("%p: from %p", object, proxy->container);
    g_object_set_qdata(object, container_quark, NULL);
    g_object_set_qdata(object, own_key_quark, NULL);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_spectra_changed, proxy);
    g_object_unref(object);
    gtk_list_store_remove(proxy->lists[GWY_PAGE_SPECTRA].store, iter);
}

/**
 * gwy_app_data_proxy_reconnect_spectra:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the spectra in @proxy's list store.
 * @object: The spectra object (passed as #GObject).
 *
 * Updates data proxy's list store when the spectra object is switched for another spectra object.
 **/
static void
gwy_app_data_proxy_reconnect_spectra(GwyAppDataProxy *proxy, GtkTreeIter *iter, GObject *object)
{
    GObject *old;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_SPECTRA].store), iter, MODEL_OBJECT, &old, -1);
    g_signal_handlers_disconnect_by_func(old, gwy_app_data_proxy_spectra_changed, proxy);
    gwy_app_data_proxy_switch_object_data(proxy, old, object);
    gtk_list_store_set(proxy->lists[GWY_PAGE_SPECTRA].store, iter, MODEL_OBJECT, object, -1);
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_spectra_changed), proxy);
    g_object_unref(old);
}

/**
 * gwy_app_data_proxy_brick_changed:
 * @brick: The data field representing a brick.
 * @proxy: Data proxy.
 *
 * Updates brick display in the data browser when brick data change.
 **/
static void
gwy_app_data_proxy_brick_changed(GwyDataField *brick, GwyAppDataProxy *proxy)
{
    GwyDataView *data_view;
    GwyAppDataList *list;
    GwyAppKeyType type;
    GtkTreeIter iter;
    GQuark quark;
    gint id;

    gwy_debug("proxy=%p brick=%p", proxy, brick);
    quark = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(brick), own_key_quark));
    g_return_if_fail(quark);
    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    update_data_object_timestamp(proxy, GWY_PAGE_VOLUMES, id);

    list = &proxy->lists[GWY_PAGE_VOLUMES];
    if (gwy_app_data_proxy_find_object(list->store, id, &iter)) {
        gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
        if (data_view) {
            _gwy_app_update_brick_info(proxy->container, id, data_view);
            g_object_unref(data_view);
        }
    }
}

/**
 * gwy_app_data_proxy_connect_brick:
 * @proxy: Data proxy.
 * @id: Channel id.
 * @object: The brick to add (passed as #GObject).
 *
 * Adds a data field as brick of specified id, setting qdata and connecting signals.
 **/
static void
gwy_app_data_proxy_connect_brick(GwyAppDataProxy *proxy, gint id, GtkTreeIter *iter, GObject *object)
{
    GQuark quark = gwy_app_get_brick_key_for_id(id);

    gwy_app_data_proxy_add_object(&proxy->lists[GWY_PAGE_VOLUMES], id, iter, object);
    gwy_debug("%p: %d in %p", object, id, proxy->container);
    g_object_set_qdata(object, container_quark, proxy->container);
    g_object_set_qdata(object, own_key_quark, GUINT_TO_POINTER(quark));
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_brick_changed), proxy);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_VOLUMES, id, GWY_DATA_WATCH_EVENT_ADDED);
}

/**
 * gwy_app_data_proxy_disconnect_brick:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the brick in @proxy's list store.
 *
 * Disconnects signals from a brick, removes qdata and finally removes it from the data proxy list store.
 **/
static void
gwy_app_data_proxy_disconnect_brick(GwyAppDataProxy *proxy, GtkTreeIter *iter)
{
    GObject *object;
    gint id;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_VOLUMES].store), iter,
                       MODEL_OBJECT, &object,
                       MODEL_ID, &id,
                       -1);
    gwy_debug("%p: from %p", object, proxy->container);
    g_object_set_qdata(object, container_quark, NULL);
    g_object_set_qdata(object, own_key_quark, NULL);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_brick_changed, proxy);
    g_object_unref(object);
    gtk_list_store_remove(proxy->lists[GWY_PAGE_VOLUMES].store, iter);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_VOLUMES, id, GWY_DATA_WATCH_EVENT_REMOVED);
}

/**
 * gwy_app_data_proxy_reconnect_brick:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the brick in @proxy's list store.
 * @object: The brick representing the volume data (passed as #GObject).
 *
 * Updates data proxy's list store when the data brick representing volume data is switched for another brick.
 **/
static void
gwy_app_data_proxy_reconnect_brick(GwyAppDataProxy *proxy, GtkTreeIter *iter, GObject *object)
{
    GObject *old;
    gint id;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_VOLUMES].store), iter,
                       MODEL_OBJECT, &old,
                       MODEL_ID, &id,
                       -1);
    g_signal_handlers_disconnect_by_func(old, gwy_app_data_proxy_brick_changed, proxy);
    gwy_app_data_proxy_switch_object_data(proxy, old, object);
    gtk_list_store_set(proxy->lists[GWY_PAGE_VOLUMES].store, iter, MODEL_OBJECT, object, -1);
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_brick_changed), proxy);
    g_object_unref(old);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_VOLUMES, id, GWY_DATA_WATCH_EVENT_CHANGED);
}

static void
gwy_app_data_proxy_brick_preview_changed(GObject *preview, GwyAppDataProxy *proxy)
{
    GwyAppKeyType type;
    GQuark quark;
    gint id;

    gwy_debug("proxy=%p preview=%p", proxy, preview);
    quark = GPOINTER_TO_UINT(g_object_get_qdata(preview, own_key_quark));
    g_return_if_fail(quark);
    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    g_return_if_fail(type == KEY_IS_BRICK_PREVIEW);
    update_data_object_timestamp(proxy, GWY_PAGE_VOLUMES, id);
}

static void
gwy_app_data_proxy_connect_brick_preview(GwyAppDataProxy *proxy, gint id, GObject *object)
{
    GwyAppDataAssociation *assoc;
    GQuark quark = gwy_app_get_brick_preview_key_for_id(id);

    gwy_debug("%p: %d in %p", object, id, proxy->container);
    g_object_set_qdata(object, container_quark, proxy->container);
    g_object_set_qdata(object, own_key_quark, GUINT_TO_POINTER(quark));

    g_object_ref(object);
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_brick_preview_changed), proxy);
    assoc = g_slice_new(GwyAppDataAssociation);
    assoc->object = object;
    assoc->id = id;
    proxy->associated_brick_preview = g_list_prepend(proxy->associated_brick_preview, assoc);
    update_data_object_timestamp(proxy, GWY_PAGE_VOLUMES, id);
}

static void
gwy_app_data_proxy_disconnect_brick_preview(GwyAppDataProxy *proxy, gint id)
{
    GwyAppDataAssociation *assoc;
    GList *item;
    GObject *object;

    gwy_debug("%u: from %p", id, proxy->container);
    assoc = gwy_app_data_assoc_get(&proxy->associated_brick_preview, id);
    g_return_if_fail(assoc);
    object = assoc->object;
    g_object_set_qdata(object, container_quark, NULL);
    g_object_set_qdata(object, own_key_quark, NULL);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_brick_preview_changed, proxy);
    item = proxy->associated_brick_preview;
    proxy->associated_brick_preview = g_list_remove_link(proxy->associated_brick_preview, item);
    g_slice_free(GwyAppDataAssociation, assoc);
    g_list_free_1(item);
    g_object_unref(object);
    update_data_object_timestamp(proxy, GWY_PAGE_VOLUMES, id);
}

static void
gwy_app_data_proxy_reconnect_brick_preview(GwyAppDataProxy *proxy, gint id, GObject *object)
{
    GwyAppDataAssociation *assoc;
    GObject *old;

    gwy_debug("%p: %d in %p", object, id, proxy->container);
    assoc = gwy_app_data_assoc_get(&proxy->associated_brick_preview, id);
    g_return_if_fail(assoc);
    old = G_OBJECT(assoc->object);
    g_object_ref(object);
    g_signal_handlers_disconnect_by_func(old, gwy_app_data_proxy_brick_preview_changed, proxy);
    gwy_app_data_proxy_switch_object_data(proxy, old, object);
    assoc->object = object;
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_brick_preview_changed), proxy);
    g_object_unref(old);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_VOLUMES, id, GWY_DATA_WATCH_EVENT_CHANGED);
}

/**
 * gwy_app_data_proxy_surface_changed:
 * @surface: The surface representing a XYZ data.
 * @proxy: Data proxy.
 *
 * Updates surface display in the data browser when surface data change. It also requests re-rendering of the preview.
 **/
static void
gwy_app_data_proxy_surface_changed(GwyDataField *surface, GwyAppDataProxy *proxy)
{
    GwyAppKeyType type;
    GwyDataView *data_view;
    GtkTreeIter iter;
    GQuark quark;
    GtkListStore *store;
    gint id;

    gwy_debug("proxy=%p surface=%p", proxy, surface);
    quark = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(surface), own_key_quark));
    g_return_if_fail(quark);
    store = proxy->lists[GWY_PAGE_XYZS].store;
    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    g_return_if_fail(id >= 0);
    if (!gwy_app_data_proxy_find_object(store, id, &iter))
        return;

    gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, MODEL_WIDGET, &data_view, -1);
    if (data_view) {
        _gwy_app_update_surface_info(proxy->container, id, data_view);
        g_object_set_qdata(G_OBJECT(surface), surface_update_quark, GUINT_TO_POINTER(TRUE));
        g_object_unref(data_view);
    }
    update_data_object_timestamp(proxy, GWY_PAGE_XYZS, id);
}

/**
 * gwy_app_data_proxy_connect_surface:
 * @proxy: Data proxy.
 * @id: Channel id.
 * @object: The surface to add (passed as #GObject).
 *
 * Adds a surface as XYZ data of specified id, setting qdata and connecting signals.
 **/
static void
gwy_app_data_proxy_connect_surface(GwyAppDataProxy *proxy, gint id, GtkTreeIter *iter, GObject *object)
{
    GQuark quark = gwy_app_get_surface_key_for_id(id);

    gwy_app_data_proxy_add_object(&proxy->lists[GWY_PAGE_XYZS], id, iter, object);
    gwy_debug("%p: %d in %p", object, id, proxy->container);
    g_object_set_qdata(object, container_quark, proxy->container);
    g_object_set_qdata(object, own_key_quark, GUINT_TO_POINTER(quark));
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_surface_changed), proxy);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_XYZS, id, GWY_DATA_WATCH_EVENT_ADDED);
}

/**
 * gwy_app_data_proxy_disconnect_surface:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the surface in @proxy's list store.
 *
 * Disconnects signals from a surface, removes qdata and finally removes it from the data proxy list store.
 **/
static void
gwy_app_data_proxy_disconnect_surface(GwyAppDataProxy *proxy, GtkTreeIter *iter)
{
    GObject *object;
    gint id;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_XYZS].store), iter,
                       MODEL_OBJECT, &object,
                       MODEL_ID, &id,
                       -1);
    gwy_debug("%p: from %p", object, proxy->container);
    g_object_set_qdata(object, container_quark, NULL);
    g_object_set_qdata(object, own_key_quark, NULL);
    g_object_set_qdata(object, surface_update_quark, NULL);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_surface_changed, proxy);
    g_object_unref(object);
    gtk_list_store_remove(proxy->lists[GWY_PAGE_XYZS].store, iter);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_XYZS, id, GWY_DATA_WATCH_EVENT_REMOVED);
}

/**
 * gwy_app_data_proxy_reconnect_surface:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the surface in @proxy's list store.
 * @object: The surface representing the XYZ data (passed as #GObject).
 *
 * Updates data proxy's list store when the surfacd representing XYZ data is switched for another surface.
 **/
static void
gwy_app_data_proxy_reconnect_surface(GwyAppDataProxy *proxy, GtkTreeIter *iter, GObject *object)
{
    GObject *old;
    gint id;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_XYZS].store), iter,
                       MODEL_OBJECT, &old,
                       MODEL_ID, &id,
                       -1);
    g_signal_handlers_disconnect_by_func(old, gwy_app_data_proxy_surface_changed, proxy);
    g_object_set_qdata(old, surface_update_quark, NULL);
    gwy_app_data_proxy_switch_object_data(proxy, old, object);
    gtk_list_store_set(proxy->lists[GWY_PAGE_XYZS].store, iter, MODEL_OBJECT, object, -1);
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_surface_changed), proxy);
    g_object_unref(old);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_XYZS, id, GWY_DATA_WATCH_EVENT_CHANGED);
}

static void
gwy_app_data_proxy_raster_changed(GObject *raster, G_GNUC_UNUSED GwyAppDataProxy *proxy)
{
    GwyAppKeyType type;
    GQuark quark;
    gint id;

    gwy_debug("proxy=%p raster=%p", proxy, raster);
    quark = GPOINTER_TO_UINT(g_object_get_qdata(raster, own_key_quark));
    g_return_if_fail(quark);
    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    g_return_if_fail(id >= 0 && type == KEY_IS_SURFACE_PREVIEW);
    /* We do not want to recaulcate the thumbnail when just the preview image changes.  The thumbnail would be the
     * same. */
#if 0
    if (!gwy_app_data_proxy_find_object(proxy->lists[GWY_PAGE_XYZS].store, id, &iter))
        return;
#endif
}

static void
gwy_app_data_proxy_connect_raster(GwyAppDataProxy *proxy, gint id,
                                  GObject *object)
{
    GwyAppDataAssociation *assoc;
    GQuark quark = gwy_app_get_surface_preview_key_for_id(id);

    gwy_debug("%p: %d in %p", object, id, proxy->container);
    g_object_set_qdata(object, container_quark, proxy->container);
    g_object_set_qdata(object, own_key_quark, GUINT_TO_POINTER(quark));

    g_object_ref(object);
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_raster_changed), proxy);
    assoc = g_slice_new(GwyAppDataAssociation);
    assoc->object = object;
    assoc->id = id;
    proxy->associated_raster = g_list_prepend(proxy->associated_raster, assoc);
    /* We do not want to recaulcate the thumbnail when just the preview image changes.  The thumbnail would be the
     * same. */
#if 0
    if (!gwy_app_data_proxy_find_object(proxy->lists[GWY_PAGE_XYZS].store, id, &iter))
        return;
#endif
}

static void
gwy_app_data_proxy_disconnect_raster(GwyAppDataProxy *proxy, gint id)
{
    GwyAppDataAssociation *assoc;
    GList *item;
    GObject *object;

    gwy_debug("%u: from %p", id, proxy->container);
    assoc = gwy_app_data_assoc_get(&proxy->associated_raster, id);
    g_return_if_fail(assoc);
    object = assoc->object;
    g_object_set_qdata(object, container_quark, NULL);
    g_object_set_qdata(object, own_key_quark, NULL);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_raster_changed, proxy);
    item = proxy->associated_raster;
    proxy->associated_raster = g_list_remove_link(proxy->associated_raster, item);
    g_slice_free(GwyAppDataAssociation, assoc);
    g_list_free_1(item);
    g_object_unref(object);
    /* We do not want to recaulcate the thumbnail when just the preview image changes.  The thumbnail would be the
     * same. */
#if 0
    if (!gwy_app_data_proxy_find_object(proxy->lists[GWY_PAGE_XYZS].store, id, &iter))
        return;
#endif
}

static void
gwy_app_data_proxy_reconnect_raster(GwyAppDataProxy *proxy, gint id, GObject *object)
{
    GwyAppDataAssociation *assoc;
    GObject *old;

    gwy_debug("%p: %d in %p", object, id, proxy->container);
    assoc = gwy_app_data_assoc_get(&proxy->associated_raster, id);
    g_return_if_fail(assoc);
    old = G_OBJECT(assoc->object);
    g_object_ref(object);
    g_signal_handlers_disconnect_by_func(old, gwy_app_data_proxy_raster_changed, proxy);
    gwy_app_data_proxy_switch_object_data(proxy, old, object);
    assoc->object = object;
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_raster_changed), proxy);
    g_object_unref(old);
}

/**
 * gwy_app_data_proxy_lawn_changed:
 * @lawn: The data field representing a lawn.
 * @proxy: Data proxy.
 *
 * Updates lawn display in the data browser when lawn data change.
 **/
static void
gwy_app_data_proxy_lawn_changed(GwyDataField *lawn, GwyAppDataProxy *proxy)
{
    GwyDataView *data_view;
    GwyAppDataList *list;
    GwyAppKeyType type;
    GtkTreeIter iter;
    GQuark quark;
    gint id;

    gwy_debug("proxy=%p lawn=%p", proxy, lawn);
    quark = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(lawn), own_key_quark));
    g_return_if_fail(quark);
    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    update_data_object_timestamp(proxy, GWY_PAGE_CURVE_MAPS, id);

    list = &proxy->lists[GWY_PAGE_CURVE_MAPS];
    if (gwy_app_data_proxy_find_object(list->store, id, &iter)) {
        gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
        if (data_view) {
            _gwy_app_update_lawn_info(proxy->container, id, data_view);
            g_object_unref(data_view);
        }
    }
}

/**
 * gwy_app_data_proxy_connect_lawn:
 * @proxy: Data proxy.
 * @id: Channel id.
 * @object: The lawn to add (passed as #GObject).
 *
 * Adds a data field as lawn of specified id, setting qdata and connecting signals.
 **/
static void
gwy_app_data_proxy_connect_lawn(GwyAppDataProxy *proxy, gint id, GtkTreeIter *iter, GObject *object)
{
    GQuark quark = gwy_app_get_lawn_key_for_id(id);

    gwy_app_data_proxy_add_object(&proxy->lists[GWY_PAGE_CURVE_MAPS], id, iter, object);
    gwy_debug("%p: %d in %p", object, id, proxy->container);
    g_object_set_qdata(object, container_quark, proxy->container);
    g_object_set_qdata(object, own_key_quark, GUINT_TO_POINTER(quark));
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_lawn_changed), proxy);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_CURVE_MAPS, id, GWY_DATA_WATCH_EVENT_ADDED);
}

/**
 * gwy_app_data_proxy_disconnect_lawn:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the lawn in @proxy's list store.
 *
 * Disconnects signals from a lawn, removes qdata and finally removes it from the data proxy list store.
 **/
static void
gwy_app_data_proxy_disconnect_lawn(GwyAppDataProxy *proxy, GtkTreeIter *iter)
{
    GObject *object;
    gint id;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_CURVE_MAPS].store), iter,
                       MODEL_OBJECT, &object,
                       MODEL_ID, &id,
                       -1);
    gwy_debug("%p: from %p", object, proxy->container);
    g_object_set_qdata(object, container_quark, NULL);
    g_object_set_qdata(object, own_key_quark, NULL);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_lawn_changed, proxy);
    g_object_unref(object);
    gtk_list_store_remove(proxy->lists[GWY_PAGE_CURVE_MAPS].store, iter);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_CURVE_MAPS, id, GWY_DATA_WATCH_EVENT_REMOVED);
}

/**
 * gwy_app_data_proxy_reconnect_lawn:
 * @proxy: Data proxy.
 * @iter: Tree iterator pointing to the lawn in @proxy's list store.
 * @object: The lawn representing the volume data (passed as #GObject).
 *
 * Updates data proxy's list store when the data lawn representing volume data is switched for another lawn.
 **/
static void
gwy_app_data_proxy_reconnect_lawn(GwyAppDataProxy *proxy, GtkTreeIter *iter, GObject *object)
{
    GObject *old;
    gint id;

    gtk_tree_model_get(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_CURVE_MAPS].store), iter,
                       MODEL_OBJECT, &old,
                       MODEL_ID, &id,
                       -1);
    g_signal_handlers_disconnect_by_func(old, gwy_app_data_proxy_lawn_changed, proxy);
    gwy_app_data_proxy_switch_object_data(proxy, old, object);
    gtk_list_store_set(proxy->lists[GWY_PAGE_CURVE_MAPS].store, iter, MODEL_OBJECT, object, -1);
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_lawn_changed), proxy);
    g_object_unref(old);
    gwy_app_data_browser_notify_watch(proxy->container, GWY_PAGE_CURVE_MAPS, id, GWY_DATA_WATCH_EVENT_CHANGED);
}

static void
gwy_app_data_proxy_lawn_preview_changed(GObject *preview, GwyAppDataProxy *proxy)
{
    GwyAppKeyType type;
    GQuark quark;
    gint id;

    gwy_debug("proxy=%p preview=%p", proxy, preview);
    quark = GPOINTER_TO_UINT(g_object_get_qdata(preview, own_key_quark));
    g_return_if_fail(quark);
    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    g_return_if_fail(type == KEY_IS_LAWN_PREVIEW);
    update_data_object_timestamp(proxy, GWY_PAGE_CURVE_MAPS, id);
}

static void
gwy_app_data_proxy_connect_lawn_preview(GwyAppDataProxy *proxy, gint id, GObject *object)
{
    GwyAppDataAssociation *assoc;
    GQuark quark = gwy_app_get_lawn_preview_key_for_id(id);

    gwy_debug("%p: %d in %p", object, id, proxy->container);
    g_object_set_qdata(object, container_quark, proxy->container);
    g_object_set_qdata(object, own_key_quark, GUINT_TO_POINTER(quark));

    g_object_ref(object);
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_lawn_preview_changed), proxy);
    assoc = g_slice_new(GwyAppDataAssociation);
    assoc->object = object;
    assoc->id = id;
    proxy->associated_lawn_preview = g_list_prepend(proxy->associated_lawn_preview, assoc);
    update_data_object_timestamp(proxy, GWY_PAGE_CURVE_MAPS, id);
}

static void
gwy_app_data_proxy_disconnect_lawn_preview(GwyAppDataProxy *proxy, gint id)
{
    GwyAppDataAssociation *assoc;
    GList *item;
    GObject *object;

    gwy_debug("%u: from %p", id, proxy->container);
    assoc = gwy_app_data_assoc_get(&proxy->associated_lawn_preview, id);
    g_return_if_fail(assoc);
    object = assoc->object;
    g_object_set_qdata(object, container_quark, NULL);
    g_object_set_qdata(object, own_key_quark, NULL);
    g_signal_handlers_disconnect_by_func(object, gwy_app_data_proxy_lawn_preview_changed, proxy);
    item = proxy->associated_lawn_preview;
    proxy->associated_lawn_preview = g_list_remove_link(proxy->associated_lawn_preview, item);
    g_slice_free(GwyAppDataAssociation, assoc);
    g_list_free_1(item);
    g_object_unref(object);
    update_data_object_timestamp(proxy, GWY_PAGE_CURVE_MAPS, id);
}

static void
gwy_app_data_proxy_reconnect_lawn_preview(GwyAppDataProxy *proxy, gint id, GObject *object)
{
    GwyAppDataAssociation *assoc;
    GObject *old;

    gwy_debug("%p: %d in %p", object, id, proxy->container);
    assoc = gwy_app_data_assoc_get(&proxy->associated_lawn_preview, id);
    g_return_if_fail(assoc);
    old = G_OBJECT(assoc->object);
    g_object_ref(object);
    g_signal_handlers_disconnect_by_func(old, gwy_app_data_proxy_lawn_preview_changed, proxy);
    gwy_app_data_proxy_switch_object_data(proxy, old, object);
    assoc->object = object;
    g_signal_connect(object, "data-changed", G_CALLBACK(gwy_app_data_proxy_lawn_preview_changed), proxy);
    g_object_unref(old);
    update_data_object_timestamp(proxy, GWY_PAGE_CURVE_MAPS, id);
}

/**
 * gwy_app_data_proxy_scan_data:
 * @key: Container quark key.
 * @value: Value at @key.
 * @userdata: Data proxy.
 *
 * Adds a data object from Container to data proxy.
 *
 * More precisely, if the key and value is found to be data channel or graph it's added.  Other container items are
 * ignored.
 **/
static void
gwy_app_data_proxy_scan_data(gpointer key,
                             gpointer value,
                             gpointer userdata)
{
    GQuark quark = GPOINTER_TO_UINT(key);
    GValue *gvalue = (GValue*)value;
    GwyAppDataProxy *proxy = (GwyAppDataProxy*)userdata;
    const gchar *strkey;
    GwyAppKeyType type;
    GtkTreeIter iter;
    GObject *object;
    gint i;

    strkey = g_quark_to_string(quark);
    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    if (i < 0)
        return;

    switch (type) {
        case KEY_IS_DATA:
        gwy_debug("Found data %d (%s)", i, strkey);
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_DATA_FIELD(object));
        gwy_app_data_proxy_connect_channel(proxy, i, &iter, object);
        break;

        case KEY_IS_GRAPH:
        gwy_debug("Found graph %d (%s)", i, strkey);
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_GRAPH_MODEL(object));
        gwy_app_data_proxy_connect_graph(proxy, i, &iter, object);
        break;

        case KEY_IS_SPECTRA:
        gwy_debug("Found spectra %d (%s)", i, strkey);
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_SPECTRA(object));
        gwy_app_data_proxy_connect_spectra(proxy, i, &iter, object);
        break;

        case KEY_IS_BRICK:
        gwy_debug("Found brick %d (%s)", i, strkey);
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_BRICK(object));
        gwy_app_data_proxy_connect_brick(proxy, i, &iter, object);
        break;

        case KEY_IS_SURFACE:
        gwy_debug("Found surface %d (%s)", i, strkey);
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_SURFACE(object));
        gwy_app_data_proxy_connect_surface(proxy, i, &iter, object);
        break;

        case KEY_IS_LAWN:
        gwy_debug("Found lawn %d (%s)", i, strkey);
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_LAWN(object));
        gwy_app_data_proxy_connect_lawn(proxy, i, &iter, object);
        break;

        case KEY_IS_MASK:
        gwy_debug("Found mask %d (%s)", i, strkey);
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_DATA_FIELD(object));
        gwy_app_data_proxy_connect_mask(proxy, i, object);
        break;

        case KEY_IS_SHOW:
        /* FIXME */
        gwy_debug("Found presentation %d (%s)", i, strkey);
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_DATA_FIELD(object));
        break;

        case KEY_IS_SELECT:
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_SELECTION(object));
        break;

        case KEY_IS_BRICK_PREVIEW:
        gwy_debug("Found brick preview %d (%s)", i, strkey);
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_DATA_FIELD(object));
        gwy_app_data_proxy_connect_brick_preview(proxy, i, object);
        break;

        case KEY_IS_LAWN_PREVIEW:
        gwy_debug("Found lawn preview %d (%s)", i, strkey);
        g_return_if_fail(G_VALUE_HOLDS_OBJECT(gvalue));
        object = g_value_get_object(gvalue);
        g_return_if_fail(GWY_IS_DATA_FIELD(object));
        gwy_app_data_proxy_connect_lawn_preview(proxy, i, object);
        break;

        default:
        break;
    }
}

/**
 * gwy_app_data_proxy_visible_count:
 * @proxy: Data proxy.
 *
 * Calculates the total number of visible objects in all data proxy object lists.
 *
 * Returns: The total number of visible objects.
 **/
static inline gint
gwy_app_data_proxy_visible_count(GwyAppDataProxy *proxy)
{
    gint i, n = 0;

    for (i = 0; i < GWY_NPAGES; i++) {
        n += proxy->lists[i].visible_count;
    }

    g_assert(n >= 0);
    gwy_debug("%p total visible_count: %d", proxy, n);

    return n;
}

/**
 * gwy_app_data_proxy_finalize_list:
 * @model: A tree model.
 * @column: Model column that contains the objects.
 * @func: A callback connected to the objects.
 * @data: User data for @func.
 *
 * Disconnect a callback from all objects in a tree model.
 **/
static void
gwy_app_data_proxy_finalize_list(GtkTreeModel *model,
                                 gint column,
                                 gpointer func,
                                 gpointer data)
{
    GObject *object;
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gtk_tree_model_get(model, &iter, column, &object, -1);
            g_signal_handlers_disconnect_by_func(object, func, data);
            g_object_unref(object);
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    g_object_unref(model);
}

/**
 * gwy_app_data_proxy_find_object:
 * @model: Data proxy list store (channels, graphs).
 * @i: Object number to find.
 * @iter: Tree iterator to set to row containing object @i.
 *
 * Find an object in data proxy list store.
 *
 * Returns: %TRUE if object was found and @iter set, %FALSE otherwise (@iter is invalid then).
 **/
static gboolean
gwy_app_data_proxy_find_object(GtkListStore *store,
                               gint i,
                               GtkTreeIter *iter)
{
    GtkTreeModel *model;
    gint objid;

    gwy_debug("looking for objid %d", i);
    if (i < 0)
        return FALSE;

    model = GTK_TREE_MODEL(store);
    if (!gtk_tree_model_get_iter_first(model, iter))
        return FALSE;

    do {
        gtk_tree_model_get(model, iter, MODEL_ID, &objid, -1);
        gwy_debug("found objid %d", objid);
        if (objid == i)
            return TRUE;
    } while (gtk_tree_model_iter_next(model, iter));

    return FALSE;
}

/**
 * gwy_app_data_proxy_item_changed:
 * @data: A data container.
 * @quark: Quark key of item that has changed.
 * @proxy: Data proxy.
 *
 * Updates a data proxy in response to a Container "item-changed" signal.
 **/
static void
gwy_app_data_proxy_item_changed(GwyContainer *data,
                                GQuark quark,
                                GwyAppDataProxy *proxy)
{
    GObject *object = NULL;
    GwyAppDataList *list;
    const gchar *strkey;
    GwyAppKeyType type;
    GtkTreeIter iter;
    GwyDataView *data_view = NULL;
    gboolean found;
    GList *item;
    gint id;
    GwyAppPage pageno = GWY_PAGE_NOPAGE;

    strkey = g_quark_to_string(quark);
    id = _gwy_app_analyse_data_key(strkey, &type, NULL);
    if (id < 0) {
        if (type == KEY_IS_FILENAME) {
            gwy_app_data_browser_update_filename(proxy);
            if (!gui_disabled)
                gwy_app_data_proxy_update_window_titles(proxy);
        }
        return;
    }
    gwy_debug("key: <%s>", strkey);

    switch (type) {
        case KEY_IS_DATA:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_CHANNELS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        gwy_debug("Channel <%s>: %s in container, %s in list store",
                  strkey,
                  object ? "present" : "missing",
                  found ? "present" : "missing");
        g_return_if_fail(object || found);
        if (object && !found)
            gwy_app_data_proxy_connect_channel(proxy, id, &iter, object);
        else if (!object && found)
            gwy_app_data_proxy_disconnect_channel(proxy, &iter);
        else if (object && found) {
            gwy_app_data_proxy_reconnect_channel(proxy, &iter, object);
            gwy_list_store_row_changed(list->store, &iter, NULL, -1);
        }
        /* Prevent thumbnail update */
        if (!object)
            pageno = GWY_PAGE_NOPAGE;
        break;

        case KEY_IS_GRAPH:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_GRAPHS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        gwy_debug("Graph <%s>: %s in container, %s in list store",
                  strkey,
                  object ? "present" : "missing",
                  found ? "present" : "missing");
        g_return_if_fail(object || found);
        if (object && !found)
            gwy_app_data_proxy_connect_graph(proxy, id, &iter, object);
        else if (!object && found)
            gwy_app_data_proxy_disconnect_graph(proxy, &iter);
        else if (object && found) {
            gwy_app_data_proxy_reconnect_graph(proxy, &iter, object);
            gwy_list_store_row_changed(list->store, &iter, NULL, -1);
        }
        /* Prevent thumbnail update */
        if (!object)
            pageno = GWY_PAGE_NOPAGE;
        break;

        case KEY_IS_SPECTRA:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_SPECTRA;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        gwy_debug("Spectra <%s>: %s in container, %s in list store",
                  strkey,
                  object ? "present" : "missing",
                  found ? "present" : "missing");
        g_return_if_fail(object || found);
        if (object && !found)
            gwy_app_data_proxy_connect_spectra(proxy, id, &iter, object);
        else if (!object && found)
            gwy_app_data_proxy_disconnect_spectra(proxy, &iter);
        else if (object && found) {
            gwy_app_data_proxy_reconnect_spectra(proxy, &iter, object);
            gwy_list_store_row_changed(list->store, &iter, NULL, -1);
        }
        /* Prevent thumbnail update */
        if (!object)
            pageno = GWY_PAGE_NOPAGE;
        break;

        case KEY_IS_MASK:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_CHANNELS;
        list = &proxy->lists[pageno];
        found = !!gwy_app_data_assoc_get(&proxy->associated_mask, id);
        if (object && !found)
            gwy_app_data_proxy_connect_mask(proxy, id, object);
        else if (!object && found)
            gwy_app_data_proxy_disconnect_mask(proxy, id);
        else if (object && found)
            gwy_app_data_proxy_reconnect_mask(proxy, id, object);

        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found)
            gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
        else
            pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        /* XXX: This is not a good place to do that, DataProxy should be non-GUI */
        if (data_view) {
            _gwy_app_sync_mask(data, quark, data_view);
            g_object_unref(data_view);
        }
        break;

        case KEY_IS_CALDATA:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_CHANNELS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found)
            gwy_list_store_row_changed(proxy->lists[GWY_PAGE_CHANNELS].store, &iter, NULL, -1);
        if (!found)
            pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        break;

        case KEY_IS_SHOW:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_CHANNELS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found) {
            gwy_list_store_row_changed(proxy->lists[GWY_PAGE_CHANNELS].store, &iter, NULL, -1);
            gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
        }
        /* XXX: This is not a good place to do that, DataProxy should be non-GUI */
        if (data_view) {
            _gwy_app_sync_show(data, quark, data_view);
            _gwy_app_update_data_range_type(data_view, id);
            g_object_unref(data_view);
        }
        if (!found)
            pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        break;

        case KEY_IS_BRICK:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_VOLUMES;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        gwy_debug("Brick <%s>: %s in container, %s in list store",
                  strkey,
                  object ? "present" : "missing",
                  found ? "present" : "missing");
        g_return_if_fail(object || found);
        if (object && !found)
            gwy_app_data_proxy_connect_brick(proxy, id, &iter, object);
        else if (!object && found)
            gwy_app_data_proxy_disconnect_brick(proxy, &iter);
        else if (object && found) {
            gwy_app_data_proxy_reconnect_brick(proxy, &iter, object);
            gwy_list_store_row_changed(list->store, &iter, NULL, -1);
        }
        if (object && found) {
            gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
            /* XXX: This is not a good place to do that, DataProxy should be non-GUI */
            if (data_view) {
                _gwy_app_update_brick_info(data, id, data_view);
                g_object_unref(data_view);
            }
        }
        /* Prevent thumbnail update; it depends on the preview field */
        pageno = GWY_PAGE_NOPAGE;
        break;

        case KEY_IS_SURFACE:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_XYZS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        gwy_debug("Surface <%s>: %s in container, %s in list store",
                  strkey,
                  object ? "present" : "missing",
                  found ? "present" : "missing");
        g_return_if_fail(object || found);
        if (object && !found)
            gwy_app_data_proxy_connect_surface(proxy, id, &iter, object);
        else if (!object && found)
            gwy_app_data_proxy_disconnect_surface(proxy, &iter);
        else if (object && found) {
            gwy_app_data_proxy_reconnect_surface(proxy, &iter, object);
            gwy_list_store_row_changed(list->store, &iter, NULL, -1);
        }
        if (object && found) {
            gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
            /* XXX: This is not a good place to do that, DataProxy should be non-GUI */
            if (data_view) {
                _gwy_app_update_surface_info(data, id, data_view);
                replace_surface_preview(data, GTK_TREE_MODEL(list->store), &iter);
                g_object_unref(data_view);
            }
        }
        /* Prevent thumbnail update; it depends on the preview field */
        pageno = GWY_PAGE_NOPAGE;
        break;

        case KEY_IS_LAWN:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_CURVE_MAPS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        gwy_debug("Brick <%s>: %s in container, %s in list store",
                  strkey,
                  object ? "present" : "missing",
                  found ? "present" : "missing");
        g_return_if_fail(object || found);
        if (object && !found)
            gwy_app_data_proxy_connect_lawn(proxy, id, &iter, object);
        else if (!object && found)
            gwy_app_data_proxy_disconnect_lawn(proxy, &iter);
        else if (object && found) {
            gwy_app_data_proxy_reconnect_lawn(proxy, &iter, object);
            gwy_list_store_row_changed(list->store, &iter, NULL, -1);
        }
        if (object && found) {
            gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
            /* XXX: This is not a good place to do that, DataProxy should be non-GUI */
            if (data_view) {
                _gwy_app_update_lawn_info(data, id, data_view);
                g_object_unref(data_view);
            }
        }
        /* Prevent thumbnail update; it depends on the preview field */
        pageno = GWY_PAGE_NOPAGE;
        break;

        case KEY_IS_TITLE:
        pageno = GWY_PAGE_CHANNELS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found) {
            gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
            gwy_app_data_browser_notify_watch(proxy->container, pageno, id, GWY_DATA_WATCH_EVENT_CHANGED);
        }
        /* XXX: This is not a good place to do that, DataProxy should be non-GUI */
        if (data_view) {
            gwy_app_update_data_window_title(data_view, id);
            g_object_unref(data_view);
        }
        if ((item = gwy_app_data_proxy_get_3d(proxy, id))) {
            GwyAppDataAssociation *assoc = (GwyAppDataAssociation*)item->data;
            _gwy_app_update_3d_window_title(GWY_3D_WINDOW(assoc->object), id);
        }
        pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        break;

        case KEY_IS_RANGE_TYPE:
        pageno = GWY_PAGE_CHANNELS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found) {
            gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
            gwy_app_data_browser_notify_watch(proxy->container, pageno, id, GWY_DATA_WATCH_EVENT_CHANGED);
        }
        /* XXX: This is not a good place to do that, DataProxy should be non-GUI */
        if (data_view) {
            _gwy_app_update_data_range_type(data_view, id);
            g_object_unref(data_view);
        }
        if (!found)
            pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        break;

        case KEY_IS_PALETTE:
        case KEY_IS_RANGE:
        case KEY_IS_MASK_COLOR:
        pageno = GWY_PAGE_CHANNELS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found)
            gwy_app_data_browser_notify_watch(proxy->container, pageno, id, GWY_DATA_WATCH_EVENT_CHANGED);
        else {
            pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        }
        break;

        case KEY_IS_REAL_SQUARE:
        pageno = GWY_PAGE_CHANNELS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found) {
            gwy_app_data_browser_notify_watch(proxy->container, pageno, id, GWY_DATA_WATCH_EVENT_CHANGED);
            try_to_fix_data_window_size(proxy, &iter, pageno);
        }
        else {
            pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        }
        break;

        case KEY_IS_BRICK_TITLE:
        pageno = GWY_PAGE_VOLUMES;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found) {
            gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
            gwy_app_data_browser_notify_watch(proxy->container, pageno, id, GWY_DATA_WATCH_EVENT_CHANGED);
        }
        /* XXX: This is not a good place to do that, DataProxy should be non-GUI */
        if (data_view) {
            gwy_app_update_brick_window_title(data_view, id);
            g_object_unref(data_view);
        }
        pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        break;

        case KEY_IS_BRICK_PREVIEW:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_VOLUMES;
        list = &proxy->lists[pageno];
        found = !!gwy_app_data_assoc_get(&proxy->associated_brick_preview, id);
        if (object && !found)
            gwy_app_data_proxy_connect_brick_preview(proxy, id, object);
        else if (!object && found)
            gwy_app_data_proxy_disconnect_brick_preview(proxy, id);
        else if (object && found)
            gwy_app_data_proxy_reconnect_brick_preview(proxy, id, object);
        if (!found || !object)
            pageno = GWY_PAGE_NOPAGE;
        if (!gwy_app_data_proxy_find_object(list->store, id, &iter))
            pageno = GWY_PAGE_NOPAGE;
        break;

        case KEY_IS_BRICK_PREVIEW_PALETTE:
        pageno = GWY_PAGE_VOLUMES;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (!found)
            pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        break;

        case KEY_IS_SURFACE_TITLE:
        pageno = GWY_PAGE_XYZS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found) {
            gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
            gwy_app_data_browser_notify_watch(proxy->container, pageno, id, GWY_DATA_WATCH_EVENT_CHANGED);
        }
        /* XXX: This is not a good place to do that, DataProxy should be non-GUI */
        if (data_view) {
            gwy_app_update_surface_window_title(data_view, id);
            g_object_unref(data_view);
        }
        pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        break;

        case KEY_IS_SURFACE_PREVIEW:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_XYZS;
        list = &proxy->lists[pageno];
        found = !!gwy_app_data_assoc_get(&proxy->associated_raster, id);
        if (object && !found)
            gwy_app_data_proxy_connect_raster(proxy, id, object);
        else if (!object && found)
            gwy_app_data_proxy_disconnect_raster(proxy, id);
        else if (object && found)
            gwy_app_data_proxy_reconnect_raster(proxy, id, object);
        if (!found || !object)
            pageno = GWY_PAGE_NOPAGE;
        if (!gwy_app_data_proxy_find_object(list->store, id, &iter))
            pageno = GWY_PAGE_NOPAGE;
        break;

        case KEY_IS_SURFACE_PREVIEW_PALETTE:
        pageno = GWY_PAGE_XYZS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (!found)
            pageno = GWY_PAGE_NOPAGE;
        break;

        case KEY_IS_LAWN_TITLE:
        pageno = GWY_PAGE_CURVE_MAPS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found) {
            gtk_tree_model_get(GTK_TREE_MODEL(list->store), &iter, MODEL_WIDGET, &data_view, -1);
            gwy_app_data_browser_notify_watch(proxy->container, pageno, id, GWY_DATA_WATCH_EVENT_CHANGED);
        }
        /* XXX: This is not a good place to do that, DataProxy should be non-GUI */
        if (data_view) {
            gwy_app_update_lawn_window_title(data_view, id);
            g_object_unref(data_view);
        }
        pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        break;

        case KEY_IS_LAWN_PREVIEW:
        gwy_container_gis_object(data, quark, &object);
        pageno = GWY_PAGE_CURVE_MAPS;
        list = &proxy->lists[pageno];
        found = !!gwy_app_data_assoc_get(&proxy->associated_lawn_preview, id);
        if (object && !found)
            gwy_app_data_proxy_connect_lawn_preview(proxy, id, object);
        else if (!object && found)
            gwy_app_data_proxy_disconnect_lawn_preview(proxy, id);
        else if (object && found)
            gwy_app_data_proxy_reconnect_lawn_preview(proxy, id, object);
        if (!found || !object)
            pageno = GWY_PAGE_NOPAGE;
        if (!gwy_app_data_proxy_find_object(list->store, id, &iter))
            pageno = GWY_PAGE_NOPAGE;
        break;

        case KEY_IS_LAWN_PREVIEW_PALETTE:
        pageno = GWY_PAGE_CURVE_MAPS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (!found)
            pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        break;

        case KEY_IS_LAWN_REAL_SQUARE:
        pageno = GWY_PAGE_CURVE_MAPS;
        list = &proxy->lists[pageno];
        found = gwy_app_data_proxy_find_object(list->store, id, &iter);
        if (found)
            try_to_fix_data_window_size(proxy, &iter, pageno);
        else {
            pageno = GWY_PAGE_NOPAGE; /* Prevent thumbnail update */
        }
        break;

        case KEY_IS_DATA_VISIBLE:
        if (!proxy->resetting_visibility && !gui_disabled) {
            pageno = GWY_PAGE_CHANNELS;
            list = &proxy->lists[pageno];
            found = gwy_app_data_proxy_find_object(list->store, id, &iter);
            if (found) {
                gboolean visible = FALSE;
                gwy_container_gis_boolean(data, quark, &visible);
                gwy_app_data_proxy_channel_set_visible(proxy, &iter, visible);
            }
            pageno = GWY_PAGE_NOPAGE;
        }
        break;

        case KEY_IS_GRAPH_VISIBLE:
        if (!proxy->resetting_visibility && !gui_disabled) {
            pageno = GWY_PAGE_GRAPHS;
            list = &proxy->lists[pageno];
            found = gwy_app_data_proxy_find_object(list->store, id, &iter);
            if (found) {
                gboolean visible = FALSE;
                gwy_container_gis_boolean(data, quark, &visible);
                gwy_app_data_proxy_graph_set_visible(proxy, &iter, visible);
            }
            pageno = GWY_PAGE_NOPAGE;
        }
        break;

        case KEY_IS_BRICK_VISIBLE:
        if (!proxy->resetting_visibility && !gui_disabled) {
            pageno = GWY_PAGE_VOLUMES;
            list = &proxy->lists[pageno];
            found = gwy_app_data_proxy_find_object(list->store, id, &iter);
            if (found) {
                gboolean visible = FALSE;
                gwy_container_gis_boolean(data, quark, &visible);
                gwy_app_data_proxy_brick_set_visible(proxy, &iter, visible);
            }
            pageno = GWY_PAGE_NOPAGE;
        }
        break;

        case KEY_IS_SURFACE_VISIBLE:
        if (!proxy->resetting_visibility && !gui_disabled) {
            pageno = GWY_PAGE_XYZS;
            list = &proxy->lists[pageno];
            found = gwy_app_data_proxy_find_object(list->store, id, &iter);
            if (found) {
                gboolean visible = FALSE;
                gwy_container_gis_boolean(data, quark, &visible);
                gwy_app_data_proxy_surface_set_visible(proxy, &iter, visible);
            }
            pageno = GWY_PAGE_NOPAGE;
        }
        break;

        case KEY_IS_LAWN_VISIBLE:
        if (!proxy->resetting_visibility && !gui_disabled) {
            pageno = GWY_PAGE_CURVE_MAPS;
            list = &proxy->lists[pageno];
            found = gwy_app_data_proxy_find_object(list->store, id, &iter);
            if (found) {
                gboolean visible = FALSE;
                gwy_container_gis_boolean(data, quark, &visible);
                gwy_app_data_proxy_lawn_set_visible(proxy, &iter, visible);
            }
            pageno = GWY_PAGE_NOPAGE;
        }
        break;

        default:
        break;
    }

    if (pageno == GWY_PAGE_NOPAGE)
        return;

    /* XXX: This code asserts list and iter was set above. */
    gtk_list_store_set(list->store, &iter, MODEL_TIMESTAMP, gwy_get_timestamp(), -1);
}

static void
gwy_app_data_proxy_watch_remove_all(gint page, GwyAppDataProxy *proxy)
{
    GtkTreeModel *model = GTK_TREE_MODEL(proxy->lists[page].store);
    GtkTreeIter iter;
    gint id;

    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gtk_tree_model_get(model, &iter, MODEL_ID, &id, -1);
            gwy_app_data_browser_notify_watch(proxy->container, page, id, GWY_DATA_WATCH_EVENT_REMOVED);
        } while (gtk_tree_model_iter_next(model, &iter));
    }
}

static void
gwy_app_data_assoc_finalize_list(GList *list,
                                 GwyAppDataProxy *proxy,
                                 gpointer func_to_disconnect)
{
    GList *l;

    for (l = list; l; l = g_list_next(l)) {
        GwyAppDataAssociation *assoc = (GwyAppDataAssociation*)l->data;
        GObject *object = assoc->object;

        g_object_set_qdata(object, container_quark, NULL);
        g_object_set_qdata(object, own_key_quark, NULL);
        g_signal_handlers_disconnect_by_func(object, func_to_disconnect, proxy);
        g_object_unref(object);
        g_slice_free(GwyAppDataAssociation, assoc);
    }
    g_list_free(list);
}

/**
 * gwy_app_data_proxy_finalize:
 * @user_data: A data proxy.
 *
 * Finalizes a data proxy, which was already removed from the data browser.
 *
 * Usually called in idle loop as things do not like being finalized inside
 * their signal callbacks.
 *
 * Returns: Always %FALSE.
 **/
static gboolean
gwy_app_data_proxy_finalize(gpointer user_data)
{
    GwyAppDataProxy *proxy = (GwyAppDataProxy*)user_data;
    GwyAppDataBrowser *browser;
    gint i;

    proxy->finalize_id = 0;

    for (i = 0; i < GWY_NPAGES; i++)
        gwy_app_data_proxy_watch_remove_all(i, proxy);

    if (gwy_app_data_proxy_visible_count(proxy)) {
        g_assert(gwy_app_data_browser_get_proxy(gwy_app_data_browser, proxy->container));
        return FALSE;
    }

    gwy_debug("Freeing proxy for Container %p", proxy->container);

    browser = gwy_app_data_browser;
    if (browser == proxy->parent) {
        /* FIXME: This is crude. */
        if (browser->current == proxy) {
            gwy_app_data_browser_switch_data(NULL);
            _gwy_app_data_view_set_current(NULL);
        }

        browser->proxy_list = g_list_remove(browser->proxy_list, proxy);
    }

    g_signal_handlers_disconnect_by_func(proxy->container, gwy_app_data_proxy_item_changed, proxy);
    gwy_app_data_proxy_finalize_list(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_CHANNELS].store),
                                     MODEL_OBJECT, &gwy_app_data_proxy_channel_changed, proxy);
    gwy_app_data_proxy_finalize_list(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_GRAPHS].store),
                                     MODEL_OBJECT, &gwy_app_data_proxy_graph_changed, proxy);
    gwy_app_data_proxy_finalize_list(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_SPECTRA].store),
                                     MODEL_OBJECT, &gwy_app_data_proxy_spectra_changed, proxy);
    gwy_app_data_proxy_finalize_list(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_VOLUMES].store),
                                     MODEL_OBJECT, &gwy_app_data_proxy_brick_changed, proxy);
    gwy_app_data_proxy_finalize_list(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_XYZS].store),
                                     MODEL_OBJECT, &gwy_app_data_proxy_surface_changed, proxy);
    gwy_app_data_proxy_finalize_list(GTK_TREE_MODEL(proxy->lists[GWY_PAGE_CURVE_MAPS].store),
                                     MODEL_OBJECT, &gwy_app_data_proxy_lawn_changed, proxy);

    g_object_unref(proxy->container);
    g_slice_free(GwyAppDataProxy, proxy);

    /* Ask for removal if used in idle function */
    return FALSE;
}

static void
gwy_app_data_proxy_queue_finalize(GwyAppDataProxy *proxy)
{
    gwy_debug("proxy %p", proxy);

    if (proxy->finalize_id)
        return;

    proxy->finalize_id = g_idle_add(&gwy_app_data_proxy_finalize, proxy);
}

/**
 * gwy_app_data_proxy_finalize_lists:
 * @proxy: Data proxy.
 *
 * Destroys all associated auxiliary data lists (masks, volume and surface previews, ...) but not 3D.
 *
 * XXX: Probably it is also possible to possible abstract away 3D to get it included here.
 **/
static void
gwy_app_data_proxy_finalize_lists(GwyAppDataProxy *proxy)
{
    gwy_app_data_assoc_finalize_list(proxy->associated_mask, proxy, &gwy_app_data_proxy_mask_changed);
    gwy_app_data_assoc_finalize_list(proxy->associated_brick_preview, proxy, &gwy_app_data_proxy_brick_preview_changed);
    gwy_app_data_assoc_finalize_list(proxy->associated_lawn_preview, proxy, &gwy_app_data_proxy_lawn_preview_changed);
    gwy_app_data_assoc_finalize_list(proxy->associated_raster, proxy, &gwy_app_data_proxy_raster_changed);
    proxy->associated_mask = NULL;
    proxy->associated_brick_preview = NULL;
    proxy->associated_lawn_preview = NULL;
    proxy->associated_raster = NULL;
}

/**
 * gwy_app_data_proxy_maybe_finalize:
 * @proxy: Data proxy.
 *
 * Checks whether there are any visible objects in a data proxy.
 *
 * If there are none, it queues finalization.  However, if @keep_invisible flag is set on the proxy, it is not
 * finalized.
 **/
static void
gwy_app_data_proxy_maybe_finalize(GwyAppDataProxy *proxy)
{
    gwy_debug("proxy %p", proxy);

    if (!proxy->keep_invisible && gwy_app_data_proxy_visible_count(proxy) == 0) {
        gwy_app_data_proxy_destroy_all_3d(proxy);
        gwy_app_data_proxy_destroy_messages(proxy);
        gwy_app_data_proxy_queue_finalize(proxy);
        gwy_app_data_proxy_finalize_lists(proxy);
    }
}

/**
 * gwy_app_data_proxy_list_setup:
 * @list: A data proxy list.
 *
 * Creates the list store of a data proxy object list and performs some basic setup.
 *
 * XXX: The @last field is set to -1, however for historical reasons graphs are 1-based and therefore graph lists need
 * to set it to 0.
 **/
static void
gwy_app_data_proxy_list_setup(GwyAppDataList *list)
{
    list->store = gtk_list_store_new(MODEL_N_COLUMNS,
                                     G_TYPE_INT,
                                     G_TYPE_OBJECT,
                                     G_TYPE_OBJECT,
                                     G_TYPE_DOUBLE,
                                     GDK_TYPE_PIXBUF);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list->store), MODEL_ID, GTK_SORT_ASCENDING);
    list->last = -1;
    list->active = -1;
    list->visible_count = 0;
}

/**
 * gwy_app_data_list_update_last:
 * @list: A data proxy list.
 * @empty_last: The value to set @last item to when there are no objects.
 *
 * Updates the value of @last field to the actual last object id.
 *
 * This function is intended to be used after object removal to keep the object id set compact (and the id numbers
 * low).
 **/
static void
gwy_app_data_list_update_last(GwyAppDataList *list,
                              gint empty_last)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint id, max = empty_last;

    model = GTK_TREE_MODEL(list->store);
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gtk_tree_model_get(model, &iter, MODEL_ID, &id, -1);
            if (id > max)
                max = id;
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    gwy_debug("new last item id: %d", max);
    list->last = max;
}

static void
gwy_app_data_browser_update_filename(GwyAppDataProxy *proxy)
{
    GwyAppDataBrowser *browser;
    const guchar *filename;
    gchar *s;

    browser = gwy_app_data_browser;
    if (!browser->window)
        return;

    if (gwy_container_gis_string(proxy->container, filename_quark, &filename)) {
        s = g_path_get_basename(filename);
        gtk_widget_set_tooltip_text(browser->filename, filename);
    }
    else {
        s = g_strdup_printf("%s %d", _("Untitled"), proxy->untitled_no);
        gtk_widget_set_tooltip_text(browser->filename, NULL);
    }
    gtk_label_set_text(GTK_LABEL(browser->filename), s);
    g_free(s);
}

/**
 * gwy_app_data_proxy_new:
 * @browser: Parent data browser for the new proxy.
 * @data: Data container to manage by the new proxy.
 *
 * Creates a data proxy for a data container.
 *
 * Note not only @parent field of the new proxy is set to @browser, but in addition the new proxy is added to
 * @browser's container list (as the new list head).
 *
 * Returns: A new data proxy.
 **/
static GwyAppDataProxy*
gwy_app_data_proxy_new(GwyAppDataBrowser *browser,
                       GwyContainer *data)
{
    GwyAppDataProxy *proxy;
    guint i;

    gwy_debug("Creating proxy for Container %p", data);
    g_object_ref(data);
    proxy = g_slice_new0(GwyAppDataProxy);
    proxy->container = data;
    proxy->data_no = ++last_data_number;
    proxy->parent = browser;
    proxy->untitled_no = ++browser->untitled_counter;
    browser->proxy_list = g_list_prepend(browser->proxy_list, proxy);
    g_signal_connect_after(data, "item-changed", G_CALLBACK(gwy_app_data_proxy_item_changed), proxy);

    for (i = 0; i < GWY_NPAGES; i++) {
        gwy_app_data_proxy_list_setup(&proxy->lists[i]);
        g_object_set_qdata(G_OBJECT(proxy->lists[i].store), page_id_quark, GINT_TO_POINTER(i + PAGENO_SHIFT));
    }
    /* For historical reasons, graphs are numbered from 1 */
    proxy->lists[GWY_PAGE_GRAPHS].last = 0;

    gwy_container_foreach(data, NULL, gwy_app_data_proxy_scan_data, proxy);
    ensure_brick_previews(proxy);
    ensure_lawn_previews(proxy);

    return proxy;
}

/**
 * gwy_app_data_browser_get_proxy:
 * @browser: A data browser.
 * @data: The container to find data proxy for.
 *
 * Finds data proxy managing a container.
 *
 * Returns: The data proxy managing container or %NULL.  It is assumed only one
 *          proxy exists for each container.
 **/
static GwyAppDataProxy*
gwy_app_data_browser_get_proxy(GwyAppDataBrowser *browser,
                               GwyContainer *data)
{
    GList *item;

    /* Optimize the fast path */
    if (browser->current && browser->current->container == data)
        return browser->current;

    item = g_list_find_custom(browser->proxy_list, data, &gwy_app_data_proxy_compare_data);
    if (!item)
        return NULL;

    /* Move container to head */
    if (item != browser->proxy_list) {
        browser->proxy_list = g_list_remove_link(browser->proxy_list, item);
        browser->proxy_list = g_list_concat(item, browser->proxy_list);
    }

    return (GwyAppDataProxy*)item->data;
}

static void
gwy_app_data_proxy_update_visibility(GObject *object,
                                     gboolean visible)
{
    GwyContainer *data;
    const gchar *strkey;
    gchar key[48];
    GQuark quark;

    data = g_object_get_qdata(object, container_quark);
    quark = GPOINTER_TO_UINT(g_object_get_qdata(object, own_key_quark));
    strkey = g_quark_to_string(quark);
    g_snprintf(key, sizeof(key), "%s/visible", strkey);
    if (visible)
        gwy_container_set_boolean_by_name(data, key, TRUE);
    else
        gwy_container_remove_by_name(data, key);
}

/**************************************************************************
 *
 * All treeviews
 *
 **************************************************************************/

static void
gwy_app_data_list_get_title_column(GtkTreeView *treeview,
                                   GtkTreeViewColumn **column,
                                   GtkCellRenderer **renderer)
{
    const gchar *col_id;
    GList *list, *l;

    list = gtk_tree_view_get_columns(treeview);
    for (l = list; l; l = g_list_next(l)) {
        *column = GTK_TREE_VIEW_COLUMN(l->data);
        col_id = g_object_get_qdata(G_OBJECT(*column), column_id_quark);
        if (!col_id || !gwy_strequal(col_id, "title"))
            continue;

        g_list_free(list);
        list = gtk_tree_view_column_get_cell_renderers(*column);
        if (g_list_length(list) > 1)
            g_warning("Too many cell renderers in title column");

        *renderer = GTK_CELL_RENDERER(list->data);
        g_list_free(list);
        g_assert(GTK_IS_CELL_RENDERER_TEXT(*renderer));
        return;
    }
    g_list_free(list);
    g_assert_not_reached();
}

static gboolean
gwy_app_data_list_key_pressed(GtkTreeView *treeview,
                              GdkEventKey *event,
                              G_GNUC_UNUSED gpointer user_data)
{
    GtkTreeSelection *selection;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    gboolean editable;

    if (event->keyval == GDK_Return || event->keyval == GDK_KP_Enter || event->keyval == GDK_F2) {
        selection = gtk_tree_view_get_selection(treeview);
        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
            gwy_app_data_list_get_title_column(treeview, &column, &renderer);
            g_object_get(renderer, "editable", &editable, NULL);
            if (!editable) {
                gtk_widget_grab_focus(GTK_WIDGET(treeview));
                path = gtk_tree_model_get_path(model, &iter);
                g_object_set(renderer, "editable", TRUE, NULL);
                gtk_tree_view_set_cursor(treeview, path, column, TRUE);
                gtk_tree_path_free(path);
                return TRUE;
            }
        }
    }
    return FALSE;
}

static gboolean
gwy_app_data_list_button_pressed(G_GNUC_UNUSED GtkTreeView *treeview,
                                 GdkEventButton *event,
                                 gpointer user_data)
{
    GwyAppDataBrowser *browser = (GwyAppDataBrowser*)user_data;

    if (event->type == GDK_2BUTTON_PRESS && event->button == 1)
        browser->doubleclick = TRUE;
    return FALSE;
}

static gboolean
gwy_app_data_list_button_released(GtkTreeView *treeview,
                                  GdkEventButton *event,
                                  gpointer user_data)
{
    GwyAppDataBrowser *browser = (GwyAppDataBrowser*)user_data;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column, *eventcolumn;
    GtkTreePath *path;
    gboolean editable;

    if (browser->doubleclick) {
        browser->doubleclick = FALSE;
        gwy_app_data_list_get_title_column(treeview, &column, &renderer);
        if (gtk_tree_view_get_path_at_pos(treeview, event->x, event->y, &path, &eventcolumn, NULL, NULL)
            && eventcolumn == column) {
            g_object_get(renderer, "editable", &editable, NULL);
            if (!editable) {
                gwy_debug("enabling editable");
                gtk_widget_grab_focus(GTK_WIDGET(treeview));
                g_object_set(renderer, "editable", TRUE, NULL);
                gtk_tree_view_set_cursor(treeview, path, column, TRUE);
            }
            gtk_tree_path_free(path);
        }
    }
    return FALSE;
}

static void
gwy_app_data_list_disable_edit(GtkCellRenderer *renderer,
                               gpointer check_time)
{
    if (GPOINTER_TO_UINT(check_time)) {
        GwyAppDataBrowser *browser = gwy_app_get_data_browser();
        if (gwy_get_timestamp() - browser->edit_timestamp < 0.1)
            return;
    }

    gwy_debug("disabling title editable (%p)", renderer);
    g_object_set(renderer, "editable", FALSE, NULL);
}

static void
gwy_app_data_list_name_edited(GtkCellRenderer *renderer,
                              const gchar *strpath,
                              const gchar *text,
                              GwyAppDataBrowser *browser)
{
    GwyAppDataProxy *proxy;
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeIter iter;
    gchar *title;

    g_return_if_fail(browser->current);
    proxy = browser->current;
    model = GTK_TREE_MODEL(proxy->lists[browser->active_page].store);

    path = gtk_tree_path_new_from_string(strpath);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

    title = g_strstrip(g_strdup(text));

    if (browser->active_page == GWY_PAGE_CHANNELS)
        gwy_app_data_proxy_channel_name_edited(proxy, &iter, title);
    else if (browser->active_page == GWY_PAGE_GRAPHS)
        gwy_app_data_proxy_graph_name_edited(proxy, &iter, title);
    else if (browser->active_page == GWY_PAGE_SPECTRA)
        gwy_app_data_proxy_spectra_name_edited(proxy, &iter, title);
    else if (browser->active_page == GWY_PAGE_VOLUMES)
        gwy_app_data_proxy_brick_name_edited(proxy, &iter, title);
    else if (browser->active_page == GWY_PAGE_XYZS)
        gwy_app_data_proxy_surface_name_edited(proxy, &iter, title);
    else if (browser->active_page == GWY_PAGE_CURVE_MAPS)
        gwy_app_data_proxy_lawn_name_edited(proxy, &iter, title);
    else {
        g_assert_not_reached();
    }

    gwy_app_data_list_disable_edit(renderer, GUINT_TO_POINTER(TRUE));
}

static void
gwy_app_data_browser_render_visible(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                    GtkCellRenderer *renderer,
                                    GtkTreeModel *model,
                                    GtkTreeIter *iter,
                                    G_GNUC_UNUSED gpointer userdata)
{
    GtkWidget *widget;

    gtk_tree_model_get(model, iter, MODEL_WIDGET, &widget, -1);
    g_object_set(renderer, "active", widget != NULL, NULL);
    GWY_OBJECT_UNREF(widget);
}

/* Does NOT set up the actual cell data function! */
static void
gwy_app_data_list_make_title_column(GwyAppDataBrowser *browser,
                                    GtkTreeViewColumn **column,
                                    GtkCellRenderer **renderer)
{
    *renderer = gtk_cell_renderer_text_new();
    g_object_set(*renderer,
                 "ellipsize", PANGO_ELLIPSIZE_END,
                 "ellipsize-set", TRUE,
                 "editable", FALSE,
                 "editable-set", TRUE,
                 NULL);
    g_signal_connect(*renderer, "edited", G_CALLBACK(gwy_app_data_list_name_edited), browser);
    g_signal_connect(*renderer, "editing-canceled", G_CALLBACK(gwy_app_data_list_disable_edit), NULL);
    *column = gtk_tree_view_column_new_with_attributes("Title", *renderer, NULL);
    gtk_tree_view_column_set_expand(*column, TRUE);
    g_object_set_qdata(G_OBJECT(*column), column_id_quark, "title");
}

static void
gwy_app_data_browser_selection_changed(GtkTreeSelection *selection,
                                       GwyAppDataBrowser *browser)
{
    GwyAppPage pageno;
    gboolean any;

    pageno = GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(selection), page_id_quark)) - PAGENO_SHIFT;
    if (pageno != browser->active_page)
        return;

    any = gtk_tree_selection_get_selected(selection, NULL, NULL);
    gwy_debug("Any: %d (page %d)", any, pageno);

    gwy_sensitivity_group_set_state(browser->sensgroup, SENS_OBJECT, any ? SENS_OBJECT : 0);
}

static void
update_window_icon(GtkTreeModel *model, GtkTreeIter *iter)
{
    GtkWidget *widget, *window;
    GdkPixbuf *pixbuf;

    g_return_if_fail(GTK_IS_LIST_STORE(model));

    gtk_tree_model_get(model, iter,
                       MODEL_THUMBNAIL, &pixbuf,
                       MODEL_WIDGET, &widget,
                       -1);

    if (pixbuf && widget) {
        window = gtk_widget_get_toplevel(GTK_WIDGET(widget));
        if (window && GTK_IS_WINDOW(window))
            gtk_window_set_icon(GTK_WINDOW(window), pixbuf);
    }

    GWY_OBJECT_UNREF(pixbuf);
    GWY_OBJECT_UNREF(widget);
}

static void
set_up_data_list_signals(GtkTreeView *treeview, GwyAppDataBrowser *browser)
{
    g_signal_connect(treeview, "key-press-event", G_CALLBACK(gwy_app_data_list_key_pressed), browser);
    g_signal_connect(treeview, "button-press-event", G_CALLBACK(gwy_app_data_list_button_pressed), browser);
    g_signal_connect(treeview, "button-release-event", G_CALLBACK(gwy_app_data_list_button_released), browser);
}

/**************************************************************************
 *
 * Channels treeview
 *
 **************************************************************************/

static void
gwy_app_data_browser_channel_render_title(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                          GtkCellRenderer *renderer,
                                          GtkTreeModel *model,
                                          GtkTreeIter *iter,
                                          gpointer userdata)
{
    GwyAppDataBrowser *browser = (GwyAppDataBrowser*)userdata;
    gchar *title;
    GwyContainer *data;
    gint channel;

    /* XXX: browser->current must match what is visible in the browser */
    data = browser->current->container;
    gtk_tree_model_get(model, iter, MODEL_ID, &channel, -1);
    title = _gwy_app_figure_out_channel_title(data, channel);
    g_object_set(renderer, "text", title, NULL);
    g_free(title);
}

static void
gwy_app_data_browser_channel_render_flags(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                          GtkCellRenderer *renderer,
                                          GtkTreeModel *model,
                                          GtkTreeIter *iter,
                                          gpointer userdata)
{
    GwyAppDataBrowser *browser = (GwyAppDataBrowser*)userdata;
    gboolean has_mask, has_show, has_cal;
    GwyContainer *data;
    gchar key[24];
    gint channel;

    /* XXX: browser->current must match what is visible in the browser */
    data = browser->current->container;

    gtk_tree_model_get(model, iter, MODEL_ID, &channel, -1);
    has_mask = gwy_container_contains(data, gwy_app_get_mask_key_for_id(channel));
    has_show = gwy_container_contains(data, gwy_app_get_show_key_for_id(channel));
    //FIXME, all the fields should be present
    g_snprintf(key, sizeof(key), "/%d/data/cal_zunc", channel);
    has_cal = gwy_container_contains_by_name(data, key);

    g_snprintf(key, sizeof(key), "%s%s%s",
               has_mask ? "M" : "",
               has_show ? "P" : "",
               has_cal ? "C" : "");

    g_object_set(renderer, "text", key, NULL);
}

static void
gwy_app_data_browser_render_channel(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                    GtkCellRenderer *renderer,
                                    GtkTreeModel *model,
                                    GtkTreeIter *iter,
                                    G_GNUC_UNUSED gpointer userdata)
{
    GwyContainer *container;
    GObject *object;
    GdkPixbuf *pixbuf;
    gdouble timestamp, *pbuf_timestamp = NULL;
    gint id;

    gtk_tree_model_get(model, iter,
                       MODEL_ID, &id,
                       MODEL_OBJECT, &object,
                       MODEL_TIMESTAMP, &timestamp,
                       MODEL_THUMBNAIL, &pixbuf,
                       -1);

    container = g_object_get_qdata(object, container_quark);
    g_object_unref(object);

    if (pixbuf) {
        pbuf_timestamp = (gdouble*)g_object_get_data(G_OBJECT(pixbuf), "timestamp");
        g_object_unref(pixbuf);
        if (*pbuf_timestamp >= timestamp) {
            g_object_set(renderer, "pixbuf", pixbuf, NULL);
            return;
        }
    }

    pixbuf = gwy_app_get_channel_thumbnail(container, id, THUMB_SIZE, THUMB_SIZE);
    pbuf_timestamp = g_new(gdouble, 1);
    *pbuf_timestamp = gwy_get_timestamp();
    g_object_set_data_full(G_OBJECT(pixbuf), "timestamp", pbuf_timestamp, g_free);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, MODEL_THUMBNAIL, pixbuf, -1);
    g_object_set(renderer, "pixbuf", pixbuf, NULL);
    g_object_unref(pixbuf);

    update_window_icon(model, iter);
}

/**
 * gwy_app_data_browser_channel_deleted:
 * @data_window: A data window that was deleted.
 *
 * Destroys a deleted data window, updating proxy.
 *
 * This functions makes sure various updates happen in reasonable order,
 * simple gtk_widget_destroy() on the data window would not do that.
 *
 * Returns: Always %TRUE to be usable as terminal event handler.
 **/
static gboolean
gwy_app_data_browser_channel_deleted(GwyDataWindow *data_window)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GwyAppKeyType type;
    GwyContainer *data;
    GwyDataView *data_view;
    GwyPixmapLayer *layer;
    GtkTreeIter iter;
    const gchar *strkey;
    GObject *object;
    GQuark quark;
    gint i;

    gwy_debug("Data window %p deleted", data_window);
    data_view = gwy_data_window_get_data_view(data_window);
    data = gwy_data_view_get_data(data_view);
    layer = gwy_data_view_get_base_layer(data_view);
    strkey = gwy_pixmap_layer_get_data_key(layer);
    quark = g_quark_from_string(strkey);
    g_return_val_if_fail(data && quark, TRUE);
    object = gwy_container_get_object(data, quark);

    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_val_if_fail(i >= 0 && type == KEY_IS_DATA, TRUE);

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    list = &proxy->lists[GWY_PAGE_CHANNELS];
    if (!gwy_app_data_proxy_find_object(list->store, i, &iter)) {
        g_critical("Cannot find data field %p (%d)", object, i);
        return TRUE;
    }

    proxy->resetting_visibility = TRUE;
    gwy_app_data_proxy_channel_set_visible(proxy, &iter, FALSE);
    proxy->resetting_visibility = FALSE;
    gwy_app_data_proxy_maybe_finalize(proxy);

    return TRUE;
}

static gboolean
gwy_app_graph_window_dnd_curve_received(GtkWidget *destwidget,
                                        GtkTreeModel *model,
                                        GtkTreePath *path)
{
    GwyGraphWindow *destwindow, *srcwindow;
    GwyGraphModel *destmodel, *srcmodel;
    GwyGraphCurveModel *gcmodel;
    const gint *indices;
    GtkWidget *w;

    srcwindow = GWY_GRAPH_WINDOW(g_object_get_qdata(G_OBJECT(model), graph_window_quark));
    destwindow = GWY_GRAPH_WINDOW(destwidget);

    w = gwy_graph_window_get_graph(srcwindow);
    srcmodel = gwy_graph_get_model(GWY_GRAPH(w));
    w = gwy_graph_window_get_graph(destwindow);
    destmodel = gwy_graph_get_model(GWY_GRAPH(w));

    /* Ignore drops to the same graph */
    if (srcmodel == destmodel || !gwy_graph_model_units_are_compatible(destmodel, srcmodel))
        return FALSE;

    /* Copy curve */
    indices = gtk_tree_path_get_indices(path);
    gcmodel = gwy_graph_model_get_curve(srcmodel, indices[0]);
    gcmodel = gwy_graph_curve_model_duplicate(gcmodel);
    gwy_graph_model_add_curve(destmodel, gcmodel);
    g_object_unref(gcmodel);

    return TRUE;
}

static void
gwy_app_window_dnd_data_received(GtkWidget *window,
                                 GdkDragContext *context,
                                 G_GNUC_UNUSED gint x,
                                 G_GNUC_UNUSED gint y,
                                 GtkSelectionData *data,
                                 G_GNUC_UNUSED guint info,
                                 guint time_,
                                 gpointer user_data)
{
    GwyAppDataBrowser *browser = (GwyAppDataBrowser*)user_data;
    GwyAppDataProxy *srcproxy, *destproxy;
    GwyContainer *container = NULL;
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeIter iter;
    GwyAppPage pageno;

    if (!gtk_tree_get_row_drag_data(data, &model, &path)) {
        g_warning("Cannot get row drag data");
        gtk_drag_finish(context, FALSE, FALSE, time_);
        return;
    }

    window = gtk_widget_get_ancestor(window, GTK_TYPE_WINDOW);
    if (GWY_IS_GRAPH_WINDOW(window) && g_object_get_qdata(G_OBJECT(model), graph_window_quark)) {
        gboolean ok;

        ok = gwy_app_graph_window_dnd_curve_received(window, model, path);
        gtk_tree_path_free(path);
        gtk_drag_finish(context, ok, FALSE, time_);
        return;
    }

    srcproxy = browser->current;
    if (!(pageno = GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(model), page_id_quark)))) {
        gtk_drag_finish(context, FALSE, FALSE, time_);
        return;
    }
    pageno -= PAGENO_SHIFT;

    if (!gtk_tree_model_get_iter(model, &iter, path)) {
        g_warning("Received data browser drop of a nonexistent path");
        gtk_drag_finish(context, FALSE, FALSE, time_);
        return;
    }
    gtk_tree_path_free(path);

    if (GWY_IS_DATA_WINDOW(window)) {
        container = gwy_data_window_get_data(GWY_DATA_WINDOW(window));
    }
    else if (GWY_IS_GRAPH_WINDOW(window)) {
        GtkWidget *graph;
        GObject *object;

        graph = gwy_graph_window_get_graph(GWY_GRAPH_WINDOW(window));
        object = G_OBJECT(gwy_graph_get_model(GWY_GRAPH(graph)));
        container = g_object_get_qdata(object, container_quark);
    }

    /* Foreign tree models */
    if (pageno == GWY_PAGE_NOPAGE) {
        gwy_app_data_browser_copy_other(model, &iter, window, container);
    }
    else if (container) {
        destproxy = gwy_app_data_browser_get_proxy(browser, container);
        gwy_app_data_browser_copy_object(srcproxy, pageno, model, &iter, destproxy);
    }
    else
        g_warning("Cannot determine drop target GwyContainer");

    gtk_drag_finish(context, TRUE, FALSE, time_);
}

/**
 * gwy_app_data_browser_create_channel:
 * @browser: A data browser.
 * @id: The channel id.
 *
 * Creates a data window for a data field when its visibility is switched on.
 *
 * This is actually `make visible', should not be used outside
 * gwy_app_data_proxy_channel_set_visible().
 *
 * Returns: The data view (NOT data window).
 **/
static GtkWidget*
gwy_app_data_browser_create_channel(GwyAppDataBrowser *browser,
                                    GwyAppDataProxy *proxy,
                                    gint id)
{
    GtkWidget *data_view, *data_window;
    GObject *dfield = NULL;
    GwyPixmapLayer *layer;
    GwyLayerBasic *layer_basic;

    gwy_container_gis_object(proxy->container, gwy_app_get_data_key_for_id(id), &dfield);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(dfield), NULL);

    layer = gwy_layer_basic_new();
    layer_basic = GWY_LAYER_BASIC(layer);
    gwy_pixmap_layer_set_data_key(layer, g_quark_to_string(gwy_app_get_data_key_for_id(id)));
    gwy_layer_basic_set_presentation_key(layer_basic, g_quark_to_string(gwy_app_get_show_key_for_id(id)));
    gwy_layer_basic_set_min_max_key(layer_basic, g_quark_to_string(gwy_app_get_data_base_key_for_id(id)));
    gwy_layer_basic_set_range_type_key(layer_basic, g_quark_to_string(gwy_app_get_data_range_type_key_for_id(id)));
    gwy_layer_basic_set_gradient_key(layer_basic, g_quark_to_string(gwy_app_get_data_palette_key_for_id(id)));

    data_view = gwy_data_view_new(proxy->container);
    gwy_data_view_set_data_prefix(GWY_DATA_VIEW(data_view), gwy_pixmap_layer_get_data_key(layer));
    gwy_data_view_set_base_layer(GWY_DATA_VIEW(data_view), layer);

    data_window = gwy_data_window_new(GWY_DATA_VIEW(data_view));
    g_object_set_data(G_OBJECT(data_window), "gwy-app-page", GUINT_TO_POINTER(GWY_PAGE_CHANNELS));
    gwy_app_update_data_window_title(GWY_DATA_VIEW(data_view), id);

    gwy_app_data_proxy_update_visibility(dfield, TRUE);
    g_signal_connect_swapped(data_window, "focus-in-event",
                             G_CALLBACK(gwy_app_data_browser_select_data_view2), data_view);
    g_signal_connect(data_window, "delete-event", G_CALLBACK(gwy_app_data_browser_channel_deleted), NULL);
    _gwy_app_data_window_setup(GWY_DATA_WINDOW(data_window));

    /* Channel DnD */
    gtk_drag_dest_set(data_window, GTK_DEST_DEFAULT_ALL,
                      dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);
    g_signal_connect(data_window, "drag-data-received", G_CALLBACK(gwy_app_window_dnd_data_received), browser);

    _gwy_app_sync_mask(proxy->container, gwy_app_get_mask_key_for_id(id), GWY_DATA_VIEW(data_view));
    _gwy_app_update_data_range_type(GWY_DATA_VIEW(data_view), id);

    /* FIXME: A silly place for this? */
    gwy_app_data_browser_set_file_present(browser, TRUE);
    gtk_widget_show_all(data_window);
    _gwy_app_data_view_set_current(GWY_DATA_VIEW(data_view));
    _gwy_app_update_channel_sens();

    return data_view;
}

static void
gwy_app_update_data_window_title(GwyDataView *data_view,
                                 gint id)
{
    GtkWidget *data_window;
    GwyContainer *data;
    const guchar *filename;
    gchar *ctitle, *title, *bname;

    data_window = gtk_widget_get_ancestor(GTK_WIDGET(data_view), GWY_TYPE_DATA_WINDOW);
    if (!data_window) {
        g_warning("GwyDataView has no GwyDataWindow ancestor");
        return;
    }

    data = gwy_data_view_get_data(data_view);
    ctitle = _gwy_app_figure_out_channel_title(data, id);
    if (gwy_container_gis_string(data, filename_quark, &filename)) {
        bname = g_path_get_basename(filename);
        title = g_strdup_printf("%s [%s]", bname, ctitle);
        g_free(bname);
    }
    else {
        GwyAppDataBrowser *browser;
        GwyAppDataProxy *proxy;

        browser = gwy_app_get_data_browser();
        proxy = gwy_app_data_browser_get_proxy(browser, data);
        title = g_strdup_printf("%s %d [%s]", _("Untitled"), proxy->untitled_no, ctitle);
    }
    gwy_data_window_set_data_name(GWY_DATA_WINDOW(data_window), title);
    g_free(title);
    g_free(ctitle);
}

static void
gwy_app_data_proxy_update_window_titles(GwyAppDataProxy *proxy)
{
    GwyDataView *data_view;
    GwyAppDataList *list;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GList *item;
    gint id;

    list = &proxy->lists[GWY_PAGE_CHANNELS];
    model = GTK_TREE_MODEL(list->store);
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gtk_tree_model_get(model, &iter,
                               MODEL_ID, &id,
                               MODEL_WIDGET, &data_view,
                               -1);
            if (data_view) {
                gwy_app_update_data_window_title(data_view, id);
                g_object_unref(data_view);
            }
            if ((item = gwy_app_data_proxy_get_3d(proxy, id))) {
                GwyAppDataAssociation *assoc = (GwyAppDataAssociation*)item->data;

                _gwy_app_update_3d_window_title(GWY_3D_WINDOW(assoc->object), id);
            }
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    list = &proxy->lists[GWY_PAGE_VOLUMES];
    model = GTK_TREE_MODEL(list->store);
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gtk_tree_model_get(model, &iter,
                               MODEL_ID, &id,
                               MODEL_WIDGET, &data_view,
                               -1);
            if (data_view) {
                gwy_app_update_brick_window_title(data_view, id);
                g_object_unref(data_view);
            }
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    list = &proxy->lists[GWY_PAGE_XYZS];
    model = GTK_TREE_MODEL(list->store);
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gtk_tree_model_get(model, &iter,
                               MODEL_ID, &id,
                               MODEL_WIDGET, &data_view,
                               -1);
            if (data_view) {
                gwy_app_update_surface_window_title(data_view, id);
                g_object_unref(data_view);
            }
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    list = &proxy->lists[GWY_PAGE_CURVE_MAPS];
    model = GTK_TREE_MODEL(list->store);
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            gtk_tree_model_get(model, &iter,
                               MODEL_ID, &id,
                               MODEL_WIDGET, &data_view,
                               -1);
            if (data_view) {
                gwy_app_update_lawn_window_title(data_view, id);
                g_object_unref(data_view);
            }
        } while (gtk_tree_model_iter_next(model, &iter));
    }
}

static gboolean
gwy_app_data_proxy_channel_set_visible(GwyAppDataProxy *proxy,
                                       GtkTreeIter *iter,
                                       gboolean visible)
{
    GwyAppDataList *list;
    GtkTreeModel *model;
    GtkWidget *widget, *window;
    GObject *object;
    gint id;

    list = &proxy->lists[GWY_PAGE_CHANNELS];
    model = GTK_TREE_MODEL(list->store);

    gtk_tree_model_get(model, iter,
                       MODEL_WIDGET, &widget,
                       MODEL_OBJECT, &object,
                       MODEL_ID, &id,
                       -1);
    if (visible == (widget != NULL)) {
        g_object_unref(object);
        GWY_OBJECT_UNREF(widget);
        return FALSE;
    }

    if (visible) {
        widget = gwy_app_data_browser_create_channel(proxy->parent, proxy, id);
        gtk_list_store_set(list->store, iter, MODEL_WIDGET, widget, -1);
        update_window_icon(model, iter);
        list->visible_count++;
    }
    else {
        gwy_app_data_proxy_update_visibility(object, FALSE);
        window = gtk_widget_get_ancestor(widget, GWY_TYPE_DATA_WINDOW);
        gtk_widget_destroy(window);
        gtk_list_store_set(list->store, iter, MODEL_WIDGET, NULL, -1);
        g_object_unref(widget);
        list->visible_count--;
        _gwy_app_update_channel_sens();
    }
    g_object_unref(object);

    gwy_debug("visible_count: %d", list->visible_count);

    return TRUE;
}

static void
gwy_app_data_browser_channel_toggled(GtkCellRendererToggle *renderer,
                                     gchar *path_str,
                                     GwyAppDataBrowser *browser)
{
    GwyAppDataProxy *proxy;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeModel *model;
    gboolean active, toggled;

    gwy_debug("Toggled channel row %s", path_str);
    proxy = browser->current;
    g_return_if_fail(proxy);

    path = gtk_tree_path_new_from_string(path_str);
    model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_CHANNELS].store);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

    active = gtk_cell_renderer_toggle_get_active(renderer);
    proxy->resetting_visibility = TRUE;
    toggled = gwy_app_data_proxy_channel_set_visible(proxy, &iter, !active);
    proxy->resetting_visibility = FALSE;
    g_assert(toggled);

    gwy_app_data_proxy_maybe_finalize(proxy);
}

static void
gwy_app_data_proxy_channel_name_edited(GwyAppDataProxy *proxy,
                                       GtkTreeIter *iter,
                                       gchar *title)
{
    GtkTreeModel *model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_CHANNELS].store);
    gint id;

    gtk_tree_model_get(model, iter, MODEL_ID, &id, -1);
    if (!*title) {
        g_free(title);
        gwy_app_set_data_field_title(proxy->container, id, NULL);
    }
    else {
        gwy_container_set_string(proxy->container, gwy_app_get_data_title_key_for_id(id), title);
    }
}

static GtkWidget*
gwy_app_data_browser_construct_channels(GwyAppDataBrowser *browser)
{
    GtkWidget *retval;
    GtkTreeView *treeview;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    /* Construct the GtkTreeView that will display data channels */
    retval = gtk_tree_view_new();
    treeview = GTK_TREE_VIEW(retval);
    set_up_data_list_signals(treeview, browser);

    /* Add the thumbnail column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes("Thumbnail", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_channel, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the visibility column */
    renderer = gtk_cell_renderer_toggle_new();
    g_object_set(renderer, "activatable", TRUE, NULL);
    g_signal_connect(renderer, "toggled", G_CALLBACK(gwy_app_data_browser_channel_toggled), browser);
    column = gtk_tree_view_column_new_with_attributes("Visible", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_visible, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the title column */
    gwy_app_data_list_make_title_column(browser, &column, &renderer);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_channel_render_title, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the flags column */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "width-chars", 5, NULL);
    column = gtk_tree_view_column_new_with_attributes("Flags", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_channel_render_flags, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    gtk_tree_view_set_headers_visible(treeview, FALSE);

    /* Selection */
    selection = gtk_tree_view_get_selection(treeview);
    g_object_set_qdata(G_OBJECT(selection), page_id_quark, GINT_TO_POINTER(GWY_PAGE_CHANNELS + PAGENO_SHIFT));
    g_signal_connect(selection, "changed", G_CALLBACK(gwy_app_data_browser_selection_changed), browser);

    /* DnD */
    gtk_tree_view_enable_model_drag_source(treeview, GDK_BUTTON1_MASK,
                                           dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);

    return retval;
}

/* Find an object in a list of #GwyAppDataAssociation items, making the
 * returned item the new list head. Return %NULL if nothing is found. */
G_GNUC_UNUSED
static GwyAppDataAssociation*
gwy_app_data_assoc_find(GList **assoclist, GObject *object)
{
    GList *l;

    for (l = *assoclist; l; l = g_list_next(l)) {
        GwyAppDataAssociation *assoc = (GwyAppDataAssociation*)l->data;

        if (assoc->object == object) {
            *assoclist = g_list_remove_link(*assoclist, l);
            *assoclist = g_list_concat(l, *assoclist);
            return assoc;
        }
    }

    return NULL;
}

/* Find an id in a list of #GwyAppDataAssociation items, making the
 * returned item the new list head. Return %NULL if nothing is found. */
static GwyAppDataAssociation*
gwy_app_data_assoc_get(GList **assoclist, gint id)
{
    GList *l;

    for (l = *assoclist; l; l = g_list_next(l)) {
        GwyAppDataAssociation *assoc = (GwyAppDataAssociation*)l->data;

        if (assoc->id == id) {
            *assoclist = g_list_remove_link(*assoclist, l);
            *assoclist = g_list_concat(l, *assoclist);
            return assoc;
        }
    }

    return NULL;
}

/**************************************************************************
 *
 * Channels 3D
 *
 **************************************************************************/

static GList*
gwy_app_data_proxy_find_3d(GwyAppDataProxy *proxy,
                           Gwy3DWindow *window3d)
{
    GList *l;

    for (l = proxy->associated_3d; l; l = g_list_next(l)) {
        GwyAppDataAssociation *assoc = (GwyAppDataAssociation*)l->data;

        if ((Gwy3DWindow*)assoc->object == window3d)
            return l;
    }

    return NULL;
}

static GList*
gwy_app_data_proxy_get_3d(GwyAppDataProxy *proxy,
                          gint id)
{
    GList *l;

    for (l = proxy->associated_3d; l; l = g_list_next(l)) {
        GwyAppDataAssociation *assoc = (GwyAppDataAssociation*)l->data;

        if (assoc->id == id)
            return l;
    }

    return NULL;
}

static void
gwy_app_data_proxy_3d_destroyed(Gwy3DWindow *window3d,
                                GwyAppDataProxy *proxy)
{
    GwyAppDataAssociation *assoc;
    GList *item;

    item = gwy_app_data_proxy_find_3d(proxy, window3d);
    g_return_if_fail(item);

    assoc = (GwyAppDataAssociation*)item->data;
    g_slice_free(GwyAppDataAssociation, assoc);
    proxy->associated_3d = g_list_delete_link(proxy->associated_3d, item);
}

static void
gwy_app_data_proxy_channel_destroy_3d(GwyAppDataProxy *proxy,
                                      gint id)
{
    GwyAppDataAssociation *assoc;
    GList *l;

    l = gwy_app_data_proxy_get_3d(proxy, id);
    if (!l)
        return;

    proxy->associated_3d = g_list_remove_link(proxy->associated_3d, l);
    assoc = (GwyAppDataAssociation*)l->data;
    g_signal_handlers_disconnect_by_func(assoc->object, gwy_app_data_proxy_3d_destroyed, proxy);
    gtk_widget_destroy(GTK_WIDGET(assoc->object));
    g_slice_free(GwyAppDataAssociation, assoc);
    g_list_free_1(l);
}

static void
gwy_app_data_proxy_destroy_all_3d(GwyAppDataProxy *proxy)
{
    while (proxy->associated_3d) {
        GwyAppDataAssociation *assoc;

        assoc = (GwyAppDataAssociation*)proxy->associated_3d->data;
        gwy_app_data_proxy_channel_destroy_3d(proxy, assoc->id);
    }
}

static GtkWidget*
gwy_app_data_browser_create_3d(G_GNUC_UNUSED GwyAppDataBrowser *browser,
                               GwyAppDataProxy *proxy,
                               gint id)
{
    GtkWidget *view3d, *window3d;
    GObject *dfield = NULL;
    GwyAppDataAssociation *assoc;
    gchar key[40];
    GQuark mask_key;
    const guchar *palette = NULL;
    guint len;

    g_snprintf(key, sizeof(key), "/%d/data", id);
    gwy_container_gis_object_by_name(proxy->container, key, &dfield);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(dfield), NULL);

    g_snprintf(key, sizeof(key), "/%d/base/palette", id);
    gwy_container_gis_string_by_name(proxy->container, key, &palette);

    view3d = gwy_3d_view_new(proxy->container);

    g_snprintf(key, sizeof(key), "/%d/", id);
    len = strlen(key);

    g_strlcat(key, "3d", sizeof(key));
    /* Since gwy_3d_view_set_setup_prefix() instantiates a new 3d setup if none is present, we have to check whether
     * any is present and create a new one with user's defaults before calling this method.  After that we cannot tell
     * whether the 3d setup was in the container from previous 3d views or it has been just created. */
    _gwy_app_3d_view_init_setup(proxy->container, key);
    gwy_3d_view_set_setup_prefix(GWY_3D_VIEW(view3d), key);

    key[len] = '\0';
    g_strlcat(key, "data", sizeof(key));
    gwy_3d_view_set_data_key(GWY_3D_VIEW(view3d), key);

    /* key[len] = '\0';
    g_strlcat(key, "3d/data2ref", sizeof(key));
    gwy_3d_view_set_data2_key(GWY_3D_VIEW(view3d), key);
    */
    key[len] = '\0';
    g_strlcat(key, "3d/palette", sizeof(key));
    gwy_3d_view_set_gradient_key(GWY_3D_VIEW(view3d), key);

    if (palette)
        gwy_container_set_const_string_by_name(proxy->container, key, palette);

    key[len] = '\0';
    g_strlcat(key, "3d/material", sizeof(key));
    gwy_3d_view_set_material_key(GWY_3D_VIEW(view3d), key);

    mask_key = gwy_app_get_mask_key_for_id(id);
    gwy_3d_view_set_mask_key(GWY_3D_VIEW(view3d), g_quark_to_string(mask_key));

    window3d = gwy_3d_window_new(GWY_3D_VIEW(view3d));

    _gwy_app_update_3d_window_title(GWY_3D_WINDOW(window3d), id);

    g_signal_connect(window3d, "destroy", G_CALLBACK(gwy_app_data_proxy_3d_destroyed), proxy);

    assoc = g_slice_new(GwyAppDataAssociation);
    assoc->object = G_OBJECT(window3d);
    assoc->id = id;
    proxy->associated_3d = g_list_prepend(proxy->associated_3d, assoc);

    _gwy_app_3d_window_setup(GWY_3D_WINDOW(window3d));
    gtk_widget_show_all(window3d);

    return window3d;
}

/**
 * gwy_app_data_browser_show_3d:
 * @data: A data container.
 * @id: Channel id.
 *
 * Shows a 3D window displaying a channel.
 *
 * If a 3D window of the specified channel already exists, it is just presented to the user.  If it does not exist, it
 * is created.
 *
 * The caller must ensure 3D display is available, for example by checking gwy_app_gl_is_ok().
 **/
void
gwy_app_data_browser_show_3d(GwyContainer *data,
                             gint id)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GtkWidget *window3d;
    GList *item;

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    g_return_if_fail(proxy);

    if (gui_disabled)
        return;

    item = gwy_app_data_proxy_get_3d(proxy, id);
    if (item)
        window3d = GTK_WIDGET(((GwyAppDataAssociation*)item->data)->object);
    else
        window3d = gwy_app_data_browser_create_3d(browser, proxy, id);
    g_return_if_fail(window3d);
    gtk_window_present(GTK_WINDOW(window3d));
}

/**************************************************************************
 *
 * Graphs treeview
 *
 **************************************************************************/

static void
gwy_app_data_browser_graph_render_title(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                        GtkCellRenderer *renderer,
                                        GtkTreeModel *model,
                                        GtkTreeIter *iter,
                                        G_GNUC_UNUSED gpointer userdata)
{
    GObject *gmodel;
    gchar *title;

    gtk_tree_model_get(model, iter, MODEL_OBJECT, &gmodel, -1);
    g_object_get(gmodel, "title", &title, NULL);
    g_object_set(renderer, "text", title, NULL);
    g_free(title);
    g_object_unref(gmodel);
}

static void
gwy_app_data_browser_graph_render_flags(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                        GtkCellRenderer *renderer,
                                        GtkTreeModel *model,
                                        GtkTreeIter *iter,
                                        G_GNUC_UNUSED gpointer userdata)
{
    GwyGraphModel *gmodel;
    gchar s[8];
    gboolean has_cal = FALSE;
    gint i, nc;

    gtk_tree_model_get(model, iter, MODEL_OBJECT, &gmodel, -1);
    nc = gwy_graph_model_get_n_curves(gmodel);
    for (i = 0; i < nc; i++) {
        if (gwy_graph_curve_model_get_calibration_data(gwy_graph_model_get_curve(gmodel, i))) {
            has_cal = TRUE;
            break;
        }
    }
    if (has_cal)
        g_snprintf(s, sizeof(s), "%d C", gwy_graph_model_get_n_curves(gmodel));
    else
        g_snprintf(s, sizeof(s), "%d", gwy_graph_model_get_n_curves(gmodel));
    g_object_set(renderer, "text", s, NULL);
    g_object_unref(gmodel);
}

static void
gwy_app_data_browser_render_graph(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                  GtkCellRenderer *renderer,
                                  GtkTreeModel *model,
                                  GtkTreeIter *iter,
                                  G_GNUC_UNUSED gpointer userdata)
{
    GwyContainer *container;
    GObject *object;
    GdkPixbuf *pixbuf;
    gdouble timestamp, *pbuf_timestamp = NULL;
    gint id;

    gtk_tree_model_get(model, iter,
                       MODEL_ID, &id,
                       MODEL_OBJECT, &object,
                       MODEL_TIMESTAMP, &timestamp,
                       MODEL_THUMBNAIL, &pixbuf,
                       -1);

    container = g_object_get_qdata(object, container_quark);

    if (pixbuf) {
        pbuf_timestamp = (gdouble*)g_object_get_data(G_OBJECT(pixbuf), "timestamp");
        g_object_unref(pixbuf);
        if (*pbuf_timestamp >= timestamp) {
            g_object_set(renderer, "pixbuf", pixbuf, NULL);
            return;
        }
    }

    pixbuf = gwy_app_get_graph_thumbnail(container, id,
                                         500*THUMB_SIZE/433,
                                         433*THUMB_SIZE/500);
    pbuf_timestamp = g_new(gdouble, 1);
    *pbuf_timestamp = gwy_get_timestamp();
    g_object_set_data_full(G_OBJECT(pixbuf), "timestamp", pbuf_timestamp, g_free);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, MODEL_THUMBNAIL, pixbuf, -1);
    g_object_set(renderer, "pixbuf", pixbuf, NULL);
    g_object_unref(pixbuf);
}

/**
 * gwy_app_data_browser_graph_deleted:
 * @graph_window: A graph window that was deleted.
 *
 * Destroys a deleted graph window, updating proxy.
 *
 * This functions makes sure various updates happen in reasonable order,
 * simple gtk_widget_destroy() on the graph window would not do that.
 *
 * Returns: Always %TRUE to be usable as terminal event handler.
 **/
static gboolean
gwy_app_data_browser_graph_deleted(GwyGraphWindow *graph_window)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GwyAppKeyType type;
    GObject *object;
    GwyContainer *data;
    GtkWidget *graph;
    GtkTreeIter iter;
    const gchar *strkey;
    GQuark quark;
    gint i;

    gwy_debug("Graph window %p deleted", graph_window);
    graph = gwy_graph_window_get_graph(graph_window);
    object = G_OBJECT(gwy_graph_get_model(GWY_GRAPH(graph)));
    data = g_object_get_qdata(object, container_quark);
    quark = GPOINTER_TO_UINT(g_object_get_qdata(object, own_key_quark));
    g_return_val_if_fail(data && quark, TRUE);

    strkey = g_quark_to_string(quark);
    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_val_if_fail(i >= 0 && type == KEY_IS_GRAPH, TRUE);

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    list = &proxy->lists[GWY_PAGE_GRAPHS];
    if (!gwy_app_data_proxy_find_object(list->store, i, &iter)) {
        g_critical("Cannot find graph model %p (%d)", object, i);
        return TRUE;
    }

    proxy->resetting_visibility = TRUE;
    gwy_app_data_proxy_graph_set_visible(proxy, &iter, FALSE);
    proxy->resetting_visibility = FALSE;
    gwy_app_data_proxy_maybe_finalize(proxy);

    return TRUE;
}

/**
 * gwy_app_data_browser_create_graph:
 * @browser: A data browser.
 * @id: The graph id.
 *
 * Creates a graph window for a graph model when its visibility is switched on.
 *
 * This is actually `make visible', should not be used outside
 * gwy_app_data_proxy_graph_set_visible().
 *
 * Returns: The graph widget (NOT graph window).
 **/
static GtkWidget*
gwy_app_data_browser_create_graph(GwyAppDataBrowser *browser,
                                  GwyAppDataProxy *proxy,
                                  gint id)
{
    GtkWidget *graph, *curves, *graph_window;
    GtkTreeModel *model;
    GQuark quark;
    GObject *gmodel;

    quark = gwy_app_get_graph_key_for_id(id);
    gwy_container_gis_object(proxy->container, quark, &gmodel);
    g_return_val_if_fail(GWY_IS_GRAPH_MODEL(gmodel), NULL);

    graph = gwy_graph_new(GWY_GRAPH_MODEL(gmodel));
    graph_window = gwy_graph_window_new(GWY_GRAPH(graph));

    /* Graphs do not reference Container, fake it */
    g_object_ref(proxy->container);
    g_object_weak_ref(G_OBJECT(graph_window), (GWeakNotify)g_object_unref, proxy->container);

    gwy_app_data_proxy_update_visibility(gmodel, TRUE);
    g_signal_connect_swapped(graph_window, "focus-in-event", G_CALLBACK(gwy_app_data_browser_select_graph2), graph);
    g_signal_connect(graph_window, "delete-event", G_CALLBACK(gwy_app_data_browser_graph_deleted), NULL);
    _gwy_app_graph_window_setup(GWY_GRAPH_WINDOW(graph_window), proxy->container, quark);

    /* Graph DnD */
    gtk_drag_dest_set(graph_window, GTK_DEST_DEFAULT_ALL,
                      dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);
    g_signal_connect(graph_window, "drag-data-received", G_CALLBACK(gwy_app_window_dnd_data_received), browser);

    /* Graph curve DnD */
    curves = gwy_graph_window_get_graph_curves(GWY_GRAPH_WINDOW(graph_window));
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(curves));
    g_object_set_qdata(G_OBJECT(model), graph_window_quark, graph_window);
    gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(curves), GDK_BUTTON1_MASK,
                                           dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);

    /* FIXME: A silly place for this? */
    gwy_app_data_browser_set_file_present(browser, TRUE);
    gtk_widget_show_all(graph_window);
    _gwy_app_update_graph_sens();

    return graph;
}

static gboolean
gwy_app_data_proxy_graph_set_visible(GwyAppDataProxy *proxy,
                                     GtkTreeIter *iter,
                                     gboolean visible)
{
    GwyAppDataList *list;
    GtkTreeModel *model;
    GtkWidget *widget, *window;
    GObject *object;
    gint id;

    list = &proxy->lists[GWY_PAGE_GRAPHS];
    model = GTK_TREE_MODEL(list->store);

    gtk_tree_model_get(model, iter,
                       MODEL_WIDGET, &widget,
                       MODEL_OBJECT, &object,
                       MODEL_ID, &id,
                       -1);
    if (visible == (widget != NULL)) {
        g_object_unref(object);
        GWY_OBJECT_UNREF(widget);
        return FALSE;
    }

    if (visible) {
        widget = gwy_app_data_browser_create_graph(proxy->parent, proxy, id);
        gtk_list_store_set(list->store, iter, MODEL_WIDGET, widget, -1);
        list->visible_count++;
    }
    else {
        gwy_app_data_proxy_update_visibility(object, FALSE);
        window = gtk_widget_get_toplevel(widget);
        gtk_widget_destroy(window);
        gtk_list_store_set(list->store, iter, MODEL_WIDGET, NULL, -1);
        g_object_unref(widget);
        list->visible_count--;
        _gwy_app_update_graph_sens();
    }
    g_object_unref(object);

    gwy_debug("visible_count: %d", list->visible_count);

    return TRUE;
}

static void
gwy_app_data_browser_graph_toggled(GtkCellRendererToggle *renderer,
                                   gchar *path_str,
                                   GwyAppDataBrowser *browser)
{
    GwyAppDataProxy *proxy;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeModel *model;
    gboolean active, toggled;

    gwy_debug("Toggled graph row %s", path_str);
    proxy = browser->current;
    g_return_if_fail(proxy);

    path = gtk_tree_path_new_from_string(path_str);
    model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_GRAPHS].store);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

    active = gtk_cell_renderer_toggle_get_active(renderer);
    proxy->resetting_visibility = TRUE;
    toggled = gwy_app_data_proxy_graph_set_visible(proxy, &iter, !active);
    proxy->resetting_visibility = FALSE;
    g_assert(toggled);

    gwy_app_data_proxy_maybe_finalize(proxy);
}

static void
gwy_app_data_proxy_graph_name_edited(GwyAppDataProxy *proxy,
                                     GtkTreeIter *iter,
                                     gchar *title)
{
    GtkTreeModel *model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_GRAPHS].store);
    GwyGraphModel *gmodel;
    gint id;

    gtk_tree_model_get(model, iter, MODEL_ID, &id, MODEL_OBJECT, &gmodel, -1);
    if (!*title) {
        g_free(title);
        title = g_strdup_printf("%s %d", _("Untitled"), id);
    }
    g_object_set(gmodel, "title", title, NULL);
    g_free(title);
    g_object_unref(gmodel);
}

static GtkWidget*
gwy_app_data_browser_construct_graphs(GwyAppDataBrowser *browser)
{
    GtkTreeView *treeview;
    GtkWidget *retval;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    /* Construct the GtkTreeView that will display graphs */
    retval = gtk_tree_view_new();
    treeview = GTK_TREE_VIEW(retval);
    set_up_data_list_signals(treeview, browser);

    /* Add the thumbnail column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes("Thumbnail", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_graph, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the visibility column */
    renderer = gtk_cell_renderer_toggle_new();
    g_object_set(renderer, "activatable", TRUE, NULL);
    g_signal_connect(renderer, "toggled", G_CALLBACK(gwy_app_data_browser_graph_toggled), browser);
    column = gtk_tree_view_column_new_with_attributes("Visible", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_visible, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the title column */
    gwy_app_data_list_make_title_column(browser, &column, &renderer);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_graph_render_title, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the flags column */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "width-chars", 4, NULL);
    column = gtk_tree_view_column_new_with_attributes("Curves", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_graph_render_flags, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    gtk_tree_view_set_headers_visible(treeview, FALSE);

    /* Selection */
    selection = gtk_tree_view_get_selection(treeview);
    g_object_set_qdata(G_OBJECT(selection), page_id_quark, GINT_TO_POINTER(GWY_PAGE_GRAPHS + PAGENO_SHIFT));
    g_signal_connect(selection, "changed", G_CALLBACK(gwy_app_data_browser_selection_changed), browser);

    /* DnD */
    gtk_tree_view_enable_model_drag_source(treeview, GDK_BUTTON1_MASK,
                                           dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);

    return retval;
}

/**************************************************************************
 *
 * Spectra treeview
 *
 **************************************************************************/

static void
gwy_app_data_browser_spectra_toggled(G_GNUC_UNUSED GtkCellRendererToggle *renderer,
                                     gchar *path_str,
                                     GwyAppDataBrowser *browser)
{
    GwyAppDataProxy *proxy;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeModel *model;

    gwy_debug("Toggled spectra row %s", path_str);
    proxy = browser->current;
    g_return_if_fail(proxy);

    path = gtk_tree_path_new_from_string(path_str);
    model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_SPECTRA].store);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

    g_warning("Cannot make spectra visible and this column should not be "
              "visible anyway.");
}

static void
gwy_app_data_proxy_spectra_name_edited(GwyAppDataProxy *proxy,
                                       GtkTreeIter *iter,
                                       gchar *title)
{
    GtkTreeModel *model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_SPECTRA].store);
    GwySpectra *spectra;
    gint id;

    gtk_tree_model_get(model, iter, MODEL_ID, &id, MODEL_OBJECT, &spectra, -1);
    if (!*title) {
        g_free(title);
        title = g_strdup_printf("%s %d", _("Untitled"), id);
    }
    g_object_set(spectra, "title", title, NULL);
    g_free(title);
    g_object_unref(spectra);
}

/* XXX: Performs some common tasks as `select_spectra' */
static void
gwy_app_data_browser_spectra_selected(GtkTreeSelection *selection,
                                      GwyAppDataBrowser *browser)
{
    GwySpectra *tspectra, *aspectra;
    GwyContainer *data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    const gchar *strkey;
    GwyAppKeyType type;
    GQuark quark;
    gint i, id;

    gwy_app_data_browser_get_current(GWY_APP_SPECTRA, &aspectra,
                                     GWY_APP_SPECTRA_ID, &id,
                                     GWY_APP_DATA_FIELD_ID, &i,
                                     0);
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, MODEL_OBJECT, &tspectra, -1);
        g_object_unref(tspectra);
    }
    else
        tspectra = NULL;

    gwy_debug("tspectra: %p, aspectra: %p", tspectra, aspectra);
    if (aspectra == tspectra) {
        /* Ensure the selection is remembered. A spectra item is selected by default even if the user has not
         * specifically selected anything, therefore we can get here even if sps-id is not set in the container.
         * Since GwyContainer is intelligent and does not emit "item-changed" when the value does not actually change,
         * we won't recurse to death here. */
        if (aspectra) {
            gchar key[40];

            data = g_object_get_qdata(G_OBJECT(aspectra), container_quark);
            g_return_if_fail(data == browser->current->container);
            g_snprintf(key, sizeof(key), "/%d/data/sps-id", i);
            gwy_container_set_int32_by_name(data, key, id);
        }
        return;
    }

    if (tspectra) {
        data = g_object_get_qdata(G_OBJECT(tspectra), container_quark);
        g_return_if_fail(data == browser->current->container);
        quark = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(tspectra), own_key_quark));
        strkey = g_quark_to_string(quark);
        id = _gwy_app_analyse_data_key(strkey, &type, NULL);
        g_return_if_fail(i >= 0 && type == KEY_IS_SPECTRA);
        browser->current->lists[GWY_PAGE_SPECTRA].active = id;
    }
    else {
        id = -1;
        data = NULL;  /* and gcc is stupid */
    }

    /* XXX: Do not delete the reference when i == -1 because this can happen
     * on descruction.  Must prevent it or handle it differently. */
    if (id > -1 && i > -1) {
        gchar key[40];

        g_snprintf(key, sizeof(key), "/%d/data/sps-id", i);
        gwy_container_set_int32_by_name(data, key, id);
    }

    _gwy_app_spectra_set_current(tspectra);
}

static void
gwy_app_data_browser_spectra_render_title(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                          GtkCellRenderer *renderer,
                                          GtkTreeModel *model,
                                          GtkTreeIter *iter,
                                          G_GNUC_UNUSED gpointer userdata)
{
    GObject *spectra;
    gchar *title;

    gtk_tree_model_get(model, iter, MODEL_OBJECT, &spectra, -1);
    g_object_get(spectra, "title", &title, NULL);
    g_object_set(renderer, "text", title, NULL);
    g_free(title);
    g_object_unref(spectra);
}

static void
gwy_app_data_browser_spectra_render_npoints(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                            GtkCellRenderer *renderer,
                                            GtkTreeModel *model,
                                            GtkTreeIter *iter,
                                            G_GNUC_UNUSED gpointer userdata)
{
    GwySpectra *spectra;
    gchar s[8];

    gtk_tree_model_get(model, iter, MODEL_OBJECT, &spectra, -1);
    g_snprintf(s, sizeof(s), "%d", gwy_spectra_get_n_spectra(spectra));
    g_object_set(renderer, "text", s, NULL);
    g_object_unref(spectra);
}

static GtkWidget*
gwy_app_data_browser_construct_spectra(GwyAppDataBrowser *browser)
{
    GtkWidget *retval;
    GtkTreeView *treeview;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    /* Construct the GtkTreeView that will display data channels */
    retval = gtk_tree_view_new();
    treeview = GTK_TREE_VIEW(retval);
    set_up_data_list_signals(treeview, browser);

    /* Add the thumbnail column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes("Thumbnail", renderer, NULL);
    gtk_tree_view_column_set_visible(column, FALSE);
    gtk_tree_view_append_column(treeview, column);

    /* Add the visibility column */
    renderer = gtk_cell_renderer_toggle_new();
    g_object_set(renderer, "activatable", TRUE, NULL);
    g_signal_connect(renderer, "toggled", G_CALLBACK(gwy_app_data_browser_spectra_toggled), browser);
    column = gtk_tree_view_column_new_with_attributes("Visible", renderer, NULL);
    gtk_tree_view_column_set_visible(column, FALSE);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_visible, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the title column */
    gwy_app_data_list_make_title_column(browser, &column, &renderer);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_spectra_render_title, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the flags column */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "width-chars", 7, NULL);
    column = gtk_tree_view_column_new_with_attributes("Points", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            gwy_app_data_browser_spectra_render_npoints, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    gtk_tree_view_set_headers_visible(treeview, FALSE);

    /* Selection */
    selection = gtk_tree_view_get_selection(treeview);
    g_object_set_qdata(G_OBJECT(selection), page_id_quark,
                       GINT_TO_POINTER(GWY_PAGE_SPECTRA + PAGENO_SHIFT));
    g_signal_connect(selection, "changed", G_CALLBACK(gwy_app_data_browser_selection_changed), browser);
    /* XXX: For spectra changing selection in the list actually changes the
     * current spectra. */
    g_signal_connect(selection, "changed", G_CALLBACK(gwy_app_data_browser_spectra_selected), browser);

    /* DnD */
    gtk_tree_view_enable_model_drag_source(treeview, GDK_BUTTON1_MASK,
                                           dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);

    return retval;
}

/**************************************************************************
 *
 * Brick treeview
 *
 **************************************************************************/

static void
gwy_app_data_browser_brick_toggled(G_GNUC_UNUSED GtkCellRendererToggle *renderer,
                                   gchar *path_str,
                                   GwyAppDataBrowser *browser)
{
    GwyAppDataProxy *proxy;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeModel *model;
    gboolean active, toggled;

    gwy_debug("Toggled brick row %s", path_str);
    proxy = browser->current;
    g_return_if_fail(proxy);

    path = gtk_tree_path_new_from_string(path_str);
    model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_VOLUMES].store);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

    active = gtk_cell_renderer_toggle_get_active(renderer);
    proxy->resetting_visibility = TRUE;
    toggled = gwy_app_data_proxy_brick_set_visible(proxy, &iter, !active);
    proxy->resetting_visibility = FALSE;
    g_assert(toggled);

    gwy_app_data_proxy_maybe_finalize(proxy);
}

static void
gwy_app_data_proxy_brick_name_edited(GwyAppDataProxy *proxy,
                                     GtkTreeIter *iter,
                                     gchar *title)
{
    GtkTreeModel *model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_VOLUMES].store);
    gint id;

    gtk_tree_model_get(model, iter, MODEL_ID, &id, -1);
    if (!*title) {
        g_free(title);
        gwy_app_set_brick_title(proxy->container, id, NULL);
    }
    else
        gwy_container_set_string(proxy->container, gwy_app_get_brick_title_key_for_id(id), title);
}

static void
gwy_app_data_browser_brick_render_title(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                        GtkCellRenderer *renderer,
                                        GtkTreeModel *model,
                                        GtkTreeIter *iter,
                                        gpointer userdata)
{
    GwyAppDataBrowser *browser = (GwyAppDataBrowser*)userdata;
    const guchar *title = _("Untitled");
    GwyContainer *data;
    gint id;

    /* XXX: browser->current must match what is visible in the browser */
    data = browser->current->container;
    gtk_tree_model_get(model, iter, MODEL_ID, &id, -1);
    gwy_container_gis_string(data, gwy_app_get_brick_title_key_for_id(id), &title);
    g_object_set(renderer, "text", title, NULL);
}

static void
gwy_app_data_browser_brick_render_nlevels(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                          GtkCellRenderer *renderer,
                                          GtkTreeModel *model,
                                          GtkTreeIter *iter,
                                          G_GNUC_UNUSED gpointer userdata)
{
    GwyBrick *brick;
    gchar buf[20];

    gtk_tree_model_get(model, iter, MODEL_OBJECT, &brick, -1);
    g_snprintf(buf, sizeof(buf), "%d %s",
               gwy_brick_get_zres(brick),
               gwy_brick_get_zcalibration(brick) ? "Z" : "");
    g_object_set(renderer, "text", buf, NULL);
    g_object_unref(brick);
}

static void
gwy_app_data_browser_render_brick(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                  GtkCellRenderer *renderer,
                                  GtkTreeModel *model,
                                  GtkTreeIter *iter,
                                  G_GNUC_UNUSED gpointer userdata)
{
    GwyContainer *container;
    GObject *object;
    GdkPixbuf *pixbuf;
    gdouble timestamp, *pbuf_timestamp = NULL;
    gint id;

    gtk_tree_model_get(model, iter,
                       MODEL_ID, &id,
                       MODEL_OBJECT, &object,
                       MODEL_TIMESTAMP, &timestamp,
                       MODEL_THUMBNAIL, &pixbuf,
                       -1);

    container = g_object_get_qdata(object, container_quark);
    g_object_unref(object);

    if (pixbuf) {
        pbuf_timestamp = (gdouble*)g_object_get_data(G_OBJECT(pixbuf), "timestamp");
        g_object_unref(pixbuf);
        if (*pbuf_timestamp >= timestamp) {
            g_object_set(renderer, "pixbuf", pixbuf, NULL);
            return;
        }
    }

    pixbuf = gwy_app_get_volume_thumbnail(container, id, THUMB_SIZE, THUMB_SIZE);
    pbuf_timestamp = g_new(gdouble, 1);
    *pbuf_timestamp = gwy_get_timestamp();
    g_object_set_data_full(G_OBJECT(pixbuf), "timestamp", pbuf_timestamp, g_free);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, MODEL_THUMBNAIL, pixbuf, -1);
    g_object_set(renderer, "pixbuf", pixbuf, NULL);
    g_object_unref(pixbuf);

    update_window_icon(model, iter);
}

/**
 * gwy_app_data_browser_volume_deleted:
 * @data_window: A data window that was deleted.
 *
 * Destroys a deleted data window, updating proxy.
 *
 * This functions makes sure various updates happen in reasonable order,
 * simple gtk_widget_destroy() on the data window would not do that.
 *
 * Returns: Always %TRUE to be usable as terminal event handler.
 **/
static gboolean
gwy_app_data_browser_volume_deleted(GwyDataWindow *data_window)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GwyAppKeyType type;
    GwyContainer *data;
    GwyDataView *data_view;
    GwyPixmapLayer *layer;
    GtkTreeIter iter;
    const gchar *strkey;
    GObject *object;
    GQuark quark;
    gint i;

    gwy_debug("Data window %p deleted", data_window);
    data_view = gwy_data_window_get_data_view(data_window);
    data = gwy_data_view_get_data(data_view);
    layer = gwy_data_view_get_base_layer(data_view);
    strkey = gwy_pixmap_layer_get_data_key(layer);
    quark = g_quark_from_string(strkey);
    g_return_val_if_fail(data && quark, TRUE);

    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_val_if_fail(i >= 0 && type == KEY_IS_BRICK_PREVIEW, TRUE);
    quark = gwy_app_get_brick_key_for_id(i);
    object = gwy_container_get_object(data, quark);

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    list = &proxy->lists[GWY_PAGE_VOLUMES];
    if (!gwy_app_data_proxy_find_object(list->store, i, &iter)) {
        g_critical("Cannot find brick %p (%d)", object, i);
        return TRUE;
    }

    proxy->resetting_visibility = TRUE;
    gwy_app_data_proxy_brick_set_visible(proxy, &iter, FALSE);
    proxy->resetting_visibility = FALSE;
    gwy_app_data_proxy_maybe_finalize(proxy);

    return TRUE;
}

/**
 * gwy_app_data_browser_create_volume:
 * @browser: A data browser.
 * @id: The channel id.
 *
 * Creates a data window for a data brick when its visibility is switched on.
 *
 * This is actually `make visible', should not be used outside gwy_app_data_proxy_brick_set_visible().
 *
 * Returns: The data view (NOT data window).
 **/
static GtkWidget*
gwy_app_data_browser_create_volume(GwyAppDataBrowser *browser,
                                   GwyAppDataProxy *proxy,
                                   gint id)
{
    GtkWidget *data_view, *data_window;
    GObject *brick = NULL;
    GwyDataField *preview = NULL;
    GwyPixmapLayer *layer;
    GwyLayerBasic *layer_basic;

    gwy_container_gis_object(proxy->container, gwy_app_get_brick_key_for_id(id), &brick);
    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    gwy_container_gis_object(proxy->container, gwy_app_get_brick_preview_key_for_id(id), &preview);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(preview), NULL);

    layer = gwy_layer_basic_new();
    layer_basic = GWY_LAYER_BASIC(layer);
    gwy_pixmap_layer_set_data_key(layer, g_quark_to_string(gwy_app_get_brick_preview_key_for_id(id)));
    gwy_layer_basic_set_gradient_key(layer_basic, g_quark_to_string(gwy_app_get_brick_palette_key_for_id(id)));

    data_view = gwy_data_view_new(proxy->container);
    gwy_data_view_set_data_prefix(GWY_DATA_VIEW(data_view), gwy_pixmap_layer_get_data_key(layer));
    gwy_data_view_set_base_layer(GWY_DATA_VIEW(data_view), layer);

    data_window = gwy_data_window_new(GWY_DATA_VIEW(data_view));
    g_object_set_data(G_OBJECT(data_window), "gwy-app-page", GUINT_TO_POINTER(GWY_PAGE_VOLUMES));
    gwy_app_update_brick_window_title(GWY_DATA_VIEW(data_view), id);

    gwy_app_data_proxy_update_visibility(brick, TRUE);
    g_signal_connect_swapped(data_window, "focus-in-event", G_CALLBACK(gwy_app_data_browser_select_volume2), data_view);
    g_signal_connect(data_window, "delete-event", G_CALLBACK(gwy_app_data_browser_volume_deleted), NULL);

    _gwy_app_brick_window_setup(GWY_DATA_WINDOW(data_window));

    gtk_drag_dest_set(data_window, GTK_DEST_DEFAULT_ALL,
                      dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);
    g_signal_connect(data_window, "drag-data-received", G_CALLBACK(gwy_app_window_dnd_data_received), browser);

    /* FIXME: A silly place for this? */
    gwy_app_data_browser_set_file_present(browser, TRUE);
    gtk_widget_show_all(data_window);
    _gwy_app_update_brick_info(proxy->container, id, GWY_DATA_VIEW(data_view));
    _gwy_app_update_brick_sens();

    return data_view;
}

static gboolean
gwy_app_data_proxy_brick_set_visible(GwyAppDataProxy *proxy,
                                     GtkTreeIter *iter,
                                     gboolean visible)
{
    GwyAppDataList *list;
    GtkTreeModel *model;
    GtkWidget *widget, *window;
    GObject *object;
    gint id;

    list = &proxy->lists[GWY_PAGE_VOLUMES];
    model = GTK_TREE_MODEL(list->store);

    gtk_tree_model_get(model, iter,
                       MODEL_WIDGET, &widget,
                       MODEL_OBJECT, &object,
                       MODEL_ID, &id,
                       -1);
    if (visible == (widget != NULL)) {
        g_object_unref(object);
        GWY_OBJECT_UNREF(widget);
        return FALSE;
    }

    if (visible) {
        widget = gwy_app_data_browser_create_volume(proxy->parent, proxy, id);
        gtk_list_store_set(list->store, iter, MODEL_WIDGET, widget, -1);
        update_window_icon(model, iter);
        list->visible_count++;
    }
    else {
        gwy_app_data_proxy_update_visibility(object, FALSE);
        window = gtk_widget_get_ancestor(widget, GWY_TYPE_DATA_WINDOW);
        gtk_widget_destroy(window);
        gtk_list_store_set(list->store, iter, MODEL_WIDGET, NULL, -1);
        g_object_unref(widget);
        list->visible_count--;
        _gwy_app_update_brick_sens();
    }
    g_object_unref(object);

    gwy_debug("visible_count: %d", list->visible_count);

    return TRUE;
}

static GtkWidget*
gwy_app_data_browser_construct_bricks(GwyAppDataBrowser *browser)
{
    GtkWidget *retval;
    GtkTreeView *treeview;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    /* Construct the GtkTreeView that will display data channels */
    retval = gtk_tree_view_new();
    treeview = GTK_TREE_VIEW(retval);
    set_up_data_list_signals(treeview, browser);

    /* Add the thumbnail column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes("Thumbnail", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_brick, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the visibility column */
    renderer = gtk_cell_renderer_toggle_new();
    g_object_set(renderer, "activatable", TRUE, NULL);
    g_signal_connect(renderer, "toggled", G_CALLBACK(gwy_app_data_browser_brick_toggled), browser);
    column = gtk_tree_view_column_new_with_attributes("Visible", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_visible, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the title column */
    gwy_app_data_list_make_title_column(browser, &column, &renderer);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_brick_render_title, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "width-chars", 7, NULL);
    column = gtk_tree_view_column_new_with_attributes("Levels", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_brick_render_nlevels, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    gtk_tree_view_set_headers_visible(treeview, FALSE);

    /* Selection */
    selection = gtk_tree_view_get_selection(treeview);
    g_object_set_qdata(G_OBJECT(selection), page_id_quark, GINT_TO_POINTER(GWY_PAGE_VOLUMES + PAGENO_SHIFT));
    g_signal_connect(selection, "changed", G_CALLBACK(gwy_app_data_browser_selection_changed), browser);

    /* DnD */
    gtk_tree_view_enable_model_drag_source(treeview, GDK_BUTTON1_MASK,
                                           dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);

    return retval;
}

static void
gwy_app_update_brick_window_title(GwyDataView *data_view,
                                  gint id)
{
    GtkWidget *data_window;
    GwyContainer *data;
    const guchar *filename;
    gchar *title, *bname, *btitle;

    data_window = gtk_widget_get_ancestor(GTK_WIDGET(data_view), GWY_TYPE_DATA_WINDOW);
    if (!data_window) {
        g_warning("GwyDataView has no GwyDataWindow ancestor");
        return;
    }

    data = gwy_data_view_get_data(data_view);
    btitle = gwy_app_get_brick_title(data, id);
    if (gwy_container_gis_string(data, filename_quark, &filename)) {
        bname = g_path_get_basename(filename);
        title = g_strdup_printf("%s [%s]", bname, btitle);
        g_free(bname);
    }
    else {
        GwyAppDataBrowser *browser;
        GwyAppDataProxy *proxy;

        browser = gwy_app_get_data_browser();
        proxy = gwy_app_data_browser_get_proxy(browser, data);
        title = g_strdup_printf("%s %d [%s]", _("Untitled"), proxy->untitled_no, btitle);
    }
    gwy_data_window_set_data_name(GWY_DATA_WINDOW(data_window), title);
    g_free(btitle);
    g_free(title);
}

static void
ensure_brick_previews(GwyAppDataProxy *proxy)
{
    GwyAppDataList *list = proxy->lists + GWY_PAGE_VOLUMES;
    GtkTreeModel *model = GTK_TREE_MODEL(list->store);
    GtkTreeIter iter;

    if (!gtk_tree_model_get_iter_first(model, &iter))
        return;

    do {
        GwyBrick *brick;
        GwyDataField *preview;
        GQuark quark;
        gint id;

        gtk_tree_model_get(model, &iter, MODEL_ID, &id, MODEL_OBJECT, &brick, -1);
        quark = gwy_app_get_brick_preview_key_for_id(id);
        if (!gwy_container_gis_object(proxy->container, quark, (GObject**)&preview) || !GWY_IS_DATA_FIELD(preview)) {
            preview = _gwy_app_create_brick_preview_field(brick);
            gwy_container_set_object(proxy->container, quark, preview);
            g_object_unref(preview);
        }
        g_object_unref(brick);
    } while (gtk_tree_model_iter_next(model, &iter));
}

static void
ensure_lawn_previews(GwyAppDataProxy *proxy)
{
    GwyAppDataList *list = proxy->lists + GWY_PAGE_CURVE_MAPS;
    GtkTreeModel *model = GTK_TREE_MODEL(list->store);
    GtkTreeIter iter;

    if (!gtk_tree_model_get_iter_first(model, &iter))
        return;

    do {
        GwyLawn *lawn;
        GwyDataField *preview;
        GQuark quark;
        gint id;

        gtk_tree_model_get(model, &iter, MODEL_ID, &id, MODEL_OBJECT, &lawn, -1);
        quark = gwy_app_get_lawn_preview_key_for_id(id);
        if (!gwy_container_gis_object(proxy->container, quark, (GObject**)&preview) || !GWY_IS_DATA_FIELD(preview)) {
            preview = _gwy_app_create_lawn_preview_field(lawn);
            gwy_container_set_object(proxy->container, quark, preview);
            g_object_unref(preview);
        }
        g_object_unref(lawn);
    } while (gtk_tree_model_iter_next(model, &iter));
}

/**************************************************************************
 *
 * XYZ treeview
 *
 **************************************************************************/

static void
gwy_app_data_browser_surface_toggled(G_GNUC_UNUSED GtkCellRendererToggle *renderer,
                                     gchar *path_str,
                                     GwyAppDataBrowser *browser)
{
    GwyAppDataProxy *proxy;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeModel *model;
    gboolean active, toggled;

    gwy_debug("Toggled surface row %s", path_str);
    proxy = browser->current;
    g_return_if_fail(proxy);

    path = gtk_tree_path_new_from_string(path_str);
    model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_XYZS].store);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

    active = gtk_cell_renderer_toggle_get_active(renderer);
    proxy->resetting_visibility = TRUE;
    toggled = gwy_app_data_proxy_surface_set_visible(proxy, &iter, !active);
    proxy->resetting_visibility = FALSE;
    g_assert(toggled);

    gwy_app_data_proxy_maybe_finalize(proxy);
}

static void
gwy_app_data_proxy_surface_name_edited(GwyAppDataProxy *proxy,
                                       GtkTreeIter *iter,
                                       gchar *title)
{
    GtkTreeModel *model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_XYZS].store);
    gint id;

    gtk_tree_model_get(model, iter, MODEL_ID, &id, -1);
    if (!*title) {
        g_free(title);
        gwy_app_set_surface_title(proxy->container, id, NULL);
    }
    else
        gwy_container_set_string(proxy->container, gwy_app_get_surface_title_key_for_id(id), title);
}

static void
gwy_app_data_browser_surface_render_title(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                          GtkCellRenderer *renderer,
                                          GtkTreeModel *model,
                                          GtkTreeIter *iter,
                                          gpointer userdata)
{
    GwyAppDataBrowser *browser = (GwyAppDataBrowser*)userdata;
    const guchar *title = _("Untitled");
    GwyContainer *data;
    gint id;

    /* XXX: browser->current must match what is visible in the browser */
    data = browser->current->container;
    gtk_tree_model_get(model, iter, MODEL_ID, &id, -1);
    gwy_container_gis_string(data, gwy_app_get_surface_title_key_for_id(id), &title);
    g_object_set(renderer, "text", title, NULL);
}

static void
gwy_app_data_browser_surface_render_npoints(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                            GtkCellRenderer *renderer,
                                            GtkTreeModel *model,
                                            GtkTreeIter *iter,
                                            G_GNUC_UNUSED gpointer userdata)
{
    GwySurface *surface;
    gchar buf[16];

    gtk_tree_model_get(model, iter, MODEL_OBJECT, &surface, -1);
    g_snprintf(buf, sizeof(buf), "%d", surface->n);
    g_object_set(renderer, "text", buf, NULL);
    g_object_unref(surface);
}

static void
replace_surface_preview(GwyContainer *container,
                        GtkTreeModel *model, GtkTreeIter *iter)
{
    GwyPreviewSurfaceFlags flags = GWY_PREVIEW_SURFACE_FILL;
    GtkWidget *widget;
    GwySurface *surface;
    GwyDataField *raster;
    GQuark quark;
    gint id;

    g_return_if_fail(GTK_IS_LIST_STORE(model));
    gtk_tree_model_get(model, iter,
                       MODEL_WIDGET, &widget,
                       MODEL_ID, &id,
                       MODEL_OBJECT, &surface,
                       -1);

    g_return_if_fail(GWY_IS_SURFACE(surface));
    if (!widget) {
        GWY_OBJECT_UNREF(surface);
        return;
    }

    g_return_if_fail(GWY_IS_DATA_VIEW(widget));
    if (g_object_get_data(G_OBJECT(widget), "gwy-app-surface-density-map"))
        flags |= GWY_PREVIEW_SURFACE_DENSITY;

    quark = gwy_app_get_surface_preview_key_for_id(id);
    raster = gwy_container_get_object(container, quark);
    g_return_if_fail(GWY_IS_DATA_FIELD(raster));
    gwy_preview_surface_to_datafield(surface, raster, widget->allocation.width, widget->allocation.height, flags);
    gwy_data_view_set_zoom(GWY_DATA_VIEW(widget), 1.0);
    gwy_data_field_data_changed(raster);
    g_object_unref(surface);
    g_object_unref(widget);
}

static void
gwy_app_data_browser_render_surface(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                    GtkCellRenderer *renderer,
                                    GtkTreeModel *model,
                                    GtkTreeIter *iter,
                                    G_GNUC_UNUSED gpointer userdata)
{
    GwyContainer *container;
    GObject *object;
    GdkPixbuf *pixbuf;
    gdouble timestamp, *pbuf_timestamp = NULL;
    gboolean do_update;
    gint id;

    gtk_tree_model_get(model, iter,
                       MODEL_ID, &id,
                       MODEL_OBJECT, &object,
                       MODEL_TIMESTAMP, &timestamp,
                       MODEL_THUMBNAIL, &pixbuf,
                       -1);

    container = g_object_get_qdata(object, container_quark);
    g_object_unref(object);

    if (pixbuf) {
        pbuf_timestamp = (gdouble*)g_object_get_data(G_OBJECT(pixbuf), "timestamp");
        g_object_unref(pixbuf);
        if (*pbuf_timestamp >= timestamp) {
            g_object_set(renderer, "pixbuf", pixbuf, NULL);
            return;
        }
    }

    /* XXX: We need to recalculate the raster preview itself somewhere upon getting "data-changed" for the surface.
     * This is not a very nice place to do that but it is a mechanism that is already in place and handles queuing and
     * consolidation of multiple updates.  Also note that we need to do this before setting the timestamp to avoid an
     * infinite loop. */
    do_update = !!g_object_get_qdata(object, surface_update_quark);
    if (do_update) {
        g_object_set_qdata(object, surface_update_quark, NULL);
        replace_surface_preview(container, model, iter);
    }

    pixbuf = gwy_app_get_xyz_thumbnail(container, id, THUMB_SIZE, THUMB_SIZE);
    pbuf_timestamp = g_new(gdouble, 1);
    *pbuf_timestamp = gwy_get_timestamp();
    g_object_set_data_full(G_OBJECT(pixbuf), "timestamp", pbuf_timestamp, g_free);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, MODEL_THUMBNAIL, pixbuf, -1);
    g_object_set(renderer, "pixbuf", pixbuf, NULL);
    g_object_unref(pixbuf);

    update_window_icon(model, iter);
}

/**
 * gwy_app_data_browser_xyz_deleted:
 * @data_window: A data window that was deleted.
 *
 * Destroys a deleted data window, updating proxy.
 *
 * This functions makes sure various updates happen in reasonable order, simple gtk_widget_destroy() on the data
 * window would not do that.
 *
 * Returns: Always %TRUE to be usable as terminal event handler.
 **/
static gboolean
gwy_app_data_browser_xyz_deleted(GwyDataWindow *data_window)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GwyAppKeyType type;
    GwyContainer *data;
    GwyDataView *data_view;
    GwyPixmapLayer *layer;
    GtkTreeIter iter;
    const gchar *strkey;
    GObject *object;
    GQuark quark;
    gint i;

    gwy_debug("Data window %p deleted", data_window);
    data_view = gwy_data_window_get_data_view(data_window);
    data = gwy_data_view_get_data(data_view);
    layer = gwy_data_view_get_base_layer(data_view);
    strkey = gwy_pixmap_layer_get_data_key(layer);
    quark = g_quark_from_string(strkey);
    g_return_val_if_fail(data && quark, TRUE);

    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_val_if_fail(i >= 0 && type == KEY_IS_SURFACE_PREVIEW, TRUE);
    quark = gwy_app_get_surface_key_for_id(i);
    object = gwy_container_get_object(data, quark);

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    list = &proxy->lists[GWY_PAGE_XYZS];
    if (!gwy_app_data_proxy_find_object(list->store, i, &iter)) {
        g_critical("Cannot find surface %p (%d)", object, i);
        return TRUE;
    }

    proxy->resetting_visibility = TRUE;
    gwy_app_data_proxy_surface_set_visible(proxy, &iter, FALSE);
    proxy->resetting_visibility = FALSE;
    gwy_app_data_proxy_maybe_finalize(proxy);

    return TRUE;
}

/**
 * gwy_app_data_browser_create_xyz:
 * @browser: A data browser.
 * @id: The channel id.
 *
 * Creates a data window for a data surface when its visibility is switched on.
 *
 * This is actually `make visible', should not be used outside
 * gwy_app_data_proxy_surface_set_visible().
 *
 * Returns: The data view (NOT data window).
 **/
static GtkWidget*
gwy_app_data_browser_create_xyz(GwyAppDataBrowser *browser,
                                GwyAppDataProxy *proxy,
                                gint id)
{
    GtkWidget *data_view, *data_window;
    GObject *surface = NULL;
    GwyDataField *raster = NULL;
    GwyPixmapLayer *layer;
    GwyLayerBasic *layer_basic;

    gwy_container_gis_object(proxy->container, gwy_app_get_surface_key_for_id(id), &surface);
    g_return_val_if_fail(GWY_IS_SURFACE(surface), NULL);

    if (!gwy_container_gis_object(proxy->container, gwy_app_get_surface_preview_key_for_id(id), &raster)
        || !GWY_IS_DATA_FIELD(raster)) {
        raster = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
        gwy_preview_surface_to_datafield(GWY_SURFACE(surface), raster, SURFACE_PREVIEW_SIZE, SURFACE_PREVIEW_SIZE, 0);
        gwy_container_set_object(proxy->container, gwy_app_get_surface_preview_key_for_id(id), raster);
        g_object_unref(raster);
    }

    layer = gwy_layer_basic_new();
    layer_basic = GWY_LAYER_BASIC(layer);
    gwy_pixmap_layer_set_data_key(layer, g_quark_to_string(gwy_app_get_surface_preview_key_for_id(id)));
    gwy_layer_basic_set_gradient_key(layer_basic, g_quark_to_string(gwy_app_get_surface_palette_key_for_id(id)));

    data_view = gwy_data_view_new(proxy->container);
    gwy_data_view_set_data_prefix(GWY_DATA_VIEW(data_view), gwy_pixmap_layer_get_data_key(layer));
    gwy_data_view_set_base_layer(GWY_DATA_VIEW(data_view), layer);

    data_window = gwy_data_window_new(GWY_DATA_VIEW(data_view));
    g_object_set_data(G_OBJECT(data_window), "gwy-app-page", GUINT_TO_POINTER(GWY_PAGE_XYZS));
    gwy_app_update_surface_window_title(GWY_DATA_VIEW(data_view), id);

    gwy_app_data_proxy_update_visibility(surface, TRUE);
    g_signal_connect_swapped(data_window, "focus-in-event", G_CALLBACK(gwy_app_data_browser_select_xyz2), data_view);
    g_signal_connect(data_window, "delete-event", G_CALLBACK(gwy_app_data_browser_xyz_deleted), NULL);

    _gwy_app_surface_window_setup(GWY_DATA_WINDOW(data_window));

    gtk_drag_dest_set(data_window, GTK_DEST_DEFAULT_ALL,
                      dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);
    g_signal_connect(data_window, "drag-data-received", G_CALLBACK(gwy_app_window_dnd_data_received), browser);

    /* FIXME: A silly place for this? */
    gwy_app_data_browser_set_file_present(browser, TRUE);
    gtk_widget_show_all(data_window);
    _gwy_app_update_surface_info(proxy->container, id, GWY_DATA_VIEW(data_view));
    _gwy_app_update_surface_sens();

    return data_view;
}

static gboolean
gwy_app_data_proxy_surface_set_visible(GwyAppDataProxy *proxy,
                                       GtkTreeIter *iter,
                                       gboolean visible)
{
    GwyAppDataList *list;
    GtkTreeModel *model;
    GtkWidget *widget, *window;
    GObject *object;
    gint id;

    list = &proxy->lists[GWY_PAGE_XYZS];
    model = GTK_TREE_MODEL(list->store);

    gtk_tree_model_get(model, iter,
                       MODEL_WIDGET, &widget,
                       MODEL_OBJECT, &object,
                       MODEL_ID, &id,
                       -1);
    if (visible == (widget != NULL)) {
        g_object_unref(object);
        GWY_OBJECT_UNREF(widget);
        return FALSE;
    }

    if (visible) {
        widget = gwy_app_data_browser_create_xyz(proxy->parent, proxy, id);
        gtk_list_store_set(list->store, iter, MODEL_WIDGET, widget, -1);
        update_window_icon(model, iter);
        list->visible_count++;
    }
    else {
        gwy_app_data_proxy_update_visibility(object, FALSE);
        window = gtk_widget_get_ancestor(widget, GWY_TYPE_DATA_WINDOW);
        gtk_widget_destroy(window);
        gtk_list_store_set(list->store, iter, MODEL_WIDGET, NULL, -1);
        g_object_unref(widget);
        list->visible_count--;
        _gwy_app_update_surface_sens();
    }
    g_object_unref(object);

    gwy_debug("visible_count: %d", list->visible_count);

    return TRUE;
}

static GtkWidget*
gwy_app_data_browser_construct_surfaces(GwyAppDataBrowser *browser)
{
    GtkWidget *retval;
    GtkTreeView *treeview;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    /* Construct the GtkTreeView that will display data channels */
    retval = gtk_tree_view_new();
    treeview = GTK_TREE_VIEW(retval);
    set_up_data_list_signals(treeview, browser);

    /* Add the thumbnail column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes("Thumbnail", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_surface, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the visibility column */
    renderer = gtk_cell_renderer_toggle_new();
    g_object_set(renderer, "activatable", TRUE, NULL);
    g_signal_connect(renderer, "toggled", G_CALLBACK(gwy_app_data_browser_surface_toggled), browser);
    column = gtk_tree_view_column_new_with_attributes("Visible", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_visible, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the title column */
    gwy_app_data_list_make_title_column(browser, &column, &renderer);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_surface_render_title, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "width-chars", 7, NULL);
    column = gtk_tree_view_column_new_with_attributes("Points", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            gwy_app_data_browser_surface_render_npoints, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    gtk_tree_view_set_headers_visible(treeview, FALSE);

    /* Selection */
    selection = gtk_tree_view_get_selection(treeview);
    g_object_set_qdata(G_OBJECT(selection), page_id_quark, GINT_TO_POINTER(GWY_PAGE_XYZS + PAGENO_SHIFT));
    g_signal_connect(selection, "changed", G_CALLBACK(gwy_app_data_browser_selection_changed), browser);

    /* DnD */
    gtk_tree_view_enable_model_drag_source(treeview, GDK_BUTTON1_MASK,
                                           dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);

    return retval;
}

static void
gwy_app_update_surface_window_title(GwyDataView *data_view,
                                    gint id)
{
    GtkWidget *data_window;
    GwyContainer *data;
    const guchar *filename;
    gchar *title, *bname, *stitle;

    data_window = gtk_widget_get_ancestor(GTK_WIDGET(data_view), GWY_TYPE_DATA_WINDOW);
    if (!data_window) {
        g_warning("GwyDataView has no GwyDataWindow ancestor");
        return;
    }

    data = gwy_data_view_get_data(data_view);
    stitle = gwy_app_get_surface_title(data, id);
    if (gwy_container_gis_string(data, filename_quark, &filename)) {
        bname = g_path_get_basename(filename);
        title = g_strdup_printf("%s [%s]", bname, stitle);
        g_free(bname);
    }
    else {
        GwyAppDataBrowser *browser;
        GwyAppDataProxy *proxy;

        browser = gwy_app_get_data_browser();
        proxy = gwy_app_data_browser_get_proxy(browser, data);
        title = g_strdup_printf("%s %d [%s]", _("Untitled"), proxy->untitled_no, stitle);
    }
    gwy_data_window_set_data_name(GWY_DATA_WINDOW(data_window), title);
    g_free(stitle);
    g_free(title);
}

/**************************************************************************
 *
 * Curve map treeview
 *
 **************************************************************************/

static void
gwy_app_data_browser_lawn_toggled(G_GNUC_UNUSED GtkCellRendererToggle *renderer,
                                  gchar *path_str,
                                  GwyAppDataBrowser *browser)
{
    GwyAppDataProxy *proxy;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeModel *model;
    gboolean active, toggled;

    gwy_debug("Toggled lawn row %s", path_str);
    proxy = browser->current;
    g_return_if_fail(proxy);

    path = gtk_tree_path_new_from_string(path_str);
    model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_CURVE_MAPS].store);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

    active = gtk_cell_renderer_toggle_get_active(renderer);
    proxy->resetting_visibility = TRUE;
    toggled = gwy_app_data_proxy_lawn_set_visible(proxy, &iter, !active);
    proxy->resetting_visibility = FALSE;
    g_assert(toggled);

    gwy_app_data_proxy_maybe_finalize(proxy);
}

static void
gwy_app_data_proxy_lawn_name_edited(GwyAppDataProxy *proxy,
                                    GtkTreeIter *iter,
                                    gchar *title)
{
    GtkTreeModel *model = GTK_TREE_MODEL(proxy->lists[GWY_PAGE_CURVE_MAPS].store);
    gint id;

    gtk_tree_model_get(model, iter, MODEL_ID, &id, -1);
    if (!*title) {
        g_free(title);
        gwy_app_set_lawn_title(proxy->container, id, NULL);
    }
    else
        gwy_container_set_string(proxy->container, gwy_app_get_lawn_title_key_for_id(id), title);
}

static void
gwy_app_data_browser_lawn_render_title(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                       GtkCellRenderer *renderer,
                                       GtkTreeModel *model,
                                       GtkTreeIter *iter,
                                       gpointer userdata)
{
    GwyAppDataBrowser *browser = (GwyAppDataBrowser*)userdata;
    const guchar *title = _("Untitled");
    GwyContainer *data;
    gint id;

    /* XXX: browser->current must match what is visible in the browser */
    data = browser->current->container;
    gtk_tree_model_get(model, iter, MODEL_ID, &id, -1);
    gwy_container_gis_string(data, gwy_app_get_lawn_title_key_for_id(id), &title);
    g_object_set(renderer, "text", title, NULL);
}

static void
gwy_app_data_browser_lawn_render_ncurves(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                         GtkCellRenderer *renderer,
                                         GtkTreeModel *model,
                                         GtkTreeIter *iter,
                                         G_GNUC_UNUSED gpointer userdata)
{
    GwyLawn *lawn;
    gchar buf[24];
    gint nsegments, ncurves;

    gtk_tree_model_get(model, iter, MODEL_OBJECT, &lawn, -1);
    ncurves = gwy_lawn_get_n_curves(lawn);
    nsegments = gwy_lawn_get_n_segments(lawn);
    if (nsegments)
        g_snprintf(buf, sizeof(buf), "%d:%d", ncurves, nsegments);
    else
        g_snprintf(buf, sizeof(buf), "%d", ncurves);
    g_object_set(renderer, "text", buf, NULL);
    g_object_unref(lawn);
}

static void
gwy_app_data_browser_render_lawn(G_GNUC_UNUSED GtkTreeViewColumn *column,
                                 GtkCellRenderer *renderer,
                                 GtkTreeModel *model,
                                 GtkTreeIter *iter,
                                 G_GNUC_UNUSED gpointer userdata)
{
    GwyContainer *container;
    GObject *object;
    GdkPixbuf *pixbuf;
    gdouble timestamp, *pbuf_timestamp = NULL;
    gint id;

    gtk_tree_model_get(model, iter,
                       MODEL_ID, &id,
                       MODEL_OBJECT, &object,
                       MODEL_TIMESTAMP, &timestamp,
                       MODEL_THUMBNAIL, &pixbuf,
                       -1);

    container = g_object_get_qdata(object, container_quark);
    g_object_unref(object);

    if (pixbuf) {
        pbuf_timestamp = (gdouble*)g_object_get_data(G_OBJECT(pixbuf), "timestamp");
        g_object_unref(pixbuf);
        if (*pbuf_timestamp >= timestamp) {
            g_object_set(renderer, "pixbuf", pixbuf, NULL);
            return;
        }
    }

    pixbuf = gwy_app_get_curve_map_thumbnail(container, id, THUMB_SIZE, THUMB_SIZE);
    pbuf_timestamp = g_new(gdouble, 1);
    *pbuf_timestamp = gwy_get_timestamp();
    g_object_set_data_full(G_OBJECT(pixbuf), "timestamp", pbuf_timestamp, g_free);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, MODEL_THUMBNAIL, pixbuf, -1);
    g_object_set(renderer, "pixbuf", pixbuf, NULL);
    g_object_unref(pixbuf);

    update_window_icon(model, iter);
}

/**
 * gwy_app_data_browser_curve_map_deleted:
 * @data_window: A data window that was deleted.
 *
 * Destroys a deleted data window, updating proxy.
 *
 * This functions makes sure various updates happen in reasonable order, simple gtk_widget_destroy() on the data
 * window would not do that.
 *
 * Returns: Always %TRUE to be usable as terminal event handler.
 **/
static gboolean
gwy_app_data_browser_curve_map_deleted(GwyDataWindow *data_window)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GwyAppKeyType type;
    GwyContainer *data;
    GwyDataView *data_view;
    GwyPixmapLayer *layer;
    GtkTreeIter iter;
    const gchar *strkey;
    GObject *object;
    GQuark quark;
    gint i;

    gwy_debug("Data window %p deleted", data_window);
    data_view = gwy_data_window_get_data_view(data_window);
    data = gwy_data_view_get_data(data_view);
    layer = gwy_data_view_get_base_layer(data_view);
    strkey = gwy_pixmap_layer_get_data_key(layer);
    quark = g_quark_from_string(strkey);
    g_return_val_if_fail(data && quark, TRUE);

    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_val_if_fail(i >= 0 && type == KEY_IS_LAWN_PREVIEW, TRUE);
    quark = gwy_app_get_lawn_key_for_id(i);
    object = gwy_container_get_object(data, quark);

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    list = &proxy->lists[GWY_PAGE_CURVE_MAPS];
    if (!gwy_app_data_proxy_find_object(list->store, i, &iter)) {
        g_critical("Cannot find lawn %p (%d)", object, i);
        return TRUE;
    }

    proxy->resetting_visibility = TRUE;
    gwy_app_data_proxy_lawn_set_visible(proxy, &iter, FALSE);
    proxy->resetting_visibility = FALSE;
    gwy_app_data_proxy_maybe_finalize(proxy);

    return TRUE;
}

/**
 * gwy_app_data_browser_create_curve_map:
 * @browser: A data browser.
 * @id: The channel id.
 *
 * Creates a data window for a #GwyLawn curve map when its visibility is switched on.
 *
 * This is actually `make visible', should not be used outside gwy_app_data_proxy_lawn_set_visible().
 *
 * Returns: The data view (NOT data window).
 **/
static GtkWidget*
gwy_app_data_browser_create_curve_map(GwyAppDataBrowser *browser,
                                      GwyAppDataProxy *proxy, gint id)
{
    GtkWidget *data_view, *data_window;
    GObject *lawn = NULL;
    GwyDataField *preview = NULL;
    GwyPixmapLayer *layer;
    GwyLayerBasic *layer_basic;
    gchar key[48];

    g_snprintf(key, sizeof(key), "/lawn/%d", id);
    gwy_container_gis_object_by_name(proxy->container, key, &lawn);
    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);

    g_snprintf(key, sizeof(key), "/lawn/%d/preview", id);
    gwy_container_gis_object_by_name(proxy->container, key, &preview);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(preview), NULL);

    layer = gwy_layer_basic_new();
    layer_basic = GWY_LAYER_BASIC(layer);
    gwy_pixmap_layer_set_data_key(layer, key);
    g_snprintf(key, sizeof(key), "/lawn/%d/preview/palette", id);
    gwy_layer_basic_set_gradient_key(layer_basic, key);

    data_view = gwy_data_view_new(proxy->container);
    gwy_data_view_set_data_prefix(GWY_DATA_VIEW(data_view), gwy_pixmap_layer_get_data_key(layer));
    gwy_data_view_set_base_layer(GWY_DATA_VIEW(data_view), layer);

    data_window = gwy_data_window_new(GWY_DATA_VIEW(data_view));
    g_object_set_data(G_OBJECT(data_window), "gwy-app-page", GUINT_TO_POINTER(GWY_PAGE_CURVE_MAPS));
    gwy_app_update_lawn_window_title(GWY_DATA_VIEW(data_view), id);

    gwy_app_data_proxy_update_visibility(lawn, TRUE);
    g_signal_connect_swapped(data_window, "focus-in-event",
                             G_CALLBACK(gwy_app_data_browser_select_curve_map2), data_view);
    g_signal_connect(data_window, "delete-event", G_CALLBACK(gwy_app_data_browser_curve_map_deleted), NULL);

    _gwy_app_lawn_window_setup(GWY_DATA_WINDOW(data_window));

    gtk_drag_dest_set(data_window, GTK_DEST_DEFAULT_ALL,
                      dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);
    g_signal_connect(data_window, "drag-data-received", G_CALLBACK(gwy_app_window_dnd_data_received), browser);

    /* FIXME: A silly place for this? */
    gwy_app_data_browser_set_file_present(browser, TRUE);
    gtk_widget_show_all(data_window);
    _gwy_app_update_lawn_info(proxy->container, id, GWY_DATA_VIEW(data_view));
    _gwy_app_update_lawn_sens();

    return data_view;
}

static gboolean
gwy_app_data_proxy_lawn_set_visible(GwyAppDataProxy *proxy,
                                    GtkTreeIter *iter,
                                    gboolean visible)
{
    GwyAppDataList *list;
    GtkTreeModel *model;
    GtkWidget *widget, *window;
    GObject *object;
    gint id;

    list = &proxy->lists[GWY_PAGE_CURVE_MAPS];
    model = GTK_TREE_MODEL(list->store);

    gtk_tree_model_get(model, iter,
                       MODEL_WIDGET, &widget,
                       MODEL_OBJECT, &object,
                       MODEL_ID, &id,
                       -1);
    if (visible == (widget != NULL)) {
        g_object_unref(object);
        GWY_OBJECT_UNREF(widget);
        return FALSE;
    }

    if (visible) {
        widget = gwy_app_data_browser_create_curve_map(proxy->parent, proxy, id);
        gtk_list_store_set(list->store, iter, MODEL_WIDGET, widget, -1);
        update_window_icon(model, iter);
        list->visible_count++;
    }
    else {
        gwy_app_data_proxy_update_visibility(object, FALSE);
        window = gtk_widget_get_ancestor(widget, GWY_TYPE_DATA_WINDOW);
        gtk_widget_destroy(window);
        gtk_list_store_set(list->store, iter, MODEL_WIDGET, NULL, -1);
        g_object_unref(widget);
        list->visible_count--;
        _gwy_app_update_lawn_sens();
    }
    g_object_unref(object);

    gwy_debug("visible_count: %d", list->visible_count);

    return TRUE;
}

static GtkWidget*
gwy_app_data_browser_construct_lawns(GwyAppDataBrowser *browser)
{
    GtkWidget *retval;
    GtkTreeView *treeview;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    /* Construct the GtkTreeView that will display data channels */
    retval = gtk_tree_view_new();
    treeview = GTK_TREE_VIEW(retval);
    set_up_data_list_signals(treeview, browser);

    /* Add the thumbnail column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes("Thumbnail", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_lawn, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the visibility column */
    renderer = gtk_cell_renderer_toggle_new();
    g_object_set(renderer, "activatable", TRUE, NULL);
    g_signal_connect(renderer, "toggled", G_CALLBACK(gwy_app_data_browser_lawn_toggled), browser);
    column = gtk_tree_view_column_new_with_attributes("Visible", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_render_visible, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    /* Add the title column */
    gwy_app_data_list_make_title_column(browser, &column, &renderer);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_lawn_render_title, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "width-chars", 4, NULL);
    column = gtk_tree_view_column_new_with_attributes("Curves", renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, gwy_app_data_browser_lawn_render_ncurves, browser, NULL);
    gtk_tree_view_append_column(treeview, column);

    gtk_tree_view_set_headers_visible(treeview, FALSE);

    /* Selection */
    selection = gtk_tree_view_get_selection(treeview);
    g_object_set_qdata(G_OBJECT(selection), page_id_quark, GINT_TO_POINTER(GWY_PAGE_CURVE_MAPS + PAGENO_SHIFT));
    g_signal_connect(selection, "changed", G_CALLBACK(gwy_app_data_browser_selection_changed), browser);

    /* DnD */
    gtk_tree_view_enable_model_drag_source(treeview, GDK_BUTTON1_MASK,
                                           dnd_target_table, G_N_ELEMENTS(dnd_target_table), GDK_ACTION_COPY);

    return retval;
}

static void
gwy_app_update_lawn_window_title(GwyDataView *data_view,
                                 gint id)
{
    GtkWidget *data_window;
    GwyContainer *data;
    const guchar *filename;
    gchar *title, *bname, *stitle;

    data_window = gtk_widget_get_ancestor(GTK_WIDGET(data_view), GWY_TYPE_DATA_WINDOW);
    if (!data_window) {
        g_warning("GwyDataView has no GwyDataWindow ancestor");
        return;
    }

    data = gwy_data_view_get_data(data_view);
    stitle = gwy_app_get_lawn_title(data, id);
    if (gwy_container_gis_string(data, filename_quark, &filename)) {
        bname = g_path_get_basename(filename);
        title = g_strdup_printf("%s [%s]", bname, stitle);
        g_free(bname);
    }
    else {
        GwyAppDataBrowser *browser;
        GwyAppDataProxy *proxy;

        browser = gwy_app_get_data_browser();
        proxy = gwy_app_data_browser_get_proxy(browser, data);
        title = g_strdup_printf("%s %d [%s]", _("Untitled"), proxy->untitled_no, stitle);
    }
    gwy_data_window_set_data_name(GWY_DATA_WINDOW(data_window), title);
    g_free(stitle);
    g_free(title);
}

/**************************************************************************
 *
 * Common GUI
 *
 **************************************************************************/

/* GUI only */
static void
gwy_app_data_browser_delete_object(GwyAppDataProxy *proxy,
                                   GwyAppPage pageno,
                                   GtkTreeModel *model,
                                   GtkTreeIter *iter)
{
    GObject *object;
    GtkWidget *widget;
    GwyContainer *data;
    gchar key[48];
    gint i;

    data = proxy->container;
    gtk_tree_model_get(model, iter,
                       MODEL_ID, &i,
                       MODEL_OBJECT, &object,
                       MODEL_WIDGET, &widget,
                       -1);

    /* Get rid of widget displaying this object.  This may invoke complete destruction later in idle handler. */
    if (pageno == GWY_PAGE_CHANNELS)
        gwy_app_data_proxy_channel_destroy_3d(proxy, i);

    if (widget) {
        g_object_unref(widget);
        switch (pageno) {
            case GWY_PAGE_CHANNELS:
            proxy->resetting_visibility = TRUE;
            gwy_app_data_proxy_channel_set_visible(proxy, iter, FALSE);
            proxy->resetting_visibility = FALSE;
            break;

            case GWY_PAGE_GRAPHS:
            proxy->resetting_visibility = TRUE;
            gwy_app_data_proxy_graph_set_visible(proxy, iter, FALSE);
            proxy->resetting_visibility = FALSE;
            break;

            case GWY_PAGE_SPECTRA:
            /* FIXME */
            break;

            case GWY_PAGE_VOLUMES:
            proxy->resetting_visibility = TRUE;
            gwy_app_data_proxy_brick_set_visible(proxy, iter, FALSE);
            proxy->resetting_visibility = FALSE;
            break;

            case GWY_PAGE_XYZS:
            proxy->resetting_visibility = TRUE;
            gwy_app_data_proxy_surface_set_visible(proxy, iter, FALSE);
            proxy->resetting_visibility = FALSE;
            break;

            case GWY_PAGE_CURVE_MAPS:
            proxy->resetting_visibility = TRUE;
            gwy_app_data_proxy_lawn_set_visible(proxy, iter, FALSE);
            proxy->resetting_visibility = FALSE;
            break;

            default:
            g_return_if_reached();
            break;
        }
        gwy_app_data_proxy_maybe_finalize(proxy);
    }

    /* Remove object from container, this causes of removal from tree model
     * too */
    switch (pageno) {
        case GWY_PAGE_CHANNELS:
        g_snprintf(key, sizeof(key), "/%d/data", i);
        gwy_container_remove_by_name(data, key);
        /* XXX: Cannot just remove /0, because all graphs are under
         * GRAPH_PREFIX == "/0/graph/graph" */
        if (i) {
            g_snprintf(key, sizeof(key), "/%d", i);
            gwy_container_remove_by_prefix(data, key);
            gwy_app_undo_container_remove(data, key);
        }
        else {
            /* TODO: should be done in one pass through the container */
            g_snprintf(key, sizeof(key), "/%d/data", i);
            gwy_container_remove_by_prefix(data, key);
            gwy_app_undo_container_remove(data, key);
            g_snprintf(key, sizeof(key), "/%d/base", i);
            gwy_container_remove_by_prefix(data, key);
            g_snprintf(key, sizeof(key), "/%d/mask", i);
            gwy_container_remove_by_prefix(data, key);
            gwy_app_undo_container_remove(data, key);
            g_snprintf(key, sizeof(key), "/%d/show", i);
            gwy_container_remove_by_prefix(data, key);
            gwy_app_undo_container_remove(data, key);
            g_snprintf(key, sizeof(key), "/%d/select", i);
            gwy_container_remove_by_prefix(data, key);
            g_snprintf(key, sizeof(key), "/%d/meta", i);
            gwy_container_remove_by_prefix(data, key);
            g_snprintf(key, sizeof(key), "/%d/3d", i);
            gwy_container_remove_by_prefix(data, key);
            g_snprintf(key, sizeof(key), "/%d/cal_xunc", i);
            gwy_container_remove_by_prefix(data, key);
            g_snprintf(key, sizeof(key), "/%d/cal_yunc", i);
            gwy_container_remove_by_prefix(data, key);
            g_snprintf(key, sizeof(key), "/%d/cal_zunc", i);
            gwy_container_remove_by_prefix(data, key);
            g_snprintf(key, sizeof(key), "/%d/cal_xerr", i);
            gwy_container_remove_by_prefix(data, key);
            g_snprintf(key, sizeof(key), "/%d/cal_yerr", i);
            gwy_container_remove_by_prefix(data, key);
            g_snprintf(key, sizeof(key), "/%d/cal_zerr", i);
            gwy_container_remove_by_prefix(data, key);
        }
        break;

        case GWY_PAGE_GRAPHS:
        g_snprintf(key, sizeof(key), "%s/%d", GRAPH_PREFIX, i);
        gwy_container_remove_by_prefix(data, key);
        break;

        case GWY_PAGE_SPECTRA:
        g_snprintf(key, sizeof(key), "%s/%d", SPECTRA_PREFIX, i);
        gwy_container_remove_by_prefix(data, key);
        break;

        case GWY_PAGE_VOLUMES:
        g_snprintf(key, sizeof(key), "%s/%d", BRICK_PREFIX, i);
        gwy_container_remove_by_prefix(data, key);
        break;

        case GWY_PAGE_XYZS:
        g_snprintf(key, sizeof(key), "%s/%d", SURFACE_PREFIX, i);
        gwy_container_remove_by_prefix(data, key);
        break;

        case GWY_PAGE_CURVE_MAPS:
        g_snprintf(key, sizeof(key), "%s/%d", LAWN_PREFIX, i);
        gwy_container_remove_by_prefix(data, key);
        break;

        default:
        g_return_if_reached();
        break;
    }
    g_object_unref(object);

    /* Graph numbers start from 1 for historical reasons. */
    if (pageno == GWY_PAGE_GRAPHS)
        gwy_app_data_list_update_last(&proxy->lists[pageno], 0);
    else
        gwy_app_data_list_update_last(&proxy->lists[pageno], -1);
}

static void
gwy_app_data_browser_copy_object(GwyAppDataProxy *srcproxy,
                                 GwyAppPage pageno,
                                 GtkTreeModel *model,
                                 GtkTreeIter *iter,
                                 GwyAppDataProxy *destproxy)
{
    GwyContainer *container;
    gint id;

    gtk_tree_model_get(model, iter, MODEL_ID, &id, -1);

    if (!destproxy) {
        gwy_debug("Create a new file");
        container = gwy_container_new();
        gwy_app_data_browser_add(container);
    }
    else {
        gwy_debug("Create a new object in container %p", destproxy->container);
        container = destproxy->container;
    }

    switch (pageno) {
        case GWY_PAGE_CHANNELS:
        gwy_app_data_browser_copy_channel(srcproxy->container, id, container);
        break;

        case GWY_PAGE_GRAPHS:
        {
            GwyGraphModel *gmodel, *gmodel2;

            gtk_tree_model_get(model, iter, MODEL_OBJECT, &gmodel, -1);
            gmodel2 = gwy_graph_model_duplicate(gmodel);
            gwy_app_data_browser_add_graph_model(gmodel2, container, TRUE);
            g_object_unref(gmodel);
        }
        break;

        case GWY_PAGE_SPECTRA:
        {
            GwySpectra *spectra, *spectra2;

            gtk_tree_model_get(model, iter, MODEL_OBJECT, &spectra, -1);
            spectra2 = gwy_spectra_duplicate(spectra);
            gwy_app_data_browser_add_spectra(spectra2, container, FALSE);
            g_object_unref(spectra);
        }
        break;

        case GWY_PAGE_VOLUMES:
        gwy_app_data_browser_copy_volume(srcproxy->container, id, container);
        break;

        case GWY_PAGE_XYZS:
        gwy_app_data_browser_copy_xyz(srcproxy->container, id, container);
        break;

        case GWY_PAGE_CURVE_MAPS:
        gwy_app_data_browser_copy_curve_map(srcproxy->container, id, container);
        break;

        default:
        g_return_if_reached();
        break;
    }

    if (!destproxy)
        g_object_unref(container);
}

static void
gwy_app_data_browser_copy_other(GtkTreeModel *model,
                                GtkTreeIter *iter,
                                GtkWidget *window,
                                GwyContainer *container)
{
    GwyContainer *srccontainer;
    GwyDataView *data_view;
    GwyPixmapLayer *layer;
    GwyDataField *dfield, *srcfield;
    GQuark srcquark, targetquark, destquark;
    GObject *object, *destobject;
    GwySelection *selection;
    const gchar *srckey, *targetkey;
    GwyAppKeyType type;
    gint id;
    gchar *destkey, *srcfieldkey;
    gdouble originx = 0.0, originy = 0.;
    guint len;

    /* XXX: At this moment, the copying possibilities are fairly limited. */
    if (!GWY_IS_DATA_WINDOW(window))
        return;

    /* Source */
    gtk_tree_model_get(model, iter,
                       MODEL_ID, &srcquark,
                       MODEL_OBJECT, &object,
                       -1);
    if (!object)
        return;
    srckey = g_quark_to_string(srcquark);
    if (!srckey) {
        g_object_unref(object);
        return;
    }
    gwy_debug("DnD: key %08x <%s>, object %p <%s>\n", srcquark, srckey, object, G_OBJECT_TYPE_NAME(object));

    id = _gwy_app_analyse_data_key(srckey, &type, &len);
    /* XXX: At this moment, the copying possibilities are fairly limited. */
    if (id == -1 || type != KEY_IS_SELECT || !GWY_IS_SELECTION(object)) {
        g_object_unref(object);
        return;
    }

    /* This is set by SelectionManager, the only drag source for selections. */
    srccontainer = g_object_get_qdata(object, container_quark);
    gwy_debug("source container: %p", srccontainer);
    srcfieldkey = g_strdup_printf("/%d/data", id);
    if (gwy_container_gis_object_by_name(srccontainer, srcfieldkey, (GObject**)&srcfield)
        && GWY_IS_DATA_FIELD(srcfield)) {
        originx = gwy_data_field_get_xoffset(srcfield);
        originy = gwy_data_field_get_yoffset(srcfield);
    }
    g_free(srcfieldkey);

    /* Target */
    data_view  = gwy_data_window_get_data_view(GWY_DATA_WINDOW(window));
    layer = gwy_data_view_get_base_layer(data_view);
    targetkey = gwy_pixmap_layer_get_data_key(layer);
    targetquark = g_quark_from_string(targetkey);
    g_return_if_fail(targetquark);
    id = _gwy_app_analyse_data_key(targetkey, &type, NULL);
    g_return_if_fail(id >= 0 && type == KEY_IS_DATA);
    dfield = gwy_container_get_object(container, targetquark);
    g_return_if_fail(GWY_IS_DATA_FIELD(dfield));

    if (gwy_data_field_check_compatibility(dfield, srcfield, GWY_DATA_COMPATIBILITY_LATERAL)) {
        g_object_unref(object);
        return;
    }

    /* Destination */
    destkey = g_strdup_printf("/%d/select%s", id, srckey+len);
    destquark = g_quark_from_string(destkey);
    g_free(destkey);

    /* Avoid copies if source is the same as the target */
    if (!gwy_container_gis_object(container, destquark, &destobject) || destobject != object) {
        gdouble xoff, yoff, xreal, yreal;

        xoff = gwy_data_field_get_xoffset(dfield);
        yoff = gwy_data_field_get_yoffset(dfield);
        xreal = gwy_data_field_get_xreal(dfield);
        yreal = gwy_data_field_get_yreal(dfield);
        destobject = gwy_serializable_duplicate(G_OBJECT(object));
        selection = GWY_SELECTION(object);

        /* Crop the selection, taking into account that the coordinates do not
         * include field offset, and move it relative to the new origin.
         * But for Lattice, which is origin-free, just limit it so that it
         * fits inside. */
        if (gwy_strequal(G_OBJECT_TYPE_NAME(destobject), "GwySelectionLattice")) {
            gwy_selection_crop(selection, -0.5*xreal, -0.5*yreal, 0.5*xreal, 0.5*yreal);
        }
        else {
            gwy_selection_move(selection, originx, originy);
            gwy_selection_crop(selection, xoff, yoff, xoff + xreal, yoff + yreal);
            gwy_selection_move(selection, -xoff, -yoff);
        }
        if (gwy_selection_get_data(selection, NULL))
            gwy_container_set_object(container, destquark, destobject);
        g_object_unref(destobject);
    }

    g_object_unref(object);
}

static void
gwy_app_data_browser_close_file(GwyAppDataBrowser *browser)
{
    g_return_if_fail(browser->current);
    gwy_app_data_browser_remove(browser->current->container);
}

static void
gwy_app_data_browser_page_changed(GwyAppDataBrowser *browser,
                                  G_GNUC_UNUSED GtkNotebookPage *useless_crap,
                                  GwyAppPage pageno)
{
    GtkTreeSelection *selection;

    gwy_debug("Page changed to: %d", pageno);

    browser->active_page = pageno;
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->lists[pageno]));
    gwy_app_data_browser_selection_changed(selection, browser);
}

static gboolean
gwy_app_data_browser_deleted(GwyAppDataBrowser *browser)
{
    gwy_app_data_browser_hide_real(browser);

    return TRUE;
}

static gboolean
gwy_app_data_browser_configured(GwyAppDataBrowser *browser)
{
    if (!browser || !browser->window || !GTK_WIDGET_VISIBLE(browser->window))
        return FALSE;

    gwy_app_save_window_position(GTK_WINDOW(browser->window), "/app/data-browser", TRUE, TRUE);

    return FALSE;
}

static void
gwy_app_data_browser_window_destroyed(GwyAppDataBrowser *browser)
{
    guint i;

    browser->window = NULL;
    browser->active_page = GWY_PAGE_NOPAGE;
    browser->sensgroup = NULL;
    browser->filename = NULL;
    browser->notebook = NULL;
    for (i = 0; i < GWY_NPAGES; i++)
        browser->lists[i] = NULL;
}

static void
gwy_app_data_browser_shoot_object(GObject *button,
                                  GwyAppDataBrowser *browser)
{
    GwyAppDataProxy *proxy;
    GtkTreeView *treeview;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    GtkTreeModel *model;
    GwyAppPage pageno;
    const gchar *action;

    g_return_if_fail(browser->current);

    action = g_object_get_data(button, "action");
    gwy_debug("action: %s", action);

    proxy = browser->current;
    pageno = browser->active_page;

    treeview = GTK_TREE_VIEW(browser->lists[pageno]);
    selection = gtk_tree_view_get_selection(treeview);
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        g_warning("Nothing is selected");
        return;
    }

    if (gwy_strequal(action, "delete"))
        gwy_app_data_browser_delete_object(proxy, pageno, model, &iter);
    else if (gwy_strequal(action, "duplicate"))
        gwy_app_data_browser_copy_object(proxy, pageno, model, &iter, proxy);
    else if (gwy_strequal(action, "extract"))
        gwy_app_data_browser_copy_object(proxy, pageno, model, &iter, NULL);
    else
        g_warning("Unknown action <%s>", action);
}

static GtkWidget*
gwy_app_data_browser_construct_buttons(GwyAppDataBrowser *browser)
{
    static const struct {
        const gchar *stock_id;
        const gchar *tooltip;
        const gchar *action;
        guint accelkey;
        GdkModifierType accelmods;
    }
    actions[] = {
        {
            GTK_STOCK_NEW,
            N_("Extract to a new file"),
            "extract",
            GDK_Insert,
            GDK_CONTROL_MASK,
        },
        {
            GTK_STOCK_COPY,
            N_("Duplicate"),
            "duplicate",
            GDK_d,
            GDK_CONTROL_MASK,
        },
        {
            GTK_STOCK_DELETE,
            N_("Delete"),
            "delete",
            GDK_Delete,
            GDK_CONTROL_MASK,
        },
    };

    GtkWidget *hbox, *button, *image, *main_window;
    GtkAccelGroup *accel_group = NULL;
    guint i;

    main_window = gwy_app_main_window_get();
    if (main_window)
        accel_group = GTK_ACCEL_GROUP(g_object_get_data(G_OBJECT(main_window), "accel_group"));

    hbox = gtk_hbox_new(TRUE, 0);

    for (i = 0; i < G_N_ELEMENTS(actions); i++) {
        image = gtk_image_new_from_stock(actions[i].stock_id, GTK_ICON_SIZE_LARGE_TOOLBAR);
        button = gtk_button_new();
        g_object_set_data(G_OBJECT(button), "action", (gpointer)actions[i].action);
        gtk_widget_set_tooltip_text(button, gwy_sgettext(actions[i].tooltip));
        gtk_container_add(GTK_CONTAINER(button), image);
        gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
        gwy_sensitivity_group_add_widget(browser->sensgroup, button, SENS_OBJECT);
        g_signal_connect(button, "clicked", G_CALLBACK(gwy_app_data_browser_shoot_object), browser);
        gtk_widget_add_accelerator(button, "clicked", accel_group, actions[i].accelkey, actions[i].accelmods, 0);
    }

    return hbox;
}

static void
gwy_app_data_browser_construct_window(GwyAppDataBrowser *browser)
{
    GtkWidget *label, *box_page, *scwin, *vbox, *hbox, *button, *image;

    browser->sensgroup = gwy_sensitivity_group_new();
    browser->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect_swapped(browser->window, "destroy", G_CALLBACK(gwy_app_data_browser_window_destroyed), browser);

    gtk_window_set_default_size(GTK_WINDOW(browser->window), 320, 400);
    gtk_window_set_title(GTK_WINDOW(browser->window), _("Data Browser"));
    gtk_window_set_role(GTK_WINDOW(browser->window), GWY_DATABROWSER_WM_ROLE);
    gwy_app_add_main_accel_group(GTK_WINDOW(browser->window));
    gwy_help_add_to_window(GTK_WINDOW(browser->window), "data-browser", NULL, GWY_HELP_DEFAULT);

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(browser->window), vbox);

    /* Filename row */
    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Filename */
    browser->filename = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(browser->filename), PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment(GTK_MISC(browser->filename), 0.0, 0.5);
    gtk_misc_set_padding(GTK_MISC(browser->filename), 4, 2);
    gtk_box_pack_start(GTK_BOX(hbox), browser->filename, TRUE, TRUE, 0);

    /* Messages button */
    browser->messages_button = button = gtk_toggle_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    image = gtk_image_new_from_stock(GWY_STOCK_LOAD_INFO, GTK_ICON_SIZE_BUTTON);
    gtk_container_add(GTK_CONTAINER(button), image);
    gtk_widget_set_tooltip_text(button, _("Show file messages"));
    gtk_widget_set_no_show_all(browser->messages_button, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    g_signal_connect(button, "toggled", G_CALLBACK(gwy_app_data_browser_show_hide_messages), browser);

    /* Close button */
    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    image = gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_BUTTON);
    gtk_container_add(GTK_CONTAINER(button), image);
    gtk_widget_set_tooltip_text(button, _("Close file"));
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    gwy_sensitivity_group_add_widget(browser->sensgroup, button, SENS_FILE);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(gwy_app_data_browser_close_file), browser);

    /* Notebook */
    browser->notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), browser->notebook, TRUE, TRUE, 0);

    /* Channels tab */
    box_page = gtk_vbox_new(FALSE, 0);
    label = gtk_label_new(_("Images"));
    gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook), box_page, label);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(box_page), scwin, TRUE, TRUE, 0);

    browser->lists[GWY_PAGE_CHANNELS] = gwy_app_data_browser_construct_channels(browser);
    gtk_container_add(GTK_CONTAINER(scwin), browser->lists[GWY_PAGE_CHANNELS]);

    /* Graphs tab */
    box_page = gtk_vbox_new(FALSE, 0);
    label = gtk_label_new(_("Graphs"));
    gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook), box_page, label);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(box_page), scwin, TRUE, TRUE, 0);

    browser->lists[GWY_PAGE_GRAPHS] = gwy_app_data_browser_construct_graphs(browser);
    gtk_container_add(GTK_CONTAINER(scwin), browser->lists[GWY_PAGE_GRAPHS]);

    /* Single point spectra */
    box_page = gtk_vbox_new(FALSE, 0);
    label = gtk_label_new(_("Spectra"));
    gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook), box_page, label);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(box_page), scwin, TRUE, TRUE, 0);

    browser->lists[GWY_PAGE_SPECTRA] = gwy_app_data_browser_construct_spectra(browser);
    gtk_container_add(GTK_CONTAINER(scwin), browser->lists[GWY_PAGE_SPECTRA]);

    /* Bricks (volume data) */
    box_page = gtk_vbox_new(FALSE, 0);
    label = gtk_label_new(_("Volume"));
    gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook), box_page, label);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(box_page), scwin, TRUE, TRUE, 0);

    browser->lists[GWY_PAGE_VOLUMES] = gwy_app_data_browser_construct_bricks(browser);
    gtk_container_add(GTK_CONTAINER(scwin), browser->lists[GWY_PAGE_VOLUMES]);

    /* Surfaces (XYZ data) */
    box_page = gtk_vbox_new(FALSE, 0);
    label = gtk_label_new(_("XYZ"));
    gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook), box_page, label);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(box_page), scwin, TRUE, TRUE, 0);

    browser->lists[GWY_PAGE_XYZS] = gwy_app_data_browser_construct_surfaces(browser);
    gtk_container_add(GTK_CONTAINER(scwin), browser->lists[GWY_PAGE_XYZS]);

    /* Lawns (curve map data) */
    box_page = gtk_vbox_new(FALSE, 0);
    label = gtk_label_new(_("Curve Maps"));
    gtk_notebook_append_page(GTK_NOTEBOOK(browser->notebook), box_page, label);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(box_page), scwin, TRUE, TRUE, 0);

    browser->lists[GWY_PAGE_CURVE_MAPS] = gwy_app_data_browser_construct_lawns(browser);
    gtk_container_add(GTK_CONTAINER(scwin), browser->lists[GWY_PAGE_CURVE_MAPS]);

    /* Buttons */
    hbox = gwy_app_data_browser_construct_buttons(browser);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Finish */
    g_signal_connect_swapped(browser->notebook, "switch-page", G_CALLBACK(gwy_app_data_browser_page_changed), browser);
    g_signal_connect_swapped(browser->window, "delete-event", G_CALLBACK(gwy_app_data_browser_deleted), browser);
    g_signal_connect_swapped(browser->window, "configure-event", G_CALLBACK(gwy_app_data_browser_configured), browser);
    g_object_unref(browser->sensgroup);

    gtk_widget_show_all(vbox);
}

/**
 * gwy_app_get_data_browser:
 *
 * Gets the application data browser.
 *
 * When it does not exist yet, it is created as a side effect.
 *
 * Returns: The data browser.
 **/
static GwyAppDataBrowser*
gwy_app_get_data_browser(void)
{
    GwyAppDataBrowser *browser;

    if (gwy_app_data_browser)
        return gwy_app_data_browser;

    own_key_quark = g_quark_from_static_string("gwy-app-data-browser-own-key");
    container_quark = g_quark_from_static_string("gwy-app-data-browser-container");
    page_id_quark = g_quark_from_static_string("gwy-app-data-browser-page-id");
    column_id_quark = g_quark_from_static_string("gwy-app-data-browser-column-id");
    filename_quark = g_quark_from_static_string("/filename");
    graph_window_quark = g_quark_from_static_string("gwy-app-data-browser-window-model");
    surface_update_quark = g_quark_from_static_string("gwy-data-browser-must-update-preview");

    browser = g_new0(GwyAppDataBrowser, 1);
    gwy_app_data_browser = browser;

    return browser;
}

static void
gwy_app_data_browser_select_iter(GtkTreeView *treeview,
                                 GtkTreeIter *iter)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreePath *path;

    selection = gtk_tree_view_get_selection(treeview);
    gtk_tree_selection_select_iter(selection, iter);

    model = gtk_tree_view_get_model(treeview);
    path = gtk_tree_model_get_path(model, iter);
    gtk_tree_view_scroll_to_cell(treeview, path, NULL, FALSE, 0.0, 1.0);
    gtk_tree_path_free(path);
}

static void
gwy_app_data_browser_restore_active(GtkTreeView *treeview,
                                    GwyAppDataList *list)
{
    GtkTreeIter iter;

    gtk_tree_view_set_model(treeview, GTK_TREE_MODEL(list->store));
    if (gwy_app_data_proxy_find_object(list->store, list->active, &iter))
        gwy_app_data_browser_select_iter(treeview, &iter);
}

static void
gwy_app_data_browser_switch_data(GwyContainer *data)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    guint i;

    browser = gwy_app_get_data_browser();
    if (!data) {
        if (browser->window) {
            for (i = 0; i < GWY_NPAGES; i++)
                gtk_tree_view_set_model(GTK_TREE_VIEW(browser->lists[i]), NULL);
            gtk_label_set_text(GTK_LABEL(browser->filename), NULL);
            gtk_widget_set_tooltip_text(browser->filename, NULL);
            gwy_app_data_browser_set_file_present(browser, FALSE);
        }
        browser->current = NULL;
        update_all_sens();
        return;
    }

    if (browser->current && browser->current->container == data)
        return;

    proxy = gwy_app_data_browser_get_proxy(browser, data);
    g_return_if_fail(proxy);
    if (proxy->finalize_id)
        return;

    browser->current = proxy;

    gwy_app_data_browser_update_filename(proxy);
    if (browser->window) {
        for (i = 0; i < GWY_NPAGES; i++)
            gwy_app_data_browser_restore_active(GTK_TREE_VIEW(browser->lists[i]), &proxy->lists[i]);
        gwy_app_data_browser_set_file_present(browser, TRUE);
    }
    update_all_sens();
}

static void
update_all_sens(void)
{
    _gwy_app_update_channel_sens();
    _gwy_app_update_graph_sens();
    _gwy_app_update_brick_sens();
    _gwy_app_update_surface_sens();
    update_message_button();
}

static void
gwy_app_data_browser_select_object(GwyAppDataBrowser *browser,
                                   GwyAppDataProxy *proxy,
                                   GwyAppPage pageno)
{
    GtkTreeView *treeview;
    GtkTreeIter iter;

    if (!browser->window)
        return;

    treeview = GTK_TREE_VIEW(browser->lists[pageno]);
    gwy_app_data_proxy_find_object(proxy->lists[pageno].store, proxy->lists[pageno].active, &iter);
    gwy_app_data_browser_select_iter(treeview, &iter);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(browser->notebook), pageno);
}

/**
 * gwy_app_data_browser_select_data_view:
 * @data_view: A data view widget.
 *
 * Switches application data browser to display container of @data_view's data and selects @data_view's data in the
 * channel list.
 **/
void
gwy_app_data_browser_select_data_view(GwyDataView *data_view)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyPixmapLayer *layer;
    GwyContainer *data, *olddata;
    const gchar *strkey;
    GwyAppKeyType type;
    gint i;

    browser = gwy_app_get_data_browser();
    olddata = browser->current ? browser->current->container : NULL;

    data = gwy_data_view_get_data(data_view);
    gwy_app_data_browser_switch_data(data);

    proxy = gwy_app_data_browser_get_proxy(browser, data);
    g_return_if_fail(proxy);

    layer = gwy_data_view_get_base_layer(data_view);
    strkey = gwy_pixmap_layer_get_data_key(layer);
    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_if_fail(i >= 0 && type == KEY_IS_DATA);
    proxy->lists[GWY_PAGE_CHANNELS].active = i;

    gwy_app_data_browser_select_object(browser, proxy, GWY_PAGE_CHANNELS);
    _gwy_app_data_view_set_current(data_view);
    _gwy_app_update_channel_sens();

    /* Restore the last used spectra.  If the reference is dangling, remove it from the container. */
    {
        gboolean selected = FALSE;
        GwySpectra *spectra;
        gchar key[40];
        gint id;

        g_snprintf(key, sizeof(key), "/%d/data/sps-id", i);
        if (gwy_container_gis_int32_by_name(data, key, &id)) {
            GQuark quark;

            quark = gwy_app_get_spectra_key_for_id(id);
            if (gwy_container_gis_object(data, quark, &spectra)) {
                gwy_app_data_browser_select_spectra(spectra);
                selected = TRUE;
            }
            else
                gwy_container_remove_by_name(data, key);
        }
        /* We have to ensure NULL spectra selection is emitted when we switch to data that have no spectra.  And
         * generally whenever we switch to another container, we make spectra from that container active (or none). */
        if (!selected) {
            if (data != olddata) {
                GwyAppDataList *list = &proxy->lists[GWY_PAGE_SPECTRA];
                GtkTreeModel *model;
                GtkTreeIter iter;

                model = GTK_TREE_MODEL(list->store);
                if (gwy_app_data_proxy_find_object(list->store, list->active, &iter)
                    || gtk_tree_model_get_iter_first(model, &iter)) {
                    gtk_tree_model_get(model, &iter, MODEL_OBJECT, &spectra, -1);
                    gwy_app_data_browser_select_spectra(spectra);
                    g_object_unref(spectra);
                }
                else {
                    _gwy_app_spectra_set_current(NULL);
                }
            }
        }
    }
}

static gboolean
gwy_app_data_browser_select_data_view2(GwyDataView *data_view)
{
    gwy_app_data_browser_select_data_view(data_view);
    return FALSE;
}

/**
 * gwy_app_data_browser_select_graph:
 * @graph: A graph widget.
 *
 * Switches application data browser to display container of @graph's data and selects @graph's data in the graph
 * list.
 **/
void
gwy_app_data_browser_select_graph(GwyGraph *graph)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyGraphModel *gmodel;
    GwyContainer *data;
    const gchar *strkey;
    GwyAppKeyType type;
    GQuark quark;
    gint i;

    gmodel = gwy_graph_get_model(graph);
    data = g_object_get_qdata(G_OBJECT(gmodel), container_quark);
    g_return_if_fail(data);
    gwy_app_data_browser_switch_data(data);

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    g_return_if_fail(proxy);

    quark = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(gmodel), own_key_quark));
    strkey = g_quark_to_string(quark);
    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_if_fail(i >= 0 && type == KEY_IS_GRAPH);
    proxy->lists[GWY_PAGE_GRAPHS].active = i;

    gwy_app_data_browser_select_object(browser, proxy, GWY_PAGE_GRAPHS);
    _gwy_app_update_graph_sens();
}

static gboolean
gwy_app_data_browser_select_graph2(GwyGraph *graph)
{
    gwy_app_data_browser_select_graph(graph);
    return FALSE;
}

/**
 * gwy_app_data_browser_select_spectra:
 * @spectra: A spectra object.
 *
 * Switches application data browser to display container of @spectra's data and selects @spectra's data in the graph
 * list.
 *
 * However, it is not actually supposed to work with spectra from a different container than those of the currently
 * active channel, so do not try that for now.
 *
 * Since: 2.7
 **/
void
gwy_app_data_browser_select_spectra(GwySpectra *spectra)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyContainer *data;
    const gchar *strkey;
    GwyAppKeyType type;
    GQuark quark;
    gint i;

    data = g_object_get_qdata(G_OBJECT(spectra), container_quark);
    g_return_if_fail(data);
    gwy_app_data_browser_switch_data(data);

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    g_return_if_fail(proxy);

    quark = GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(spectra), own_key_quark));
    strkey = g_quark_to_string(quark);
    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_if_fail(i >= 0 && type == KEY_IS_SPECTRA);
    proxy->lists[GWY_PAGE_SPECTRA].active = i;

    gwy_app_data_browser_select_object(browser, proxy, GWY_PAGE_SPECTRA);
    _gwy_app_spectra_set_current(spectra);
}

/**
 * gwy_app_data_browser_select_volume:
 * @data_view: A data view widget showing a preview of volume data.
 *
 * Switches application data browser to display container of data and selects @data_view's volume data in the graph
 * list.
 *
 * Since: 2.32
 **/
void
gwy_app_data_browser_select_volume(GwyDataView *data_view)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyPixmapLayer *layer;
    GwyContainer *data;
    const gchar *strkey;
    GwyAppKeyType type;
    gint i;

    browser = gwy_app_get_data_browser();

    data = gwy_data_view_get_data(data_view);
    gwy_app_data_browser_switch_data(data);

    proxy = gwy_app_data_browser_get_proxy(browser, data);
    g_return_if_fail(proxy);

    layer = gwy_data_view_get_base_layer(data_view);
    strkey = gwy_pixmap_layer_get_data_key(layer);
    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_if_fail(i >= 0 && type == KEY_IS_BRICK_PREVIEW);
    proxy->lists[GWY_PAGE_VOLUMES].active = i;

    gwy_app_data_browser_select_object(browser, proxy, GWY_PAGE_VOLUMES);
    _gwy_app_update_brick_sens();
}

static gboolean
gwy_app_data_browser_select_volume2(GwyDataView *data_view)
{
    gwy_app_data_browser_select_volume(data_view);
    return FALSE;
}

/**
 * gwy_app_data_browser_select_xyz:
 * @data_view: A data view widget showing a preview of XYZ data.
 *
 * Switches application data browser to display container of data and selects @data_view's XYZ data in the graph list.
 *
 * Since: 2.45
 **/
void
gwy_app_data_browser_select_xyz(GwyDataView *data_view)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyPixmapLayer *layer;
    GwyContainer *data;
    const gchar *strkey;
    GwyAppKeyType type;
    gint i;

    browser = gwy_app_get_data_browser();

    data = gwy_data_view_get_data(data_view);
    gwy_app_data_browser_switch_data(data);

    proxy = gwy_app_data_browser_get_proxy(browser, data);
    g_return_if_fail(proxy);

    layer = gwy_data_view_get_base_layer(data_view);
    strkey = gwy_pixmap_layer_get_data_key(layer);
    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_if_fail(i >= 0 && type == KEY_IS_SURFACE_PREVIEW);
    proxy->lists[GWY_PAGE_XYZS].active = i;

    gwy_app_data_browser_select_object(browser, proxy, GWY_PAGE_XYZS);
    _gwy_app_update_surface_sens();
}

static gboolean
gwy_app_data_browser_select_xyz2(GwyDataView *data_view)
{
    gwy_app_data_browser_select_xyz(data_view);
    return FALSE;
}

/**
 * gwy_app_data_browser_select_curve_map:
 * @data_view: A data view widget showing a preview of curve_map data.
 *
 * Switches application data browser to display container of data and selects @data_view's curve_map data in the graph
 * list.
 *
 * Since: 2.60
 **/
void
gwy_app_data_browser_select_curve_map(GwyDataView *data_view)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyPixmapLayer *layer;
    GwyContainer *data;
    const gchar *strkey;
    GwyAppKeyType type;
    gint i;

    browser = gwy_app_get_data_browser();

    data = gwy_data_view_get_data(data_view);
    gwy_app_data_browser_switch_data(data);

    proxy = gwy_app_data_browser_get_proxy(browser, data);
    g_return_if_fail(proxy);

    layer = gwy_data_view_get_base_layer(data_view);
    strkey = gwy_pixmap_layer_get_data_key(layer);
    i = _gwy_app_analyse_data_key(strkey, &type, NULL);
    g_return_if_fail(i >= 0 && type == KEY_IS_LAWN_PREVIEW);
    proxy->lists[GWY_PAGE_CURVE_MAPS].active = i;

    gwy_app_data_browser_select_object(browser, proxy, GWY_PAGE_CURVE_MAPS);
    _gwy_app_update_lawn_sens();
}

static gboolean
gwy_app_data_browser_select_curve_map2(GwyDataView *data_view)
{
    gwy_app_data_browser_select_curve_map(data_view);
    return FALSE;
}

static GwyAppDataProxy*
gwy_app_data_browser_select(GwyContainer *data,
                            gint id,
                            GwyAppPage pageno,
                            GtkTreeIter *iter)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;

    gwy_app_data_browser_switch_data(data);

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    if (!gwy_app_data_proxy_find_object(proxy->lists[pageno].store, id, iter)) {
        g_warning("Cannot find object to select");
        return NULL;
    }

    proxy->lists[pageno].active = id;
    gwy_app_data_browser_select_object(browser, proxy, pageno);

    return proxy;
}

/**
 * gwy_app_data_browser_select_data_field:
 * @data: The container to select.
 * @id: Number (id) of the data field in @data to select.
 *
 * Makes a data field (image) current in the data browser.
 *
 * <warning>This function does not do what you might expect.  Selecting a data object which is not displayed in any
 * view makes it just possible to delete or duplicate in the data browser.  Module functions can be only run on
 * visible data.</warning>
 **/
void
gwy_app_data_browser_select_data_field(GwyContainer *data, gint id)
{
    GtkTreeIter iter;
    gwy_app_data_browser_select(data, id, GWY_PAGE_CHANNELS, &iter);
}

/**
 * gwy_app_data_browser_select_graph_model:
 * @data: The container to select.
 * @id: Number (id) of the graph model in @data to select.
 *
 * Makes a graph model current in the data browser.
 *
 * <warning>This function does not do what you might expect.  Selecting a data object which is not displayed in any
 * view makes it just possible to delete or duplicate in the data browser.  Module functions can be only run on
 * visible data.</warning>
 **/
void
gwy_app_data_browser_select_graph_model(GwyContainer *data, gint id)
{
    GtkTreeIter iter;
    gwy_app_data_browser_select(data, id, GWY_PAGE_GRAPHS, &iter);
}

/**
 * gwy_app_data_browser_select_surface:
 * @data: The container to select.
 * @id: Number (id) of the surface in @data to select.
 *
 * Makes a surface (XYZ data) current in the data browser.
 *
 * <warning>This function does not do what you might expect.  Selecting a data object which is not displayed in any
 * view makes it just possible to delete or duplicate in the data browser.  Module functions can be only run on
 * visible data.</warning>
 *
 * Since: 2.61
 **/
void
gwy_app_data_browser_select_surface(GwyContainer *data, gint id)
{
    GtkTreeIter iter;
    gwy_app_data_browser_select(data, id, GWY_PAGE_XYZS, &iter);
}

/**
 * gwy_app_data_browser_select_brick:
 * @data: The container to select.
 * @id: Number (id) of the brick in @data to select.
 *
 * Makes a brick (volume data) current in the data browser.
 *
 * <warning>This function does not do what you might expect.  Selecting a data object which is not displayed in any
 * view makes it just possible to delete or duplicate in the data browser.  Module functions can be only run on
 * visible data.</warning>
 *
 * Since: 2.61
 **/
void
gwy_app_data_browser_select_brick(GwyContainer *data, gint id)
{
    GtkTreeIter iter;
    gwy_app_data_browser_select(data, id, GWY_PAGE_VOLUMES, &iter);
}

/**
 * gwy_app_data_browser_select_lawn:
 * @data: The container to select.
 * @id: Number (id) of the lawn in @data to select.
 *
 * Makes a lawn (curve map) current in the data browser.
 *
 * <warning>This function does not do what you might expect.  Selecting a data object which is not displayed in any
 * view makes it just possible to delete or duplicate in the data browser.  Module functions can be only run on
 * visible data.</warning>
 *
 * Since: 2.61
 **/
void
gwy_app_data_browser_select_lawn(GwyContainer *data, gint id)
{
    GtkTreeIter iter;
    gwy_app_data_browser_select(data, id, GWY_PAGE_CURVE_MAPS, &iter);
}

static void
gwy_app_data_list_reset_visibility(GwyAppDataProxy *proxy,
                                   GwyAppDataList *list,
                                   SetVisibleFunc set_visible,
                                   gboolean visible)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    model = GTK_TREE_MODEL(list->store);
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            set_visible(proxy, &iter, visible);
        } while (gtk_tree_model_iter_next(model, &iter));
    }
}

static void
gwy_app_data_list_reconstruct_visibility(GwyAppDataProxy *proxy,
                                         GwyAppDataList *list,
                                         SetVisibleFunc set_visible)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    GObject *object;
    GQuark quark;
    const gchar *strkey;
    gchar key[48];
    gboolean visible;

    proxy->resetting_visibility = TRUE;
    model = GTK_TREE_MODEL(list->store);
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        do {
            visible = FALSE;
            gtk_tree_model_get(model, &iter, MODEL_OBJECT, &object, -1);
            quark = GPOINTER_TO_UINT(g_object_get_qdata(object, own_key_quark));
            strkey = g_quark_to_string(quark);
            g_snprintf(key, sizeof(key), "%s/visible", strkey);
            gwy_container_gis_boolean_by_name(proxy->container, key, &visible);
            set_visible(proxy, &iter, visible);
            g_object_unref(object);
        } while (gtk_tree_model_iter_next(model, &iter));
    }
    proxy->resetting_visibility = FALSE;
}

/**
 * gwy_app_data_browser_reset_visibility:
 * @data: A data container.
 * @reset_type: Type of visibility reset.
 *
 * Resets visibility of all data objects in a container.
 *
 * Returns: %TRUE if anything is visible after the reset.
 **/
gboolean
gwy_app_data_browser_reset_visibility(GwyContainer *data,
                                      GwyVisibilityResetType reset_type)
{
    static const SetVisibleFunc set_visible[GWY_NPAGES] = {
        &gwy_app_data_proxy_channel_set_visible,
        &gwy_app_data_proxy_graph_set_visible,
        NULL,
        &gwy_app_data_proxy_brick_set_visible,
        &gwy_app_data_proxy_surface_set_visible,
        &gwy_app_data_proxy_lawn_set_visible,
    };

    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy = NULL;
    GwyAppDataList *list;
    gboolean visible;
    gint i;

    g_return_val_if_fail(GWY_IS_CONTAINER(data), FALSE);

    if (gui_disabled)
        return FALSE;

    if ((browser = gwy_app_data_browser))
        proxy = gwy_app_data_browser_get_proxy(browser, data);

    if (!proxy) {
        g_critical("Data container is unknown to data browser.");
        return FALSE;
    }

    if (reset_type == GWY_VISIBILITY_RESET_RESTORE || reset_type == GWY_VISIBILITY_RESET_DEFAULT) {
        for (i = 0; i < GWY_NPAGES; i++) {
            if (set_visible[i])
                gwy_app_data_list_reconstruct_visibility(proxy, &proxy->lists[i], set_visible[i]);
        }
        if (gwy_app_data_proxy_visible_count(proxy))
            return TRUE;

        /* For RESTORE, we are content even with nothing being displayed */
        if (reset_type == GWY_VISIBILITY_RESET_RESTORE)
            return FALSE;

        /* Attempt to show something. FIXME: Crude. */
        for (i = 0; i < GWY_NPAGES; i++) {
            GtkTreeModel *model;
            GtkTreeIter iter;

            if (!set_visible[i])
                continue;

            list = &proxy->lists[i];
            model = GTK_TREE_MODEL(list->store);
            if (!gtk_tree_model_get_iter_first(model, &iter))
                continue;

            proxy->resetting_visibility = TRUE;
            set_visible[i](proxy, &iter, TRUE);
            proxy->resetting_visibility = FALSE;
        }

        return FALSE;
    }

    if (reset_type == GWY_VISIBILITY_RESET_HIDE_ALL)
        visible = FALSE;
    else if (reset_type == GWY_VISIBILITY_RESET_SHOW_ALL)
        visible = TRUE;
    else {
        g_critical("Wrong reset_type value");
        return FALSE;
    }

    proxy->resetting_visibility = TRUE;
    for (i = 0; i < GWY_NPAGES; i++) {
        if (set_visible[i])
            gwy_app_data_list_reset_visibility(proxy, &proxy->lists[i], set_visible[i], visible);
    }
    proxy->resetting_visibility = FALSE;

    return visible && gwy_app_data_proxy_visible_count(proxy);
}

/**
 * gwy_app_data_browser_add:
 * @data: A data container.
 *
 * Adds a data container to the application data browser.
 *
 * The data browser takes a reference on the container so you can release yours.
 **/
void
gwy_app_data_browser_add(GwyContainer *data)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;

    g_return_if_fail(GWY_IS_CONTAINER(data));

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    if (proxy) {
        g_critical("GwyContainer %p was already added!", data);
        g_object_ref(data);
        return;
    }
    gwy_app_data_proxy_new(browser, data);
}

/**
 * gwy_app_data_browser_remove:
 * @data: A data container.
 *
 * Removed a data container from the application data browser.
 **/
void
gwy_app_data_browser_remove(GwyContainer *data)
{
    GwyAppDataProxy *proxy;

    proxy = gwy_app_data_browser_get_proxy(gwy_app_get_data_browser(), data);
    g_return_if_fail(proxy);

    gwy_app_data_proxy_destroy_all_3d(proxy);
    gwy_app_data_proxy_destroy_messages(proxy);
    gwy_app_data_browser_reset_visibility(proxy->container, GWY_VISIBILITY_RESET_HIDE_ALL);
    g_return_if_fail(gwy_app_data_proxy_visible_count(proxy) == 0);
    gwy_app_data_proxy_finalize_lists(proxy);
    gwy_app_data_proxy_finalize(proxy);
}

static gint
compare_int_direct(gconstpointer a,
                   gconstpointer b)
{
    gint ia, ib;

    ia = GPOINTER_TO_INT(a);
    ib = GPOINTER_TO_INT(b);
    if (ia < ib)
        return -1;
    if (ia > ib)
        return 1;
    return 0;
}

/**
 * gwy_app_data_browser_merge:
 * @data: A data container, not managed by the data browser.
 *
 * Merges the data from a data container to the current one.
 *
 * Since: 2.7
 **/
void
gwy_app_data_browser_merge(GwyContainer *container)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GList *ids[GWY_NPAGES], *l;
    GHashTable *map[GWY_NPAGES+1];
    GwyAppPage pageno;
    gint last;

    g_return_if_fail(GWY_IS_CONTAINER(container));
    browser = gwy_app_get_data_browser();

    proxy = gwy_app_data_browser_get_proxy(browser, container);
    if (proxy) {
        g_critical("Live files cannot be merged");
        return;
    }
    proxy = browser->current;
    if (!proxy) {
        g_warning("There is no current data to merge to");
        gwy_app_data_browser_add(container);
        return;
    }

    /* Build a map from container ids to destination ids */
    memset(&ids[0], 0, GWY_NPAGES*sizeof(GList*));
    gwy_container_foreach(container, NULL, _gwy_app_data_merge_gather, &ids[0]);
    for (pageno = 0; pageno < (GwyAppPage)GWY_NPAGES; pageno++) {
        gwy_debug("page %d", pageno);
        last = proxy->lists[pageno].last;
        map[pageno] = g_hash_table_new(g_direct_hash, g_direct_equal);
        ids[pageno] = g_list_sort(ids[pageno], compare_int_direct);
        for (l = ids[pageno]; l; l = g_list_next(l)) {
            last++;
            g_hash_table_insert(map[pageno], l->data, GINT_TO_POINTER(last));
            gwy_debug("mapping %d -> %d", GPOINTER_TO_INT(l->data), last);
        }
        g_list_free(ids[pageno]);
    }

    /* Perform the transfer */
    map[GWY_NPAGES] = (GHashTable*)proxy->container;
    proxy->resetting_visibility = TRUE;
    gwy_container_foreach(container, NULL, _gwy_app_data_merge_copy_1, &map[0]);
    gwy_container_foreach(container, NULL, _gwy_app_data_merge_copy_2, &map[0]);
    ensure_brick_previews(proxy);
    ensure_lawn_previews(proxy);
    proxy->resetting_visibility = FALSE;
    gwy_app_data_browser_reset_visibility(proxy->container, GWY_VISIBILITY_RESET_RESTORE);
}

/**
 * gwy_app_data_browser_get:
 * @number: Numerical identifier of open data managed by the data browser, or
 *          zero.
 *
 * Gets the data corresponding to a numerical identifier.
 *
 * The identifier can be obtained with gwy_app_data_browser_get_number(). See its documentation for discussion.
 *
 * Returns: The corresponding data container.  %NULL is returned if @number does not identify any existing data.
 *
 * Since: 2.41
 **/
GwyContainer*
gwy_app_data_browser_get(gint number)
{
    GwyAppDataBrowser *browser;
    GList *l;

    browser = gwy_app_get_data_browser();
    for (l = browser->proxy_list; l; l = g_list_next(l)) {
        GwyAppDataProxy *proxy = (GwyAppDataProxy*)l->data;
        if (proxy->data_no == number)
            return proxy->container;
    }
    return NULL;
}

/**
 * gwy_app_data_browser_get_number:
 * @data: A data container managed by the data browser.  For convenience, %NULL is also permitted.
 *
 * Gets the numerical identifier of data.
 *
 * Each time a data container is added with gwy_app_data_browser_add() it is assigned a new unique numerical
 * identifier. This number can be used in multi-data modules to remember and restore secondary data.
 *
 * Note, however, that the number is only guaranteed to be unique within one process. It does not persist across
 * different program invocations and it does not make sense to store it to the settings or other kinds of permanent
 * storage.
 *
 * Returns: A positive numerical identifier of @data.  Zero is returned if @data is %NULL.
 *
 * Since: 2.41
 **/
gint
gwy_app_data_browser_get_number(GwyContainer *data)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;

    g_return_val_if_fail(!data || GWY_IS_CONTAINER(data), 0);
    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    return proxy ? proxy->data_no : 0;
}

static void
update_messages_textbuf_since(GwyAppDataProxy *proxy, guint from)
{
    GArray *messages = proxy->messages;
    GtkTextBuffer *textbuf = proxy->message_textbuf;
    guint i;

    if (!messages || !textbuf)
        return;

    for (i = from; i < messages->len; i++) {
        const GwyAppLogMessage *message = &g_array_index(messages, GwyAppLogMessage, i);
        proxy->log_levels_seen |= message->log_level;
        _gwy_app_log_add_message_to_textbuf(textbuf, message->message, message->log_level);
    }
}

void
_gwy_app_data_browser_add_messages(GwyContainer *data)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppLogMessage *messages;
    guint nmesg;

    if (!data) {
        _gwy_app_log_discard_captured_messages();
        g_warning("Cannot add messages for NULL data.");
        return;
    }
    g_return_if_fail(GWY_IS_CONTAINER(data));

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    if (!proxy) {
        _gwy_app_log_discard_captured_messages();
        g_critical("Data container is unknown to data browser.");
        return;
    }

    messages = _gwy_app_log_get_captured_messages(&nmesg);
    if (!messages)
        return;

    if (!proxy->messages)
        proxy->messages = g_array_new(FALSE, FALSE, sizeof(GwyAppLogMessage));

    g_array_append_vals(proxy->messages, messages, nmesg);
    g_free(messages);

    update_messages_textbuf_since(proxy, proxy->messages->len - nmesg);
    update_message_button();
}

static void
update_message_button(void)
{
    GwyAppDataBrowser *browser = gwy_app_get_data_browser();
    GwyAppDataProxy *proxy = browser->current;
    GtkWidget *button = browser->messages_button, *image;
    GLogLevelFlags log_levels_seen;

    if (!browser->window)
        return;

    if (!proxy || !proxy->messages || !proxy->messages->len) {
        gtk_widget_set_no_show_all(button, TRUE);
        gtk_widget_hide(button);
        return;
    }

    log_levels_seen = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "log-level-seen"));
    if (log_levels_seen != proxy->log_levels_seen) {
        const gchar *stock_name = GWY_STOCK_LOAD_INFO;

        gtk_widget_destroy(gtk_bin_get_child(GTK_BIN(button)));
        if (proxy->log_levels_seen & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING))
            stock_name = GWY_STOCK_LOAD_WARNING;
        else if (proxy->log_levels_seen & (G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO))
            stock_name = GWY_STOCK_LOAD_INFO;
        else if (proxy->log_levels_seen & G_LOG_LEVEL_DEBUG)
            stock_name = GWY_STOCK_LOAD_DEBUG;

        image = gtk_image_new_from_stock(stock_name, GTK_ICON_SIZE_BUTTON);
        gtk_container_add(GTK_CONTAINER(button), image);
        g_object_set_data(G_OBJECT(button), "log-level-seen", GUINT_TO_POINTER(proxy->log_levels_seen));
    }

    gtk_widget_set_no_show_all(button, FALSE);
    gtk_widget_show_all(button);
    /* The "toggled" handler can deal with setting state to the existing state. */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), !!proxy->message_window);
}

static void
message_log_window_destroyed(gpointer data,
                             G_GNUC_UNUSED GObject *where_the_object_was)
{
    GwyAppDataProxy *proxy = (GwyAppDataProxy*)data;
    GwyAppDataBrowser *browser = proxy->parent;

    proxy->message_window = NULL;
    GWY_OBJECT_UNREF(proxy->message_textbuf);
    if (proxy == browser->current) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(browser->messages_button), FALSE);
    }
}

static void
message_log_updated(GtkTextBuffer *textbuf, GtkTextView *textview)
{
    GtkTextIter iter;

    gtk_text_buffer_get_end_iter(textbuf, &iter);
    gtk_text_view_scroll_to_iter(textview, &iter, 0.0, FALSE, 0.0, 1.0);
}

static gboolean
message_log_key_pressed(GtkWidget *window, GdkEventKey *event)
{
    if (event->keyval != GDK_Escape || (event->state & important_mods))
        return FALSE;

    gtk_widget_hide(window);
    return TRUE;
}

static void
create_message_log_window(GwyAppDataProxy *proxy)
{
    GtkWindow *window;
    GtkTextBuffer *textbuf;
    GtkWidget *logview, *scwin;
    const guchar *filename;
    gchar *title, *bname;

    if (gwy_container_gis_string(proxy->container, filename_quark, &filename)) {
        bname = g_path_get_basename(filename);
        title = g_strdup_printf(_("Messages for %s"), bname);
        g_free(bname);
    }
    else
        title = g_strdup(_("Messages for Untitled"));

    proxy->message_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    window = GTK_WINDOW(proxy->message_window);
    gtk_window_set_title(window, title);
    g_free(title);
    gtk_window_set_default_size(window, 480, 320);

    textbuf = proxy->message_textbuf = _gwy_app_log_create_textbuf();
    logview = gtk_text_view_new_with_buffer(textbuf);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(logview), FALSE);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(scwin), logview);
    gtk_widget_show_all(scwin);

    gtk_container_add(GTK_CONTAINER(window), scwin);

    gwy_app_add_main_accel_group(window);
    g_signal_connect(textbuf, "changed", G_CALLBACK(message_log_updated), logview);
    g_signal_connect(window, "key-press-event", G_CALLBACK(message_log_key_pressed), NULL);
    g_object_weak_ref(G_OBJECT(window), message_log_window_destroyed, proxy);
}

static void
gwy_app_data_browser_show_hide_messages(GtkToggleButton *toggle,
                                        GwyAppDataBrowser *browser)
{
    GwyAppDataProxy *proxy = browser->current;
    gboolean active = gtk_toggle_button_get_active(toggle);
    gboolean have_window = proxy && proxy->message_window;

    if (!active == !have_window)
        return;

    if (have_window)
        gtk_widget_destroy(proxy->message_window);
    else {
        create_message_log_window(proxy);
        update_messages_textbuf_since(proxy, 0);
        gtk_window_present(GTK_WINDOW(proxy->message_window));
    }
}

static void
gwy_app_data_proxy_destroy_messages(GwyAppDataProxy *proxy)
{
    GArray *messages = proxy->messages;
    guint i;

    if (proxy->message_window)
        gtk_widget_destroy(proxy->message_window);

    if (!messages)
        return;

    for (i = 0; i < messages->len; i++)
        g_free(g_array_index(messages, GwyAppLogMessage, i).message);
    g_array_free(messages, TRUE);
    proxy->messages = NULL;
}

static void
try_to_fix_data_window_size(GwyAppDataProxy *proxy, GtkTreeIter *iter,
                            GwyAppPage pageno)
{
    GtkTreeModel *model;
    GtkWidget *data_view, *data_window;

    model = GTK_TREE_MODEL(proxy->lists[pageno].store);
    gtk_tree_model_get(model, iter, MODEL_WIDGET, &data_view, -1);
    if (!data_view)
        return;

    data_window = gtk_widget_get_ancestor(data_view, GWY_TYPE_DATA_WINDOW);
    g_object_unref(data_view);   /* model-get */
    if (!data_window)
        return;

    gwy_data_window_fit_to_screen(GWY_DATA_WINDOW(data_window));
}

/**
 * gwy_app_data_browser_set_keep_invisible:
 * @data: A data container.
 * @keep_invisible: %TRUE to retain @data in the browser even when it becomes inaccessible, %FALSE to dispose of it.
 *
 * Sets data browser behaviour for inaccessible data.
 *
 * Normally, when all visual objects belonging to a file are closed the container is removed from the data browser and
 * dereferenced, leading to its finalization.  By setting @keep_invisible to %TRUE the container can be made to sit in
 * the browser indefinitely.
 **/
void
gwy_app_data_browser_set_keep_invisible(GwyContainer *data,
                                        gboolean keep_invisible)
{
    GwyAppDataProxy *proxy;

    proxy = gwy_app_data_browser_get_proxy(gwy_app_get_data_browser(), data);
    g_return_if_fail(proxy);
    proxy->keep_invisible = keep_invisible;
}

/**
 * gwy_app_data_browser_get_keep_invisible:
 * @data: A data container.
 *
 * Gets data browser behaviour for inaccessible data.
 *
 * Returns: See gwy_app_data_browser_set_keep_invisible().
 **/
gboolean
gwy_app_data_browser_get_keep_invisible(GwyContainer *data)
{
    GwyAppDataProxy *proxy;

    proxy = gwy_app_data_browser_get_proxy(gwy_app_get_data_browser(), data);
    g_return_val_if_fail(proxy, FALSE);

    return proxy->keep_invisible;
}

/**
 * gwy_app_data_browser_add_data_field:
 * @dfield: A data field to add.
 * @data: A data container to add @dfield to. It can be %NULL to add the data field to current data container.
 * @showit: %TRUE to display it immediately, %FALSE to just add it.
 *
 * Adds a data field to a data container.
 *
 * The data browser takes a reference to @dfield so usually you will want to release your reference, especially when
 * done as the ‘create output’ step of a module function.
 *
 * Returns: The id of the data field in the container.
 **/
gint
gwy_app_data_browser_add_data_field(GwyDataField *dfield,
                                    GwyContainer *data,
                                    gboolean showit)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GtkTreeIter iter;
    GQuark quark;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(dfield), -1);
    g_return_val_if_fail(!data || GWY_IS_CONTAINER(data), -1);

    browser = gwy_app_get_data_browser();
    if (data)
        proxy = gwy_app_data_browser_get_proxy(browser, data);
    else
        proxy = browser->current;
    if (!proxy) {
        g_critical("Data container is unknown to data browser.");
        return -1;
    }

    list = &proxy->lists[GWY_PAGE_CHANNELS];
    quark = gwy_app_get_data_key_for_id(list->last + 1);
    /* This invokes "item-changed" callback that will finish the work.
     * Among other things, it will update proxy->lists[GWY_PAGE_CHANNELS].last.
     */
    gwy_container_set_object(proxy->container, quark, dfield);

    if (showit && !gui_disabled) {
        gwy_app_data_proxy_find_object(list->store, list->last, &iter);
        proxy->resetting_visibility = TRUE;
        /* XXX: It is kind of bad doing this here, because settings like realsquare will be only set later.  The
         * caller, rather logically on his part, waits for the new id to set them.  So size calculations will occur
         * too soon, etc.  I cannot see any way to fix it without breaking the way it's used in everymodule.  */
        gwy_app_data_proxy_channel_set_visible(proxy, &iter, TRUE);
        proxy->resetting_visibility = FALSE;
    }

    return list->last;
}

/**
 * gwy_app_data_browser_add_graph_model:
 * @gmodel: A graph model to add.
 * @data: A data container to add @gmodel to.
 *        It can be %NULL to add the graph model to current data container.
 * @showit: %TRUE to display it immediately, %FALSE to just add it.
 *
 * Adds a graph model to a data container.
 *
 * The data browser takes a reference to @gmodel so usually you will want to release your reference, especially when
 * done as the ‘create output’ step of a module function.
 *
 * Returns: The id of the graph model in the container.
 **/
gint
gwy_app_data_browser_add_graph_model(GwyGraphModel *gmodel,
                                     GwyContainer *data,
                                     gboolean showit)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GtkTreeIter iter;
    GQuark quark;

    g_return_val_if_fail(GWY_IS_GRAPH_MODEL(gmodel), -1);
    g_return_val_if_fail(!data || GWY_IS_CONTAINER(data), -1);

    browser = gwy_app_get_data_browser();
    if (data)
        proxy = gwy_app_data_browser_get_proxy(browser, data);
    else
        proxy = browser->current;
    if (!proxy) {
        g_critical("Data container is unknown to data browser.");
        return -1;
    }

    list = &proxy->lists[GWY_PAGE_GRAPHS];
    quark = gwy_app_get_graph_key_for_id(list->last + 1);
    /* This invokes "item-changed" callback that will finish the work.
     * Among other things, it will update proxy->lists[GWY_PAGE_GRAPHS].last. */
    gwy_container_set_object(proxy->container, quark, gmodel);

    if (showit && !gui_disabled) {
        gwy_app_data_proxy_find_object(list->store, list->last, &iter);
        proxy->resetting_visibility = TRUE;
        gwy_app_data_proxy_graph_set_visible(proxy, &iter, TRUE);
        proxy->resetting_visibility = FALSE;
    }

    return list->last;
}

/**
 * gwy_app_data_browser_add_spectra:
 * @spectra: A spectra object to add.
 * @data: A data container to add @gmodel to. It can be %NULL to add the spectra to current data container.
 * @showit: %TRUE to display it immediately, %FALSE to just add it.
 *
 * Adds a spectra object to a data container.
 *
 * The data browser takes a reference to @spectra so usually you will want to release your reference, especially when
 * done as the ‘create output’ step of a module function.
 *
 * Returns: The id of the spectra object in the container.
 *
 * Since: 2.7
 **/
gint
gwy_app_data_browser_add_spectra(GwySpectra *spectra,
                                 GwyContainer *data,
                                 gboolean showit)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GtkTreeIter iter;
    GQuark quark;

    g_return_val_if_fail(GWY_IS_SPECTRA(spectra), -1);
    g_return_val_if_fail(!data || GWY_IS_CONTAINER(data), -1);

    browser = gwy_app_get_data_browser();
    if (data)
        proxy = gwy_app_data_browser_get_proxy(browser, data);
    else
        proxy = browser->current;
    if (!proxy) {
        g_critical("Data container is unknown to data browser.");
        return -1;
    }

    list = &proxy->lists[GWY_PAGE_SPECTRA];
    quark = gwy_app_get_spectra_key_for_id(list->last + 1);
    /* This invokes "item-changed" callback that will finish the work.
     * Among other things, it will update proxy->lists[GWY_PAGE_SPECTRA].last. */
    gwy_container_set_object(proxy->container, quark, spectra);

    if (showit && !gui_disabled) {
        gwy_app_data_proxy_find_object(list->store, list->last, &iter);
        /* FIXME */
        g_warning("Cannot make spectra visible");
        /* gwy_app_data_proxy_spectra_set_visible(proxy, &iter, TRUE); */
    }

    return list->last;
}

/**
 * gwy_app_data_browser_add_brick:
 * @brick: A data brick to add.
 * @preview: Preview data field.  It may be %NULL to create a
 *           preview automatically.  If non-%NULL, its dimensions
 *           should match those of brick planes.  You must
 *           <emphasis>not</emphasis> pass a field which already represents a
 *           channel.  If you want a to show the same field as an existing
 *           channel you must create a copy with gwy_data_field_duplicate().
 * @data: A data container to add @brick to. It can be %NULL to add the data field to current data container.
 * @showit: %TRUE to display it immediately, %FALSE to just add it.
 *
 * Adds a volume data brick to a data container.
 *
 * The data browser takes a reference to @brick (and @preview if given) so usually you will want to release your
 * reference, especially when done as the ‘create output’ step of a module function.
 *
 * Returns: The id of the data brick in the container.
 *
 * Since: 2.32
 **/
gint
gwy_app_data_browser_add_brick(GwyBrick *brick,
                               GwyDataField *preview,
                               GwyContainer *data,
                               gboolean showit)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GtkTreeIter iter;
    GQuark quark;
    gint xres, yres;
    gboolean must_free_preview = FALSE;

    g_return_val_if_fail(GWY_IS_BRICK(brick), -1);
    g_return_val_if_fail(!data || GWY_IS_CONTAINER(data), -1);
    g_return_val_if_fail(!preview || GWY_IS_DATA_FIELD(preview), -1);

    browser = gwy_app_get_data_browser();
    if (data)
        proxy = gwy_app_data_browser_get_proxy(browser, data);
    else
        proxy = browser->current;
    if (!proxy) {
        g_critical("Data container is unknown to data browser.");
        return -1;
    }

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    if (preview) {
        if (gwy_data_field_get_xres(preview) != xres || gwy_data_field_get_yres(preview) != yres) {
            g_warning("Preview field dimensions differ from brick plane dimensions.");
            /* XXX: With some care this may actually work.  But we do not consider it sane anyway. */
        }
    }
    else {
        preview = _gwy_app_create_brick_preview_field(brick);
        must_free_preview = TRUE;
    }

    list = &proxy->lists[GWY_PAGE_VOLUMES];
    quark = gwy_app_get_brick_key_for_id(list->last + 1);
    /* This invokes "item-changed" callback that will finish the work.
     * Among other things, it will update proxy->lists[GWY_PAGE_VOLUMES].last. */
    gwy_container_set_object(proxy->container, quark, brick);

    quark = gwy_app_get_brick_preview_key_for_id(list->last);
    gwy_container_set_object(proxy->container, quark, preview);
    if (must_free_preview)
        g_object_unref(preview);

    if (showit && !gui_disabled) {
        gwy_app_data_proxy_find_object(list->store, list->last, &iter);
        proxy->resetting_visibility = TRUE;
        gwy_app_data_proxy_brick_set_visible(proxy, &iter, TRUE);
        proxy->resetting_visibility = FALSE;
    }

    return list->last;
}

/**
 * gwy_app_data_browser_add_surface:
 * @surface: XYZ surface data to add.
 * @data: A data container to add @surface to. It can be %NULL to add the data field to current data container.
 * @showit: %TRUE to display it immediately, %FALSE to just add it.
 *
 * Adds XYZ surface data to a data container.
 *
 * The data browser takes a reference to @surface so usually you will want to release your reference, especially when
 * done as the ‘create output’ step of a module function.
 *
 * Returns: The id of the data surface in the container.
 *
 * Since: 2.45
 **/
gint
gwy_app_data_browser_add_surface(GwySurface *surface,
                                 GwyContainer *data,
                                 gboolean showit)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GwyDataField *raster;
    GtkTreeIter iter;
    GQuark quark;

    g_return_val_if_fail(GWY_IS_SURFACE(surface), -1);
    g_return_val_if_fail(!data || GWY_IS_CONTAINER(data), -1);

    browser = gwy_app_get_data_browser();
    if (data)
        proxy = gwy_app_data_browser_get_proxy(browser, data);
    else
        proxy = browser->current;
    if (!proxy) {
        g_critical("Data container is unknown to data browser.");
        return -1;
    }

    list = &proxy->lists[GWY_PAGE_XYZS];
    quark = gwy_app_get_surface_key_for_id(list->last + 1);
    /* This invokes "item-changed" callback that will finish the work.
     * Among other things, it will update proxy->lists[GWY_PAGE_XYZS].last. */
    gwy_container_set_object(proxy->container, quark, surface);

    raster = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    gwy_preview_surface_to_datafield(surface, raster, SURFACE_PREVIEW_SIZE, SURFACE_PREVIEW_SIZE, 0);
    quark = gwy_app_get_surface_preview_key_for_id(list->last);
    gwy_container_set_object(proxy->container, quark, raster);
    g_object_unref(raster);

    if (showit && !gui_disabled) {
        gwy_app_data_proxy_find_object(list->store, list->last, &iter);
        proxy->resetting_visibility = TRUE;
        gwy_app_data_proxy_surface_set_visible(proxy, &iter, TRUE);
        proxy->resetting_visibility = FALSE;
    }

    return list->last;
}

/**
 * gwy_app_data_browser_add_lawn:
 * @lawn: #GwyLawn to add.
 * @preview: Preview data field.  It may be %NULL to create a
 *           preview automatically.  If non-%NULL, its dimensions
 *           should match those of lawn planes.  You must
 *           <emphasis>not</emphasis> pass a field which already represents a
 *           channel.  If you want a to show the same field as an existing
 *           channel you must create a copy with gwy_data_field_duplicate().
 * @data: A data container to add @lawn to.
 *        It can be %NULL to add the data field to current data container.
 * @showit: %TRUE to display it immediately, %FALSE to just add it.
 *
 * Adds #GwyLawn curve map data to a data container.
 *
 * The data browser takes a reference to @lawn so usually you will want to release your reference, especially when
 * done as the ‘create output’ step of a module function.
 *
 * Returns: The id of the data lawn in the container.
 *
 * Since: 2.60
 **/
gint
gwy_app_data_browser_add_lawn(GwyLawn *lawn,
                              GwyDataField *preview,
                              GwyContainer *data,
                              gboolean showit)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GtkTreeIter iter;
    GQuark quark;
    gint xres, yres;
    gboolean must_free_preview = FALSE;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), -1);
    g_return_val_if_fail(!data || GWY_IS_CONTAINER(data), -1);

    browser = gwy_app_get_data_browser();
    if (data)
        proxy = gwy_app_data_browser_get_proxy(browser, data);
    else
        proxy = browser->current;
    if (!proxy) {
        g_critical("Data container is unknown to data browser.");
        return -1;
    }

    xres = gwy_lawn_get_xres(lawn);
    yres = gwy_lawn_get_yres(lawn);
    if (preview) {
        if (gwy_data_field_get_xres(preview) != xres || gwy_data_field_get_yres(preview) != yres) {
            g_warning("Preview field dimensions differ from lawn plane dimensions.");
            /* XXX: With some care this may actually work.  But we do not consider it sane anyway. */
        }
    }
    else {
        preview = _gwy_app_create_lawn_preview_field(lawn);
        must_free_preview = TRUE;
    }

    list = &proxy->lists[GWY_PAGE_CURVE_MAPS];
    quark = gwy_app_get_lawn_key_for_id(list->last + 1);
    /* This invokes "item-changed" callback that will finish the work.
     * Among other things, it will update proxy->lists[GWY_PAGE_CURVE_MAPS].last. */
    gwy_container_set_object(proxy->container, quark, lawn);

    quark = gwy_app_get_lawn_preview_key_for_id(list->last);
    gwy_container_set_object(proxy->container, quark, preview);
    if (must_free_preview)
        g_object_unref(preview);

    if (showit && !gui_disabled) {
        gwy_app_data_proxy_find_object(list->store, list->last, &iter);
        proxy->resetting_visibility = TRUE;
        gwy_app_data_proxy_lawn_set_visible(proxy, &iter, TRUE);
        proxy->resetting_visibility = FALSE;
    }

    return list->last;
}

/**
 * gwy_app_data_browser_get_current:
 * @what: First information about current objects to obtain.
 * @...: pointer to store the information to (object pointer for objects, #GQuark pointer for keys, #gint pointer for
 *       ids), followed by 0-terminated list of #GwyAppWhat, pointer couples.
 *
 * Gets information about current objects.
 *
 * All output arguments are always set to some value, even if the requested object does not exist.  Object arguments
 * are set to pointer to the object if it exists (no reference is added), or cleared to %NULL if no such object
 * exists.
 *
 * Quark arguments are set to the corresponding key even if no such object is actually present (use object arguments
 * to check for object presence) but the location where it would be stored is known.  This is common with
 * presentations and masks.  They are be set to 0 if no corresponding location exists -- for example, when the current
 * mask key is requested but the current data contains no channel (or there is no current data at all).
 *
 * The rules for id arguments are similar to quarks, except they are set to -1 to indicate undefined result.
 *
 * The current objects can change due to user interaction even during the execution of modal dialogs (typically used
 * by modules).  Therefore to achieve consistency one has to ask for the complete set of current objects at once.
 **/
void
gwy_app_data_browser_get_current(GwyAppWhat what,
                                 ...)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *current = NULL;
    GwyAppDataList *channels = NULL, *graphs = NULL, *spectras = NULL, *volumes = NULL, *xyzs = NULL, *cmaps = NULL;
    GtkTreeIter iter;
    GObject *object, **otarget;
    /* Cache the current object by type */
    GObject *dfield = NULL, *gmodel = NULL, *spectra = NULL, *brick = NULL, *surface = NULL;
    GQuark quark, *qtarget;
    gint *itarget;
    va_list ap;

    if (!what)
        return;

    va_start(ap, what);
    browser = gwy_app_data_browser;
    if (browser) {
        current = browser->current;
        if (current) {
            channels = &current->lists[GWY_PAGE_CHANNELS];
            graphs = &current->lists[GWY_PAGE_GRAPHS];
            spectras = &current->lists[GWY_PAGE_SPECTRA];
            volumes = &current->lists[GWY_PAGE_VOLUMES];
            xyzs = &current->lists[GWY_PAGE_XYZS];
            cmaps = &current->lists[GWY_PAGE_CURVE_MAPS];
        }
    }

    do {
        switch (what) {
            case GWY_APP_CONTAINER:
            otarget = va_arg(ap, GObject**);
            *otarget = current ? G_OBJECT(current->container) : NULL;
            break;

            case GWY_APP_CONTAINER_ID:
            itarget = va_arg(ap, gint*);
            *itarget = current ? current->data_no : 0;
            break;

            case GWY_APP_PAGE:
            /* Return NOPAGE when we have no data. */
            itarget = va_arg(ap, GwyAppPage*);
            *itarget = current ? browser->active_page : GWY_PAGE_NOPAGE;
            break;

            case GWY_APP_DATA_VIEW:
            otarget = va_arg(ap, GObject**);
            *otarget = NULL;
            if (channels && gwy_app_data_proxy_find_object(channels->store, channels->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(channels->store), &iter, MODEL_WIDGET, otarget, -1);
                if (*otarget)
                    g_object_unref(*otarget);
            }
            break;

            case GWY_APP_GRAPH:
            otarget = va_arg(ap, GObject**);
            *otarget = NULL;
            if (graphs && gwy_app_data_proxy_find_object(graphs->store, graphs->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(graphs->store), &iter, MODEL_WIDGET, otarget, -1);
                if (*otarget)
                    g_object_unref(*otarget);
            }
            break;

            case GWY_APP_VOLUME_VIEW:
            otarget = va_arg(ap, GObject**);
            *otarget = NULL;
            if (volumes && gwy_app_data_proxy_find_object(volumes->store, volumes->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(volumes->store), &iter, MODEL_WIDGET, otarget, -1);
                if (*otarget)
                    g_object_unref(*otarget);
            }
            break;

            case GWY_APP_XYZ_VIEW:
            otarget = va_arg(ap, GObject**);
            *otarget = NULL;
            if (xyzs
                && gwy_app_data_proxy_find_object(xyzs->store, xyzs->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(xyzs->store), &iter, MODEL_WIDGET, otarget, -1);
                if (*otarget)
                    g_object_unref(*otarget);
            }
            break;

            case GWY_APP_CURVE_MAP_VIEW:
            otarget = va_arg(ap, GObject**);
            *otarget = NULL;
            if (cmaps && gwy_app_data_proxy_find_object(cmaps->store, cmaps->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(cmaps->store), &iter, MODEL_WIDGET, otarget, -1);
                if (*otarget)
                    g_object_unref(*otarget);
            }
            break;

            case GWY_APP_DATA_FIELD:
            case GWY_APP_DATA_FIELD_KEY:
            case GWY_APP_DATA_FIELD_ID:
            case GWY_APP_MASK_FIELD:
            case GWY_APP_MASK_FIELD_KEY:
            case GWY_APP_SHOW_FIELD:
            case GWY_APP_SHOW_FIELD_KEY:
            if (!dfield && current && gwy_app_data_proxy_find_object(channels->store, channels->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(channels->store), &iter, MODEL_OBJECT, &object, -1);
                dfield = object;
                g_object_unref(object);
            }
            else
                quark = 0;
            switch (what) {
                case GWY_APP_DATA_FIELD:
                otarget = va_arg(ap, GObject**);
                *otarget = dfield;
                break;

                case GWY_APP_DATA_FIELD_KEY:
                qtarget = va_arg(ap, GQuark*);
                *qtarget = 0;
                if (dfield)
                    *qtarget = GPOINTER_TO_UINT(g_object_get_qdata(dfield, own_key_quark));
                break;

                case GWY_APP_DATA_FIELD_ID:
                itarget = va_arg(ap, gint*);
                *itarget = dfield ? channels->active : -1;
                break;

                case GWY_APP_MASK_FIELD:
                otarget = va_arg(ap, GObject**);
                *otarget = NULL;
                if (dfield) {
                    quark = gwy_app_get_mask_key_for_id(channels->active);
                    gwy_container_gis_object(current->container, quark, otarget);
                }
                break;

                case GWY_APP_MASK_FIELD_KEY:
                qtarget = va_arg(ap, GQuark*);
                *qtarget = 0;
                if (dfield)
                    *qtarget = gwy_app_get_mask_key_for_id(channels->active);
                break;

                case GWY_APP_SHOW_FIELD:
                otarget = va_arg(ap, GObject**);
                *otarget = NULL;
                if (dfield) {
                    quark = gwy_app_get_show_key_for_id(channels->active);
                    gwy_container_gis_object(current->container, quark, otarget);
                }
                break;

                case GWY_APP_SHOW_FIELD_KEY:
                qtarget = va_arg(ap, GQuark*);
                *qtarget = 0;
                if (dfield)
                    *qtarget = gwy_app_get_show_key_for_id(channels->active);
                break;

                default:
                /* Hi, gcc */
                break;
            }
            break;

            case GWY_APP_GRAPH_MODEL:
            case GWY_APP_GRAPH_MODEL_KEY:
            case GWY_APP_GRAPH_MODEL_ID:
            if (!gmodel && current && gwy_app_data_proxy_find_object(graphs->store, graphs->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(graphs->store), &iter, MODEL_OBJECT, &object, -1);
                gmodel = object;
                g_object_unref(object);
            }
            switch (what) {
                case GWY_APP_GRAPH_MODEL:
                otarget = va_arg(ap, GObject**);
                *otarget = gmodel;
                break;

                case GWY_APP_GRAPH_MODEL_KEY:
                qtarget = va_arg(ap, GQuark*);
                *qtarget = 0;
                if (gmodel)
                    *qtarget = GPOINTER_TO_UINT(g_object_get_qdata(gmodel, own_key_quark));
                break;

                case GWY_APP_GRAPH_MODEL_ID:
                itarget = va_arg(ap, gint*);
                *itarget = gmodel ? graphs->active : -1;
                break;

                default:
                /* Hi, gcc */
                break;
            }
            break;

            case GWY_APP_SPECTRA:
            case GWY_APP_SPECTRA_KEY:
            case GWY_APP_SPECTRA_ID:
            if (!spectra && current && gwy_app_data_proxy_find_object(spectras->store, spectras->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(spectras->store), &iter, MODEL_OBJECT, &object, -1);
                spectra = object;
                g_object_unref(object);
            }
            switch (what) {
                case GWY_APP_SPECTRA:
                otarget = va_arg(ap, GObject**);
                *otarget = spectra;
                break;

                case GWY_APP_SPECTRA_KEY:
                qtarget = va_arg(ap, GQuark*);
                *qtarget = 0;
                if (spectra)
                    *qtarget = GPOINTER_TO_UINT(g_object_get_qdata(spectra, own_key_quark));
                break;

                case GWY_APP_SPECTRA_ID:
                itarget = va_arg(ap, gint*);
                *itarget = spectra ? spectras->active : -1;
                break;

                default:
                /* Hi, gcc */
                break;
            }
            break;

            case GWY_APP_BRICK:
            case GWY_APP_BRICK_KEY:
            case GWY_APP_BRICK_ID:
            if (!brick && current && gwy_app_data_proxy_find_object(volumes->store, volumes->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(volumes->store), &iter, MODEL_OBJECT, &object, -1);
                brick = object;
                g_object_unref(object);
            }
            switch (what) {
                case GWY_APP_BRICK:
                otarget = va_arg(ap, GObject**);
                *otarget = brick;
                break;

                case GWY_APP_BRICK_KEY:
                qtarget = va_arg(ap, GQuark*);
                *qtarget = 0;
                if (brick)
                    *qtarget = GPOINTER_TO_UINT(g_object_get_qdata(brick, own_key_quark));
                break;

                case GWY_APP_BRICK_ID:
                itarget = va_arg(ap, gint*);
                *itarget = brick ? volumes->active : -1;
                break;

                default:
                /* Hi, gcc */
                break;
            }
            break;

            case GWY_APP_SURFACE:
            case GWY_APP_SURFACE_KEY:
            case GWY_APP_SURFACE_ID:
            if (!surface && current && gwy_app_data_proxy_find_object(xyzs->store, xyzs->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(xyzs->store), &iter, MODEL_OBJECT, &object, -1);
                surface = object;
                g_object_unref(object);
            }
            switch (what) {
                case GWY_APP_SURFACE:
                otarget = va_arg(ap, GObject**);
                *otarget = surface;
                break;

                case GWY_APP_SURFACE_KEY:
                qtarget = va_arg(ap, GQuark*);
                *qtarget = 0;
                if (surface)
                    *qtarget = GPOINTER_TO_UINT(g_object_get_qdata(surface, own_key_quark));
                break;

                case GWY_APP_SURFACE_ID:
                itarget = va_arg(ap, gint*);
                *itarget = surface ? xyzs->active : -1;
                break;

                default:
                /* Hi, gcc */
                break;
            }
            break;

            case GWY_APP_LAWN:
            case GWY_APP_LAWN_KEY:
            case GWY_APP_LAWN_ID:
            if (!surface && current && gwy_app_data_proxy_find_object(cmaps->store, cmaps->active, &iter)) {
                gtk_tree_model_get(GTK_TREE_MODEL(cmaps->store), &iter, MODEL_OBJECT, &object, -1);
                surface = object;
                g_object_unref(object);
            }
            switch (what) {
                case GWY_APP_LAWN:
                otarget = va_arg(ap, GObject**);
                *otarget = surface;
                break;

                case GWY_APP_LAWN_KEY:
                qtarget = va_arg(ap, GQuark*);
                *qtarget = 0;
                if (surface)
                    *qtarget = GPOINTER_TO_UINT(g_object_get_qdata(surface, own_key_quark));
                break;

                case GWY_APP_LAWN_ID:
                itarget = va_arg(ap, gint*);
                *itarget = surface ? cmaps->active : -1;
                break;

                default:
                /* Hi, gcc */
                break;
            }
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while ((what = va_arg(ap, GwyAppWhat)));

    va_end(ap);
}

static gboolean
title_matches_pattern(GwyContainer *data,
                      GwyAppPage pageno,
                      gint id,
                      GPatternSpec *pattern)
{
    gboolean ok = FALSE;
    GQuark quark;
    GObject *object;
    gchar *title;

    if (!pattern)
        return TRUE;

    if (pageno == GWY_PAGE_CHANNELS)
        title = _gwy_app_figure_out_channel_title(data, id);
    else if (pageno == GWY_PAGE_VOLUMES)
        title = gwy_app_get_brick_title(data, id);
    else if (pageno == GWY_PAGE_XYZS)
        title = gwy_app_get_surface_title(data, id);
    else if (pageno == GWY_PAGE_CURVE_MAPS)
        title = gwy_app_get_lawn_title(data, id);
    else if (pageno == GWY_PAGE_GRAPHS || pageno == GWY_PAGE_SPECTRA) {
        if (pageno == GWY_PAGE_GRAPHS)
            quark = gwy_app_get_graph_key_for_id(id);
        else
            quark = gwy_app_get_spectra_key_for_id(id);

        object = gwy_container_get_object(data, quark);
        g_object_get(object, "title", &title, NULL);
    }
    else {
        g_return_val_if_reached(FALSE);
    }

    ok = g_pattern_match_string(pattern, title);
    g_free(title);

    return ok;
}

static gint*
gwy_app_data_list_get_object_ids(GwyContainer *data,
                                 GwyAppPage pageno,
                                 const gchar *titleglob)
{
    GPatternSpec *pattern = NULL;
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint *ids = NULL;
    gint n, j;

    browser = gwy_app_get_data_browser();
    proxy = gwy_app_data_browser_get_proxy(browser, data);
    if (titleglob)
        pattern = g_pattern_spec_new(titleglob);

    if (proxy) {
        model = GTK_TREE_MODEL(proxy->lists[pageno].store);
        n = gtk_tree_model_iter_n_children(model, NULL);
        ids = g_new(gint, n+1);
        if (n) {
            n = 0;
            gtk_tree_model_get_iter_first(model, &iter);
            do {
                gtk_tree_model_get(model, &iter, MODEL_ID, ids + n, -1);
                if (title_matches_pattern(data, pageno, ids[n], pattern))
                    n++;
            } while (gtk_tree_model_iter_next(model, &iter));
        }
    }
    else {
        struct {
            GwyAppPage page;
            GwyAppKeyType keytype;
            GType objtype;
        }
        page2key[] = {
            { GWY_PAGE_CHANNELS,   KEY_IS_DATA,    GWY_TYPE_DATA_FIELD,  },
            { GWY_PAGE_GRAPHS,     KEY_IS_GRAPH,   GWY_TYPE_GRAPH_MODEL, },
            { GWY_PAGE_VOLUMES,    KEY_IS_BRICK,   GWY_TYPE_BRICK,       },
            { GWY_PAGE_XYZS,       KEY_IS_SURFACE, GWY_TYPE_SURFACE,     },
            { GWY_PAGE_CURVE_MAPS, KEY_IS_LAWN,    GWY_TYPE_LAWN,        },
            { GWY_PAGE_SPECTRA,    KEY_IS_SPECTRA, GWY_TYPE_SPECTRA,     },
        };
        for (n = 0; n < G_N_ELEMENTS(page2key); n++) {
            if (pageno == page2key[n].page) {
                ids = _gwy_app_find_ids_unmanaged(data, page2key[n].keytype, page2key[n].objtype);
                break;
            }
        }
        g_return_val_if_fail(ids, NULL);
        for (j = n = 0; ids[j] != -1; j++) {
            if (title_matches_pattern(data, pageno, ids[j], pattern))
                ids[n++] = ids[j];
        }
    }
    ids[n] = -1;

    if (pattern)
        g_pattern_spec_free(pattern);

    return ids;
}

/**
 * gwy_app_data_browser_get_data_ids:
 * @data: A data container.
 *
 * Gets the list of all channels in a data container.
 *
 * The function originally could be used only for data containers managed by the data browser.  Since version 2.45 it
 * can be used for all file-like data containers.
 *
 * Returns: A newly allocated array with channel ids, -1 terminated.
 **/
gint*
gwy_app_data_browser_get_data_ids(GwyContainer *data)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_CHANNELS, NULL);
}

/**
 * gwy_app_data_browser_get_graph_ids:
 * @data: A data container.
 *
 * Gets the list of all graphs in a data container.
 *
 * The function originally could be used only for data containers managed by the data browser.  Since version 2.45 it
 * can be used for all file-like data containers.
 *
 * Returns: A newly allocated array with graph ids, -1 terminated.
 **/
gint*
gwy_app_data_browser_get_graph_ids(GwyContainer *data)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_GRAPHS, NULL);
}

/**
 * gwy_app_data_browser_get_spectra_ids:
 * @data: A data container.
 *
 * Gets the list of all spectra in a data container.
 *
 * The function originally could be used only for data containers managed by the data browser.  Since version 2.45 it
 * can be used for all file-like data containers.
 *
 * Returns: A newly allocated array with spectrum ids, -1 terminated.
 *
 * Since: 2.7
 **/
gint*
gwy_app_data_browser_get_spectra_ids(GwyContainer *data)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_SPECTRA, NULL);
}

/**
 * gwy_app_data_browser_get_volume_ids:
 * @data: A data container.
 *
 * Gets the list of all volume data in a data container.
 *
 * The function originally could be used only for data containers managed by the data browser.  Since version 2.45 it
 * can be used for all file-like data containers.
 *
 * Returns: A newly allocated array with volume data ids, -1 terminated.
 *
 * Since: 2.33
 **/
gint*
gwy_app_data_browser_get_volume_ids(GwyContainer *data)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_VOLUMES, NULL);
}

/**
 * gwy_app_data_browser_get_xyz_ids:
 * @data: A data container.
 *
 * Gets the list of all XYZ data in a data container.
 *
 * Returns: A newly allocated array with XYZ data ids, -1 terminated.
 *
 * Since: 2.45
 **/
gint*
gwy_app_data_browser_get_xyz_ids(GwyContainer *data)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_XYZS, NULL);
}

/**
 * gwy_app_data_browser_get_curve_map_ids:
 * @data: A data container.
 *
 * Gets the list of all #GwyLawn curve map data in a data container.
 *
 * Returns: A newly allocated array with #GwyLawn curve map data ids, -1 terminated.
 *
 * Since: 2.60
 **/
gint*
gwy_app_data_browser_get_curve_map_ids(GwyContainer *data)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_CURVE_MAPS, NULL);
}

static GtkWindow*
find_window_for_id(GwyContainer *data,
                   GwyAppPage pageno,
                   gint id)
{
    GtkWidget *view = NULL, *window;
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GwyAppDataList *list;
    GtkTreeModel *model;
    GtkTreeIter iter;

    browser = gwy_app_data_browser;
    if (!browser)
        return NULL;

    proxy = gwy_app_data_browser_get_proxy(browser, data);
    if (!proxy)
        return NULL;

    list = &proxy->lists[pageno];
    model = GTK_TREE_MODEL(list->store);
    if (id >= 0) {
        if (!gwy_app_data_proxy_find_object(list->store, id, &iter))
            return NULL;

        gtk_tree_model_get(model, &iter, MODEL_WIDGET, &view, -1);
    }
    else {
        if (!gtk_tree_model_get_iter_first(model, &iter))
            return NULL;

        do {
            gtk_tree_model_get(model, &iter, MODEL_WIDGET, &view, -1);
            if (view)
                break;
        } while (gtk_tree_model_iter_next(model, &iter));
    }

    if (!view)
        return NULL;

    if (pageno == GWY_PAGE_GRAPHS) {
        window = gtk_widget_get_ancestor(view, GWY_TYPE_GRAPH_WINDOW);
        g_object_unref(view);
    }
    else {
        window = gtk_widget_get_ancestor(view, GWY_TYPE_DATA_WINDOW);
        g_object_unref(view);
    }

    return window ? GTK_WINDOW(window) : NULL;
}

/**
 * gwy_app_find_window_for_channel:
 * @data: A data container to find window for.
 * @id: Data channel id.  It can be -1 to find any data window displaying a channel from @data.
 *
 * Finds the window displaying a data channel.
 *
 * Returns: The window if found, %NULL if no data window displays the requested channel.
 **/
GtkWindow*
gwy_app_find_window_for_channel(GwyContainer *data,
                                gint id)
{
    return find_window_for_id(data, GWY_PAGE_CHANNELS, id);
}

/**
 * gwy_app_find_window_for_graph:
 * @data: A data container to find window for.
 * @id: Graph model id.  It can be -1 to find any graph window displaying a graph model from @data.
 *
 * Finds the window displaying a graph model.
 *
 * Returns: The window if found, %NULL if no graph window displays the requested channel.
 *
 * Since: 2.45
 **/
GtkWindow*
gwy_app_find_window_for_graph(GwyContainer *data,
                              gint id)
{
    return find_window_for_id(data, GWY_PAGE_GRAPHS, id);
}

/**
 * gwy_app_find_window_for_volume:
 * @data: A data container to find window for.
 * @id: Volume data id.  It can be -1 to find any data window displaying volume data from @data.
 *
 * Finds the window displaying given volume data.
 *
 * Returns: The window if found, %NULL if no data window displays the requested volume data.
 *
 * Since: 2.42
 **/
GtkWindow*
gwy_app_find_window_for_volume(GwyContainer *data,
                               gint id)
{
    return find_window_for_id(data, GWY_PAGE_VOLUMES, id);
}

/**
 * gwy_app_find_window_for_xyz:
 * @data: A data container to find window for.
 * @id: XYZ data id.  It can be -1 to find any data window displaying XYZ data from @data.
 *
 * Finds the window displaying given XYZ data.
 *
 * Returns: The window if found, %NULL if no data window displays the requested XYZ data.
 *
 * Since: 2.45
 **/
GtkWindow*
gwy_app_find_window_for_xyz(GwyContainer *data,
                            gint id)
{
    return find_window_for_id(data, GWY_PAGE_XYZS, id);
}

/**
 * gwy_app_find_window_for_curve_map:
 * @data: A data container to find window for.
 * @id: curve map id.  It can be -1 to find any data window displaying curve map from @data.
 *
 * Finds the window displaying given curve map.
 *
 * Returns: The window if found, %NULL if no data window displays the requested curve map.
 *
 * Since: 2.60
 **/
GtkWindow*
gwy_app_find_window_for_curve_map(GwyContainer *data,
                                  gint id)
{
    return find_window_for_id(data, GWY_PAGE_CURVE_MAPS, id);
}

static void
gwy_app_data_selection_gather(G_GNUC_UNUSED gpointer key,
                              gpointer value,
                              gpointer user_data)
{
    GObject *selection;
    GSList **l = (GSList**)user_data;

    if (!G_VALUE_HOLDS_OBJECT(value))
        return;

    selection = g_value_get_object(value);
    if (GWY_IS_SELECTION(selection)) {
        g_object_ref(selection);
        *l = g_slist_prepend(*l, selection);
    }
}

/**
 * gwy_app_data_clear_selections:
 * @data: A data container.
 * @id: Data channel id.
 *
 * Clears all selections associated with a data channel.
 *
 * This is the preferred selection handling after changes in data geometry as they have generally unpredictable
 * effects on selections.  Selection should not be removed because this is likely to make the current tool stop
 * working.
 **/
void
gwy_app_data_clear_selections(GwyContainer *data,
                              gint id)
{
    gchar buf[28];
    GSList *list = NULL, *l;

    g_snprintf(buf, sizeof(buf), "/%d/select", id);
    /* Afraid of chain reactions when selections are changed inside
     * gwy_container_foreach(), gather them first, then clear */
    gwy_container_foreach(data, buf, &gwy_app_data_selection_gather, &list);
    for (l = list; l; l = g_slist_next(l)) {
        gwy_selection_clear(GWY_SELECTION(l->data));
        GWY_OBJECT_UNREF(l->data);
    }
    g_slist_free(list);
}

/**
 * gwy_app_data_browser_foreach:
 * @function: Function to run on each data container.
 * @user_data: Data to pass as second argument of @function.
 *
 * Calls a function for each data container managed by data browser.
 **/
void
gwy_app_data_browser_foreach(GwyAppDataForeachFunc function,
                             gpointer user_data)
{
    GwyAppDataBrowser *browser;
    GwyAppDataProxy *proxy;
    GList *proxies, *l;

    g_return_if_fail(function);

    browser = gwy_app_data_browser;
    if (!browser)
        return;

    /* The copy is necessary as even innocent functions can move a proxy to
     * list head. */
    proxies = g_list_copy(browser->proxy_list);
    for (l = proxies; l; l = g_list_next(l)) {
        proxy = (GwyAppDataProxy*)l->data;
        function(proxy->container, user_data);
    }
    g_list_free(proxies);
}

/**
 * gwy_app_data_browser_show:
 *
 * Shows the data browser window.
 *
 * If the window does not exist, it is created.
 **/
void
gwy_app_data_browser_show(void)
{
    GwyContainer *settings;

    settings = gwy_app_settings_get();
    gwy_container_set_boolean_by_name(settings, "/app/data-browser/visible", TRUE);
    gwy_app_data_browser_restore();
}

/**
 * gwy_app_data_browser_restore:
 *
 * Restores the data browser window.
 *
 * The data browser window is always created (if it does not exist). If it should be visible according to settings, is
 * shown at the saved position.  Otherwise it is kept hidden until gwy_app_data_browser_show().
 **/
void
gwy_app_data_browser_restore(void)
{
    GwyAppDataBrowser *browser;
    GwyContainer *settings;
    gboolean visible = TRUE;

    if (gui_disabled)
        return;

    browser = gwy_app_get_data_browser();
    if (!browser->window)
        gwy_app_data_browser_construct_window(browser);

    settings = gwy_app_settings_get();
    gwy_container_gis_boolean_by_name(settings, "/app/data-browser/visible", &visible);

    if (visible)
        gwy_app_data_browser_show_real(browser);
}

static void
gwy_app_data_browser_show_real(GwyAppDataBrowser *browser)
{
    GtkWindow *window;

    window = GTK_WINDOW(browser->window);

    gwy_app_restore_window_position(window, "/app/data-browser", FALSE);
    gtk_widget_show_all(browser->window);
    gtk_window_present(window);
    gwy_app_restore_window_position(window, "/app/data-browser", FALSE);
}

static void
gwy_app_data_browser_hide_real(GwyAppDataBrowser *browser)
{
    GwyContainer *settings;

    if (!browser || !browser->window || !GTK_WIDGET_VISIBLE(browser->window))
        return;

    gwy_app_save_window_position(GTK_WINDOW(browser->window), "/app/data-browser", TRUE, TRUE);

    settings = gwy_app_settings_get();
    gwy_container_set_boolean_by_name(settings, "/app/data-browser/visible", FALSE);
    gtk_widget_hide(browser->window);
}

/**
 * gwy_app_data_browser_shut_down:
 *
 * Releases data browser resources and saves its state.
 **/
void
gwy_app_data_browser_shut_down(void)
{
    GwyAppDataBrowser *browser;
    guint i;

    browser = gwy_app_data_browser;
    if (!browser)
        return;

    if (browser->window && GTK_WIDGET_VISIBLE(browser->window))
        gwy_app_save_window_position(GTK_WINDOW(browser->window),
                                     "/app/data-browser", TRUE, TRUE);

    /* XXX: EXIT-CLEAN-UP */
    /* This clean-up is only to make sure we've got the references right.
     * Remove in production version. */
    while (browser->proxy_list) {
        browser->current = (GwyAppDataProxy*)browser->proxy_list->data;
        browser->current->keep_invisible = FALSE;
        gwy_app_data_browser_close_file(browser);
    }

    if (browser->window) {
        for (i = 0; i < GWY_NPAGES; i++)
            gtk_tree_view_set_model(GTK_TREE_VIEW(browser->lists[i]), NULL);
    }
}

/**
 * gwy_app_data_browser_get_gui_enabled:
 *
 * Reports whether creation of windows by the data-browser is enabled.
 *
 * Returns: %TRUE if the data-browser is permitted to create windows, %FALSE if it is not.
 *
 * Since: 2.21
 **/
gboolean
gwy_app_data_browser_get_gui_enabled(void)
{
    return !gui_disabled;
}

/**
 * gwy_app_data_browser_set_gui_enabled:
 * @setting: %TRUE to enable creation of widgets by the data-browser, %FALSE to disable it.
 *
 * Globally enables or disables creation of widgets by the data-browser.
 *
 * By default, the data-browser creates windows for data objects automatically, for instance when reconstructing view
 * of a loaded file, after a module function creates a new channel or graph or when it is explicitly asked so by
 * gwy_app_data_browser_show_3d().  Non-GUI applications that run module functions usually wish to disable GUI.
 *
 * If GUI is disabled the data browser never creates windows showing data objects and also gwy_app_data_browser_show()
 * becomes no-op.
 *
 * Disabling GUI after widgets have been already created is a bad idea. Hence you should do so before loading files or
 * calling module functions.
 *
 * Since: 2.21
 **/
void
gwy_app_data_browser_set_gui_enabled(gboolean setting)
{
    GwyAppDataBrowser *browser;

    browser = gwy_app_data_browser;
    if (!gui_disabled && !setting && browser && browser->window) {
        g_warning("Disabling GUI when widgets have been already constructed. "
                  "This does not really work.");
        gtk_widget_hide(browser->window);
    }

    gui_disabled = !setting;
}

/**
 * gwy_app_data_browser_find_data_by_title:
 * @data: A data container.
 * @titleglob: Pattern, as used by #GPatternSpec, to match the channel titles against.
 *
 * Gets the list of all channels in a data container whose titles match the specified pattern.
 *
 * The function originally could be used only for data containers managed by the data browser.  Since version 2.49 it
 * can be used for all file-like data containers.
 *
 * Returns: A newly allocated array with channel ids, -1 terminated.
 *
 * Since: 2.21
 **/
gint*
gwy_app_data_browser_find_data_by_title(GwyContainer *data,
                                        const gchar *titleglob)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_CHANNELS, titleglob);
}

/**
 * gwy_app_data_browser_find_graphs_by_title:
 * @data: A data container.
 * @titleglob: Pattern, as used by #GPatternSpec, to match the graph titles against.
 *
 * Gets the list of all graphs in a data container whose titles match the specified pattern.
 *
 * The function originally could be used only for data containers managed by the data browser.  Since version 2.49 it
 * can be used for all file-like data containers.
 *
 * Returns: A newly allocated array with graph ids, -1 terminated.
 *
 * Since: 2.21
 **/
gint*
gwy_app_data_browser_find_graphs_by_title(GwyContainer *data,
                                          const gchar *titleglob)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_GRAPHS, titleglob);
}

/**
 * gwy_app_data_browser_find_spectra_by_title:
 * @data: A data container.
 * @titleglob: Pattern, as used by #GPatternSpec, to match the spectra titles against.
 *
 * Gets the list of all spectra in a data container whose titles match the specified pattern.
 *
 * The function originally could be used only for data containers managed by the data browser.  Since version 2.49 it
 * can be used for all file-like data containers.
 *
 * Returns: A newly allocated array with spectra ids, -1 terminated.
 *
 * Since: 2.21
 **/
gint*
gwy_app_data_browser_find_spectra_by_title(GwyContainer *data,
                                           const gchar *titleglob)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_SPECTRA, titleglob);
}

/**
 * gwy_app_data_browser_find_volume_by_title:
 * @data: A data container.
 * @titleglob: Pattern, as used by #GPatternSpec, to match the volume data titles against.
 *
 * Gets the list of all volume data in a data container whose titles match the specified pattern.
 *
 * The function originally could be used only for data containers managed by the data browser.  Since version 2.49 it
 * can be used for all file-like data containers.
 *
 * Returns: A newly allocated array with volume data ids, -1 terminated.
 *
 * Since: 2.45
 **/
gint*
gwy_app_data_browser_find_volume_by_title(GwyContainer *data,
                                          const gchar *titleglob)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_VOLUMES, titleglob);
}

/**
 * gwy_app_data_browser_find_xyz_by_title:
 * @data: A data container.
 * @titleglob: Pattern, as used by #GPatternSpec, to match the XYZ data titles against.
 *
 * Gets the list of all XYZ data in a data container whose titles match the specified pattern.
 *
 * The function originally could be used only for data containers managed by the data browser.  Since version 2.49 it
 * can be used for all file-like data containers.
 *
 * Returns: A newly allocated array with XYZ data ids, -1 terminated.
 *
 * Since: 2.45
 **/
gint*
gwy_app_data_browser_find_xyz_by_title(GwyContainer *data,
                                       const gchar *titleglob)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_XYZS, titleglob);
}

/**
 * gwy_app_data_browser_find_curve_map_by_title:
 * @data: A data container.
 * @titleglob: Pattern, as used by #GPatternSpec, to match the curve map data titles against.
 *
 * Gets the list of all curve_map data in a data container whose titles match the specified pattern.
 *
 * Returns: A newly allocated array with curve map data ids, -1 terminated.
 *
 * Since: 2.60
 **/
gint*
gwy_app_data_browser_find_curve_map_by_title(GwyContainer *data,
                                             const gchar *titleglob)
{
    return gwy_app_data_list_get_object_ids(data, GWY_PAGE_CURVE_MAPS, titleglob);
}

static void
gwy_app_data_browser_notify_watch(GwyContainer *container, GwyAppPage pageno, gint id,
                                  GwyDataWatchEventType event)
{
    GList *l;

    for (l = data_watchers[pageno]; l; l = g_list_next(l)) {
        GwyAppWatcherData *wdata = (GwyAppWatcherData*)l->data;
        wdata->function(container, id, event, wdata->user_data);
    }
}

static gulong
gwy_app_data_browser_add_watch(GwyAppPage pageno,
                               GwyAppDataWatchFunc function, gpointer user_data)
{
    GwyAppWatcherData *wdata;

    g_return_val_if_fail(function, 0);
    wdata = g_new(GwyAppWatcherData, 1);
    wdata->function = function;
    wdata->user_data = user_data;
    wdata->id = ++watcher_id;
    data_watchers[pageno] = g_list_append(data_watchers[pageno], wdata);

    return wdata->id;
}

static void
gwy_app_data_browser_remove_watch(GwyAppPage pageno, gulong id)
{
    GList *l;

    for (l = data_watchers[pageno]; l; l = g_list_next(l)) {
        GwyAppWatcherData *wdata = (GwyAppWatcherData*)l->data;

        if (wdata->id == id) {
            data_watchers[pageno] = g_list_delete_link(data_watchers[pageno], l);
            return;
        }
    }
    g_warning("Cannot find watch with id %lu.", id);
}

/**
 * gwy_app_data_browser_add_channel_watch:
 * @function: Function to call when a channel changes.
 * @user_data: User data to pass to @function.
 *
 * Adds a watch function called when a channel changes.
 *
 * The function is called whenever a channel is added, removed, its data changes or its metadata such as the title
 * changes.  If a channel is removed it may longer exist when the function is called.
 *
 * Returns: The id of the added watch func that can be used to remove it later using
 *          gwy_app_data_browser_remove_channel_watch().
 *
 * Since: 2.21
 **/
gulong
gwy_app_data_browser_add_channel_watch(GwyAppDataWatchFunc function,
                                       gpointer user_data)
{
    return gwy_app_data_browser_add_watch(GWY_PAGE_CHANNELS, function, user_data);
}

/**
 * gwy_app_data_browser_remove_channel_watch:
 * @id: Watch function id, as returned by gwy_app_data_browser_add_channel_watch().
 *
 * Removes a channel watch function.
 *
 * Since: 2.21
 **/
void
gwy_app_data_browser_remove_channel_watch(gulong id)
{
    gwy_app_data_browser_remove_watch(GWY_PAGE_CHANNELS, id);
}

/**
 * gwy_app_data_browser_add_graph_watch:
 * @function: Function to call when a graph changes.
 * @user_data: User data to pass to @function.
 *
 * Adds a watch function called when a graph changes.
 *
 * The function is called whenever a graph is added, removed or its properties change.  If a graph is removed it may
 * longer exist when the function is called.
 *
 * Returns: The id of the added watch func that can be used to remove it later using
 *          gwy_app_data_browser_remove_graph_watch().
 *
 * Since: 2.41
 **/
gulong
gwy_app_data_browser_add_graph_watch(GwyAppDataWatchFunc function,
                                     gpointer user_data)
{
    return gwy_app_data_browser_add_watch(GWY_PAGE_GRAPHS, function, user_data);
}

/**
 * gwy_app_data_browser_remove_graph_watch:
 * @id: Watch function id, as returned by gwy_app_data_browser_add_graph_watch().
 *
 * Removes a graph watch function.
 *
 * Since: 2.41
 **/
void
gwy_app_data_browser_remove_graph_watch(gulong id)
{
    gwy_app_data_browser_remove_watch(GWY_PAGE_GRAPHS, id);
}

/**
 * gwy_app_data_browser_add_volume_watch:
 * @function: Function to call when volume data change.
 * @user_data: User data to pass to @function.
 *
 * Adds a watch function called when volume data change.
 *
 * The function is called whenever a volume is added, removed, its data changes or its metadata such as the title
 * changes.  If a volume is removed it may longer exist when the function is called.
 *
 * Returns: The id of the added watch func that can be used to remove it later using
 *          gwy_app_data_browser_remove_volume_watch().
 *
 * Since: 2.60
 **/
gulong
gwy_app_data_browser_add_volume_watch(GwyAppDataWatchFunc function,
                                      gpointer user_data)
{
    return gwy_app_data_browser_add_watch(GWY_PAGE_VOLUMES, function, user_data);
}

/**
 * gwy_app_data_browser_remove_volume_watch:
 * @id: Watch function id, as returned by gwy_app_data_browser_add_volume_watch().
 *
 * Removes a volume data watch function.
 *
 * Since: 2.60
 **/
void
gwy_app_data_browser_remove_volume_watch(gulong id)
{
    gwy_app_data_browser_remove_watch(GWY_PAGE_VOLUMES, id);
}

/**
 * gwy_app_data_browser_add_xyz_watch:
 * @function: Function to call when XYZ data change.
 * @user_data: User data to pass to @function.
 *
 * Adds a watch function called when XYZ data change.
 *
 * The function is called whenever an XYZ surface is added, removed, its data changes or its metadata such as the
 * title changes.  If an XYZ is removed it may longer exist when the function is called.
 *
 * Returns: The id of the added watch func that can be used to remove it later using
 *          gwy_app_data_browser_remove_xyz_watch().
 *
 * Since: 2.60
 **/
gulong
gwy_app_data_browser_add_xyz_watch(GwyAppDataWatchFunc function,
                                   gpointer user_data)
{
    return gwy_app_data_browser_add_watch(GWY_PAGE_XYZS, function, user_data);
}

/**
 * gwy_app_data_browser_remove_xyz_watch:
 * @id: Watch function id, as returned by gwy_app_data_browser_add_xyz_watch().
 *
 * Removes an XYZ data watch function.
 *
 * Since: 2.60
 **/
void
gwy_app_data_browser_remove_xyz_watch(gulong id)
{
    gwy_app_data_browser_remove_watch(GWY_PAGE_XYZS, id);
}

/**
 * gwy_app_data_browser_add_curve_map_watch:
 * @function: Function to call when curve map data change.
 * @user_data: User data to pass to @function.
 *
 * Adds a watch function called when curve map data change.
 *
 * The function is called whenever a curve map is added, removed, its data changes or its metadata such as the title
 * changes.  If a curve map is removed it may longer exist when the function is called.
 *
 * Returns: The id of the added watch func that can be used to remove it later using
 *          gwy_app_data_browser_remove_curve_map_watch().
 *
 * Since: 2.60
 **/
gulong
gwy_app_data_browser_add_curve_map_watch(GwyAppDataWatchFunc function,
                                         gpointer user_data)
{
    return gwy_app_data_browser_add_watch(GWY_PAGE_CURVE_MAPS, function, user_data);
}

/**
 * gwy_app_data_browser_remove_curve_map_watch:
 * @id: Watch function id, as returned by gwy_app_data_browser_add_curve_map_watch().
 *
 * Removes a curve map data watch function.
 *
 * Since: 2.60
 **/
void
gwy_app_data_browser_remove_curve_map_watch(gulong id)
{
    gwy_app_data_browser_remove_watch(GWY_PAGE_CURVE_MAPS, id);
}

/************************** Documentation ****************************/

/**
 * SECTION:data-browser
 * @title: data-browser
 * @short_description: Data browser
 *
 * The data browser is both an entity that monitors of various data in Gwyddion and the corresponding user interface
 * showing the data lists and letting the user deleting or copying them.  The public functions are generally related
 * to the first part.
 *
 * An #GwyContainer that represents an SPM file is managed by functions such as gwy_app_data_browser_add() or
 * gwy_app_data_browser_remove().  Note that the high-level libgwyapp functions (with app in their name) such as
 * gwy_app_file_load() already call the data browser functions as appropriate.
 *
 * If a file-like #GwyContainer has not been added to the data browser it is unmanaged and cannot be used with most of
 * the data browser functions. The exceptions are #GQuark constructors such as gwy_app_get_mask_key_for_id(), copying
 * functions such as gwy_app_data_browser_copy_channel(), title management functions such as
 * gwy_app_set_surface_title(), thumbnail creation helpers such as gwy_app_get_graph_thumbnail(), and functions for
 * listing the ids of various data types such as gwy_app_data_browser_get_data_ids().
 *
 * Individual data pieces can be added to managed #GwyContainers with functions such as
 * gwy_app_data_browser_add_data_field() that can take care of creating the window showing the new data.  Removal is
 * generally done by directly removing the corresponding data object(s) from the #GwyContainer.
 *
 * An important part of the data browser is keeping track which data item is currently selected (to know what a data
 * processing method should process, etc.).  You can obtain the information about various currently selected objects
 * using gwy_app_data_browser_get_current().  Making a data item currently selected is accomplished either using
 * function such as gwy_app_data_browser_select_data_view(), which corresponds to the user switching to the data
 * window, or gwy_app_data_browser_select_data_field() which just selects the data item as current in the browser.
 * The latter is less safe and can result in strange behaviour because for some purposes only data actually displayed
 * can be ‘current’.
 **/

/**
 * GwyAppDataForeachFunc:
 * @data: A data container managed by the data-browser.
 * @user_data: User data passed to gwy_app_data_browser_foreach().
 *
 * Type of function passed to gwy_app_data_browser_foreach().
 **/

/**
 * GwyAppDataWatchFunc:
 * @data: A data container managed by the data-browser.
 * @id: Object (channel) id in the container.
 * @user_data: User data passed to gwy_app_data_browser_add_channel_watch().
 *
 * Type of function passed to gwy_app_data_browser_add_channel_watch().
 *
 * Since: 2.21
 **/

/**
 * GwyDataWatchEventType:
 * @GWY_DATA_WATCH_EVENT_ADDED: A new data object has appeared.
 * @GWY_DATA_WATCH_EVENT_CHANGED: A data object has changed.
 * @GWY_DATA_WATCH_EVENT_REMOVED: A data object has been removed.
 *
 * Type of event reported to #GwyAppDataWatchFunc watcher functions.
 *
 * Since: 2.21
 **/

/**
 * GwyVisibilityResetType:
 * @GWY_VISIBILITY_RESET_DEFAULT: Restore visibilities from container and if nothing would be visible, make an
 *                                arbitrary data object visible.
 * @GWY_VISIBILITY_RESET_RESTORE: Restore visibilities from container.
 * @GWY_VISIBILITY_RESET_SHOW_ALL: Show all data objects.
 * @GWY_VISIBILITY_RESET_HIDE_ALL: Hide all data objects.  This normally makes the file inaccessible.
 *
 * Data object visibility reset type.
 *
 * The precise behaviour of @GWY_VISIBILITY_RESET_DEFAULT may be subject of further changes.  It indicates the wish to
 * restore saved visibilities and do something reasonable when there are no visibilities to restore.
 **/

/**
 * GwyAppWhat:
 * @GWY_APP_CONTAINER: Data container (corresponds to files).
 * @GWY_APP_DATA_VIEW: Data view widget (shows a channel).
 * @GWY_APP_GRAPH: Graph widget (shows a graph model).
 * @GWY_APP_DATA_FIELD: Data field (image).
 * @GWY_APP_DATA_FIELD_KEY: Quark corresponding to the data field (image).
 * @GWY_APP_DATA_FIELD_ID: Number (id) of the data field (image) in its container.
 * @GWY_APP_MASK_FIELD: Mask data field.
 * @GWY_APP_MASK_FIELD_KEY: Quark corresponding to the mask field.
 * @GWY_APP_SHOW_FIELD: Presentation data field.
 * @GWY_APP_SHOW_FIELD_KEY: Quark corresponding to the presentation field.
 * @GWY_APP_GRAPH_MODEL: Graph model.
 * @GWY_APP_GRAPH_MODEL_KEY: Quark corresponding to the graph model.
 * @GWY_APP_GRAPH_MODEL_ID: Number (id) of the graph model in its container.
 * @GWY_APP_SPECTRA: Single point spectra. (Since 2.7)
 * @GWY_APP_SPECTRA_KEY: Quark corresponding to the single point spectra. (Since 2.7)
 * @GWY_APP_SPECTRA_ID: Number (id) of the the single point spectra in its container. (Since 2.7)
 * @GWY_APP_VOLUME_VIEW: Data view widget (shows preview of volume data) (Since 2.32).
 * @GWY_APP_BRICK: Data brick (volume data) (Since 2.32).
 * @GWY_APP_BRICK_KEY: Quark corresponding to the data brick (Since 2.32).
 * @GWY_APP_BRICK_ID: Number (id) of the the data brick in its container (Since 2.32).
 * @GWY_APP_XYZ_VIEW: Data view widget (shows preview of surface XYZ data) (Since 2.45).
 * @GWY_APP_SURFACE: Surface data (XYZ) (Since 2.45).
 * @GWY_APP_SURFACE_KEY: Quark corresponding to the surface data (Since 2.45).
 * @GWY_APP_SURFACE_ID: Number (id) of the the surface data in its container (Since 2.45).
 * @GWY_APP_CONTAINER_ID: Numeric id of data container (Since 2.41).
 * @GWY_APP_PAGE: Currently selected data browser page as a #GwyAppPage (Since 2.45).
 * @GWY_APP_LAWN: Lawn (curve map) (Since 2.60).
 * @GWY_APP_LAWN_KEY: Quark corresponding to the curve map (Since 2.60).
 * @GWY_APP_LAWN_ID: Number (id) of the lawn (curve map) in its container (Since 2.60).
 * @GWY_APP_CURVE_MAP_VIEW: Data view widget (shows preview of curve map lawn data) (Since 2.60).
 *
 * Types of current objects that can be requested with gwy_app_data_browser_get_current().
 **/

/**
 * GwyAppPage:
 * @GWY_PAGE_NOPAGE: No page.  This value is returned when no data are active in the browser.
 * @GWY_PAGE_CHANNELS: Channel (image data).
 * @GWY_PAGE_GRAPHS: Graph.
 * @GWY_PAGE_SPECTRA: Single point spectra.
 * @GWY_PAGE_VOLUMES: Volume data.
 * @GWY_PAGE_XYZS: XYZ data.
 * @GWY_PAGE_CURVE_MAPS: Curve map data.  (Since 2.60)
 *
 * Data browser page, corresponding to one of possible data types.
 *
 * Since: 2.45
 **/

/**
 * GwyDataItem:
 * @GWY_DATA_ITEM_GRADIENT: Color gradient.
 * @GWY_DATA_ITEM_PALETTE: An alias of @GWY_DATA_ITEM_GRADIENT.
 * @GWY_DATA_ITEM_MASK_COLOR: Mask color components.
 * @GWY_DATA_ITEM_TITLE: Channel title.
 * @GWY_DATA_ITEM_RANGE: Range type and range boundaries.
 * @GWY_DATA_ITEM_RANGE_TYPE: Range type.
 * @GWY_DATA_ITEM_REAL_SQUARE: Physical/pixel aspect ratio mode.
 * @GWY_DATA_ITEM_SELECTIONS: Data selections.
 * @GWY_DATA_ITEM_META: Metadata.
 * @GWY_DATA_ITEM_CALDATA: Calibration and uncertainty data.  (Since 2.23)
 * @GWY_DATA_ITEM_PREVIEW: Volume data preview.  (Since 2.51)
 *
 * Type of auxiliary channel data.
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
