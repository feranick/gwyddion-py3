/*
 *  $Id: gwygraphcurves.h 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2007 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_GRAPH_CURVES_H__
#define __GWY_GRAPH_CURVES_H__

#include <gtk/gtktreeview.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwynullstore.h>

G_BEGIN_DECLS

#define GWY_TYPE_GRAPH_CURVES            (gwy_graph_curves_get_type())
#define GWY_GRAPH_CURVES(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_GRAPH_CURVES, GwyGraphCurves))
#define GWY_GRAPH_CURVES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_GRAPH_CURVES, GwyGraphCurvesClass))
#define GWY_IS_GRAPH_CURVES(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_GRAPH_CURVES))
#define GWY_IS_GRAPH_CURVES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_GRAPH_CURVES))
#define GWY_GRAPH_CURVES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_GRAPH_CURVES, GwyGraphCurvesClass))

typedef struct _GwyGraphCurves      GwyGraphCurves;
typedef struct _GwyGraphCurvesClass GwyGraphCurvesClass;

struct _GwyGraphCurves {
    GtkTreeView treeview;

    GwyGraphModel *graph_model;
    GtkListStore *curves;
    GdkPixbuf *pixbuf;

    gulong *handler_ids;

    gpointer reserved1;
    gpointer reserved2;
    gpointer reserved3;
    gpointer reserved4;
};

struct _GwyGraphCurvesClass {
    GtkTreeViewClass parent_class;

    void (*reserved1)(void);
    void (*reserved2)(void);
    void (*reserved3)(void);
    void (*reserved4)(void);
};

GType          gwy_graph_curves_get_type (void) G_GNUC_CONST;
GtkWidget*     gwy_graph_curves_new      (GwyGraphModel *gmodel);
void           gwy_graph_curves_set_model(GwyGraphCurves *graph_curves,
                                          GwyGraphModel *gmodel);
GwyGraphModel* gwy_graph_curves_get_model(GwyGraphCurves *graph_curves);

G_END_DECLS

#endif /* __GWY_GRAPH_CURVES_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
