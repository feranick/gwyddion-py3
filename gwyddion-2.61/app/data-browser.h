/*
 *  $Id: data-browser.h 24553 2021-12-02 15:10:33Z yeti-dn $
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

#ifndef __GWY_DATA_BROWSER_H__
#define __GWY_DATA_BROWSER_H__

#include <gtk/gtkwindow.h>
#include <libprocess/datafield.h>
#include <libprocess/brick.h>
#include <libprocess/spectra.h>
#include <libprocess/surface.h>
#include <libprocess/lawn.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwygraph.h>

G_BEGIN_DECLS

/* NB: If you add more items here, update also the support in pygwy.override's gwy_app_data_browser_get_current(). */
typedef enum {
    GWY_APP_CONTAINER = 1,
    GWY_APP_DATA_VIEW,
    GWY_APP_GRAPH,
    GWY_APP_DATA_FIELD,
    GWY_APP_DATA_FIELD_KEY,
    GWY_APP_DATA_FIELD_ID,
    GWY_APP_MASK_FIELD,
    GWY_APP_MASK_FIELD_KEY,
    GWY_APP_SHOW_FIELD,
    GWY_APP_SHOW_FIELD_KEY,
    GWY_APP_GRAPH_MODEL,
    GWY_APP_GRAPH_MODEL_KEY,
    GWY_APP_GRAPH_MODEL_ID,
    GWY_APP_SPECTRA,
    GWY_APP_SPECTRA_KEY,
    GWY_APP_SPECTRA_ID,
    GWY_APP_VOLUME_VIEW,
    GWY_APP_BRICK,
    GWY_APP_BRICK_KEY,
    GWY_APP_BRICK_ID,
    GWY_APP_CONTAINER_ID,
    GWY_APP_XYZ_VIEW,
    GWY_APP_SURFACE,
    GWY_APP_SURFACE_KEY,
    GWY_APP_SURFACE_ID,
    GWY_APP_PAGE,
    GWY_APP_LAWN,
    GWY_APP_LAWN_KEY,
    GWY_APP_LAWN_ID,
    GWY_APP_CURVE_MAP_VIEW,
} GwyAppWhat;

typedef enum {
    GWY_PAGE_NOPAGE = -1,
    GWY_PAGE_CHANNELS,
    GWY_PAGE_GRAPHS,
    GWY_PAGE_SPECTRA,
    GWY_PAGE_VOLUMES,
    GWY_PAGE_XYZS,
    GWY_PAGE_CURVE_MAPS,
} GwyAppPage;

typedef enum {
    GWY_DATA_ITEM_GRADIENT = 1,
    GWY_DATA_ITEM_PALETTE = GWY_DATA_ITEM_GRADIENT,
    GWY_DATA_ITEM_MASK_COLOR,
    GWY_DATA_ITEM_TITLE,
    GWY_DATA_ITEM_RANGE,
    GWY_DATA_ITEM_RANGE_TYPE,
    GWY_DATA_ITEM_REAL_SQUARE,
    GWY_DATA_ITEM_SELECTIONS,
    GWY_DATA_ITEM_META,
    GWY_DATA_ITEM_CALDATA,
    GWY_DATA_ITEM_PREVIEW,
} GwyDataItem;

typedef enum {
    GWY_VISIBILITY_RESET_DEFAULT,
    GWY_VISIBILITY_RESET_RESTORE,
    GWY_VISIBILITY_RESET_SHOW_ALL,
    GWY_VISIBILITY_RESET_HIDE_ALL
} GwyVisibilityResetType;

typedef enum {
    GWY_DATA_WATCH_EVENT_ADDED,
    GWY_DATA_WATCH_EVENT_CHANGED,
    GWY_DATA_WATCH_EVENT_REMOVED
} GwyDataWatchEventType;

typedef void (*GwyAppDataForeachFunc)(GwyContainer *data,
                                      gpointer user_data);
typedef void (*GwyAppDataWatchFunc)(GwyContainer *data,
                                    gint id,
                                    GwyDataWatchEventType event,
                                    gpointer user_data);

void          gwy_app_data_browser_add       (GwyContainer *data);
void          gwy_app_data_browser_remove    (GwyContainer *data);
void          gwy_app_data_browser_merge     (GwyContainer *data);
GwyContainer* gwy_app_data_browser_get       (gint number);
gint          gwy_app_data_browser_get_number(GwyContainer *data);

gboolean gwy_app_data_browser_reset_visibility(GwyContainer *data,
                                               GwyVisibilityResetType reset_type);
void   gwy_app_data_browser_set_keep_invisible(GwyContainer *data,
                                               gboolean keep_invisible);
gboolean gwy_app_data_browser_get_keep_invisible(GwyContainer *data);
void   gwy_app_data_browser_select_data_view(GwyDataView *data_view);
void   gwy_app_data_browser_select_graph    (GwyGraph *graph);
void   gwy_app_data_browser_select_spectra  (GwySpectra *spectra);
void   gwy_app_data_browser_select_volume   (GwyDataView *data_view);
void   gwy_app_data_browser_select_xyz      (GwyDataView *data_view);
void   gwy_app_data_browser_select_curve_map(GwyDataView *data_view);
gint   gwy_app_data_browser_add_data_field  (GwyDataField *dfield,
                                             GwyContainer *data,
                                             gboolean showit);
gint   gwy_app_data_browser_add_graph_model (GwyGraphModel *gmodel,
                                             GwyContainer *data,
                                             gboolean showit);
gint   gwy_app_data_browser_add_spectra     (GwySpectra *spectra,
                                             GwyContainer *data,
                                             gboolean showit);
gint   gwy_app_data_browser_add_brick       (GwyBrick *brick,
                                             GwyDataField *preview,
                                             GwyContainer *data,
                                             gboolean showit);
gint   gwy_app_data_browser_add_surface     (GwySurface *surface,
                                             GwyContainer *data,
                                             gboolean showit);
gint   gwy_app_data_browser_add_lawn        (GwyLawn *lawn,
                                             GwyDataField *preview,
                                             GwyContainer *data,
                                             gboolean showit);
void   gwy_app_data_browser_get_current     (GwyAppWhat what,
                                             ...);
gint* gwy_app_data_browser_get_data_ids     (GwyContainer *data);
gint* gwy_app_data_browser_get_graph_ids    (GwyContainer *data);
gint* gwy_app_data_browser_get_spectra_ids  (GwyContainer *data);
gint* gwy_app_data_browser_get_volume_ids   (GwyContainer *data);
gint* gwy_app_data_browser_get_xyz_ids      (GwyContainer *data);
gint* gwy_app_data_browser_get_curve_map_ids(GwyContainer *data);
gint* gwy_app_data_browser_find_data_by_title     (GwyContainer *data,
                                                   const gchar *titleglob);
gint* gwy_app_data_browser_find_graphs_by_title   (GwyContainer *data,
                                                   const gchar *titleglob);
gint* gwy_app_data_browser_find_spectra_by_title  (GwyContainer *data,
                                                   const gchar *titleglob);
gint* gwy_app_data_browser_find_volume_by_title   (GwyContainer *data,
                                                   const gchar *titleglob);
gint* gwy_app_data_browser_find_xyz_by_title      (GwyContainer *data,
                                                   const gchar *titleglob);
gint* gwy_app_data_browser_find_curve_map_by_title(GwyContainer *data,
                                                   const gchar *titleglob);
void   gwy_app_data_clear_selections        (GwyContainer *data,
                                             gint id);
void   gwy_app_data_browser_foreach         (GwyAppDataForeachFunc function,
                                             gpointer user_data);

gulong gwy_app_data_browser_add_channel_watch     (GwyAppDataWatchFunc function,
                                                   gpointer user_data);
void   gwy_app_data_browser_remove_channel_watch  (gulong id);
gulong gwy_app_data_browser_add_graph_watch       (GwyAppDataWatchFunc function,
                                                   gpointer user_data);
void   gwy_app_data_browser_remove_graph_watch    (gulong id);
gulong gwy_app_data_browser_add_volume_watch      (GwyAppDataWatchFunc function,
                                                   gpointer user_data);
void   gwy_app_data_browser_remove_volume_watch   (gulong id);
gulong gwy_app_data_browser_add_xyz_watch         (GwyAppDataWatchFunc function,
                                                   gpointer user_data);
void   gwy_app_data_browser_remove_xyz_watch      (gulong id);
gulong gwy_app_data_browser_add_curve_map_watch   (GwyAppDataWatchFunc function,
                                                   gpointer user_data);
void   gwy_app_data_browser_remove_curve_map_watch(gulong id);

void   gwy_app_sync_data_items                (GwyContainer *source,
                                               GwyContainer *dest,
                                               gint from_id,
                                               gint to_id,
                                               gboolean delete_too,
                                               ...);
void   gwy_app_sync_data_itemsv               (GwyContainer *source,
                                               GwyContainer *dest,
                                               gint from_id,
                                               gint to_id,
                                               gboolean delete_too,
                                               const GwyDataItem *items,
                                               guint nitems);
void   gwy_app_sync_volume_items              (GwyContainer *source,
                                               GwyContainer *dest,
                                               gint from_id,
                                               gint to_id,
                                               gboolean delete_too,
                                               ...);
void   gwy_app_sync_volume_itemsv             (GwyContainer *source,
                                               GwyContainer *dest,
                                               gint from_id,
                                               gint to_id,
                                               gboolean delete_too,
                                               const GwyDataItem *items,
                                               guint nitems);
void   gwy_app_sync_xyz_items                 (GwyContainer *source,
                                               GwyContainer *dest,
                                               gint from_id,
                                               gint to_id,
                                               gboolean delete_too,
                                               ...);
void   gwy_app_sync_xyz_itemsv                (GwyContainer *source,
                                               GwyContainer *dest,
                                               gint from_id,
                                               gint to_id,
                                               gboolean delete_too,
                                               const GwyDataItem *items,
                                               guint nitems);
void   gwy_app_sync_curve_map_items           (GwyContainer *source,
                                               GwyContainer *dest,
                                               gint from_id,
                                               gint to_id,
                                               gboolean delete_too,
                                               ...);
void   gwy_app_sync_curve_map_itemsv          (GwyContainer *source,
                                               GwyContainer *dest,
                                               gint from_id,
                                               gint to_id,
                                               gboolean delete_too,
                                               const GwyDataItem *items,
                                               guint nitems);
gint   gwy_app_data_browser_copy_channel      (GwyContainer *source,
                                               gint id,
                                               GwyContainer *dest);
gint   gwy_app_data_browser_copy_volume       (GwyContainer *source,
                                               gint id,
                                               GwyContainer *dest);
gint   gwy_app_data_browser_copy_xyz          (GwyContainer *source,
                                               gint id,
                                               GwyContainer *dest);
gint   gwy_app_data_browser_copy_curve_map    (GwyContainer *source,
                                               gint id,
                                               GwyContainer *dest);
GQuark gwy_app_get_data_key_for_id            (gint id);
GQuark gwy_app_get_mask_key_for_id            (gint id);
GQuark gwy_app_get_show_key_for_id            (gint id);
GQuark gwy_app_get_graph_key_for_id           (gint id);
GQuark gwy_app_get_spectra_key_for_id         (gint id);
GQuark gwy_app_get_brick_key_for_id           (gint id);
GQuark gwy_app_get_surface_key_for_id         (gint id);
GQuark gwy_app_get_lawn_key_for_id            (gint id);
GQuark gwy_app_get_data_title_key_for_id      (gint id);
GQuark gwy_app_get_data_base_key_for_id       (gint id);
GQuark gwy_app_get_data_range_type_key_for_id (gint id);
GQuark gwy_app_get_data_range_min_key_for_id  (gint id);
GQuark gwy_app_get_data_range_max_key_for_id  (gint id);
GQuark gwy_app_get_data_palette_key_for_id    (gint id);
GQuark gwy_app_get_data_meta_key_for_id       (gint id);
GQuark gwy_app_get_data_real_square_key_for_id(gint id);
GQuark gwy_app_get_brick_title_key_for_id     (gint id);
GQuark gwy_app_get_brick_preview_key_for_id   (gint id);
GQuark gwy_app_get_brick_palette_key_for_id   (gint id);
GQuark gwy_app_get_brick_meta_key_for_id      (gint id);
GQuark gwy_app_get_surface_title_key_for_id   (gint id);
GQuark gwy_app_get_surface_palette_key_for_id (gint id);
GQuark gwy_app_get_surface_meta_key_for_id    (gint id);
GQuark gwy_app_get_surface_preview_key_for_id (gint id);
GQuark gwy_app_get_lawn_title_key_for_id      (gint id);
GQuark gwy_app_get_lawn_palette_key_for_id    (gint id);
GQuark gwy_app_get_lawn_meta_key_for_id       (gint id);
GQuark gwy_app_get_lawn_preview_key_for_id    (gint id);
GQuark gwy_app_get_lawn_real_square_key_for_id(gint id);
void   gwy_app_set_data_field_title           (GwyContainer *data,
                                               gint id,
                                               const gchar *name);
gchar* gwy_app_get_data_field_title           (GwyContainer *data,
                                               gint id);
void   gwy_app_set_brick_title                (GwyContainer *data,
                                               gint id,
                                               const gchar *name);
gchar* gwy_app_get_brick_title                (GwyContainer *data,
                                               gint id);
void   gwy_app_set_surface_title              (GwyContainer *data,
                                               gint id,
                                               const gchar *name);
gchar* gwy_app_get_surface_title              (GwyContainer *data,
                                               gint id);
void   gwy_app_set_lawn_title                 (GwyContainer *data,
                                               gint id,
                                               const gchar *name);
gchar* gwy_app_get_lawn_title                 (GwyContainer *data,
                                               gint id);

void       gwy_app_data_browser_show        (void);
void       gwy_app_data_browser_restore     (void);
void       gwy_app_data_browser_shut_down   (void);

GdkPixbuf* gwy_app_get_channel_thumbnail  (GwyContainer *data,
                                           gint id,
                                           gint max_width,
                                           gint max_height);
GdkPixbuf* gwy_app_get_graph_thumbnail    (GwyContainer *data,
                                           gint id,
                                           gint max_width,
                                           gint max_height);
GdkPixbuf* gwy_app_get_volume_thumbnail   (GwyContainer *data,
                                           gint id,
                                           gint max_width,
                                           gint max_height);
GdkPixbuf* gwy_app_get_xyz_thumbnail      (GwyContainer *data,
                                           gint id,
                                           gint max_width,
                                           gint max_height);
GdkPixbuf* gwy_app_get_curve_map_thumbnail(GwyContainer *data,
                                           gint id,
                                           gint max_width,
                                           gint max_height);
void gwy_app_data_browser_select_data_field (GwyContainer *data,
                                             gint id);
void gwy_app_data_browser_select_graph_model(GwyContainer *data,
                                             gint id);
void gwy_app_data_browser_select_surface    (GwyContainer *data,
                                             gint id);
void gwy_app_data_browser_select_brick      (GwyContainer *data,
                                             gint id);
void gwy_app_data_browser_select_lawn       (GwyContainer *data,
                                             gint id);
/* XXX */
void     gwy_app_data_browser_show_3d       (GwyContainer *data,
                                             gint id);
GtkWindow* gwy_app_find_window_for_channel  (GwyContainer *data,
                                             gint id);
GtkWindow* gwy_app_find_window_for_graph    (GwyContainer *data,
                                             gint id);
GtkWindow* gwy_app_find_window_for_volume   (GwyContainer *data,
                                             gint id);
GtkWindow* gwy_app_find_window_for_xyz      (GwyContainer *data,
                                             gint id);
GtkWindow* gwy_app_find_window_for_curve_map(GwyContainer *data,
                                             gint id);
gboolean gwy_app_data_browser_get_gui_enabled(void);
void     gwy_app_data_browser_set_gui_enabled(gboolean setting);

G_END_DECLS

#endif /* __GWY_DATA_BROWSER_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
