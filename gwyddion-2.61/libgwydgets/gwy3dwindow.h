/*
 *  $Id: gwy3dwindow.h 24710 2022-03-21 17:35:20Z yeti-dn $
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

#ifndef __GWY_3D_WINDOW_H__
#define __GWY_3D_WINDOW_H__

#include <gdk/gdkwindow.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtktooltips.h>

#include <libgwydgets/gwy3dview.h>

G_BEGIN_DECLS

#define GWY_TYPE_3D_WINDOW            (gwy_3d_window_get_type())
#define GWY_3D_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_3D_WINDOW, Gwy3DWindow))
#define GWY_3D_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_3D_WINDOW, Gwy3DWindowClass))
#define GWY_IS_3D_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_3D_WINDOW))
#define GWY_IS_3D_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_3D_WINDOW))
#define GWY_3D_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_3D_WINDOW, Gwy3DWindowClass))

typedef struct _Gwy3DWindow      Gwy3DWindow;
typedef struct _Gwy3DWindowClass Gwy3DWindowClass;

struct _Gwy3DWindow {
    GtkWindow parent_instance;

    GtkWidget *gwy3dview;
    GtkWidget *gradient_menu;
    GtkWidget *material_menu;
    GtkWidget *material_label;
    GtkWidget *lights_spin1;
    GtkWidget *lights_spin2;
    GtkWidget **buttons;
    GSList    *visual_mode_group;

    GtkWidget *labels_menu;
    GtkWidget *labels_text;
    GtkWidget *labels_delta_x;
    GtkWidget *labels_delta_y;
    GtkWidget *labels_size;
    GtkWidget *labels_rotation; /* to be implemented */
    GtkWidget *labels_autosize;

    GtkWidget *notebook;
    GtkWidget *actions;
    GtkWidget *vbox_small;
    GtkWidget *vbox_large;

    GSList *setup_adjustments;

    GdkWindow *resize_grip;

    GtkWidget *dataov_menu;
    GtkWidget *physcale_entry;
    GtkWidget *label_size_equal;
    GtkWidget *widget5;

    gboolean in_update;
    guint controls_full;  /* should be boolean, but keep ABI */

    gpointer reserved1;
    gpointer reserved2;
    gpointer reserved3;
    gpointer reserved4;
};

struct _Gwy3DWindowClass {
    GtkWindowClass parent_class;

    void (*reserved1)(void);
    void (*reserved2)(void);
    void (*reserved3)(void);
    void (*reserved4)(void);
};

GType        gwy_3d_window_get_type                (void) G_GNUC_CONST;
GtkWidget*   gwy_3d_window_new                     (Gwy3DView *gwy3dview);
GtkWidget*   gwy_3d_window_get_3d_view             (Gwy3DWindow *gwy3dwindow);
void         gwy_3d_window_add_action_widget       (Gwy3DWindow *gwy3dwindow,
                                                    GtkWidget *widget);
void         gwy_3d_window_add_small_toolbar_button(Gwy3DWindow *gwy3dwindow,
                                                    const gchar *stock_id,
                                                    const gchar *tooltip,
                                                    GCallback callback,
                                                    gpointer cbdata);
void         gwy_3d_window_set_overlay_chooser     (Gwy3DWindow *gwy3dwindow,
                                                    GtkWidget *chooser);
#ifndef GWY_DISABLE_DEPRECATED
void         gwy_3d_window_class_set_tooltips      (GtkTooltips *tips);
GtkTooltips* gwy_3d_window_class_get_tooltips      (void);
#endif

G_END_DECLS

#endif /* __GWY_3D_WINDOW_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
