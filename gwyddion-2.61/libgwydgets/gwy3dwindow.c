/*
 *  $Id: gwy3dwindow.c 24710 2022-03-21 17:35:20Z yeti-dn $
 *  Copyright (C) 2004-2017 David Necas (Yeti), Petr Klapetek.
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

#include "config.h"
#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtkclipboard.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtkradiobutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtktable.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkvbox.h>

#include <libprocess/stats.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwydgets/gwy3dwindow.h>
#include <libgwydgets/gwyoptionmenus.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwystock.h>
#include <libprocess/gwyprocess.h>

#define ZOOM_FACTOR 1.3195

enum {
    DEFAULT_WIDTH = 620,
    DEFAULT_HEIGHT = 360,
};

enum {
    N_BUTTONS = GWY_3D_MOVEMENT_LIGHT + 1
};

static void          gwy_3d_window_destroy              (GtkObject *object);
static void          gwy_3d_window_finalize             (GObject *object);
static GdkWindowEdge gwy_3d_window_get_grip_edge        (Gwy3DWindow *gwy3dwindow);
static void          gwy_3d_window_get_grip_rect        (Gwy3DWindow *gwy3dwindow,
                                                         GdkRectangle *rect);
static void          gwy_3d_window_set_grip_cursor      (Gwy3DWindow *gwy3dwindow);
static void          gwy_3d_window_create_resize_grip   (Gwy3DWindow *gwy3dwindow);
static void          gwy_3d_window_direction_changed    (GtkWidget *widget,
                                                         GtkTextDirection prev_dir);
static void          gwy_3d_window_destroy_resize_grip  (Gwy3DWindow *gwy3dwindow);
static gboolean      gwy_3d_window_configure            (GtkWidget *widget,
                                                         GdkEventConfigure *event);
static void          gwy_3d_window_realize              (GtkWidget *widget);
static void          gwy_3d_window_unrealize            (GtkWidget *widget);
static void          gwy_3d_window_map                  (GtkWidget *widget);
static void          gwy_3d_window_unmap                (GtkWidget *widget);
static gboolean      gwy_3d_window_button_press         (GtkWidget *widget,
                                                         GdkEventButton *event);
static gboolean      gwy_3d_window_key_pressed          (GtkWidget *widget,
                                                         GdkEventKey *event);
static gboolean      gwy_3d_window_expose               (GtkWidget *widget,
                                                         GdkEventExpose *event);
static void          gwy_3d_window_resize               (Gwy3DWindow *gwy3dwindow,
                                                         gint zoomtype);
static void          gwy_3d_window_pack_buttons         (Gwy3DWindow *gwy3dwindow,
                                                         guint offset,
                                                         GtkBox *box);
static void          gwy_3d_window_setup_adj_changed    (GtkAdjustment *adj,
                                                         Gwy3DSetup *setup);
static void          gwy_3d_window_adj_setup_changed    (Gwy3DSetup *setup,
                                                         GParamSpec *pspec,
                                                         GtkAdjustment *adj);
static GtkWidget*    gwy_3d_window_build_basic_tab      (Gwy3DWindow *window);
static GtkWidget*    gwy_3d_window_build_visual_tab     (Gwy3DWindow *window);
static GtkWidget*    gwy_3d_window_build_label_tab      (Gwy3DWindow *window);
static GtkWidget*    gwy_3d_window_build_colorbar_tab   (Gwy3DWindow *window);
static void          gwy_3d_window_set_mode             (gpointer userdata,
                                                         GtkWidget *button);
static void          gwy_3d_window_set_gradient         (GtkTreeSelection *selection,
                                                         Gwy3DWindow *gwy3dwindow);
static void          gwy_3d_window_set_material         (GtkTreeSelection *selection,
                                                         Gwy3DWindow *gwy3dwindow);
static void          gwy_3d_window_select_controls      (gpointer data,
                                                         GtkWidget *button);
static void          gwy_3d_window_set_labels           (GtkWidget *combo,
                                                         Gwy3DWindow *gwy3dwindow);
static void          gwy_3d_window_label_adj_changed    (GtkAdjustment *adj,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_projection_changed   (GtkToggleButton *check,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_show_axes_changed    (GtkToggleButton *check,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_hide_masked_changed  (GtkToggleButton *check,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_show_labels_changed  (GtkToggleButton *check,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_fmscale_rspace_changed(GtkToggleButton *check,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_show_fmscale_changed (GtkToggleButton *check,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_display_mode_changed (GtkWidget *item,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_set_visualization    (Gwy3DWindow *window,
                                                         Gwy3DVisualization visual);
static void          gwy_3d_window_visualization_changed(Gwy3DSetup *setup,
                                                         GParamSpec *pspec,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_auto_scale_changed   (GtkToggleButton *check,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_label_size_eq_changed(GtkToggleButton *check,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_labels_entry_activate(GtkEntry *entry,
                                                         Gwy3DWindow *window);
static void          gwy_3d_window_labels_reset_clicked (Gwy3DWindow *window);
static void          gwy_3d_window_reset_visualisation  (Gwy3DWindow *window);
static gboolean      gwy_3d_window_view_clicked         (GtkWidget *gwy3dwindow,
                                                         GdkEventButton *event,
                                                         GtkWidget *gwy3dview);
static void          gwy_3d_window_visual_selected      (GtkWidget *item,
                                                         Gwy3DWindow *gwy3dwindow);
static void          gwy_3d_window_gradient_selected    (GtkWidget *item,
                                                         Gwy3DWindow *gwy3dwindow);
static void          gwy_3d_window_material_selected    (GtkWidget *item,
                                                         Gwy3DWindow *gwy3dwindow);
static void          gwy_3d_window_set_zscale           (Gwy3DWindow *window);
static void          update_physcale_entry              (Gwy3DWindow *window,
                                                         GtkAdjustment *adj);
static void          sync_other_labels_to_current       (Gwy3DWindow *window);

/* These are actually class data.  To put them to Class struct someone would
 * have to do class_ref() and live with this reference to the end of time. */
static GtkTooltips *tooltips = NULL;
static gboolean tooltips_set = FALSE;
static GQuark adj_property_quark = 0;

G_DEFINE_TYPE(Gwy3DWindow, gwy_3d_window, GTK_TYPE_WINDOW)

static void
gwy_3d_window_class_init(Gwy3DWindowClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    GtkObjectClass *object_class;

    object_class = (GtkObjectClass*)klass;

    gobject_class->finalize = gwy_3d_window_finalize;
    object_class->destroy = gwy_3d_window_destroy;

    widget_class->realize = gwy_3d_window_realize;
    widget_class->unrealize = gwy_3d_window_unrealize;
    widget_class->map = gwy_3d_window_map;
    widget_class->unmap = gwy_3d_window_unmap;

    widget_class->configure_event = gwy_3d_window_configure;
    widget_class->expose_event = gwy_3d_window_expose;
    widget_class->button_press_event = gwy_3d_window_button_press;
    widget_class->key_press_event = gwy_3d_window_key_pressed;

    widget_class->direction_changed = gwy_3d_window_direction_changed;
}

static void
gwy_3d_window_init(G_GNUC_UNUSED Gwy3DWindow *gwy3dwindow)
{
    if (!tooltips_set && !tooltips) {
        tooltips = gtk_tooltips_new();
        g_object_ref(tooltips);
        gtk_object_sink(GTK_OBJECT(tooltips));
    }
}

static void
gwy_3d_window_finalize(GObject *object)
{
    Gwy3DWindow *gwy3dwindow;

    gwy3dwindow = GWY_3D_WINDOW(object);

    g_free(gwy3dwindow->buttons);

    G_OBJECT_CLASS(gwy_3d_window_parent_class)->finalize(object);
}

static void
gwy_3d_window_destroy(GtkObject *object)
{
    Gwy3DWindow *gwy3dwindow;
    gwy3dwindow = GWY_3D_WINDOW(object);

    if (gwy3dwindow->gwy3dview) {
        Gwy3DSetup *setup;
        GtkAdjustment *adj;

        setup = gwy_3d_view_get_setup(GWY_3D_VIEW(gwy3dwindow->gwy3dview));
        g_signal_handlers_disconnect_matched(setup,
                                             G_SIGNAL_MATCH_FUNC
                                             | G_SIGNAL_MATCH_DATA,
                                             0, 0, NULL,
                                             gwy_3d_window_visualization_changed,
                                             gwy3dwindow);

        while (gwy3dwindow->setup_adjustments) {
            adj = GTK_ADJUSTMENT(gwy3dwindow->setup_adjustments->data);
            g_signal_handlers_disconnect_matched(setup,
                                                 G_SIGNAL_MATCH_FUNC
                                                 | G_SIGNAL_MATCH_DATA,
                                                 0, 0, NULL,
                                                 gwy_3d_window_adj_setup_changed,
                                                 adj);
            gwy3dwindow->setup_adjustments
                = g_slist_remove(gwy3dwindow->setup_adjustments, adj);
        }
        gwy3dwindow->gwy3dview = NULL;
    }

    GTK_OBJECT_CLASS(gwy_3d_window_parent_class)->destroy(object);
}

static GdkWindowEdge
gwy_3d_window_get_grip_edge(Gwy3DWindow *gwy3dwindow)
{
    GtkWidget *widget = GTK_WIDGET(gwy3dwindow);

    if (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_LTR)
        return GDK_WINDOW_EDGE_SOUTH_EAST;
    else
        return GDK_WINDOW_EDGE_SOUTH_WEST;
}

static void
gwy_3d_window_get_grip_rect(Gwy3DWindow *gwy3dwindow,
                            GdkRectangle *rect)
{
    GtkWidget *widget;
    gint w, h;

    widget = GTK_WIDGET(gwy3dwindow);

    /* These are in effect the max/default size of the grip. */
    w = 18;
    h = 18;

    if (w > widget->allocation.width)
        w = widget->allocation.width;

    if (h > widget->allocation.height - widget->style->ythickness)
        h = widget->allocation.height - widget->style->ythickness;

    rect->width = w;
    rect->height = h;
    rect->y = widget->allocation.y + widget->allocation.height - h;

    if (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_LTR)
        rect->x = widget->allocation.x + widget->allocation.width - w;
    else
        rect->x = widget->allocation.x + widget->style->xthickness;
}

static void
gwy_3d_window_set_grip_cursor(Gwy3DWindow *gwy3dwindow)
{
    GtkWidget *widget = GTK_WIDGET(gwy3dwindow);
    GdkDisplay *display = gtk_widget_get_display(widget);
    GdkCursorType cursor_type;
    GdkCursor *cursor;

    if (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_LTR)
        cursor_type = GDK_BOTTOM_RIGHT_CORNER;
    else
        cursor_type = GDK_BOTTOM_LEFT_CORNER;

    cursor = gdk_cursor_new_for_display(display, cursor_type);
    gdk_window_set_cursor(gwy3dwindow->resize_grip, cursor);
    gdk_cursor_unref(cursor);
}

static void
gwy_3d_window_create_resize_grip(Gwy3DWindow *gwy3dwindow)
{
    GtkWidget *widget;
    GdkWindowAttr attributes;
    gint attributes_mask;
    GdkRectangle rect;

    g_return_if_fail(GTK_WIDGET_REALIZED(gwy3dwindow));

    widget = GTK_WIDGET(gwy3dwindow);
    gwy_3d_window_get_grip_rect(gwy3dwindow, &rect);

    attributes.x = rect.x;
    attributes.y = rect.y;
    attributes.width = rect.width;
    attributes.height = rect.height;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.wclass = GDK_INPUT_ONLY;
    attributes.event_mask = gtk_widget_get_events(widget)
                            | GDK_BUTTON_PRESS_MASK;
    attributes_mask = GDK_WA_X | GDK_WA_Y;

    gwy3dwindow->resize_grip = gdk_window_new(widget->window,
                                              &attributes, attributes_mask);
    gdk_window_set_user_data(gwy3dwindow->resize_grip, widget);
    gwy_3d_window_set_grip_cursor(gwy3dwindow);
}

static void
gwy_3d_window_direction_changed(GtkWidget *widget,
                                G_GNUC_UNUSED GtkTextDirection prev_dir)
{
    Gwy3DWindow *gwy3dwindow = GWY_3D_WINDOW(widget);

    gwy_3d_window_set_grip_cursor(gwy3dwindow);
}

static void
gwy_3d_window_destroy_resize_grip(Gwy3DWindow *gwy3dwindow)
{
    gdk_window_set_user_data(gwy3dwindow->resize_grip, NULL);
    gdk_window_destroy(gwy3dwindow->resize_grip);
    gwy3dwindow->resize_grip = NULL;
}

static gboolean
gwy_3d_window_configure(GtkWidget *widget,
                        GdkEventConfigure *event)
{
    GdkRectangle rect;
    Gwy3DWindow *gwy3dwindow = GWY_3D_WINDOW(widget);

    GTK_WIDGET_CLASS(gwy_3d_window_parent_class)->configure_event(widget,
                                                                  event);
    gwy_3d_window_get_grip_rect(gwy3dwindow, &rect);
    gdk_window_move(gwy3dwindow->resize_grip, rect.x, rect.y);

    return FALSE;
}

static void
gwy_3d_window_realize(GtkWidget *widget)
{
    GTK_WIDGET_CLASS(gwy_3d_window_parent_class)->realize(widget);
    gwy_3d_window_create_resize_grip(GWY_3D_WINDOW(widget));
}

static void
gwy_3d_window_unrealize(GtkWidget *widget)
{
    gwy_3d_window_destroy_resize_grip(GWY_3D_WINDOW(widget));
    GTK_WIDGET_CLASS(gwy_3d_window_parent_class)->unrealize(widget);
}

static void
gwy_3d_window_map(GtkWidget *widget)
{
    GTK_WIDGET_CLASS(gwy_3d_window_parent_class)->map(widget);
    gdk_window_show(GWY_3D_WINDOW(widget)->resize_grip);
}

static void
gwy_3d_window_unmap(GtkWidget *widget)
{
    gdk_window_hide(GWY_3D_WINDOW(widget)->resize_grip);
    GTK_WIDGET_CLASS(gwy_3d_window_parent_class)->unmap(widget);
}

static gboolean
gwy_3d_window_button_press(GtkWidget *widget,
                           GdkEventButton *event)
{
    Gwy3DWindow *gwy3dwindow;

    gwy3dwindow = GWY_3D_WINDOW(widget);
    if (event->type != GDK_BUTTON_PRESS
        || event->window != gwy3dwindow->resize_grip)
        return FALSE;

    if (event->button == 1) {
        gtk_window_begin_resize_drag(GTK_WINDOW(widget),
                                     gwy_3d_window_get_grip_edge(gwy3dwindow),
                                     event->button,
                                     event->x_root, event->y_root, event->time);
    }
    else if (event->button == 2) {
        gtk_window_begin_move_drag(GTK_WINDOW(widget),
                                   event->button,
                                   event->x_root, event->y_root, event->time);
    }
    else
        return FALSE;

    return TRUE;
}

static void
gwy_3d_window_copy_to_clipboard(Gwy3DWindow *gwy3dwindow)
{
    GtkClipboard *clipboard;
    GdkDisplay *display;
    GdkPixbuf *pixbuf;
    GdkAtom atom;

    display = gtk_widget_get_display(GTK_WIDGET(gwy3dwindow));
    atom = gdk_atom_intern("CLIPBOARD", FALSE);
    clipboard = gtk_clipboard_get_for_display(display, atom);
    pixbuf = gwy_3d_view_get_pixbuf(GWY_3D_VIEW(gwy3dwindow->gwy3dview));
    gtk_clipboard_set_image(clipboard, pixbuf);
    g_object_unref(pixbuf);
}

static gboolean
gwy_3d_window_key_pressed(GtkWidget *widget,
                          GdkEventKey *event)
{
    enum {
        important_mods = GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_RELEASE_MASK
    };
    Gwy3DWindow *gwy3dwindow;
    gboolean (*method)(GtkWidget*, GdkEventKey*);
    guint state, key;

    gwy_debug("state = %u, keyval = %u", event->state, event->keyval);
    gwy3dwindow = GWY_3D_WINDOW(widget);
    state = event->state & important_mods;
    key = event->keyval;
    if (state == GDK_CONTROL_MASK && (key == GDK_C || key == GDK_c)) {
        gwy_3d_window_copy_to_clipboard(gwy3dwindow);
        return TRUE;
    }

    if (!gwy3dwindow->controls_full && !state) {
        Gwy3DMovement movement = GWY_3D_MOVEMENT_NONE;

        if (key == GDK_R || key == GDK_r)
            movement = GWY_3D_MOVEMENT_ROTATION;
        else if (key == GDK_S || key == GDK_s)
            movement = GWY_3D_MOVEMENT_SCALE;
        else if (key == GDK_V || key == GDK_v)
            movement = GWY_3D_MOVEMENT_DEFORMATION;
        else if (key == GDK_L || key == GDK_l)
            movement = GWY_3D_MOVEMENT_LIGHT;

        if (movement != GWY_3D_MOVEMENT_NONE) {
            gtk_button_clicked(GTK_BUTTON(gwy3dwindow->buttons[movement]));
            return TRUE;
        }

        if (key == GDK_minus || key == GDK_KP_Subtract) {
            gwy_3d_window_resize(gwy3dwindow, -1);
            return TRUE;
        }
        else if (key == GDK_equal || key == GDK_KP_Equal
                 || key == GDK_plus || key == GDK_KP_Add) {
            gwy_3d_window_resize(gwy3dwindow, 1);
            return TRUE;
        }
        else if (key == GDK_Z || key == GDK_z || key == GDK_KP_Divide) {
            gwy_3d_window_resize(gwy3dwindow, 0);
            return TRUE;
        }
    }

    method = GTK_WIDGET_CLASS(gwy_3d_window_parent_class)->key_press_event;
    return method ? method(widget, event) : FALSE;
}

static gboolean
gwy_3d_window_expose(GtkWidget *widget,
                     GdkEventExpose *event)
{
    Gwy3DWindow *gwy3dwindow;
    GdkRectangle rect;

    gwy3dwindow = GWY_3D_WINDOW(widget);
    GTK_WIDGET_CLASS(gwy_3d_window_parent_class)->expose_event(widget, event);

    gwy_3d_window_get_grip_rect(gwy3dwindow, &rect);
    gtk_paint_resize_grip(widget->style,
                          widget->window,
                          GTK_WIDGET_STATE(widget),
                          NULL, widget, "gwy3dwindow",
                          gwy_3d_window_get_grip_edge(gwy3dwindow),
                          rect.x, rect.y, rect.width, rect.height);

    return FALSE;
}

static void
gwy_3d_window_resize(Gwy3DWindow *gwy3dwindow, gint zoomtype)
{
    GtkWindow *window = GTK_WINDOW(gwy3dwindow);
    GtkWidget *widget = GTK_WIDGET(gwy3dwindow);
    gint w, h;

    gtk_window_get_size(window, &w, &h);
    if (zoomtype > 0) {
        GdkScreen *screen = gtk_widget_get_screen(widget);
        gint scrwidth = gdk_screen_get_width(screen);
        gint scrheight = gdk_screen_get_height(screen);

        w = GWY_ROUND(ZOOM_FACTOR*w);
        h = GWY_ROUND(ZOOM_FACTOR*h);
        if (w > 0.9*scrwidth || h > 0.9*scrheight) {
            if ((gdouble)w/scrwidth > (gdouble)h/scrheight) {
                h = GWY_ROUND(0.9*scrwidth*h/w);
                w = GWY_ROUND(0.9*scrwidth);
            }
            else {
                w = GWY_ROUND(0.9*scrheight*w/h);
                h = GWY_ROUND(0.9*scrheight);
            }
        }
    }
    else if (zoomtype < 0) {
        GtkRequisition req = widget->requisition;

        w = GWY_ROUND(w/ZOOM_FACTOR);
        h = GWY_ROUND(h/ZOOM_FACTOR);
        if (w < req.width || h < req.height) {
            if ((gdouble)w/req.width < (gdouble)h/req.height) {
                h = GWY_ROUND((gdouble)req.width*h/w);
                w = req.width;
            }
            else {
                w = GWY_ROUND((gdouble)req.height*w/h);
                h = req.height;
            }
        }
    }
    else {
        w = DEFAULT_WIDTH;
        h = DEFAULT_HEIGHT;
    }

    gtk_window_resize(window, w, h);
}


static void
gwy_3d_window_pack_buttons(Gwy3DWindow *gwy3dwindow,
                           guint offset,
                           GtkBox *box)
{
    static struct {
        Gwy3DMovement mode;
        const gchar *stock_id;
        const gchar *tooltip;
    }
    const buttons[] = {
        {
            GWY_3D_MOVEMENT_ROTATION,
            GWY_STOCK_ROTATE_3D,
            N_("Rotate view (R)")
        },
        {
            GWY_3D_MOVEMENT_SCALE,
            GWY_STOCK_SCALE,
            N_("Scale view as a whole (S)")
        },
        {
            GWY_3D_MOVEMENT_DEFORMATION,
            GWY_STOCK_SCALE_VERTICALLY,
            N_("Scale value range (V)")
        },
        {
            GWY_3D_MOVEMENT_LIGHT,
            GWY_STOCK_LIGHT_ROTATE,
            N_("Move light source (L)")
        },
    };
    GtkWidget *button;
    GtkRadioButton *group = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS(buttons); i++) {
        button = gtk_radio_button_new_from_widget(group);
        gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
        g_object_set(button, "draw-indicator", FALSE, NULL);
        gtk_container_add(GTK_CONTAINER(button),
                          gtk_image_new_from_stock(buttons[i].stock_id,
                                               GTK_ICON_SIZE_LARGE_TOOLBAR));
        g_signal_connect_swapped(button, "clicked",
                                 G_CALLBACK(gwy_3d_window_set_mode),
                                 GINT_TO_POINTER(buttons[i].mode));
        g_object_set_data(G_OBJECT(button), "gwy3dwindow", gwy3dwindow);
        gtk_widget_set_tooltip_text(button, _(buttons[i].tooltip));
        gwy3dwindow->buttons[offset + buttons[i].mode] = button;
        if (!group)
            group = GTK_RADIO_BUTTON(button);
    }
}

/**
 * gwy_3d_window_new:
 * @gwy3dview: A #Gwy3DView containing the data-displaying widget to show.
 *
 * Creates a new OpenGL 3D data displaying window.
 *
 * Returns: A newly created widget, as #GtkWidget.
 **/
GtkWidget*
gwy_3d_window_new(Gwy3DView *gwy3dview)
{
    Gwy3DWindow *gwy3dwindow;
    GtkWidget *vbox, *hbox, *hbox2, *button;

    g_return_val_if_fail(GWY_IS_3D_VIEW(gwy3dview), NULL);

    if (!adj_property_quark) {
        adj_property_quark
            = g_quark_from_static_string("gwy-3d-window-label-property-id");
    }

    gwy3dwindow = (Gwy3DWindow*)g_object_new(GWY_TYPE_3D_WINDOW, NULL);
    gtk_window_set_wmclass(GTK_WINDOW(gwy3dwindow), "data",
                           g_get_application_name());
    gtk_window_set_resizable(GTK_WINDOW(gwy3dwindow), TRUE);

    gwy3dwindow->buttons = g_new0(GtkWidget*, 2*N_BUTTONS);
    gwy3dwindow->in_update = FALSE;

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(gwy3dwindow), hbox);

    gwy3dwindow->gwy3dview = (GtkWidget*)gwy3dview;
    gtk_box_pack_start(GTK_BOX(hbox), gwy3dwindow->gwy3dview, TRUE, TRUE, 0);
    g_signal_connect_swapped(gwy3dwindow->gwy3dview, "button-press-event",
                             G_CALLBACK(gwy_3d_window_view_clicked),
                             gwy3dwindow);
    gwy_3d_view_set_movement_type(GWY_3D_VIEW(gwy3dwindow->gwy3dview),
                                  GWY_3D_MOVEMENT_ROTATION);

    /* Small toolbar */
    gwy3dwindow->vbox_small = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), gwy3dwindow->vbox_small, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(gwy3dwindow->vbox_small), 4);

    button = gtk_button_new();
    gtk_box_pack_start(GTK_BOX(gwy3dwindow->vbox_small), button,
                       FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_stock(GWY_STOCK_MORE,
                                               GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_widget_set_tooltip_text(button, _("Show full controls"));
    g_object_set_data(G_OBJECT(button), "gwy3dwindow", gwy3dwindow);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_3d_window_select_controls),
                             GINT_TO_POINTER(FALSE));

    gwy_3d_window_pack_buttons(gwy3dwindow, 0,
                               GTK_BOX(gwy3dwindow->vbox_small));

    /* Large toolbar */
    gwy3dwindow->vbox_large = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), gwy3dwindow->vbox_large, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(gwy3dwindow->vbox_large), 4);
    gtk_widget_set_no_show_all(gwy3dwindow->vbox_large, TRUE);

    hbox2 = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gwy3dwindow->vbox_large), hbox2,
                       FALSE, FALSE, 0);

    button = gtk_button_new();
    gtk_box_pack_end(GTK_BOX(hbox2), button, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_stock(GWY_STOCK_LESS,
                                               GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_widget_set_tooltip_text(button, _("Hide full controls"));
    g_object_set_data(G_OBJECT(button), "gwy3dwindow", gwy3dwindow);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_3d_window_select_controls),
                             GINT_TO_POINTER(TRUE));

    gwy_3d_window_pack_buttons(gwy3dwindow, N_BUTTONS, GTK_BOX(hbox2));

    gwy3dwindow->notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(gwy3dwindow->vbox_large), gwy3dwindow->notebook,
                       TRUE, TRUE, 0);

    /* Basic table */
    vbox = gwy_3d_window_build_basic_tab(gwy3dwindow);
    gtk_notebook_append_page(GTK_NOTEBOOK(gwy3dwindow->notebook),
                             vbox,
                             gtk_label_new(gwy_sgettext("adjective|Basic")));

    /* Light & Material table */
    vbox = gwy_3d_window_build_visual_tab(gwy3dwindow);
    gtk_notebook_append_page(GTK_NOTEBOOK(gwy3dwindow->notebook),
                             vbox, gtk_label_new(_("Light & Material")));

    /* Labels table */
    vbox = gwy_3d_window_build_label_tab(gwy3dwindow);
    gtk_notebook_append_page(GTK_NOTEBOOK(gwy3dwindow->notebook),
                             vbox, gtk_label_new(_("Labels")));

    /* Colorbar table */
    vbox = gwy_3d_window_build_colorbar_tab(gwy3dwindow);
    gtk_notebook_append_page(GTK_NOTEBOOK(gwy3dwindow->notebook),
                             vbox, gtk_label_new(_("Colorbar")));

    gtk_widget_show_all(hbox);

    gtk_window_set_default_size(GTK_WINDOW(gwy3dwindow),
                                DEFAULT_WIDTH, DEFAULT_HEIGHT);

    return GTK_WIDGET(gwy3dwindow);
}

static void
gwy_3d_window_setup_adj_changed(GtkAdjustment *adj,
                                Gwy3DSetup *setup)
{
    const gchar *property;
    gboolean rad2deg;
    gulong id;

    property = g_object_get_data(G_OBJECT(adj), "gwy-3d-window-setup-property");
    rad2deg = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(adj),
                                                "gwy-3d-window-setup-rad2deg"));
    id = g_signal_handler_find(setup, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                               0, 0, 0, gwy_3d_window_adj_setup_changed, adj);
    g_signal_handler_block(setup, id);
    g_object_set(setup,
                 property, rad2deg ? G_PI/180.0*adj->value : adj->value,
                 NULL);
    g_signal_handler_unblock(setup, id);
}

static void
gwy_3d_window_adj_setup_changed(Gwy3DSetup *setup,
                                GParamSpec *pspec,
                                GtkAdjustment *adj)
{
    gboolean rad2deg;
    gdouble value;
    gulong id;

    rad2deg = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(adj),
                                                "gwy-3d-window-setup-rad2deg"));
    g_object_get(setup, pspec->name, &value, NULL);
    id = g_signal_handler_find(adj, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                               0, 0, 0, gwy_3d_window_setup_adj_changed, setup);
    g_signal_handler_block(adj, id);
    if (rad2deg) {
        value *= 180.0/G_PI;
        if (value < -180.0 || value > 180.0) {
            value = fmod(value, 360.0);
            if (value < -180.0)
                value += 360.0;
        }
    }
    gtk_adjustment_set_value(adj, value);
    g_signal_handler_unblock(adj, id);
}

static GtkObject*
gwy_3d_window_make_setup_adj(Gwy3DWindow *window,
                             Gwy3DSetup *setup,
                             const gchar *property,
                             gdouble min,
                             gdouble max,
                             gdouble step,
                             gdouble page,
                             gboolean rad2deg)
{
    GtkObject *adj;
    gdouble value;
    gchar *detail;

    g_object_get(setup, property, &value, NULL);
    if (rad2deg)
        value *= 180.0/G_PI;
    adj = gtk_adjustment_new(value, min, max, step, page, 0.0);
    g_object_set_data(G_OBJECT(adj), "gwy-3d-window-setup-property",
                      (gpointer)property);
    g_object_set_data(G_OBJECT(adj), "gwy-3d-window-setup-rad2deg",
                      GINT_TO_POINTER(rad2deg));
    g_signal_connect(adj, "value-changed",
                     G_CALLBACK(gwy_3d_window_setup_adj_changed), setup);

    detail = g_newa(gchar, strlen(property) + sizeof("notify::"));
    strcpy(detail, "notify::");
    strcat(detail, property);
    g_signal_connect(setup, detail,
                     G_CALLBACK(gwy_3d_window_adj_setup_changed), adj);

    window->setup_adjustments = g_slist_prepend(window->setup_adjustments, adj);

    return adj;
}

static GtkWidget*
gwy_3d_window_build_basic_tab(Gwy3DWindow *window)
{
    Gwy3DView *view;
    Gwy3DSetup *setup;
    GtkWidget *vbox, *spin, *table, *check, *button, *label;
    GtkObject *adj;
    gint row;

    view = GWY_3D_VIEW(window->gwy3dview);
    setup = gwy_3d_view_get_setup(view);

    vbox = gtk_vbox_new(FALSE, 0);

    table = gtk_table_new(11, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);
    row = 0;

    adj = gwy_3d_window_make_setup_adj(window, setup, "rotation-x",
                                       -180.0, 180.0, 1.0, 15.0, TRUE);
    spin = gwy_table_attach_adjbar(table, row++, _("φ:"), _("deg"),
                                   adj, GWY_HSCALE_LINEAR);

    adj = gwy_3d_window_make_setup_adj(window, setup, "rotation-y",
                                       -180.0, 180.0, 1.0, 15.0, TRUE);
    spin = gwy_table_attach_adjbar(table, row++, _("θ:"), _("deg"),
                                   adj, GWY_HSCALE_LINEAR);

    adj = gwy_3d_window_make_setup_adj(window, setup, "scale",
                                       0.05, 10.0, 0.01, 0.1,
                                       FALSE);
    spin = gwy_table_attach_adjbar(table, row++, _("_Scale:"), NULL,
                                   adj, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);

    adj = gwy_3d_window_make_setup_adj(window, setup, "z-scale",
                                       0.001, 100.0, 0.001, 1.0,
                                       FALSE);
    spin = gwy_table_attach_adjbar(table, row++, _("_Value scale:"), NULL,
                                   adj, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 5);
    g_signal_connect_swapped(adj, "value-changed",
                             G_CALLBACK(update_physcale_entry), window);

    label = gtk_label_new_with_mnemonic(_("Ph_ysical scale:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    window->physcale_entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(window->physcale_entry), 8);
    gtk_table_attach(GTK_TABLE(table), window->physcale_entry,
                     1, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), window->physcale_entry);
    update_physcale_entry(window, GTK_ADJUSTMENT(adj));
    g_signal_connect_swapped(window->physcale_entry, "activate",
                             G_CALLBACK(gwy_3d_window_set_zscale), window);

    button = gtk_button_new_with_mnemonic(gwy_sgettext("verb|Set"));
    gtk_table_attach(GTK_TABLE(table), button,
                     2, 3, row, row+1, 0, 0, 0, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_3d_window_set_zscale), window);
    row++;

    /* The range and step is what seems typically supported. */
    adj = gwy_3d_window_make_setup_adj(window, setup, "line-width",
                                       1.0, 10.0, 0.1, 1.0, FALSE);
    spin = gwy_table_attach_adjbar(table, row++, _("Line _width:"), _("px"),
                                   adj, GWY_HSCALE_LINEAR);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    check = gtk_check_button_new_with_mnemonic(_("Show _axes"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 setup->axes_visible);
    gtk_table_attach(GTK_TABLE(table), check,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(check, "toggled",
                     G_CALLBACK(gwy_3d_window_show_axes_changed), window);
    row++;

    check = gtk_check_button_new_with_mnemonic(_("Show _labels"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 setup->labels_visible);
    gtk_table_attach(GTK_TABLE(table), check,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(check, "toggled",
                     G_CALLBACK(gwy_3d_window_show_labels_changed), window);
    row++;

    check = gtk_check_button_new_with_mnemonic(_("_Orthographic projection"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 !setup->projection);
    gtk_table_attach(GTK_TABLE(table), check,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(check, "toggled",
                     G_CALLBACK(gwy_3d_window_projection_changed), window);
    row++;

    check = gtk_check_button_new_with_mnemonic(_("_Hide masked"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 setup->hide_masked);
    gtk_table_attach(GTK_TABLE(table), check,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(check, "toggled",
                     G_CALLBACK(gwy_3d_window_hide_masked_changed), window);
    row++;

    return vbox;
}

static GtkWidget*
gwy_3d_window_build_visual_tab(Gwy3DWindow *window)
{
    static const GwyEnum display_modes[] = {
        { N_("_Lighting"),           GWY_3D_VISUALIZATION_LIGHTING },
        { N_("_Gradient"),           GWY_3D_VISUALIZATION_GRADIENT },
        { N_("_Overlay"),            GWY_3D_VISUALIZATION_OVERLAY  },
        { N_("_Overlay - no light"), GWY_3D_VISUALIZATION_OVERLAY_NO_LIGHT  }
    };

    Gwy3DView *view;
    GwyContainer *data;
    Gwy3DSetup *setup;
    gboolean is_material = FALSE, is_gradient = FALSE;
    gboolean is_overlay = FALSE, light = FALSE;
    GtkWidget *vbox, *spin, *table, *menu, *label, *button;
    GtkObject *adj;
    const guchar *name;
    gint row;

    view = GWY_3D_VIEW(window->gwy3dview);
    data = gwy_3d_view_get_data(view);
    setup = gwy_3d_view_get_setup(view);
    if (setup->visualization == GWY_3D_VISUALIZATION_GRADIENT) {
        is_gradient = TRUE;
        light = FALSE;
    }
    else if (setup->visualization == GWY_3D_VISUALIZATION_LIGHTING) {
        is_material = TRUE;
        light = TRUE;
    }
    else if (setup->visualization == GWY_3D_VISUALIZATION_OVERLAY) {
        is_overlay  = TRUE;
        light = TRUE;
    }
    else if (setup->visualization == GWY_3D_VISUALIZATION_OVERLAY_NO_LIGHT) {
        is_overlay  = TRUE;
        light = FALSE;
    }
    else {
        g_warning("Unknown visualization mode %d.", setup->visualization);
        is_gradient = TRUE;
    }

    vbox = gtk_vbox_new(FALSE, 0);

    table = gtk_table_new(11, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);
    row = 0;

    window->visual_mode_group
        = gwy_radio_buttons_create(display_modes, G_N_ELEMENTS(display_modes),
                               G_CALLBACK(gwy_3d_window_display_mode_changed),
                               window,
                               setup->visualization);

    row = gwy_radio_buttons_attach_to_table(window->visual_mode_group,
                                            GTK_TABLE(table), 2, row);

    label = gtk_label_new_with_mnemonic(_("_Material:"));
    window->material_label = label;
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_widget_set_sensitive(label, is_material);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    name = NULL;
    gwy_container_gis_string_by_name(data, gwy_3d_view_get_material_key(view),
                                     &name);
    menu = gwy_gl_material_selection_new(G_CALLBACK(gwy_3d_window_set_material),
                                         window, name);
    window->material_menu = menu;
    gtk_widget_set_sensitive(menu, is_material);
    gtk_table_attach(GTK_TABLE(table), menu,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    adj = gwy_3d_window_make_setup_adj(window, setup, "light-phi",
                                       -180.0, 180.0, 1.0, 15.0, TRUE);
    spin = gwy_table_attach_adjbar(table, row++, _("_Light φ:"), _("deg"),
                                   adj, GWY_HSCALE_LINEAR);
    window->lights_spin1 = spin;
    gwy_table_hscale_set_sensitive(adj, light);

    adj = gwy_3d_window_make_setup_adj(window, setup, "light-theta",
                                       -180.0, 180.0, 1.0, 15.0, TRUE);
    spin = gwy_table_attach_adjbar(table, row++, _("L_ight θ:"), _("deg"),
                                   adj, GWY_HSCALE_LINEAR);
    window->lights_spin2 = spin;
    gwy_table_hscale_set_sensitive(adj, light);

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 12);
    gtk_widget_set_sensitive(window->buttons[GWY_3D_MOVEMENT_LIGHT],
                             light);
    gtk_widget_set_sensitive(window->buttons[N_BUTTONS + GWY_3D_MOVEMENT_LIGHT],
                             light);
    row++;

    name = NULL;
    gwy_container_gis_string_by_name(data, gwy_3d_view_get_gradient_key(view),
                                     &name);
    menu = gwy_gradient_selection_new(G_CALLBACK(gwy_3d_window_set_gradient),
                                      window, name);
    gtk_widget_set_sensitive(menu, is_gradient || is_overlay);
    window->gradient_menu = menu;
    gtk_table_attach(GTK_TABLE(table), menu,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    window->dataov_menu = gtk_label_new(NULL);
    gtk_table_attach(GTK_TABLE(table), window->dataov_menu,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);

    gtk_table_set_row_spacing(GTK_TABLE(table), row, 8);

    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    button = gtk_button_new_with_mnemonic(_("_Reset"));
    gtk_table_attach(GTK_TABLE(table), button,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_3d_window_reset_visualisation),
                             window);
    row++;

    g_signal_connect(setup, "notify::visualization",
                     G_CALLBACK(gwy_3d_window_visualization_changed), window);

    return vbox;
}

/**
 * gwy_3d_window_set_overlay_chooser:
 * @gwy3dwindow: A 3D data view window.
 * @chooser: Overlay chooser widget.
 *
 * Sets the overlay chooser widget of a 3D window.
 *
 * Once set, the overlay chooser widget cannot be changed.
 *
 * The 3D window does not use the provided widget in any way, it just places
 * it in an appropriate place in the user interface.  It is expected that the
 * caller will set up the layer and call gwy_3d_view_set_ovlay() appropriately
 * upon selection of data in the chooser.
 *
 * Since: 2.26
 **/
void
gwy_3d_window_set_overlay_chooser(Gwy3DWindow *gwy3dwindow,
                                  GtkWidget *chooser)
{
    Gwy3DView *view;
    Gwy3DSetup *setup;
    GtkWidget *vbox, *table;
    GList *list;
    gint row;

    g_return_if_fail(GWY_IS_3D_WINDOW(gwy3dwindow));
    g_return_if_fail(GTK_IS_WIDGET(chooser));
    /* XXX: Switching the choosers would be nice from API completness point of
     * view but of limited practical use. */
    if (chooser == gwy3dwindow->dataov_menu)
        return;
    g_return_if_fail(GTK_IS_LABEL(gwy3dwindow->dataov_menu));

    vbox = gtk_notebook_get_nth_page(GTK_NOTEBOOK(gwy3dwindow->notebook), 1);
    list = gtk_container_get_children(GTK_CONTAINER(vbox));
    table = GTK_WIDGET(list->data);
    g_list_free(list);
    g_return_if_fail(GTK_IS_TABLE(table));

    gtk_container_child_get(GTK_CONTAINER(table), gwy3dwindow->dataov_menu,
                            "top-attach", &row,
                            NULL);
    gtk_widget_destroy(gwy3dwindow->dataov_menu);
    gtk_table_attach(GTK_TABLE(table), chooser,
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    gwy3dwindow->dataov_menu = chooser;

    view = GWY_3D_VIEW(gwy3dwindow->gwy3dview);
    setup = gwy_3d_view_get_setup(view);
    gtk_widget_set_sensitive(chooser,
                         setup->visualization == GWY_3D_VISUALIZATION_OVERLAY);
}

static guint
find_equal_label_size(Gwy3DWindow *window)
{
    Gwy3DView *view;
    guint size_x, size_y, size_min, size_max;

    view = GWY_3D_VIEW(window->gwy3dview);
    size_x = gwy_3d_view_get_label(view, GWY_3D_VIEW_LABEL_X)->size;
    size_y = gwy_3d_view_get_label(view, GWY_3D_VIEW_LABEL_Y)->size;
    size_min = gwy_3d_view_get_label(view, GWY_3D_VIEW_LABEL_MIN)->size;
    size_max = gwy_3d_view_get_label(view, GWY_3D_VIEW_LABEL_MAX)->size;
    if (size_x == size_y && size_y == size_min && size_min == size_max)
        return size_x;

    return 0;
}

static GtkWidget*
gwy_3d_window_build_label_tab(Gwy3DWindow *window)
{
    static const GwyEnum label_entries[] = {
        { N_("X-axis"),          GWY_3D_VIEW_LABEL_X, },
        { N_("Y-axis"),          GWY_3D_VIEW_LABEL_Y, },
        { N_("Minimum z value"), GWY_3D_VIEW_LABEL_MIN, },
        { N_("Maximum z value"), GWY_3D_VIEW_LABEL_MAX, }
    };

    Gwy3DView *view;
    GtkWidget *vbox, *spin, *table, *combo, *label, *entry, *check, *button;
    Gwy3DLabel *gwy3dlabel;
    GtkObject *adj;
    gint row;

    view = GWY_3D_VIEW(window->gwy3dview);

    vbox = gtk_vbox_new(FALSE, 0);

    table = gtk_table_new(9, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);
    row = 0;

    combo = gwy_enum_combo_box_new(label_entries, G_N_ELEMENTS(label_entries),
                                   G_CALLBACK(gwy_3d_window_set_labels),
                                   window, -1, TRUE);
    gwy_table_attach_adjbar(table, row, _("_Label:"), NULL,
                            GTK_OBJECT(combo), GWY_HSCALE_WIDGET_NO_EXPAND);
    window->labels_menu = combo;
    row++;

    gwy3dlabel = gwy_3d_view_get_label(view, GWY_3D_VIEW_LABEL_X);
    window->labels_text = entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 100);
    gwy_widget_set_activate_on_unfocus(entry, TRUE);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(gwy_3d_window_labels_entry_activate), window);
    gtk_entry_set_text(GTK_ENTRY(entry), gwy_3d_label_get_text(gwy3dlabel));
    gtk_editable_select_region(GTK_EDITABLE(entry),
                               0, GTK_ENTRY(entry)->text_length);
    gwy_table_attach_adjbar(table, row, _("_Text:"), NULL,
                            GTK_OBJECT(entry), GWY_HSCALE_WIDGET);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    label = gtk_label_new(_("Move label"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    adj = gtk_adjustment_new(gwy3dlabel->delta_x,
                             -1000.0, 1000.0, 1.0, 10.0, 0.0);
    g_object_set_qdata(G_OBJECT(adj), adj_property_quark, "delta-x");
    spin = gwy_table_attach_adjbar(table, row, _("_Horizontally:"), _("px"),
                                   adj, GWY_HSCALE_SQRT);
    window->labels_delta_x = spin;
    g_signal_connect(adj, "value-changed",
                     G_CALLBACK(gwy_3d_window_label_adj_changed), window);
    row++;

    adj = gtk_adjustment_new(gwy3dlabel->delta_y,
                             -1000.0, 1000.0, 1.0, 10.0, 0.0);
    g_object_set_qdata(G_OBJECT(adj), adj_property_quark, "delta-y");
    spin = gwy_table_attach_adjbar(table, row, _("_Vertically:"), _("px"),
                                   adj, GWY_HSCALE_SQRT);
    window->labels_delta_y = spin;
    g_signal_connect(adj, "value-changed",
                     G_CALLBACK(gwy_3d_window_label_adj_changed), window);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    check = gtk_check_button_new_with_mnemonic(_("A_ll labels have the "
                                                 "same size"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 find_equal_label_size(window));
    gtk_table_attach(GTK_TABLE(table), check,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    window->label_size_equal = check;
    g_signal_connect(check, "toggled",
                     G_CALLBACK(gwy_3d_window_label_size_eq_changed), window);
    row++;

    check = gtk_check_button_new_with_mnemonic(_("Scale size _automatically"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 !gwy3dlabel->fixed_size);
    gtk_table_attach(GTK_TABLE(table), check,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    window->labels_autosize = check;
    g_signal_connect(check, "toggled",
                     G_CALLBACK(gwy_3d_window_auto_scale_changed), window);
    row++;

    adj = gtk_adjustment_new(gwy3dlabel->size, 1.0, 100.0, 1.0, 10.0, 0.0);
    g_object_set_qdata(G_OBJECT(adj), adj_property_quark, "size");
    spin = gwy_table_attach_adjbar(table, row, _("Si_ze:"), _("px"),
                                   adj, GWY_HSCALE_SQRT);
    gwy_table_hscale_set_sensitive(adj, gwy3dlabel->fixed_size);
    window->labels_size = spin;
    g_signal_connect(adj, "value-changed",
                     G_CALLBACK(gwy_3d_window_label_adj_changed), window);
    row++;

    button = gtk_button_new_with_mnemonic(_("_Reset"));
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_3d_window_labels_reset_clicked),
                             window);
    gtk_table_attach(GTK_TABLE(table), button,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);
    window->actions = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(window->vbox_large), window->actions,
                       FALSE, FALSE, 0);

    return vbox;
}

static GtkWidget*
gwy_3d_window_build_colorbar_tab(Gwy3DWindow *window)
{
    Gwy3DView *view;
    Gwy3DSetup *setup;
    GtkWidget *vbox, *spin, *table, *check;
    GtkObject *adj;
    gint row;

    view = GWY_3D_VIEW(window->gwy3dview);
    setup = gwy_3d_view_get_setup(view);

    vbox = gtk_vbox_new(FALSE, 0);

    table = gtk_table_new(4, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);
    row = 0;

    check = gtk_check_button_new_with_mnemonic(_("Show false _colorbar"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 setup->fmscale_visible);
    gtk_table_attach(GTK_TABLE(table), check,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(check, "toggled",
                     G_CALLBACK(gwy_3d_window_show_fmscale_changed), window);
    row++;

    check = gtk_check_button_new_with_mnemonic(_("Reserve space "
                                                 "for _colorbar"));
    g_object_set_data(G_OBJECT(window),
                      "gwy-3d-window-fmscale-reserve-space", check);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 setup->fmscale_reserve_space);
    gtk_table_attach(GTK_TABLE(table), check,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(check, "toggled",
                     G_CALLBACK(gwy_3d_window_fmscale_rspace_changed), window);
    gtk_widget_set_sensitive(check, setup->fmscale_visible);
    row++;

    adj = gwy_3d_window_make_setup_adj(window, setup, "fmscale-size",
                                       0.0, 1.0, 0.001, 0.1, FALSE);
    g_object_set_data(G_OBJECT(window), "gwy-3d-window-fmscale-size", adj);
    spin = gwy_table_attach_adjbar(table, row++, _("_Size:"), NULL,
                                   adj, GWY_HSCALE_LINEAR);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 3);
    gwy_table_hscale_set_sensitive(adj, setup->fmscale_visible);

    adj = gwy_3d_window_make_setup_adj(window, setup, "fmscale-y-align",
                                       0.0, 1.0, 0.001, 0.1, FALSE);
    g_object_set_data(G_OBJECT(window), "gwy-3d-window-fmscale-y-align", adj);
    spin = gwy_table_attach_adjbar(table, row++,
                                   _("_Vertical alignment:"), NULL,
                                   adj, GWY_HSCALE_LINEAR);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 3);
    gwy_table_hscale_set_sensitive(adj, setup->fmscale_visible);

    return vbox;
}

/**
 * gwy_3d_window_get_3d_view:
 * @gwy3dwindow: A 3D data view window.
 *
 * Returns the #Gwy3DView widget this 3D window currently shows.
 *
 * Returns: The currently shown #GwyDataView.
 **/
GtkWidget*
gwy_3d_window_get_3d_view(Gwy3DWindow *gwy3dwindow)
{
    g_return_val_if_fail(GWY_IS_3D_WINDOW(gwy3dwindow), NULL);

    return gwy3dwindow->gwy3dview;
}

/**
 * gwy_3d_window_add_action_widget:
 * @gwy3dwindow: A 3D data view window.
 * @widget: A widget to pack into the action area.
 *
 * Adds a widget (usually a button) to 3D window action area.
 *
 * The action area is located under the parameter notebook.
 **/
void
gwy_3d_window_add_action_widget(Gwy3DWindow *gwy3dwindow,
                                GtkWidget *widget)
{
    g_return_if_fail(GWY_IS_3D_WINDOW(gwy3dwindow));
    g_return_if_fail(GTK_IS_WIDGET(widget));

    gtk_box_pack_start(GTK_BOX(gwy3dwindow->actions), widget,
                       FALSE, FALSE, 0);
}

/**
 * gwy_3d_window_add_small_toolbar_button:
 * @gwy3dwindow: A 3D data view window.
 * @stock_id: Button pixmap stock id, like #GTK_STOCK_SAVE.
 * @tooltip: Button tooltip.
 * @callback: Callback action for "clicked" signal.  It is connected swapped,
 *            that is it gets @cbdata as its first argument, the clicked button
 *            as the last.
 * @cbdata: Data to pass to @callback.
 *
 * Adds a button to small @gwy3dwindow toolbar.
 *
 * The small toolbar is those visible when full controls are hidden.  Due to
 * space constraints the button must be contain only a pixmap.
 **/
void
gwy_3d_window_add_small_toolbar_button(Gwy3DWindow *gwy3dwindow,
                                       const gchar *stock_id,
                                       const gchar *tooltip,
                                       GCallback callback,
                                       gpointer cbdata)
{
    GtkWidget *button;

    g_return_if_fail(GWY_IS_3D_WINDOW(gwy3dwindow));
    g_return_if_fail(stock_id);

    button = gtk_button_new();
    gtk_box_pack_start(GTK_BOX(gwy3dwindow->vbox_small), button,
                       FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_stock(stock_id,
                                               GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_widget_set_tooltip_text(button, tooltip);
    g_object_set_data(G_OBJECT(button), "gwy3dwindow", gwy3dwindow);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(callback), cbdata);
}

/**
 * gwy_3d_window_class_set_tooltips:
 * @tips: Tooltips object #Gwy3DWindow's should use for setting tooltips.
 *        A %NULL value disables tooltips altogether.
 *
 * Sets the tooltips object to use for adding tooltips to 3D window parts.
 *
 * This function does not do anything useful.  Do not use it.
 *
 * This is a class method.  It affects only newly cerated 3D windows, existing
 * 3D windows will continue to use the tooltips they were constructed with.
 *
 * If no class tooltips object is set before first #Gwy3DWindow is created,
 * the class instantiates one on its own.  You can normally obtain it with
 * gwy_3d_window_class_get_tooltips() then.  The class takes a reference on
 * the tooltips in either case.
 **/
void
gwy_3d_window_class_set_tooltips(GtkTooltips *tips)
{
    g_return_if_fail(!tips || GTK_IS_TOOLTIPS(tips));

    if (tips) {
        g_object_ref(tips);
        gtk_object_sink(GTK_OBJECT(tips));
    }
    GWY_OBJECT_UNREF(tooltips);
    tooltips = tips;
    tooltips_set = TRUE;
}

/**
 * gwy_3d_window_class_get_tooltips:
 *
 * Gets the tooltips object used for adding tooltips to 3D window parts.
 *
 * This function does not do anything useful.  Do not use it.
 *
 * Returns: The #GtkTooltips object.
 **/
GtkTooltips*
gwy_3d_window_class_get_tooltips(void)
{
    return tooltips;
}

static void
gwy_3d_window_set_mode(gpointer userdata, GtkWidget *button)
{
    Gwy3DWindow *gwy3dwindow;
    Gwy3DMovement movement;

    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
        return;

    gwy3dwindow = (Gwy3DWindow*)g_object_get_data(G_OBJECT(button),
                                                  "gwy3dwindow");
    g_return_if_fail(GWY_IS_3D_WINDOW(gwy3dwindow));
    if (gwy3dwindow->in_update)
        return;

    gwy3dwindow->in_update = TRUE;
    movement = GPOINTER_TO_INT(userdata);
    gtk_toggle_button_set_active
        (GTK_TOGGLE_BUTTON(gwy3dwindow->buttons[movement]), TRUE);
    gtk_toggle_button_set_active
        (GTK_TOGGLE_BUTTON(gwy3dwindow->buttons[movement + N_BUTTONS]), TRUE);

    gwy_3d_view_set_movement_type(GWY_3D_VIEW(gwy3dwindow->gwy3dview),
                                  movement);
    gwy3dwindow->in_update = FALSE;
}

static void
gwy_3d_window_set_gradient(GtkTreeSelection *selection,
                           Gwy3DWindow *gwy3dwindow)
{
    Gwy3DView *view;
    GwyResource *resource;
    GtkTreeModel *model;
    GtkTreeIter iter;
    const gchar *name;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, 0, &resource, -1);
        view = GWY_3D_VIEW(gwy3dwindow->gwy3dview);
        name = gwy_resource_get_name(resource);
        gwy_container_set_const_string_by_name(gwy_3d_view_get_data(view),
                                               gwy_3d_view_get_gradient_key(view),
                                               name);
    }
}

static void
gwy_3d_window_set_material(GtkTreeSelection *selection,
                           Gwy3DWindow *gwy3dwindow)
{
    Gwy3DView *view;
    GwyResource *resource;
    GtkTreeModel *model;
    GtkTreeIter iter;
    const gchar *name;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, 0, &resource, -1);
        view = GWY_3D_VIEW(gwy3dwindow->gwy3dview);
        name = gwy_resource_get_name(resource);
        gwy_container_set_const_string_by_name(gwy_3d_view_get_data(view),
                                               gwy_3d_view_get_material_key(view),
                                               name);
    }
}

static void
gwy_3d_window_select_controls(gpointer data, GtkWidget *button)
{
    Gwy3DWindow *gwy3dwindow;
    GtkWidget *show, *hide;

    gwy3dwindow = (Gwy3DWindow*)g_object_get_data(G_OBJECT(button),
                                                  "gwy3dwindow");
    g_return_if_fail(GWY_IS_3D_WINDOW(gwy3dwindow));

    show = data ? gwy3dwindow->vbox_small : gwy3dwindow->vbox_large;
    hide = data ? gwy3dwindow->vbox_large : gwy3dwindow->vbox_small;
    gwy3dwindow->controls_full = !data;
    gtk_widget_hide(hide);
    gtk_widget_set_no_show_all(hide, TRUE);
    gtk_widget_set_no_show_all(show, FALSE);
    gtk_widget_show_all(show);
}

static void
gwy_3d_window_set_labels(GtkWidget *combo,
                         Gwy3DWindow *gwy3dwindow)
{
    gint id;
    Gwy3DLabel *label;

    id = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    label = gwy_3d_view_get_label(GWY_3D_VIEW(gwy3dwindow->gwy3dview), id);
    g_return_if_fail(label);

    gtk_entry_set_text(GTK_ENTRY(gwy3dwindow->labels_text),
                       gwy_3d_label_get_text(label));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(gwy3dwindow->labels_delta_x),
                              label->delta_x);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(gwy3dwindow->labels_delta_y),
                              label->delta_y);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(gwy3dwindow->labels_size),
                              label->size);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gwy3dwindow->labels_autosize),
                                 !label->fixed_size);
}

static void
gwy_3d_window_label_adj_changed(GtkAdjustment *adj,
                                Gwy3DWindow *window)
{
    GtkToggleButton *check;
    Gwy3DLabel *label;
    const gchar *key;
    gdouble oldval, newval;
    gint id;

    id = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(window->labels_menu));
    label = gwy_3d_view_get_label(GWY_3D_VIEW(window->gwy3dview), id);
    key = g_object_get_qdata(G_OBJECT(adj), adj_property_quark);
    g_object_get(label, key, &oldval, NULL);
    newval = gtk_adjustment_get_value(adj);
    if (oldval != newval)
        g_object_set(label, key, newval, NULL);

    check = GTK_TOGGLE_BUTTON(window->label_size_equal);
    if (gtk_toggle_button_get_active(check))
        sync_other_labels_to_current(window);
}

static void
gwy_3d_window_projection_changed(GtkToggleButton *check,
                                 Gwy3DWindow *window)
{
    Gwy3DProjection projection;
    Gwy3DSetup *setup;

    setup = gwy_3d_view_get_setup(GWY_3D_VIEW(window->gwy3dview));
    if (gtk_toggle_button_get_active(check))
        projection = GWY_3D_PROJECTION_ORTHOGRAPHIC;
    else
        projection = GWY_3D_PROJECTION_PERSPECTIVE;

    if (projection != setup->projection)
        g_object_set(setup, "projection", projection, NULL);
}

static void
gwy_3d_window_hide_masked_changed(GtkToggleButton *check,
                                 Gwy3DWindow *window)
{
    Gwy3DSetup *setup;
    gboolean hide_masked;

    setup = gwy_3d_view_get_setup(GWY_3D_VIEW(window->gwy3dview));
    hide_masked = gtk_toggle_button_get_active(check);

    if (hide_masked != setup->hide_masked)
        g_object_set(setup, "hide_masked", hide_masked, NULL);
}

static void
gwy_3d_window_show_axes_changed(GtkToggleButton *check,
                                Gwy3DWindow *window)
{
    Gwy3DSetup *setup;

    setup = gwy_3d_view_get_setup(GWY_3D_VIEW(window->gwy3dview));
    g_object_set(setup,
                 "axes-visible", gtk_toggle_button_get_active(check),
                 NULL);
}

static void
gwy_3d_window_show_labels_changed(GtkToggleButton *check,
                                  Gwy3DWindow *window)
{
    Gwy3DSetup *setup;

    setup = gwy_3d_view_get_setup(GWY_3D_VIEW(window->gwy3dview));
    g_object_set(setup,
                 "labels-visible", gtk_toggle_button_get_active(check),
                 NULL);
}

static void
gwy_3d_window_fmscale_rspace_changed(GtkToggleButton *check,
                                     Gwy3DWindow *window)
{
    Gwy3DSetup *setup;

    setup = gwy_3d_view_get_setup(GWY_3D_VIEW(window->gwy3dview));
    g_object_set(setup,
                 "fmscale-reserve-space", gtk_toggle_button_get_active(check),
                 NULL);
}

static void
gwy_3d_window_show_fmscale_changed(GtkToggleButton *check,
                                   Gwy3DWindow *window)
{
    gboolean active = gtk_toggle_button_get_active(check);
    GtkObject *adj;
    GtkWidget *button;
    Gwy3DSetup *setup;

    setup = gwy_3d_view_get_setup(GWY_3D_VIEW(window->gwy3dview));
    g_object_set(setup, "fmscale-visible", active, NULL);
    adj = g_object_get_data(G_OBJECT(window), "gwy-3d-window-fmscale-y-align");
    gwy_table_hscale_set_sensitive(adj, active);
    adj = g_object_get_data(G_OBJECT(window), "gwy-3d-window-fmscale-size");
    gwy_table_hscale_set_sensitive(adj, active);
    button = g_object_get_data(G_OBJECT(window),
                               "gwy-3d-window-fmscale-reserve-space");
    gtk_widget_set_sensitive(button, active);
}

static void
gwy_3d_window_display_mode_changed(GtkWidget *item,
                                   Gwy3DWindow *window)
{
    Gwy3DVisualization visual;
    Gwy3DSetup *setup;
    GSList *list;

    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(item)))
        return;

    setup = gwy_3d_view_get_setup(GWY_3D_VIEW(window->gwy3dview));
    list = gtk_radio_button_get_group(GTK_RADIO_BUTTON(item));
    visual = gwy_radio_buttons_get_current(list);

    if (visual != setup->visualization)
        g_object_set(setup, "visualization", visual, NULL);
}

static void
gwy_3d_window_set_visualization(Gwy3DWindow *window,
                                Gwy3DVisualization visual)
{
    gboolean is_material = FALSE, is_gradient = FALSE,
             is_overlay = FALSE, light = FALSE;
    GtkAdjustment *adj;

    if (visual == GWY_3D_VISUALIZATION_GRADIENT) {
        is_gradient = TRUE;
        light = FALSE;
    }
    else if (visual == GWY_3D_VISUALIZATION_LIGHTING) {
        is_material = TRUE;
        light = TRUE;
    }
    else if (visual == GWY_3D_VISUALIZATION_OVERLAY) {
        is_overlay  = TRUE;
        light = TRUE;
    }
    else if (visual == GWY_3D_VISUALIZATION_OVERLAY_NO_LIGHT) {
        is_overlay  = TRUE;
        light = FALSE;
    }

    gtk_widget_set_sensitive(window->material_menu, is_material);
    gtk_widget_set_sensitive(window->material_label, is_material);
    gtk_widget_set_sensitive(window->gradient_menu, is_gradient || is_overlay);
    adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(window->lights_spin1));
    gwy_table_hscale_set_sensitive(GTK_OBJECT(adj), light);
    adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(window->lights_spin2));
    gwy_table_hscale_set_sensitive(GTK_OBJECT(adj), light);
    gtk_widget_set_sensitive(window->buttons[GWY_3D_MOVEMENT_LIGHT],
                             light);
    gtk_widget_set_sensitive(window->buttons[N_BUTTONS + GWY_3D_MOVEMENT_LIGHT],
                             light);

    gtk_widget_set_sensitive(window->dataov_menu, is_overlay);

    if (!(light)
        && gwy_3d_view_get_movement_type(GWY_3D_VIEW(window->gwy3dview))
            == GWY_3D_MOVEMENT_LIGHT)
        g_signal_emit_by_name(window->buttons[GWY_3D_MOVEMENT_ROTATION],
                              "clicked");
}

static void
gwy_3d_window_visualization_changed(Gwy3DSetup *setup,
                                    G_GNUC_UNUSED GParamSpec *pspec,
                                    Gwy3DWindow *window)
{
    gwy_3d_window_set_visualization(window, setup->visualization);
}

static void
gwy_3d_window_auto_scale_changed(GtkToggleButton *check,
                                 Gwy3DWindow *window)
{
    Gwy3DLabel *label;
    gboolean active;
    GtkAdjustment *adj;
    gint id;

    active = gtk_toggle_button_get_active(check);
    adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(window->labels_size));
    gwy_table_hscale_set_sensitive(GTK_OBJECT(adj), !active);

    id = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(window->labels_menu));
    label = gwy_3d_view_get_label(GWY_3D_VIEW(window->gwy3dview), id);
    /* The check button is for the opposite of "fixed-size". */
    if (!label->fixed_size == !active)
        g_object_set(label, "fixed-size", !active, NULL);

    /* Restore the size the (previously disabled) spin button is showing. */
    if (label->fixed_size) {
        adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(window->labels_size));
        gtk_adjustment_value_changed(adj);
    }

    check = GTK_TOGGLE_BUTTON(window->label_size_equal);
    if (gtk_toggle_button_get_active(check))
        sync_other_labels_to_current(window);
}

static void
gwy_3d_window_label_size_eq_changed(GtkToggleButton *check, Gwy3DWindow *window)
{
    if (window->in_update || !gtk_toggle_button_get_active(check))
        return;

    sync_other_labels_to_current(window);
}

static void
gwy_3d_window_labels_entry_activate(GtkEntry *entry,
                                    Gwy3DWindow *window)
{
    Gwy3DLabel *label;
    gint id;

    id = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(window->labels_menu));
    label = gwy_3d_view_get_label(GWY_3D_VIEW(window->gwy3dview), id);
    gwy_3d_label_set_text(label, gtk_entry_get_text(entry));
}

/* This resets the current one.  Should we add also a reset-all button? */
static void
gwy_3d_window_labels_reset_clicked(Gwy3DWindow *window)
{
    Gwy3DLabel *label;
    gint id;

    id = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(window->labels_menu));
    label = gwy_3d_view_get_label(GWY_3D_VIEW(window->gwy3dview), id);
    gwy_3d_label_reset(label);
    gtk_entry_set_text(GTK_ENTRY(window->labels_text),
                       gwy_3d_label_get_text(label));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(window->labels_delta_x),
                              label->delta_x);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(window->labels_delta_y),
                              label->delta_y);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(window->labels_size),
                              label->size);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(window->labels_autosize),
                                 !label->fixed_size);
}

static void
gwy_3d_window_reset_visualisation(Gwy3DWindow *window)
{
    Gwy3DView *view = GWY_3D_VIEW(window->gwy3dview);
    GwyContainer *data = gwy_3d_view_get_data(view);
    const gchar *name, *key;

    /* This sequence ensures gradient changes to the *current* default, even
     * if it unset presently. */
    key = gwy_3d_view_get_gradient_key(view);
    name = gwy_inventory_get_default_item_name(gwy_gradients());
    gwy_container_set_const_string_by_name(data, key, name);
    gwy_container_remove_by_name(data, key);

    key = gwy_3d_view_get_material_key(view);
    name = gwy_inventory_get_default_item_name(gwy_gl_materials());
    gwy_container_set_const_string_by_name(data, key, name);
    gwy_container_remove_by_name(data, key);
}

static gboolean
gwy_3d_window_view_clicked(GtkWidget *gwy3dwindow,
                           GdkEventButton *event,
                           GtkWidget *gwy3dview)
{
    Gwy3DVisualization visual;
    Gwy3DSetup *setup;
    GtkWidget *menu, *item, *item2;

    if (event->button != 3)
        return FALSE;

    setup = gwy_3d_view_get_setup(GWY_3D_VIEW(gwy3dview));
    switch (setup->visualization) {
        case GWY_3D_VISUALIZATION_GRADIENT:
        menu = gwy_menu_gradient(G_CALLBACK(gwy_3d_window_gradient_selected),
                                 gwy3dwindow);
        item = gtk_menu_item_new_with_mnemonic(_("S_witch to Lighting Mode"));
        visual = GWY_3D_VISUALIZATION_LIGHTING;
        g_object_set_data(G_OBJECT(item), "display-mode",
                          GINT_TO_POINTER(visual));
        g_signal_connect(item, "activate",
                         G_CALLBACK(gwy_3d_window_visual_selected),
                         gwy3dwindow);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        item2 = gtk_menu_item_new_with_mnemonic(_("S_witch to Overlay Mode"));
        visual = GWY_3D_VISUALIZATION_OVERLAY;
        g_object_set_data(G_OBJECT(item2), "display-mode",
                          GINT_TO_POINTER(visual));
        g_signal_connect(item2, "activate",
                         G_CALLBACK(gwy_3d_window_visual_selected),
                         gwy3dwindow);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item2);
        break;


        case GWY_3D_VISUALIZATION_LIGHTING:
        menu = gwy_menu_gl_material(G_CALLBACK(gwy_3d_window_material_selected),
                                    gwy3dwindow);
        item = gtk_menu_item_new_with_mnemonic(_("S_witch to Color Gradient Mode"));
        visual = GWY_3D_VISUALIZATION_GRADIENT;
        g_object_set_data(G_OBJECT(item), "display-mode",
                          GINT_TO_POINTER(visual));
        g_signal_connect(item, "activate",
                         G_CALLBACK(gwy_3d_window_visual_selected),
                         gwy3dwindow);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        item2 = gtk_menu_item_new_with_mnemonic(_("S_witch to Overlay Mode"));
        visual = GWY_3D_VISUALIZATION_OVERLAY;
        g_object_set_data(G_OBJECT(item2), "display-mode",
                          GINT_TO_POINTER(visual));
        g_signal_connect(item2, "activate",
                         G_CALLBACK(gwy_3d_window_visual_selected),
                         gwy3dwindow);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item2);
        break;

        case GWY_3D_VISUALIZATION_OVERLAY:
        case GWY_3D_VISUALIZATION_OVERLAY_NO_LIGHT:
        menu = gwy_menu_gradient(G_CALLBACK(gwy_3d_window_gradient_selected),
                                 gwy3dwindow);
        g_object_set(menu, "reserve-toggle-size", 0, NULL);
        item = gtk_menu_item_new_with_mnemonic(_("S_witch to Color Gradient Mode"));
        visual = GWY_3D_VISUALIZATION_GRADIENT;
        g_object_set_data(G_OBJECT(item), "display-mode",
                          GINT_TO_POINTER(visual));
        g_signal_connect(item, "activate",
                         G_CALLBACK(gwy_3d_window_visual_selected),
                         gwy3dwindow);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        item2 = gtk_menu_item_new_with_mnemonic(_("S_witch to Lighting Mode"));
        visual = GWY_3D_VISUALIZATION_LIGHTING;
        g_object_set_data(G_OBJECT(item2), "display-mode",
                          GINT_TO_POINTER(visual));
        g_signal_connect(item2, "activate",
                         G_CALLBACK(gwy_3d_window_visual_selected),
                         gwy3dwindow);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item2);
        item2 = gtk_menu_item_new_with_mnemonic(_("T_oggle light"));
        visual = setup->visualization == GWY_3D_VISUALIZATION_OVERLAY
               ? GWY_3D_VISUALIZATION_OVERLAY_NO_LIGHT
               : GWY_3D_VISUALIZATION_OVERLAY;
        g_object_set_data(G_OBJECT(item2), "display-mode",
                          GINT_TO_POINTER(visual));
        g_signal_connect(item2, "activate",
                         G_CALLBACK(gwy_3d_window_visual_selected),
                         gwy3dwindow);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item2);
        break;

        default:
        g_return_val_if_reached(FALSE);
        break;
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                   event->button, event->time);
    g_signal_connect(menu, "selection-done",
                     G_CALLBACK(gtk_widget_destroy), NULL);
    return FALSE;
}

static void
gwy_3d_window_visual_selected(GtkWidget *item,
                              Gwy3DWindow *gwy3dwindow)
{
    Gwy3DVisualization visual;

    visual = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "display-mode"));
    gwy_radio_buttons_set_current(gwy3dwindow->visual_mode_group, visual);
}

static void
gwy_3d_window_gradient_selected(GtkWidget *item,
                                Gwy3DWindow *gwy3dwindow)
{
    Gwy3DView *view;
    const gchar *name;

    name = g_object_get_data(G_OBJECT(item), "gradient-name");
    gwy_gradient_selection_set_active(gwy3dwindow->gradient_menu, name);
    /* FIXME: Double update if tree view is visible. Remove once selection
     * buttons can emit signals. */
    view = GWY_3D_VIEW(gwy3dwindow->gwy3dview);
    gwy_container_set_const_string_by_name(gwy_3d_view_get_data(view),
                                           gwy_3d_view_get_gradient_key(view),
                                           name);
}

static void
gwy_3d_window_material_selected(GtkWidget *item,
                                Gwy3DWindow *gwy3dwindow)
{
    Gwy3DView *view;
    const gchar *name;

    name = g_object_get_data(G_OBJECT(item), "gl-material-name");
    gwy_gl_material_selection_set_active(gwy3dwindow->material_menu, name);
    /* FIXME: Double update if tree view is visible. Remove once selection
     * buttons can emit signals. */
    view = GWY_3D_VIEW(gwy3dwindow->gwy3dview);
    gwy_container_set_const_string_by_name(gwy_3d_view_get_data(view),
                                           gwy_3d_view_get_material_key(view),
                                           name);
}

static void
gwy_3d_window_set_zscale(Gwy3DWindow *window)
{
    Gwy3DView *view;
    GwyContainer *container;
    const gchar *data_key;
    Gwy3DSetup *setup;
    GwyDataField *dfield = NULL;
    gdouble min, max, xreal, yreal, scale, zscale, entryval;

    view = GWY_3D_VIEW(window->gwy3dview);
    container = gwy_3d_view_get_data(view);
    setup = gwy_3d_view_get_setup(view);
    data_key = gwy_3d_view_get_data_key(view);
    if (!container || !setup || !data_key)
        return;
    if (!gwy_container_gis_object_by_name(container, data_key, &dfield))
        return;

    entryval = g_strtod(gtk_entry_get_text(GTK_ENTRY(window->physcale_entry)),
                        NULL);

    /* FIXME: We need to carefully emulate the code from 3D view here. */
    gwy_data_field_get_min_max(dfield, &min, &max);
    xreal = gwy_data_field_get_xreal(dfield);
    yreal = gwy_data_field_get_yreal(dfield);
    scale = 2.0/MAX(xreal, yreal);
    zscale = scale*2*(max - min) * entryval;

    g_object_set(setup, "z-scale", zscale, NULL);
}

static void
update_physcale_entry(Gwy3DWindow *window, GtkAdjustment *adj)
{
    Gwy3DView *view;
    GwyContainer *container;
    const gchar *data_key;
    Gwy3DSetup *setup;
    GwyDataField *dfield = NULL;
    gdouble min, max, xreal, yreal, scale, physcale;
    gchar buf[40];

    view = GWY_3D_VIEW(window->gwy3dview);
    container = gwy_3d_view_get_data(view);
    setup = gwy_3d_view_get_setup(view);
    data_key = gwy_3d_view_get_data_key(view);
    if (!container || !setup || !data_key)
        return;
    if (!gwy_container_gis_object_by_name(container, data_key, &dfield))
        return;

    /* FIXME: We need to carefully emulate the code from 3D view here. */
    gwy_data_field_get_min_max(dfield, &min, &max);
    xreal = gwy_data_field_get_xreal(dfield);
    yreal = gwy_data_field_get_yreal(dfield);
    scale = 2.0/MAX(xreal, yreal);
    if (max <= min)
        physcale = 0.0;
    else
        physcale = gtk_adjustment_get_value(adj)/(scale*2*(max - min));

    g_snprintf(buf, sizeof(buf), "%g", physcale);
    gtk_entry_set_text(GTK_ENTRY(window->physcale_entry), buf);
}

static void
sync_other_labels_to_current(Gwy3DWindow *window)
{
    Gwy3DLabel *label;
    gboolean fixed_size;
    gint id, i;
    gdouble size;

    id = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(window->labels_menu));
    label = gwy_3d_view_get_label(GWY_3D_VIEW(window->gwy3dview), id);
    size = label->size;
    fixed_size = label->fixed_size;

    for (i = 0; i < GWY_3D_VIEW_NLABELS; i++) {
        if (i == id)
            continue;

        label = gwy_3d_view_get_label(GWY_3D_VIEW(window->gwy3dview), i);
        if (label->size != size || label->fixed_size != fixed_size) {
            g_object_set(label,
                         "fixed-size", fixed_size,
                         "size", size,
                         NULL);
        }
    }
}

/************************** Documentation ****************************/

/**
 * SECTION:gwy3dwindow
 * @title: Gwy3DWindow
 * @short_description: 3D data display window
 * @see_also: #Gwy3DView -- the basic 3D data display widget
 *
 * #Gwy3DWindow encapsulates a #Gwy3DView together with appropriate controls.
 * You can create a 3D window for a 3D view with gwy_3d_window_new(). It has an
 * `action area' below the controls where additional widgets can be packed with
 * gwy_3d_window_add_action_widget().
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
