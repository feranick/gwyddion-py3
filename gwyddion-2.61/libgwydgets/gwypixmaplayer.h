/*
 *  $Id: gwypixmaplayer.h 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2003,2004 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_PIXMAP_LAYER_H__
#define __GWY_PIXMAP_LAYER_H__

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <libgwydgets/gwydataviewlayer.h>

G_BEGIN_DECLS

#define GWY_TYPE_PIXMAP_LAYER            (gwy_pixmap_layer_get_type())
#define GWY_PIXMAP_LAYER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_PIXMAP_LAYER, GwyPixmapLayer))
#define GWY_PIXMAP_LAYER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_PIXMAP_LAYER, GwyPixmapLayerClass))
#define GWY_IS_PIXMAP_LAYER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_PIXMAP_LAYER))
#define GWY_IS_PIXMAP_LAYER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_PIXMAP_LAYER))
#define GWY_PIXMAP_LAYER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_PIXMAP_LAYER, GwyPixmapLayerClass))

typedef struct _GwyPixmapLayer      GwyPixmapLayer;
typedef struct _GwyPixmapLayerClass GwyPixmapLayerClass;

struct _GwyPixmapLayer {
    GwyDataViewLayer parent_instance;

    GdkPixbuf *pixbuf;
    GQuark data_key;
    GObject *data_field;
    gulong item_changed_id;
    gulong data_changed_id;
    gulong handler_id;
    gboolean wants_repaint;

    gpointer reserved1;
    gpointer reserved2;
    gint int1;
};

struct _GwyPixmapLayerClass {
    GwyDataViewLayerClass parent_class;

    GdkPixbuf* (*paint)(GwyPixmapLayer *layer);

    void (*reserved1)(void);
    void (*reserved2)(void);
};

GType            gwy_pixmap_layer_get_type      (void) G_GNUC_CONST;
gboolean         gwy_pixmap_layer_wants_repaint (GwyPixmapLayer *pixmap_layer);
GdkPixbuf*       gwy_pixmap_layer_paint         (GwyPixmapLayer *pixmap_layer);
void             gwy_pixmap_layer_set_data_key  (GwyPixmapLayer *pixmap_layer,
                                                 const gchar *key);
const gchar*     gwy_pixmap_layer_get_data_key  (GwyPixmapLayer *pixmap_layer);
void             gwy_pixmap_layer_make_pixbuf   (GwyPixmapLayer *pixmap_layer,
                                                 gboolean has_alpha);

G_END_DECLS

#endif /* __GWY_PIXMAP_LAYER_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

