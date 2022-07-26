/*
 *  $Id: gwygraph.h 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2003 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_GRAPH_H__
#define __GWY_GRAPH_H__

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtktable.h>

#include <libgwydgets/gwyaxis.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwygraphbasics.h>
#include <libgwydgets/gwygraphlabel.h>
#include <libgwydgets/gwygraphcorner.h>
#include <libgwydgets/gwygrapharea.h>

G_BEGIN_DECLS

#define GWY_TYPE_GRAPH            (gwy_graph_get_type())
#define GWY_GRAPH(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_GRAPH, GwyGraph))
#define GWY_GRAPH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_GRAPH, GwyGraphClass))
#define GWY_IS_GRAPH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_GRAPH))
#define GWY_IS_GRAPH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_GRAPH))
#define GWY_GRAPH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_GRAPH, GwyGraphClass))

typedef struct _GwyGraph      GwyGraph;
typedef struct _GwyGraphClass GwyGraphClass;


struct _GwyGraph {
    GtkTable table;

    GwyGraphModel *graph_model;

    GwyGraphArea *area;
    GwySelection *zoom_selection;
    GwyAxis *axis[4];
    GwyGraphCorner *corner[4];

    gboolean enable_user_input;
    gboolean boolean1;

    gulong rescaled_id[4];
    gulong model_notify_id;
    gulong curve_data_changed_id;
    gulong zoom_finished_id;
    gulong handler_id1;
    gulong handler_id2;

    gpointer reserved1;
    gpointer reserved2;
};

struct _GwyGraphClass {
    GtkTableClass parent_class;

    void (*reserved1)(void);
    void (*reserved2)(void);
    void (*reserved3)(void);
    void (*reserved4)(void);
};

GType              gwy_graph_get_type         (void) G_GNUC_CONST;
GtkWidget*         gwy_graph_new              (GwyGraphModel *gmodel);
GwyAxis*           gwy_graph_get_axis         (GwyGraph *graph,
                                               GtkPositionType type);
void               gwy_graph_set_axis_visible (GwyGraph *graph,
                                               GtkPositionType type,
                                               gboolean is_visible);
GtkWidget*         gwy_graph_get_area         (GwyGraph *graph);
void               gwy_graph_set_model        (GwyGraph *graph,
                                               GwyGraphModel *gmodel);
GwyGraphModel*     gwy_graph_get_model        (GwyGraph *graph);
void               gwy_graph_set_status       (GwyGraph *graph,
                                               GwyGraphStatusType status);
GwyGraphStatusType gwy_graph_get_status       (GwyGraph *graph);
void               gwy_graph_enable_user_input(GwyGraph *graph,
                                               gboolean enable);
GdkPixbuf*         gwy_graph_export_pixmap    (GwyGraph *graph,
                                               gboolean export_title,
                                               gboolean export_axis,
                                               gboolean export_labels);
GString*           gwy_graph_export_postscript(GwyGraph *graph,
                                               gboolean export_title,
                                               gboolean export_axis,
                                               gboolean export_labels,
                                               GString *str);

G_END_DECLS

#endif /* __GWY_GRAPH_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
