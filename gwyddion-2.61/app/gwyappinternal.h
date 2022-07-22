/*
 *  $Id: gwyappinternal.h 23998 2021-08-16 15:23:34Z yeti-dn $
 *  Copyright (C) 2004-2021 David Necas (Yeti), Petr Klapetek.
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

/*< private_header >*/

#ifndef __GWY_APP_INTERNAL_H__
#define __GWY_APP_INTERNAL_H__

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <libprocess/spectra.h>
#include <libprocess/brick.h>
#include <libprocess/lawn.h>
#include <libgwydgets/gwydatawindow.h>
#include <libgwydgets/gwy3dwindow.h>
#include <libgwydgets/gwygraphwindow.h>
#include <libgwydgets/gwysensitivitygroup.h>

#include <app/gwyappfilechooser.h>
#include <app/data-browser.h>

G_BEGIN_DECLS

/* The GtkTargetEntry for tree model drags.
 * FIXME: Is it Gtk+ private or what? */
#define GTK_TREE_MODEL_ROW { "GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_APP, 0 }

/* The container prefix all graph reside in.  This is a bit silly but it does not worth to break file compatibility
 * with 1.x. */
#define GRAPH_PREFIX "/0/graph/graph"

/* Thiese are sane and should remain so. */
#define SPECTRA_PREFIX "/sps"
#define BRICK_PREFIX "/brick"
#define SURFACE_PREFIX "/surface"
#define LAWN_PREFIX "/lawn"

enum {
    THUMB_SIZE = 60,
    TMS_NORMAL_THUMB_SIZE = 128,
    GWY_NPAGES = GWY_PAGE_CURVE_MAPS + 1,
};

/* Data types interesting keys can correspond to */
typedef enum {
    KEY_IS_NONE = 0,
    KEY_IS_FILENAME,
    KEY_IS_DATA,
    KEY_IS_DATA_VISIBLE,
    KEY_IS_CHANNEL_META,
    KEY_IS_CHANNEL_LOG,
    KEY_IS_TITLE,
    KEY_IS_SELECT,
    KEY_IS_RANGE_TYPE,
    KEY_IS_RANGE,
    KEY_IS_PALETTE,
    KEY_IS_REAL_SQUARE,
    KEY_IS_DATA_VIEW_SCALE,
    KEY_IS_MASK,
    KEY_IS_MASK_COLOR,
    KEY_IS_SHOW,
    KEY_IS_3D_SETUP,
    KEY_IS_3D_PALETTE,
    KEY_IS_3D_MATERIAL,
    KEY_IS_3D_LABEL,
    KEY_IS_3D_VIEW_SCALE,
    KEY_IS_3D_VIEW_SIZE,
    KEY_IS_CALDATA,
    KEY_IS_GRAPH,
    KEY_IS_GRAPH_VISIBLE,
    KEY_IS_GRAPH_LASTID,
    KEY_IS_GRAPH_VIEW_SCALE,
    KEY_IS_GRAPH_VIEW_SIZE,
    KEY_IS_SPECTRA,
    KEY_IS_SPECTRA_VISIBLE,
    KEY_IS_SPS_REF,
    KEY_IS_BRICK,
    KEY_IS_BRICK_VISIBLE,
    KEY_IS_BRICK_TITLE,
    KEY_IS_BRICK_PREVIEW,
    KEY_IS_BRICK_PREVIEW_PALETTE,
    KEY_IS_BRICK_META,
    KEY_IS_BRICK_LOG,
    KEY_IS_BRICK_VIEW_SCALE,
    KEY_IS_SURFACE,
    KEY_IS_SURFACE_VISIBLE,
    KEY_IS_SURFACE_TITLE,
    KEY_IS_SURFACE_PREVIEW,
    KEY_IS_SURFACE_PREVIEW_PALETTE,
    KEY_IS_SURFACE_META,
    KEY_IS_SURFACE_LOG,
    KEY_IS_SURFACE_VIEW_SCALE,
    KEY_IS_SURFACE_VIEW_SIZE,
    KEY_IS_LAWN,
    KEY_IS_LAWN_VISIBLE,
    KEY_IS_LAWN_TITLE,
    KEY_IS_LAWN_PREVIEW,
    KEY_IS_LAWN_PREVIEW_PALETTE,
    KEY_IS_LAWN_REAL_SQUARE,
    KEY_IS_LAWN_META,
    KEY_IS_LAWN_LOG,
    KEY_IS_LAWN_VIEW_SCALE,
} GwyAppKeyType;

typedef struct {
    GLogLevelFlags log_level;
    GQuark log_domain;
    gchar *message;
} GwyAppLogMessage;

G_GNUC_INTERNAL
gint     _gwy_app_get_n_recent_files          (void);

G_GNUC_INTERNAL
void     _gwy_app_data_window_setup           (GwyDataWindow *data_window);
G_GNUC_INTERNAL
void     _gwy_app_3d_window_setup             (Gwy3DWindow *window3d);
G_GNUC_INTERNAL
gboolean _gwy_app_3d_view_init_setup          (GwyContainer *container,
                                               const gchar *setup_prefix);
G_GNUC_INTERNAL
void     _gwy_app_graph_window_setup          (GwyGraphWindow *graph_window,
                                               GwyContainer *container,
                                               GQuark prefix);
G_GNUC_INTERNAL
void     _gwy_app_brick_window_setup          (GwyDataWindow *data_window);

G_GNUC_INTERNAL
void     _gwy_app_surface_window_setup        (GwyDataWindow *data_window);

G_GNUC_INTERNAL
void     _gwy_app_lawn_window_setup           (GwyDataWindow *data_window);

G_GNUC_INTERNAL
void     _gwy_app_data_view_set_current       (GwyDataView *data_view);
G_GNUC_INTERNAL
void     _gwy_app_spectra_set_current         (GwySpectra *spectra);

G_GNUC_INTERNAL
GwySensitivityGroup* _gwy_app_sensitivity_get_group(void);

G_GNUC_INTERNAL
GdkPixbuf* _gwy_app_recent_file_try_thumbnail  (const gchar *filename_sys);
G_GNUC_INTERNAL
void       _gwy_app_recent_file_write_thumbnail(const gchar *filename_sys,
                                                GwyContainer *data,
                                                GwyAppPage pageno,
                                                gint id,
                                                GdkPixbuf *pixbuf);

G_GNUC_INTERNAL
gint       _gwy_app_analyse_data_key           (const gchar *strkey,
                                                GwyAppKeyType *type,
                                                guint *len);

G_GNUC_INTERNAL
void              _gwy_app_log_start_message_capture    (void);
G_GNUC_INTERNAL
void              _gwy_app_log_discard_captured_messages(void);
G_GNUC_INTERNAL
GwyAppLogMessage* _gwy_app_log_get_captured_messages    (guint *nmesg);
G_GNUC_INTERNAL
void              _gwy_app_data_browser_add_messages    (GwyContainer *data);
G_GNUC_INTERNAL
GtkTextBuffer*    _gwy_app_log_create_textbuf           (void);
G_GNUC_INTERNAL
void              _gwy_app_log_add_message_to_textbuf   (GtkTextBuffer *textbuf,
                                                         const gchar *message,
                                                         GLogLevelFlags log_level);

G_GNUC_INTERNAL
guint _gwy_app_enforce_graph_abscissae_order(GwyContainer *data);

/* data-browser-aux functions */
G_GNUC_INTERNAL
void _gwy_app_data_merge_gather(gpointer key,
                                gpointer value,
                                gpointer user_data);
G_GNUC_INTERNAL
void _gwy_app_data_merge_copy_1(gpointer key,
                                gpointer value,
                                gpointer user_data);
G_GNUC_INTERNAL
void _gwy_app_data_merge_copy_2(gpointer key,
                                gpointer value,
                                gpointer user_data);
G_GNUC_INTERNAL
gint* _gwy_app_find_ids_unmanaged(GwyContainer *data,
                                  GwyAppKeyType keytype,
                                  GType gtype);
G_GNUC_INTERNAL
GwyDataField* _gwy_app_create_brick_preview_field  (GwyBrick *brick);
G_GNUC_INTERNAL
GwyDataField* _gwy_app_create_lawn_preview_field   (GwyLawn *lawn);

G_GNUC_INTERNAL
void _gwy_app_update_data_range_type(GwyDataView *data_view,
                                     gint id);
G_GNUC_INTERNAL
void _gwy_app_sync_mask(GwyContainer *data,
                        GQuark quark,
                        GwyDataView *data_view);
G_GNUC_INTERNAL
void _gwy_app_sync_show(GwyContainer *data,
                        GQuark quark,
                        GwyDataView *data_view);
G_GNUC_INTERNAL
void _gwy_app_update_channel_sens(void);
G_GNUC_INTERNAL
void _gwy_app_update_graph_sens  (void);
G_GNUC_INTERNAL
void _gwy_app_update_brick_sens  (void);
G_GNUC_INTERNAL
void _gwy_app_update_surface_sens(void);
G_GNUC_INTERNAL
void _gwy_app_update_lawn_sens   (void);

G_GNUC_INTERNAL
void _gwy_app_update_3d_window_title(Gwy3DWindow *window3d,
                                     gint id);
G_GNUC_INTERNAL
void _gwy_app_update_brick_info(GwyContainer *data,
                                gint id,
                                GwyDataView *data_view);
G_GNUC_INTERNAL
void _gwy_app_update_surface_info(GwyContainer *data,
                                  gint id,
                                  GwyDataView *data_view);
G_GNUC_INTERNAL
void _gwy_app_update_lawn_info(GwyContainer *data,
                               gint id,
                               GwyDataView *data_view);
G_GNUC_INTERNAL
gchar* _gwy_app_figure_out_channel_title(GwyContainer *data,
                                         gint channel);

G_GNUC_INTERNAL
GQuark _gwy_app_get_page_data_key_for_id(gint id,
                                         GwyAppPage pageno);


G_END_DECLS

#endif /* __GWY_APP_INTERNAL_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
