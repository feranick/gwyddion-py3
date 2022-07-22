/*
 *  $Id: datachooser.h 24088 2021-09-07 12:12:49Z yeti-dn $
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

#ifndef __GWY_DATA_CHOOSER_H__
#define __GWY_DATA_CHOOSER_H__

#include <libgwyddion/gwycontainer.h>
#include <libprocess/datafield.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct {
    gint datano;
    gint id;
} GwyAppDataId;

#define GWY_APP_DATA_ID_NONE { 0, -1 }

#define GWY_TYPE_APP_DATA_ID              (gwy_app_data_id_get_type())

#define GWY_TYPE_DATA_CHOOSER             (gwy_data_chooser_get_type())
#define GWY_DATA_CHOOSER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_DATA_CHOOSER, GwyDataChooser))
#define GWY_DATA_CHOOSER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_DATA_CHOOSER, GwyDataChooserClass))
#define GWY_IS_DATA_CHOOSER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_DATA_CHOOSER))
#define GWY_IS_DATA_CHOOSER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_DATA_CHOOSER))
#define GWY_DATA_CHOOSER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_DATA_CHOOSER, GwyDataChooserClass))

typedef gboolean (*GwyDataChooserFilterFunc)(GwyContainer *data,
                                             gint id,
                                             gpointer user_data);

typedef struct _GwyDataChooser      GwyDataChooser;
typedef struct _GwyDataChooserClass GwyDataChooserClass;

GtkWidget* gwy_data_chooser_new_channels  (void);
GtkWidget* gwy_data_chooser_new_volumes   (void);
GtkWidget* gwy_data_chooser_new_graphs    (void);
GtkWidget* gwy_data_chooser_new_xyzs      (void);
GtkWidget* gwy_data_chooser_new_curve_maps(void);

GType         gwy_app_data_id_get_type      (void)                            G_GNUC_CONST;
GwyAppDataId* gwy_app_data_id_new           (gint datano,
                                             gint id)                         G_GNUC_MALLOC;
GwyAppDataId* gwy_app_data_id_copy          (GwyAppDataId *dataid)            G_GNUC_MALLOC;
void          gwy_app_data_id_free          (GwyAppDataId *dataid);

GType         gwy_data_chooser_get_type     (void)                            G_GNUC_CONST;
gboolean      gwy_data_chooser_set_active   (GwyDataChooser *chooser,
                                             GwyContainer *data,
                                             gint id);
GwyContainer* gwy_data_chooser_get_active   (GwyDataChooser *chooser,
                                             gint *id);
gboolean      gwy_data_chooser_set_active_id(GwyDataChooser *chooser,
                                             const GwyAppDataId *id);
gboolean      gwy_data_chooser_get_active_id(GwyDataChooser *chooser,
                                             GwyAppDataId *id);
void          gwy_data_chooser_set_filter   (GwyDataChooser *chooser,
                                             GwyDataChooserFilterFunc filter,
                                             gpointer user_data,
                                             GtkDestroyNotify destroy);
const gchar*  gwy_data_chooser_get_none     (GwyDataChooser *chooser);
void          gwy_data_chooser_set_none     (GwyDataChooser *chooser,
                                             const gchar *none);
GtkTreeModel* gwy_data_chooser_get_filter   (GwyDataChooser *chooser);
void          gwy_data_chooser_refilter     (GwyDataChooser *chooser);

G_END_DECLS

#endif /* __GWY_DATA_CHOOSER_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
