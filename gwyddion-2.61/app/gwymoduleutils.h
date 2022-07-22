/*
 *  $Id: gwymoduleutils.h 24625 2022-03-03 12:57:58Z yeti-dn $
 *  Copyright (C) 2007-2022 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_MODULE_UTILS_H__
#define __GWY_MODULE_UTILS_H__

#include <stdio.h>
#include <gtk/gtkwindow.h>
#include <libprocess/surface.h>
#include <libprocess/lawn.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwygraphmodel.h>
#include <app/datachooser.h>
#include <app/params.h>

G_BEGIN_DECLS

typedef enum {
    GWY_PREVIEW_SURFACE_DENSITY = 1 << 0,
    GWY_PREVIEW_SURFACE_FILL    = 1 << 1,
} GwyPreviewSurfaceFlags;

typedef gchar* (*GwySaveAuxiliaryCreate)(gpointer user_data,
                                         gssize *data_len);
typedef void (*GwySaveAuxiliaryDestroy)(gchar *data,
                                        gpointer user_data);

gboolean      gwy_save_auxiliary_data               (const gchar *title,
                                                     GtkWindow *parent,
                                                     gssize data_len,
                                                     const gchar *data);
gboolean      gwy_save_auxiliary_with_callback      (const gchar *title,
                                                     GtkWindow *parent,
                                                     GwySaveAuxiliaryCreate create,
                                                     GwySaveAuxiliaryDestroy destroy,
                                                     gpointer user_data);
gboolean      gwy_module_data_load                  (const gchar *modname,
                                                     const gchar *filename,
                                                     gchar **contents,
                                                     gsize *length,
                                                     GError **error);
gboolean      gwy_module_data_save                  (const gchar *modname,
                                                     const gchar *filename,
                                                     gchar *contents,
                                                     gssize length,
                                                     GError **error);
FILE*         gwy_module_data_fopen                 (const gchar *modname,
                                                     const gchar *filename,
                                                     const gchar *mode,
                                                     GError **error);
void          gwy_set_data_preview_size             (GwyDataView *data_view,
                                                     gint max_size);
gboolean      gwy_require_image_same_units          (GwyDataField *field,
                                                     GwyContainer *data,
                                                     gint id,
                                                     const gchar *name);
gboolean      gwy_require_square_image              (GwyDataField *field,
                                                     GwyContainer *data,
                                                     gint id,
                                                     const gchar *name);
GtkWidget*    gwy_create_preview                    (GwyContainer *data,
                                                     gint id,
                                                     gint size,
                                                     gboolean have_mask);
GtkWidget*    gwy_create_dialog_preview_hbox        (GtkDialog *dialog,
                                                     GwyDataView *dataview,
                                                     gboolean pack_end);
GwySelection* gwy_create_preview_vector_layer       (GwyDataView *dataview,
                                                     gint id,
                                                     const gchar *name,
                                                     gint max_objects,
                                                     gboolean editable);
void          gwy_param_active_page_link_to_notebook(GwyParams *params,
                                                     gint id,
                                                     GtkNotebook *notebook);
gint          gwy_app_add_graph_or_curves           (GwyGraphModel *gmodel,
                                                     GwyContainer *data,
                                                     const GwyAppDataId *target_graph,
                                                     gint colorstep);
void          gwy_preview_surface_to_datafield      (GwySurface *surface,
                                                     GwyDataField *dfield,
                                                     gint max_xres,
                                                     gint max_yres,
                                                     GwyPreviewSurfaceFlags flags);
GtkWidget*    gwy_app_wait_preview_data_field       (GwyDataField *dfield,
                                                     GwyContainer *data,
                                                     gint id);
gboolean      gwy_app_data_id_verify_channel        (GwyAppDataId *id);
gboolean      gwy_app_data_id_verify_graph          (GwyAppDataId *id);
gboolean      gwy_app_data_id_verify_volume         (GwyAppDataId *id);
gboolean      gwy_app_data_id_verify_xyz            (GwyAppDataId *id);
gboolean      gwy_app_data_id_verify_curve_map      (GwyAppDataId *id);
gboolean      gwy_app_data_id_verify_spectra        (GwyAppDataId *id);

G_END_DECLS

#endif /* __GWY_MODULE_UTILS_H__ */

/* vim: set cin et columns=120 tw=118 ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
