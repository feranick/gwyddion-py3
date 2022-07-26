/*
 *  $Id: gwygraphwindow.h 24710 2022-03-21 17:35:20Z yeti-dn $
 *  Copyright (C) 2004 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_GRAPH_WINDOW_H__
#define __GWY_GRAPH_WINDOW_H__

#include <gtk/gtkwindow.h>
#include <gtk/gtktooltips.h>

#include <libgwydgets/gwygraph.h>

G_BEGIN_DECLS

#define GWY_TYPE_GRAPH_WINDOW            (gwy_graph_window_get_type())
#define GWY_GRAPH_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_GRAPH_WINDOW, GwyGraphWindow))
#define GWY_GRAPH_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_GRAPH_WINDOW, GwyGraphWindowClass))
#define GWY_IS_GRAPH_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_GRAPH_WINDOW))
#define GWY_IS_GRAPH_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_GRAPH_WINDOW))
#define GWY_GRAPH_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_GRAPH_WINDOW, GwyGraphWindowClass))

typedef struct _GwyGraphWindow      GwyGraphWindow;
typedef struct _GwyGraphWindowClass GwyGraphWindowClass;

struct _GwyGraphWindow {
    GtkWindow parent_instance;

    GtkWidget *notebook;
    GtkWidget *graph;
    GtkWidget *data;

    GtkWidget *measure_dialog;

    GtkWidget *button_measure_points;
    GtkWidget *button_measure_lines;

    GtkWidget *button_zoom_in;
    GtkWidget *button_zoom_to_fit;

    GtkWidget *button_x_log;
    GtkWidget *button_y_log;

    GtkWidget *statusbar;

    GwyGraphStatusType last_status;

    GtkWidget *curves;
    GtkWidget *widget2;
    GtkWidget *widget3;
    GtkWidget *widget4;
    GtkWidget *widget5;

    gpointer reserved1;
    gpointer reserved2;
    gpointer reserved3;
    gpointer reserved4;
};

struct _GwyGraphWindowClass {
    GtkWindowClass parent_class;

    void (*reserved1)(void);
    void (*reserved2)(void);
    void (*reserved3)(void);
    void (*reserved4)(void);
};

GType        gwy_graph_window_get_type          (void) G_GNUC_CONST;
GtkWidget*   gwy_graph_window_new               (GwyGraph *graph);
GtkWidget*   gwy_graph_window_get_graph         (GwyGraphWindow *graphwindow);
GtkWidget*   gwy_graph_window_get_graph_data    (GwyGraphWindow *graphwindow);
GtkWidget*   gwy_graph_window_get_graph_curves  (GwyGraphWindow *graphwindow);
#ifndef GWY_DISABLE_DEPRECATED
void         gwy_graph_window_class_set_tooltips(GtkTooltips *tips);
GtkTooltips* gwy_graph_window_class_get_tooltips(void);
#endif

G_END_DECLS

#endif /* __GWY_GRAPH_WINDOW_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
