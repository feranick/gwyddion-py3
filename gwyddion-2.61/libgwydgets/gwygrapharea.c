/*
 *  $Id: gwygrapharea.c 21782 2019-01-03 12:56:50Z yeti-dn $
 *  Copyright (C) 2003-2019 David Necas (Yeti), Petr Klapetek.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwydgettypes.h>
#include <libgwydgets/gwygraphselections.h>
#include "gwygraphareadialog.h"
#include "gwygraphlabeldialog.h"

enum {
    EDIT_CURVE,
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_STATUS,
    PROP_LAST
};

static void       gwy_graph_area_finalize              (GObject *object);
static void       gwy_graph_area_set_property          (GObject *object,
                                                        guint prop_id,
                                                        const GValue *value,
                                                        GParamSpec *pspec);
static void       gwy_graph_area_get_property          (GObject*object,
                                                        guint prop_id,
                                                        GValue *value,
                                                        GParamSpec *pspec);
static void       gwy_graph_area_destroy               (GtkObject *object);
static void       gwy_graph_area_realize               (GtkWidget *widget);
static void       gwy_graph_area_unrealize             (GtkWidget *widget);
static void       gwy_graph_area_size_allocate         (GtkWidget *widget,
                                                        GtkAllocation *allocation);
static gboolean   gwy_graph_area_expose                (GtkWidget *widget,
                                                        GdkEventExpose *event);
static gboolean   gwy_graph_area_button_press          (GtkWidget *widget,
                                                        GdkEventButton *event);
static gboolean   gwy_graph_area_button_release        (GtkWidget *widget,
                                                        GdkEventButton *event);
static gboolean   gwy_graph_area_leave_notify          (GtkWidget *widget,
                                                        GdkEventCrossing *event);
static gint       gwy_graph_area_find_curve            (GwyGraphArea *area,
                                                        gdouble x,
                                                        gdouble y);
static gint       gwy_graph_area_find_selection_edge   (GwyGraphArea *area,
                                                        gdouble x,
                                                        gdouble y,
                                                        int *eindex);
static gint       gwy_graph_area_find_selection        (GwyGraphArea *area,
                                                        gdouble x,
                                                        gdouble y);
static gint       gwy_graph_area_find_point            (GwyGraphArea *area,
                                                        gdouble x,
                                                        gdouble y);
static gint       gwy_graph_area_find_line             (GwyGraphArea *area,
                                                        gdouble position);
static void       gwy_graph_area_draw_zoom             (GdkDrawable *drawable,
                                                        GdkGC *gc,
                                                        GwyGraphArea *area);
static void       selection_changed                    (GwyGraphArea *area);
static void       gwy_graph_area_model_notify          (GwyGraphArea *area,
                                                        GParamSpec *pspec,
                                                        GwyGraphModel *gmodel);
static void       gwy_graph_area_restore_label_pos     (GwyGraphArea *area);
static void       gwy_graph_area_n_curves_changed      (GwyGraphArea *area);
static void       gwy_graph_area_curve_notify          (GwyGraphArea *area,
                                                        gint i,
                                                        GParamSpec *pspec);
static void       gwy_graph_area_curve_data_changed    (GwyGraphArea *area,
                                                        gint i);
static void       gwy_graph_area_edit_curve_real       (GwyGraphArea *area,
                                                        gint id);
static gdouble    scr_to_data_x                        (GtkWidget *widget,
                                                        gint scr);
static gdouble    scr_to_data_y                        (GtkWidget *widget,
                                                        gint scr);
static gint       data_to_scr_x                        (GtkWidget *widget,
                                                        gdouble data);
static gint       data_to_scr_y                        (GtkWidget *widget,
                                                        gdouble data);
static void       gwy_graph_area_response              (GwyGraphAreaDialog *dialog,
                                                        gint arg1,
                                                        GwyGraphArea *area);
static void       gwy_graph_label_response             (GwyGraphLabelDialog *dialog,
                                                        gint arg1,
                                                        gpointer user_data);
static void       label_geometry_changed               (GtkWidget *area,
                                                        GtkAllocation *label_allocation);
static void       gwy_graph_area_repos_label           (GwyGraphArea *area,
                                                        GtkAllocation *area_allocation,
                                                        GtkAllocation *label_allocation);
static gboolean   gwy_graph_area_motion_notify         (GtkWidget *widget,
                                                        GdkEventMotion *event);
static GtkWidget* gwy_graph_area_find_child            (GwyGraphArea *area,
                                                        gint x,
                                                        gint y);
static void       gwy_graph_area_draw_child_rectangle  (GwyGraphArea *area);
static void       gwy_graph_area_clamp_coords_for_child(GwyGraphArea *area,
                                                        gint *x,
                                                        gint *y);

static guint graph_area_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(GwyGraphArea, gwy_graph_area, GTK_TYPE_LAYOUT)

static void
gwy_graph_area_class_init(GwyGraphAreaClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class;
    GtkObjectClass *object_class;

    widget_class = (GtkWidgetClass*)klass;
    object_class = (GtkObjectClass*)klass;

    gobject_class->finalize = gwy_graph_area_finalize;
    gobject_class->get_property = gwy_graph_area_get_property;
    gobject_class->set_property = gwy_graph_area_set_property;

    widget_class->realize = gwy_graph_area_realize;
    widget_class->unrealize = gwy_graph_area_unrealize;
    widget_class->expose_event = gwy_graph_area_expose;
    widget_class->size_allocate = gwy_graph_area_size_allocate;

    object_class->destroy = gwy_graph_area_destroy;

    widget_class->button_press_event = gwy_graph_area_button_press;
    widget_class->button_release_event = gwy_graph_area_button_release;
    widget_class->motion_notify_event = gwy_graph_area_motion_notify;
    widget_class->leave_notify_event = gwy_graph_area_leave_notify;

    klass->edit_curve = gwy_graph_area_edit_curve_real;

    g_object_class_install_property
        (gobject_class,
         PROP_STATUS,
         g_param_spec_enum("status",
                          "Status",
                          "The type of reaction to mouse events (zoom, "
                          "selections).",
                          GWY_TYPE_GRAPH_STATUS_TYPE,
                          GWY_GRAPH_STATUS_PLAIN,
                          G_PARAM_READWRITE));

    /**
     * GwyGraphArea::edit-curve:
     * @gwygraphcurvemodel: The #GwyGraphArea which received the signal.
     * @arg1: The index of the curve to edit.
     *
     * The ::data-changed signal is emitted when a curve properties are to be
     * edited.
     *
     * Since: 2.5
     **/
    graph_area_signals[EDIT_CURVE]
        = g_signal_new("edit-curve",
                       G_OBJECT_CLASS_TYPE(gobject_class),
                       G_SIGNAL_ACTION | G_SIGNAL_RUN_FIRST,
                       G_STRUCT_OFFSET(GwyGraphAreaClass, edit_curve),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__INT,
                       G_TYPE_NONE, 1, G_TYPE_INT);
}

static void
gwy_graph_area_finalize(GObject *object)
{
    GwyGraphArea *area;

    area = GWY_GRAPH_AREA(object);

    g_array_free(area->x_grid_data, TRUE);
    g_array_free(area->y_grid_data, TRUE);

    GWY_SIGNAL_HANDLER_DISCONNECT(area->graph_model, area->curve_notify_id);
    GWY_SIGNAL_HANDLER_DISCONNECT(area->graph_model, area->model_notify_id);
    GWY_SIGNAL_HANDLER_DISCONNECT(area->graph_model,
                                  area->curve_data_changed_id);
    GWY_OBJECT_UNREF(area->graph_model);

    GWY_OBJECT_UNREF(area->pointsdata);
    GWY_OBJECT_UNREF(area->xseldata);
    GWY_OBJECT_UNREF(area->yseldata);
    GWY_OBJECT_UNREF(area->xlinesdata);
    GWY_OBJECT_UNREF(area->ylinesdata);
    GWY_OBJECT_UNREF(area->zoomdata);

    G_OBJECT_CLASS(gwy_graph_area_parent_class)->finalize(object);
}

static GwySelection*
gwy_graph_area_make_selection(GwyGraphArea *area, GType type)
{
    GwySelection *selection;

    selection = GWY_SELECTION(g_object_new(type, NULL));
    gwy_selection_set_max_objects(selection, 1);
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(selection_changed), area);

    return selection;
}

static GwySelection*
gwy_graph_area_make_selection2(GwyGraphArea *area, GType type,
                               GwyOrientation orientation)
{
    GwySelection *selection;

    selection = GWY_SELECTION(g_object_new(type, NULL));
    gwy_selection_set_max_objects(selection, 1);
    g_object_set(selection, "orientation", orientation, NULL);
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(selection_changed), area);

    return selection;
}

static void
gwy_graph_area_init(GwyGraphArea *area)
{
    area->pointsdata
        = gwy_graph_area_make_selection(area, GWY_TYPE_SELECTION_GRAPH_POINT);
    area->xseldata
        = gwy_graph_area_make_selection2(area,
                                         GWY_TYPE_SELECTION_GRAPH_1DAREA,
                                         GWY_ORIENTATION_HORIZONTAL);
    area->yseldata
        = gwy_graph_area_make_selection2(area,
                                         GWY_TYPE_SELECTION_GRAPH_1DAREA,
                                         GWY_ORIENTATION_VERTICAL);
    area->xlinesdata
        = gwy_graph_area_make_selection2(area,
                                         GWY_TYPE_SELECTION_GRAPH_LINE,
                                         GWY_ORIENTATION_HORIZONTAL);
    area->ylinesdata
        = gwy_graph_area_make_selection2(area,
                                         GWY_TYPE_SELECTION_GRAPH_LINE,
                                         GWY_ORIENTATION_VERTICAL);
    area->zoomdata
        = gwy_graph_area_make_selection(area, GWY_TYPE_SELECTION_GRAPH_ZOOM);

    area->x_grid_data = g_array_new(FALSE, FALSE, sizeof(gdouble));
    area->y_grid_data = g_array_new(FALSE, FALSE, sizeof(gdouble));

    area->rx0 = 1.0;
    area->ry0 = 0.0;

    area->enable_user_input = TRUE;
    area->selection_is_editable = TRUE;

    area->lab = GWY_GRAPH_LABEL(gwy_graph_label_new());
    g_signal_connect_swapped(area->lab, "size-allocate",
                             G_CALLBACK(label_geometry_changed), area);
    gtk_layout_put(GTK_LAYOUT(area), GTK_WIDGET(area->lab),
                   GTK_WIDGET(area)->allocation.width
                            - GTK_WIDGET(area->lab)->allocation.width - 5, 5);

    gtk_widget_add_events(GTK_WIDGET(area),
                          GDK_BUTTON_PRESS_MASK
                          | GDK_BUTTON_RELEASE_MASK
                          | GDK_BUTTON_MOTION_MASK
                          | GDK_POINTER_MOTION_MASK
                          | GDK_POINTER_MOTION_HINT_MASK
                          | GDK_LEAVE_NOTIFY_MASK);
}

static void
gwy_graph_area_set_property(GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
    GwyGraphArea *area = GWY_GRAPH_AREA(object);

    switch (prop_id) {
        case PROP_STATUS:
        gwy_graph_area_set_status(area, g_value_get_enum(value));
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_graph_area_get_property(GObject*object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
    GwyGraphArea *area = GWY_GRAPH_AREA(object);

    switch (prop_id) {
        case PROP_STATUS:
        g_value_set_enum(value, area->status);
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/**
 * gwy_graph_area_new:
 *
 * Creates a new graph area widget.
 *
 * Returns: Newly created graph area as #GtkWidget.
 **/
GtkWidget*
gwy_graph_area_new(void)
{
    GtkWidget *widget;

    widget = (GtkWidget*)g_object_new(GWY_TYPE_GRAPH_AREA, NULL);

    return widget;
}

/**
 * gwy_graph_area_set_model:
 * @area: A graph area.
 * @gmodel: New graph model.
 *
 * Sets the graph model of a graph area.
 **/
void
gwy_graph_area_set_model(GwyGraphArea *area,
                         GwyGraphModel *gmodel)
{
    g_return_if_fail(GWY_IS_GRAPH_AREA(area));
    g_return_if_fail(!gmodel || GWY_IS_GRAPH_MODEL(gmodel));

    if (area->graph_model == gmodel)
        return;

    GWY_SIGNAL_HANDLER_DISCONNECT(area->graph_model, area->curve_notify_id);
    GWY_SIGNAL_HANDLER_DISCONNECT(area->graph_model, area->model_notify_id);
    GWY_SIGNAL_HANDLER_DISCONNECT(area->graph_model,
                                  area->curve_data_changed_id);

    if (gmodel)
        g_object_ref(gmodel);
    GWY_OBJECT_UNREF(area->graph_model);
    area->graph_model = gmodel;

    if (gmodel) {
        area->model_notify_id
            = g_signal_connect_swapped(gmodel, "notify",
                                       G_CALLBACK(gwy_graph_area_model_notify),
                                       area);
        area->curve_notify_id
            = g_signal_connect_swapped(gmodel, "curve-notify",
                                       G_CALLBACK(gwy_graph_area_curve_notify),
                                       area);
        area->curve_data_changed_id
            = g_signal_connect_swapped(gmodel, "curve-data-changed",
                                       G_CALLBACK(gwy_graph_area_curve_data_changed),
                                       area);
    }

    gwy_graph_label_set_model(area->lab, gmodel);
    gwy_graph_area_restore_label_pos(area);
}

/**
 * gwy_graph_area_get_model:
 * @area: A graph area.
 *
 * Gets the model of a graph area.
 *
 * Returns: The graph model this graph area widget displays.
 **/
GwyGraphModel*
gwy_graph_area_get_model(GwyGraphArea *area)
{
    g_return_val_if_fail(GWY_IS_GRAPH_AREA(area), NULL);

    return area->graph_model;
}

static void
gwy_graph_area_destroy(GtkObject *object)
{
    GwyGraphArea *area;

    area = GWY_GRAPH_AREA(object);

    if (area->area_dialog) {
        gtk_widget_destroy(area->area_dialog);
        area->area_dialog = NULL;
    }
    if (area->label_dialog) {
        gtk_widget_destroy(area->label_dialog);
        area->label_dialog = NULL;
    }

    if (area->pointsdata)
        g_signal_handlers_disconnect_by_func(area->pointsdata,
                                             selection_changed, area);
    if (area->xseldata)
        g_signal_handlers_disconnect_by_func(area->xseldata,
                                             selection_changed, area);
    if (area->yseldata)
        g_signal_handlers_disconnect_by_func(area->yseldata,
                                             selection_changed, area);
    if (area->xlinesdata)
        g_signal_handlers_disconnect_by_func(area->xlinesdata,
                                             selection_changed, area);
    if (area->ylinesdata)
        g_signal_handlers_disconnect_by_func(area->ylinesdata,
                                             selection_changed, area);
    if (area->zoomdata)
        g_signal_handlers_disconnect_by_func(area->zoomdata,
                                             selection_changed, area);

    GTK_OBJECT_CLASS(gwy_graph_area_parent_class)->destroy(object);
}

static void
gwy_graph_area_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    GwyGraphArea *area;
    GtkAllocation *lab_alloc;

    area = GWY_GRAPH_AREA(widget);
    lab_alloc = &GTK_WIDGET(area->lab)->allocation;

    GTK_WIDGET_CLASS(gwy_graph_area_parent_class)->size_allocate(widget,
                                                                 allocation);

    gwy_graph_area_repos_label(area, allocation, lab_alloc);

    area->old_width = allocation->width;
    area->old_height = allocation->height;
    area->label_old_width = lab_alloc->width;
    area->label_old_height = lab_alloc->height;
}

/* Here x and y are the position where the label is or would be moved to. */
static void
calculate_rxy0(GwyGraphArea *area, gint x, gint y)
{
    GtkWidget *widget = GTK_WIDGET(area);
    GtkWidget *child = area->active;
    gint red_width = widget->allocation.width - child->allocation.width;
    gint red_height = widget->allocation.height - child->allocation.height;

    area->rx0 = (red_width > 0) ? (gdouble)x/red_width : 0.5;
    area->ry0 = (red_height > 0) ? (gdouble)y/red_height : 0.5;
}

static void
gwy_graph_area_repos_label(GwyGraphArea *area,
                           GtkAllocation *area_allocation,
                           GtkAllocation *label_allocation)
{

    gint posx, posy, oldposx, oldposy;

    posx = (gint)(area->rx0*(area_allocation->width
                             - label_allocation->width));
    posy = (gint)(area->ry0*(area_allocation->height
                             - label_allocation->height));
    posx = CLAMP(posx,
                 5, area_allocation->width - label_allocation->width - 5);
    posy = CLAMP(posy,
                 5, area_allocation->height - label_allocation->height - 5);

    gtk_container_child_get(GTK_CONTAINER(area), GTK_WIDGET(area->lab),
                            "x", &oldposx,
                            "y", &oldposy,
                            NULL);

    if (area->old_width != area_allocation->width
        || area->old_height != area_allocation->height
        || area->label_old_width != label_allocation->width
        || area->label_old_height != label_allocation->height
        || posx != oldposx
        || posy != oldposy) {
        gtk_layout_move(GTK_LAYOUT(area), GTK_WIDGET(area->lab), posx, posy);
    }
}

static void
gwy_graph_area_realize(GtkWidget *widget)
{
    GdkDisplay *display;
    GwyGraphArea *area;

    if (GTK_WIDGET_CLASS(gwy_graph_area_parent_class)->realize)
        GTK_WIDGET_CLASS(gwy_graph_area_parent_class)->realize(widget);

    area = GWY_GRAPH_AREA(widget);
    area->gc = gdk_gc_new(GTK_LAYOUT(widget)->bin_window);

    display = gtk_widget_get_display(widget);
    area->cross_cursor = gdk_cursor_new_for_display(display,
                                                    GDK_CROSS);
    area->fleur_cursor = gdk_cursor_new_for_display(display,
                                                    GDK_FLEUR);
    area->harrow_cursor = gdk_cursor_new_for_display(display,
                                                     GDK_SB_H_DOUBLE_ARROW);
    area->varrow_cursor = gdk_cursor_new_for_display(display,
                                                     GDK_SB_V_DOUBLE_ARROW);

    gdk_gc_set_rgb_bg_color(area->gc, &widget->style->white);
    gdk_gc_set_rgb_fg_color(area->gc, &widget->style->black);
}

static void
gwy_graph_area_unrealize(GtkWidget *widget)
{
    GwyGraphArea *area;

    area = GWY_GRAPH_AREA(widget);

    GWY_OBJECT_UNREF(area->gc);
    gdk_cursor_unref(area->cross_cursor);
    gdk_cursor_unref(area->fleur_cursor);
    gdk_cursor_unref(area->harrow_cursor);
    gdk_cursor_unref(area->varrow_cursor);

    if (GTK_WIDGET_CLASS(gwy_graph_area_parent_class)->unrealize)
        GTK_WIDGET_CLASS(gwy_graph_area_parent_class)->unrealize(widget);
}

static gboolean
gwy_graph_area_expose(GtkWidget *widget,
                      GdkEventExpose *event)
{
    GwyGraphArea *area;
    GdkDrawable *drawable;

    gwy_debug("%p", widget);

    area = GWY_GRAPH_AREA(widget);
    drawable = GTK_LAYOUT(widget)->bin_window;

    gdk_gc_set_rgb_fg_color(area->gc, &widget->style->white);
    gdk_draw_rectangle(drawable, area->gc, TRUE,
                       0, 0,
                       widget->allocation.width, widget->allocation.height);
    gdk_gc_set_rgb_fg_color(area->gc, &widget->style->black);

    gwy_graph_area_draw_on_drawable(area, drawable, area->gc,
                                    0, 0,
                                    widget->allocation.width,
                                    widget->allocation.height);

    if (area->status == GWY_GRAPH_STATUS_ZOOM
        && (area->selecting != 0))
        gwy_graph_area_draw_zoom(drawable, area->gc, area);

    GTK_WIDGET_CLASS(gwy_graph_area_parent_class)->expose_event(widget, event);

    return TRUE;
}

/**
 * gwy_graph_area_draw_on_drawable:
 * @area: A graph area.
 * @drawable: a #GdkDrawable (destination for graphics operations)
 * @gc: Graphics context.
 *      It is modified by this function unpredictably.
 * @x: X position in @drawable where the graph area should be drawn
 * @y: Y position in @drawable where the graph area should be drawn
 * @width: width of the graph area on the drawable
 * @height: height of the graph area on the drawable
 *
 * Draws a graph area to a Gdk drawable.
 **/
void
gwy_graph_area_draw_on_drawable(GwyGraphArea *area,
                                GdkDrawable *drawable, GdkGC *gc,
                                gint x, gint y,
                                gint width, gint height)
{
    gint nc, i;
    GwyGraphActiveAreaSpecs specs;
    GwyGraphCurveModel *curvemodel;
    GwyGraphModel *model;
    GdkColor fg;

    model = area->graph_model;
    specs.xmin = x;
    specs.ymin = y;
    specs.height = height;
    specs.width = width;
    gwy_debug("specs: %d %d %d %d",
              specs.xmin, specs.ymin, specs.width, specs.height);
    specs.real_xmin = area->x_min;
    specs.real_ymin = area->y_min;
    specs.real_width = area->x_max - area->x_min;
    specs.real_height = area->y_max - area->y_min;
    g_object_get(model,
                 "x-logarithmic", &specs.log_x,
                 "y-logarithmic", &specs.log_y,
                 NULL);

    gwy_debug("specs.real_xmin: %g, specs.real_ymin: %g",
              specs.real_xmin, specs.real_ymin);
    gwy_debug("specs.real_width: %g, specs.real_height: %g",
              specs.real_width, specs.real_height);

    /* draw continuous selection */
    if (area->status == GWY_GRAPH_STATUS_XSEL)
        gwy_graph_draw_selection_xareas(drawable, gc, &specs,
                                  GWY_SELECTION_GRAPH_1DAREA(area->xseldata));
    if (area->status == GWY_GRAPH_STATUS_YSEL)
        gwy_graph_draw_selection_yareas(drawable, gc, &specs,
                                  GWY_SELECTION_GRAPH_1DAREA(area->yseldata));


    gwy_graph_draw_grid(drawable, gc, &specs,
                        area->x_grid_data->len,
                        (const gdouble*)area->x_grid_data->data,
                        area->y_grid_data->len,
                        (const gdouble*)area->y_grid_data->data);

    nc = gwy_graph_model_get_n_curves(model);
    for (i = 0; i < nc; i++) {
        curvemodel = gwy_graph_model_get_curve(model, i);
        gwy_graph_draw_curve(drawable, gc, &specs, curvemodel);
    }

    switch (area->status) {
        case GWY_GRAPH_STATUS_POINTS:
        case GWY_GRAPH_STATUS_ZOOM:
        gwy_graph_draw_selection_points
                                 (drawable, gc, &specs,
                                  GWY_SELECTION_GRAPH_POINT(area->pointsdata));
        break;

        case GWY_GRAPH_STATUS_XLINES:
        gwy_graph_draw_selection_lines
                                 (drawable, gc, &specs,
                                  GWY_SELECTION_GRAPH_LINE(area->xlinesdata),
                                  GTK_ORIENTATION_VERTICAL);
        break;

        case GWY_GRAPH_STATUS_YLINES:
        gwy_graph_draw_selection_lines
                                 (drawable, gc, &specs,
                                  GWY_SELECTION_GRAPH_LINE(area->ylinesdata),
                                  GTK_ORIENTATION_HORIZONTAL);
        break;


        default:
        /* PLAIN */
        break;
    }

    /* draw area boundaries */
    /* FIXME: use Gtk+ theme */
    fg.red = fg.green = fg.blue = 0;
    gdk_gc_set_rgb_fg_color(gc, &fg);
    gdk_gc_set_line_attributes(gc, 1,
                               GDK_LINE_SOLID, GDK_CAP_ROUND, GDK_JOIN_MITER);
    gdk_draw_line(drawable, gc, x, y, x + width-1, y);
    gdk_draw_line(drawable, gc, x + width-1, y, x + width-1, y + height-1);
    gdk_draw_line(drawable, gc, x + width-1, y + height-1, x, y + height-1);
    gdk_draw_line(drawable, gc, x, y + height-1, x, y);
}

static void
gwy_graph_area_draw_zoom(GdkDrawable *drawable,
                         GdkGC *gc,
                         GwyGraphArea *area)
{
    gint xmin, ymin, xmax, ymax, n_of_zooms;
    gdouble sel_zoomdata[4];
    GtkWidget *widget;

    n_of_zooms = gwy_selection_get_data(area->zoomdata, NULL);

    if (n_of_zooms != 1)
        return;

    gwy_selection_get_object(area->zoomdata, 0, sel_zoomdata);

    if (sel_zoomdata[2] == 0 || sel_zoomdata[3] == 0) return;
    gdk_gc_set_function(gc, GDK_INVERT);
    widget = GTK_WIDGET(area);

    if (sel_zoomdata[2] < 0) {
        xmin = data_to_scr_x(widget, sel_zoomdata[0] + sel_zoomdata[2]);
        xmax = data_to_scr_x(widget, sel_zoomdata[0]);
    }
    else {
        xmin = data_to_scr_x(widget, sel_zoomdata[0]);
        xmax = data_to_scr_x(widget, sel_zoomdata[0] + sel_zoomdata[2]);
    }

    if (sel_zoomdata[3] > 0) {
        ymin = data_to_scr_y(widget, sel_zoomdata[1] + sel_zoomdata[3]);
        ymax = data_to_scr_y(widget, sel_zoomdata[1]);
    }
    else {
        ymin = data_to_scr_y(widget, sel_zoomdata[1]);
        ymax = data_to_scr_y(widget, sel_zoomdata[1] + sel_zoomdata[3]);
    }

    gdk_draw_rectangle(drawable, area->gc, 0,
                       xmin, ymin,
                       xmax - xmin, ymax - ymin);

    gdk_gc_set_function(area->gc, GDK_COPY);
}

static gboolean
gwy_graph_area_button_press(GtkWidget *widget, GdkEventButton *event)
{
    GwyGraphArea *area;
    GwyGraphModel *gmodel;
    GtkAllocation *allocation;
    GtkWidget *child;
    GwySelection *selection;
    gint x, y, curve, i, nc;
    gdouble dx, dy, selection_pointdata[2];
    gdouble selection_zoomdata[4];
    gboolean visible;

    g_return_val_if_fail(GWY_IS_GRAPH_AREA(widget), FALSE);

    area = GWY_GRAPH_AREA(widget);
    gwy_debug("event: %g %g", event->x, event->y);
    x = (gint)event->x;
    y = (gint)event->y;
    dx = scr_to_data_x(widget, x);
    dy = scr_to_data_y(widget, y);
    gmodel = area->graph_model;
    nc = gwy_graph_model_get_n_curves(gmodel);
    child = gwy_graph_area_find_child(area, x, y);
    if (child) {
        g_return_val_if_fail(GWY_IS_GRAPH_LABEL(child), FALSE);
        g_object_get(gmodel, "label-visible", &visible, NULL);
        if (!visible)
            return FALSE;

        /* FIXME: The conditions below are strange.  Or I don't understand
         * them. */
        if (event->type == GDK_2BUTTON_PRESS
            && area->enable_user_input) {
            if (!area->label_dialog) {
                area->label_dialog = _gwy_graph_label_dialog_new();
                g_signal_connect(area->label_dialog, "response",
                                 G_CALLBACK(gwy_graph_label_response), area);

            }
            _gwy_graph_label_dialog_set_graph_data(area->label_dialog, gmodel);
            gtk_widget_show_all(area->label_dialog);
            gtk_window_present(GTK_WINDOW(area->label_dialog));
        }
        else {
            area->active = child;
            area->x0 = x;
            area->y0 = y;
            area->xoff = 0;
            area->yoff = 0;
            allocation = &area->active->allocation;
            area->rxoff = x - allocation->x;
            area->ryoff = y - allocation->y;
            gwy_graph_area_draw_child_rectangle(area);
        }
        return FALSE;
    }

    if (area->status == GWY_GRAPH_STATUS_PLAIN && nc > 0
        && area->enable_user_input) {
        curve = gwy_graph_area_find_curve(area, dx, dy);
        if (curve >= 0) {
            gwy_graph_area_edit_curve(area, curve);
            return TRUE;
        }
    }

    if (area->status == GWY_GRAPH_STATUS_ZOOM) {
        gwy_selection_clear(area->zoomdata);
        selection_zoomdata[0] = dx;
        selection_zoomdata[1] = dy;
        selection_zoomdata[2] = 0;
        selection_zoomdata[3] = 0;
        gwy_selection_set_object(area->zoomdata, -1, selection_zoomdata);

        area->selecting = TRUE;

        return TRUE;
    }

    /* Everything below are selections. */
    if (!area->selection_is_editable)
        return TRUE;

    if (area->status == GWY_GRAPH_STATUS_POINTS
        && gwy_selection_get_max_objects(area->pointsdata) == 1)
        gwy_selection_clear(area->pointsdata);

    if (area->status == GWY_GRAPH_STATUS_XLINES
        && gwy_selection_get_max_objects(area->xlinesdata) == 1)
        gwy_selection_clear(area->xlinesdata);

    if (area->status == GWY_GRAPH_STATUS_YLINES
        && gwy_selection_get_max_objects(area->ylinesdata) == 1)
        gwy_selection_clear(area->ylinesdata);

    if (area->status == GWY_GRAPH_STATUS_YSEL
        && gwy_selection_get_max_objects(area->yseldata) == 1)
        gwy_selection_clear(area->yseldata);

    if (area->status == GWY_GRAPH_STATUS_POINTS) {
        if (event->button == 1) {
            area->selected_object_index
                = gwy_graph_area_find_point(area, dx, dy);

            if (!(gwy_selection_is_full(area->pointsdata) &&
                area->selected_object_index == -1)) {
                selection_pointdata[0] = dx;
                selection_pointdata[1] = dy;
                area->selecting = TRUE;
                gwy_selection_set_object(area->pointsdata,
                                         area->selected_object_index,
                                         selection_pointdata);
                if (area->selected_object_index == -1)
                    area->selected_object_index
                        = gwy_selection_get_data(area->pointsdata, NULL) - 1;
            }
        }
        else {
            i = gwy_graph_area_find_point(area, dx, dy);
            if (i >= 0)
                gwy_selection_delete_object(area->pointsdata, i);
            gwy_selection_finished(area->pointsdata);
        }
    }

    if (area->status == GWY_GRAPH_STATUS_XSEL
        || area->status == GWY_GRAPH_STATUS_YSEL) {
        gdouble coords[2];
        gdouble pos;

        if (area->status == GWY_GRAPH_STATUS_XSEL) {
            pos = dx;
            selection = area->xseldata;
        }
        else {
            pos = dy;
            selection = area->yseldata;
        }

        if (event->button == 1) {
            i = gwy_graph_area_find_selection_edge(area, dx, dy,
                                                   &area->selected_border);
            area->selected_object_index = i;
            /* Allow to start a new selection without explicitly clearing the
             * existing one when max_objects is 1 */
            if (gwy_selection_get_max_objects(selection) == 1 && i == -1)
                gwy_selection_clear(selection);

            if (i == -1 && !gwy_selection_is_full(selection)) {
                /* Add a new selection object */
                coords[0] = pos;
                coords[1] = pos;
                /* Start with the `other' border moving */
                area->selected_border = 1;
                area->selected_object_index
                    = gwy_selection_set_object(selection, -1, coords);
                area->selecting = TRUE;
            }
            else if (area->selected_object_index != -1) {
                /* Move existing edge */
                coords[area->selected_border] = pos;
                gwy_selection_get_object(selection, i, coords);
                area->selecting = TRUE;
            }
        }
        else {
            i = gwy_graph_area_find_selection(area, dx, dy);
            /* Remove selection */
            if (i >= 0) {
                gwy_selection_delete_object(selection, i);
                gwy_selection_finished(selection);
            }
        }
    }

    if (area->status == GWY_GRAPH_STATUS_XLINES) {
        if (event->button == 1) {
            area->selected_object_index = gwy_graph_area_find_line(area, dx);

            if (!(gwy_selection_is_full(area->xlinesdata) &&
                area->selected_object_index == -1))
            {
                gwy_selection_set_object(area->xlinesdata,
                                     area->selected_object_index,
                                     &dx);

                area->selecting = TRUE;
                if (area->selected_object_index == -1)
                    area->selected_object_index =
                                gwy_selection_get_data(area->xlinesdata, NULL) - 1;
            }

        }
        else {
            i = gwy_graph_area_find_line(area, dx);
            if (i >= 0)
                gwy_selection_delete_object(area->xlinesdata, i);
        }
    }

    if (area->status == GWY_GRAPH_STATUS_YLINES) {
        if (event->button == 1) {
            area->selected_object_index = gwy_graph_area_find_line(area, dy);

            if (!(gwy_selection_is_full(area->ylinesdata) &&
                  area->selected_object_index == -1)) {
                gwy_selection_set_object(area->ylinesdata,
                                     area->selected_object_index,
                                     &dy);

                area->selecting = TRUE;
                if (area->selected_object_index == -1)
                    area->selected_object_index
                        = gwy_selection_get_data(area->ylinesdata, NULL) - 1;
            }
        }
        else {
            i = gwy_graph_area_find_line(area, dy);
            if (i >= 0)
                gwy_selection_delete_object(area->ylinesdata, i);
        }
    }

    return TRUE;
}

static gboolean
gwy_graph_area_button_release(GtkWidget *widget, GdkEventButton *event)
{
    GwyGraphArea *area;
    gint x, y, ispos = 0, nselected;
    gdouble dx, dy, selection_pointdata[2], selection_areadata[2];
    gdouble selection_zoomdata[4], selection_linedata;

    g_return_val_if_fail(GWY_IS_GRAPH_AREA(widget), FALSE);

    area = GWY_GRAPH_AREA(widget);
    gwy_debug("event: %g %g", event->x, event->y);
    x = (gint)event->x;
    y = (gint)event->y;
    dx = scr_to_data_x(widget, x);
    dy = scr_to_data_y(widget, y);

    switch (area->status) {
        case GWY_GRAPH_STATUS_XSEL:
        if (area->selecting &&
            gwy_selection_get_object(area->xseldata,
                                     area->selected_object_index,
                                     selection_areadata)) {
            selection_areadata[area->selected_border] = dx;
            if (selection_areadata[1] == selection_areadata[0])
                gwy_selection_delete_object(area->xseldata,
                                            area->selected_object_index);
            else
                gwy_selection_set_object(area->xseldata,
                                         area->selected_object_index,
                                         selection_areadata);
            area->selecting = FALSE;
            gwy_selection_finished(area->xseldata);
        }
        break;

        case GWY_GRAPH_STATUS_YSEL:
        if (area->selecting &&
            gwy_selection_get_object(area->yseldata,
                                     area->selected_object_index,
                                     selection_areadata)) {
            selection_areadata[area->selected_border] = dy;
            if (selection_areadata[1] == selection_areadata[0])
                gwy_selection_delete_object(area->yseldata,
                                            area->selected_object_index);
            else
                gwy_selection_set_object(area->yseldata,
                                         area->selected_object_index,
                                         selection_areadata);
            area->selecting = FALSE;
            gwy_selection_finished(area->yseldata);
        }
        break;

        case GWY_GRAPH_STATUS_XLINES:
        if (area->selecting && gwy_selection_get_data(area->xlinesdata, NULL)) {
            selection_linedata = dx;
            area->selecting = FALSE;
            gwy_selection_set_object(area->xlinesdata,
                                     area->selected_object_index,
                                     &selection_linedata);
            gwy_selection_finished(area->xlinesdata);
        }
        break;

        case GWY_GRAPH_STATUS_YLINES:
        if (area->selecting && gwy_selection_get_data(area->ylinesdata, NULL)) {
            selection_linedata = dy;
            area->selecting = FALSE;
            gwy_selection_set_object(area->ylinesdata,
                                     area->selected_object_index,
                                     &selection_linedata);
            gwy_selection_finished(area->ylinesdata);
        }
        break;

        case GWY_GRAPH_STATUS_POINTS:
        if (area->selecting) {
            selection_pointdata[0] = dx;
            selection_pointdata[1] = dy;
            gwy_selection_set_object(area->pointsdata,
                                     area->selected_object_index,
                                     selection_pointdata);
            area->selecting = FALSE;
            gwy_selection_finished(area->pointsdata);
        }

        case GWY_GRAPH_STATUS_ZOOM:
        nselected = gwy_selection_get_data(area->zoomdata, NULL);
        if (area->selecting && nselected) {
            gwy_selection_get_object(area->zoomdata, nselected - 1,
                                     selection_zoomdata);

            selection_zoomdata[2] = dx - selection_zoomdata[0];
            selection_zoomdata[3] = dy - selection_zoomdata[1];

            gwy_selection_set_object(area->zoomdata, nselected - 1,
                                     selection_zoomdata);

            area->selecting = FALSE;
            gwy_selection_finished(area->zoomdata);
        }
        break;

        default:
        /* PLAIN */
        break;
    }

    if (area->active) {
        GwyGraphModel *gmodel = area->graph_model;
        GwyGraphLabelPosition pos, newpos;

        gwy_graph_area_draw_child_rectangle(area);

        if (!ispos) {
            x = (gint)event->x;
            y = (gint)event->y;
            ispos = 1;
        }
        gwy_graph_area_clamp_coords_for_child(area, &x, &y);
        x -= area->x0 - area->active->allocation.x;
        y -= area->y0 - area->active->allocation.y;
        calculate_rxy0(area, x, y);

        g_object_get(gmodel, "label-position", &pos, NULL);
        if (area->rx0 < 0.04 && area->ry0 < 0.04)
            newpos = GWY_GRAPH_LABEL_NORTHWEST;
        else if (area->rx0 > 0.96 && area->ry0 < 0.04)
            newpos = GWY_GRAPH_LABEL_NORTHEAST;
        else if (area->rx0 > 0.96 && area->ry0 > 0.96)
            newpos = GWY_GRAPH_LABEL_SOUTHEAST;
        else if (area->rx0 < 0.04 && area->ry0 > 0.96)
            newpos = GWY_GRAPH_LABEL_SOUTHWEST;
        else
            newpos = GWY_GRAPH_LABEL_USER;

        area->active = NULL;
        if (newpos != pos || newpos == GWY_GRAPH_LABEL_USER) {
            g_object_set(gmodel,
                         "label-position", newpos,
                         "label-relative-x", area->rx0,
                         "label-relative-y", area->ry0,
                         NULL);
        }
    }
    return FALSE;
}

static gboolean
gwy_graph_area_motion_notify(GtkWidget *widget, GdkEventMotion *event)
{
    GwyGraphArea *area;
    GdkWindow *window;
    gint x, y, ispos = 0, nselected;
    gdouble dx, dy, selection_pointdata[2], selection_areadata[2];
    gdouble selection_zoomdata[4], selection_linedata;

    g_return_val_if_fail(GWY_IS_GRAPH_AREA(widget), FALSE);

    area = GWY_GRAPH_AREA(widget);
    if (event->is_hint)
        gdk_window_get_pointer(widget->window, &x, &y, NULL);
    else {
        x = event->x;
        y = event->y;
    }
    gwy_debug("event: %d %d", x, y);
    dx = scr_to_data_x(widget, x);
    dy = scr_to_data_y(widget, y);

    area->mouse_present = TRUE;
    area->actual_cursor.x = dx;
    area->actual_cursor.y = dy;

    window = widget->window;

    switch (area->status) {
        case GWY_GRAPH_STATUS_XSEL:
        if (area->selecting
            || gwy_graph_area_find_selection_edge(area, dx, dy, NULL) != -1)
            gdk_window_set_cursor(window, area->harrow_cursor);
        else
            gdk_window_set_cursor(window, area->cross_cursor);

        if (area->selecting
            && gwy_selection_get_object(area->xseldata,
                                        area->selected_object_index,
                                        selection_areadata)) {
            selection_areadata[area->selected_border] = dx;
            gwy_selection_set_object(area->xseldata,
                                     area->selected_object_index,
                                     selection_areadata);
        }
        break;

        case GWY_GRAPH_STATUS_YSEL:
        if (area->selecting
            || gwy_graph_area_find_selection_edge(area, dx, dy, NULL) != -1)
            gdk_window_set_cursor(window, area->varrow_cursor);
        else
            gdk_window_set_cursor(window, area->cross_cursor);

        if (area->selecting
            && gwy_selection_get_object(area->yseldata,
                                        area->selected_object_index,
                                        selection_areadata)) {
            selection_areadata[area->selected_border] = dy;
            gwy_selection_set_object(area->yseldata,
                                     area->selected_object_index,
                                     selection_areadata);
        }
        break;

        case GWY_GRAPH_STATUS_XLINES:
        if (area->selecting || gwy_graph_area_find_line(area, dx) != -1)
            gdk_window_set_cursor(window, area->harrow_cursor);
        else
            gdk_window_set_cursor(window, area->cross_cursor);

        if (area->selecting && gwy_selection_get_data(area->xlinesdata, NULL)) {
            selection_linedata = dx;

            gwy_selection_set_object(area->xlinesdata,
                                     area->selected_object_index,
                                     &selection_linedata);
            gwy_selection_finished(area->xlinesdata);
        }
        break;

        case GWY_GRAPH_STATUS_YLINES:
        if (area->selecting || gwy_graph_area_find_line(area, dy) != -1)
            gdk_window_set_cursor(window, area->varrow_cursor);
        else
            gdk_window_set_cursor(window, area->cross_cursor);

        if (area->selecting && gwy_selection_get_data(area->ylinesdata, NULL)) {
            selection_linedata = dy;

            gwy_selection_set_object(area->ylinesdata,
                                     area->selected_object_index,
                                     &selection_linedata);
            gwy_selection_finished(area->ylinesdata);
        }
        break;

        case GWY_GRAPH_STATUS_POINTS:
        if (area->selecting || gwy_graph_area_find_point(area, dx, dy) != -1)
            gdk_window_set_cursor(window, area->fleur_cursor);
        else
            gdk_window_set_cursor(window, area->cross_cursor);

        if (area->selecting) {
            selection_pointdata[0] = dx;
            selection_pointdata[1] = dy;
            gwy_selection_set_object(area->pointsdata,
                                     area->selected_object_index,
                                     selection_pointdata);
        }
        break;

        case GWY_GRAPH_STATUS_ZOOM:
        nselected = gwy_selection_get_data(area->zoomdata, NULL);
        if (area->selecting && nselected) {
            gwy_selection_get_object(area->zoomdata, nselected - 1,
                                     selection_zoomdata);

            selection_zoomdata[2] = dx - selection_zoomdata[0];
            selection_zoomdata[3] = dy - selection_zoomdata[1];

            gwy_selection_set_object(area->zoomdata, nselected - 1,
                                     selection_zoomdata);
        }
        break;

        default:
        /* PLAIN */
        break;
    }

    /* Widget (label) movement. */
    if (area->active) {

        if (!ispos) {
            x = (gint)event->x;
            y = (gint)event->y;
            ispos = 1;
        }
        gwy_graph_area_clamp_coords_for_child(area, &x, &y);

        if (x - area->x0 == area->xoff && y - area->y0 == area->yoff)
            return FALSE;

        gwy_graph_area_draw_child_rectangle(area);
        area->xoff = x - area->x0;
        area->yoff = y - area->y0;
        gwy_graph_area_draw_child_rectangle(area);
    }

    return FALSE;
}

static gint
gwy_graph_area_find_curve(GwyGraphArea *area, gdouble x, gdouble y)
{
    gint i, j, nc, ndata;
    gint closestid = -1;
    gdouble closestdistance, distance = 0.0;
    const gdouble *xdata, *ydata;
    GwyGraphCurveModel *curvemodel;
    GwyGraphModel *model;

    closestdistance = G_MAXDOUBLE;
    model = area->graph_model;
    nc = gwy_graph_model_get_n_curves(model);
    for (i = 0; i < nc; i++) {
        curvemodel = gwy_graph_model_get_curve(model, i);
        ndata = gwy_graph_curve_model_get_ndata(curvemodel);
        xdata = gwy_graph_curve_model_get_xdata(curvemodel);
        ydata = gwy_graph_curve_model_get_ydata(curvemodel);
        for (j = 0; j < ndata - 1; j++) {
            if (xdata[j] <= x && xdata[j + 1] >= x) {
                distance = fabs(y - ydata[j]
                                + (x - xdata[j])*(ydata[j + 1] - ydata[j])
                                  /(xdata[j + 1] - xdata[j]));
                if (distance < closestdistance) {
                    closestdistance = distance;
                    closestid = i;
                }
                break;
            }
        }
    }
    if (fabs(closestdistance/(area->y_max - area->y_min)) < 0.05)
        return closestid;
    else
        return -1;
}

/**
 * gwy_graph_area_find_selection_edge:
 * @area: A graph area.
 * @x: Real x position.
 * @y: Real y position.
 * @eindex: Location store edge index (index of particular edge coordinate
 *          inside the selection object).
 *
 * Finds range selection object nearest to given coordinates.
 *
 * Returns: The index of selection object found, -1 when none is found.
 **/
static gint
gwy_graph_area_find_selection_edge(GwyGraphArea *area,
                                   gdouble x,
                                   gdouble y,
                                   int *eindex)
{
    GwySelection *selection;
    gdouble coords[2], dists[2];
    gdouble maxoff, min, pos;
    gint n, i, mi, ei;

    gwy_debug(" ");

    switch (area->status) {
        case GWY_GRAPH_STATUS_XSEL:
        case GWY_GRAPH_STATUS_YSEL:
        /* FIXME: What is 50? */
        if (area->status == GWY_GRAPH_STATUS_XSEL) {
            pos = x;
            maxoff = (area->x_max - area->x_min)/50;
            selection = area->xseldata;
        }
        else {
            pos = y;
            maxoff = (area->y_max - area->y_min)/50;
            selection = area->yseldata;
        }

        mi = -1;
        ei = -1;
        min = G_MAXDOUBLE;
        n = gwy_selection_get_data(selection, NULL);
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(selection, i, coords);

            dists[0] = fabs(coords[0] - pos);
            dists[1] = fabs(coords[1] - pos);
            if (dists[1] <= dists[0]) {
                if (dists[1] < min) {
                    min = dists[1];
                    mi = i;
                    ei = 1;
                }
            }
            else {
                if (dists[0] < min) {
                    min = dists[0];
                    mi = i;
                    ei = 0;
                }
            }
        }

        if (min > maxoff)
            mi = -1;
        else if (eindex)
            *eindex = ei;
        return mi;
        break;

        default:
        /* Not implemented */
        break;
    }

    return -1;
}

/**
 * gwy_graph_area_find_selection:
 * @area: A graph area.
 * @x: Real x position.
 * @y: Real y position.
 *
 * Finds range selection containing given coordinates.
 *
 * Returns: The index of selection object found, -1 when none is found.
 **/
static gint
gwy_graph_area_find_selection(GwyGraphArea *area,
                              gdouble x,
                              gdouble y)
{
    GwySelection *selection;
    gdouble coords[2];
    gdouble pos;
    gint n, i;

    gwy_debug(" ");

    switch (area->status) {
        case GWY_GRAPH_STATUS_XSEL:
        case GWY_GRAPH_STATUS_YSEL:
        if (area->status == GWY_GRAPH_STATUS_XSEL) {
            pos = x;
            selection = area->xseldata;
        }
        else {
            pos = y;
            selection = area->yseldata;
        }

        n = gwy_selection_get_data(selection, NULL);
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(selection, i, coords);
            if (pos >= MIN(coords[0], coords[1])
                && pos <= MAX(coords[0], coords[1]))
                return i;
        }
        return -1;
        break;

        default:
        /* Not implemented */
        break;
    }

    return -1;
}

static gint
gwy_graph_area_find_point(GwyGraphArea *area, gdouble x, gdouble y)
{
    gint i;
    gdouble xmin, ymin, xmax, ymax, xoff, yoff, selection_data[2];

    /* FIXME: What is 50? */
    xoff = (area->x_max - area->x_min)/50;
    yoff = (area->y_min - area->y_max)/50;

    for (i = 0; i < gwy_selection_get_data(area->pointsdata, NULL); i++) {
        gwy_selection_get_object(area->pointsdata,
                                 i, selection_data);

        xmin = selection_data[0] - xoff;
        xmax = selection_data[0] + xoff;
        ymin = selection_data[1] - yoff;
        ymax = selection_data[1] + yoff;

        if (xmin <= x && xmax >= x && ymin <= y && ymax >= y) return i;
    }
    return -1;
}

static gint
gwy_graph_area_find_line(GwyGraphArea *area, gdouble position)
{
    gdouble min = 0, max = 0, xoff, yoff, selection_data;
    guint n, i;

    if (area->status == GWY_GRAPH_STATUS_XLINES) {
        /* FIXME: What is 100? */
        xoff = (area->x_max - area->x_min)/100;
        n = gwy_selection_get_data(area->xlinesdata, NULL);
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(area->xlinesdata, i, &selection_data);

            min = selection_data - xoff;
            max = selection_data + xoff;
            if (min <= position && max >= position)
                return i;
        }
    }
    else if (area->status == GWY_GRAPH_STATUS_YLINES) {
        /* FIXME: What is 100? */
        yoff = (area->y_max - area->y_min)/100;
        n = gwy_selection_get_data(area->ylinesdata, NULL);
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(area->ylinesdata, i, &selection_data);

            min = selection_data - yoff;
            max = selection_data + yoff;
            if (min <= position && max >= position)
                return i;
        }
    }

    return -1;
}

static GtkWidget*
gwy_graph_area_find_child(GwyGraphArea *area, gint x, gint y)
{
    GList *children, *l;

    if (!area->graph_model->label_visible)
        return NULL;

    children = gtk_container_get_children(GTK_CONTAINER(area));
    for (l = children; l; l = g_list_next(l)) {
        GtkWidget *child;
        GtkAllocation *alloc;

        child = GTK_WIDGET(l->data);
        alloc = &child->allocation;
        if (x >= alloc->x && x < alloc->x + alloc->width
            && y >= alloc->y && y < alloc->y + alloc->height) {
            g_list_free(children);
            return child;
        }
    }
    g_list_free(children);
    return NULL;
}

static void
gwy_graph_area_clamp_coords_for_child(GwyGraphArea *area,
                                 gint *x,
                                 gint *y)
{
    GtkAllocation *allocation;
    gint min, max;

    allocation = &area->active->allocation;

    min = area->x0 - allocation->x;
    max = GTK_WIDGET(area)->allocation.width
          - (allocation->width - min) - 1;
    *x = CLAMP(*x, min, max);

    min = area->y0 - allocation->y;
    max = GTK_WIDGET(area)->allocation.height
          - (allocation->height - min) - 1;
    *y = CLAMP(*y, min, max);
}

static void
gwy_graph_area_draw_child_rectangle(GwyGraphArea *area)
{
    GtkAllocation *allocation;

    if (!area->active)
        return;

    gdk_gc_set_function(area->gc, GDK_INVERT);
    allocation = &area->active->allocation;
    gdk_draw_rectangle(GTK_LAYOUT(area)->bin_window, area->gc, FALSE,
                       allocation->x + area->xoff,
                       allocation->y + area->yoff,
                       allocation->width,
                       allocation->height);
    gdk_gc_set_function(area->gc, GDK_COPY);
}

static void
gwy_graph_area_model_notify(GwyGraphArea *area,
                            GParamSpec *pspec,
                            G_GNUC_UNUSED GwyGraphModel *gmodel)
{
    if (gwy_strequal(pspec->name, "n-curves")) {
        gwy_graph_area_n_curves_changed(area);
        gtk_widget_queue_draw(GTK_WIDGET(area));
    }

    if (gwy_strequal(pspec->name, "grid-type")) {
        gtk_widget_queue_draw(GTK_WIDGET(area));
        return;
    }

    if (gwy_strequal(pspec->name, "label-position")
        || gwy_strequal(pspec->name, "label-relative-x")
        || gwy_strequal(pspec->name, "label-relative-y")) {
        gwy_graph_area_restore_label_pos(area);
        return;
    }
}

static void
gwy_graph_area_restore_label_pos(GwyGraphArea *area)
{
    GwyGraphModel *gmodel = area->graph_model;
    GwyGraphLabelPosition pos = GWY_GRAPH_LABEL_NORTHWEST;

    if (gmodel)
        g_object_get(gmodel, "label-position", &pos, NULL);

    if (pos == GWY_GRAPH_LABEL_NORTHWEST) {
        area->rx0 = 0.0;
        area->ry0 = 0.0;
    }
    else if (pos == GWY_GRAPH_LABEL_NORTHEAST) {
        area->rx0 = 1.0;
        area->ry0 = 0.0;
    }
    else if (pos == GWY_GRAPH_LABEL_SOUTHWEST) {
        area->rx0 = 0.0;
        area->ry0 = 1.0;
    }
    else if (pos == GWY_GRAPH_LABEL_SOUTHEAST) {
        area->rx0 = 1.0;
        area->ry0 = 1.0;
    }
    else {
        g_object_get(gmodel,
                     "label-relative-x", &area->rx0,
                     "label-relative-y", &area->ry0,
                     NULL);
    }

    if (GTK_WIDGET_DRAWABLE(area))
        gtk_widget_queue_draw(GTK_WIDGET(area));
}

static void
gwy_graph_area_n_curves_changed(GwyGraphArea *area)
{
    GwyGraphAreaDialog *dialog;
    gint n, i;

    if (!area->area_dialog)
        return;

    dialog = GWY_GRAPH_AREA_DIALOG(area->area_dialog);
    n = gwy_graph_model_get_n_curves(area->graph_model);
    i = gwy_graph_model_get_curve_index(area->graph_model, dialog->curve_model);
    _gwy_graph_area_dialog_set_switching(area->area_dialog, i > 0, i < n-1);
    if (!GTK_WIDGET_VISIBLE(dialog) || !dialog->curve_model)
        return;

    if (i == -1)
        gwy_graph_area_edit_curve(area, -1);
}

static void
gwy_graph_area_curve_notify(GwyGraphArea *area,
                            G_GNUC_UNUSED gint i,
                            G_GNUC_UNUSED GParamSpec *pspec)
{
    if (GTK_WIDGET_DRAWABLE(area))
        gtk_widget_queue_draw(GTK_WIDGET(area));
}

static void
gwy_graph_area_curve_data_changed(GwyGraphArea *area,
                                  G_GNUC_UNUSED gint i)
{
    if (GTK_WIDGET_DRAWABLE(area))
        gtk_widget_queue_draw(GTK_WIDGET(area));
}

static gdouble
scr_to_data_x(GtkWidget *widget, gint scr)
{
    GwyGraphArea *area;
    GwyGraphModel *model;
    gdouble xmin, xmax;
    gint w;
    gboolean lg;

    area = GWY_GRAPH_AREA(widget);
    model = area->graph_model;
    w = widget->allocation.width;
    xmin = area->x_min;
    xmax = area->x_max;

    scr = CLAMP(scr, 0, w-1);
    g_object_get(model, "x-logarithmic", &lg, NULL);
    if (!lg)
        return xmin + scr*(xmax - xmin)/(w-1);
    return exp(log(xmin) + scr*log(xmax/xmin)/(w-1));
}

static gint
data_to_scr_x(GtkWidget *widget, gdouble data)
{
    GwyGraphArea *area;
    GwyGraphModel *model;
    gdouble xmin, xmax;
    gint w;
    gboolean lg;

    area = GWY_GRAPH_AREA(widget);
    model = area->graph_model;
    w = widget->allocation.width;
    xmin = area->x_min;
    xmax = area->x_max;

    g_object_get(model, "x-logarithmic", &lg, NULL);
    if (!lg)
        return (data - xmin)/(xmax - xmin)*(w-1);
    return log(data/xmin)/log(xmax/xmin)*(w-1);
}

static gdouble
scr_to_data_y(GtkWidget *widget, gint scr)
{
    GwyGraphArea *area;
    GwyGraphModel *model;
    gdouble ymin, ymax;
    gint h;
    gboolean lg;

    area = GWY_GRAPH_AREA(widget);
    model = area->graph_model;
    h = widget->allocation.height;
    ymin = area->y_min;
    ymax = area->y_max;

    scr = CLAMP(scr, 0, h-1);
    g_object_get(model, "y-logarithmic", &lg, NULL);
    if (!lg)
        return ymin + (h - scr)*(ymax - ymin)/(h-1);
    return exp(log(ymin) + (h - scr)*log(ymax/ymin)/(h-1));
}

static gint
data_to_scr_y(GtkWidget *widget, gdouble data)
{
    GwyGraphArea *area;
    GwyGraphModel *model;
    gdouble ymin, ymax;
    gint h;
    gboolean lg;

    area = GWY_GRAPH_AREA(widget);
    model = area->graph_model;
    h = widget->allocation.height;
    ymin = area->y_min;
    ymax = area->y_max;

    g_object_get(model, "y-logarithmic", &lg, NULL);
    if (!lg)
        return h - (data - ymin)/(ymax - ymin)*(h-1);
    return h - log(data/ymin)/log(ymax/ymin)*(h-1);
}

static void
label_geometry_changed(GtkWidget *area, GtkAllocation *label_allocation)
{
    gwy_graph_area_repos_label(GWY_GRAPH_AREA(area),
                               &(area->allocation), label_allocation);
}

static void
gwy_graph_area_response(GwyGraphAreaDialog *dialog,
                        gint response_id,
                        GwyGraphArea *area)
{
    if (response_id == GTK_RESPONSE_CLOSE)
        gtk_widget_hide(GTK_WIDGET(dialog));

    if ((response_id == GWY_GRAPH_AREA_DIALOG_RESPONSE_PREV
         || response_id == GWY_GRAPH_AREA_DIALOG_RESPONSE_NEXT)
        && area->graph_model && dialog->curve_model) {
        gint i, n;

        n = gwy_graph_model_get_n_curves(area->graph_model);
        i = gwy_graph_model_get_curve_index(area->graph_model,
                                            dialog->curve_model);
        if (response_id == GWY_GRAPH_AREA_DIALOG_RESPONSE_NEXT && i+1 < n)
            gwy_graph_area_edit_curve(area, i+1);
        else if (response_id == GWY_GRAPH_AREA_DIALOG_RESPONSE_PREV && i > 0)
            gwy_graph_area_edit_curve(area, i-1);
        /* Switching to non-existent curves should not be requested, but just
         * shrug when it happens. */
    }
}

static void
gwy_graph_label_response(GwyGraphLabelDialog *dialog,
                         gint response_id,
                         G_GNUC_UNUSED gpointer user_data)
{
    if (response_id == GTK_RESPONSE_CLOSE)
        gtk_widget_hide(GTK_WIDGET(dialog));
}

/**
 * gwy_graph_area_enable_user_input:
 * @area: A graph area.
 * @enable: %TRUE to enable user interaction, %FALSE to disable it.
 *
 * Enables/disables auxiliary graph area dialogs (invoked by clicking the
 * mouse).
 *
 * Note, however, that this setting does not control editability of selections.
 * Use gwy_graph_area_set_selection_editable() for that.
 **/
void
gwy_graph_area_enable_user_input(GwyGraphArea *area, gboolean enable)
{
    g_return_if_fail(GWY_IS_GRAPH_AREA(area));
    area->enable_user_input = enable;
    gwy_graph_label_enable_user_input(area->lab, enable);
}

/**
 * gwy_graph_area_set_selection_editable:
 * @area: A graph area.
 * @setting: %TRUE to enable selection editing, %FALSE to disable it.
 *
 * Enables/disables selection editing using mouse.
 *
 * When selection editing is disabled the graph area status type determines
 * the selection type that can be drawn on the area.  However, the user cannot
 * modify it.
 *
 * Since: 2.45
 **/
void
gwy_graph_area_set_selection_editable(GwyGraphArea *area, gboolean setting)
{
    g_return_if_fail(GWY_IS_GRAPH_AREA(area));
    area->selection_is_editable = setting;
}

/**
 * gwy_graph_area_get_cursor:
 * @area: A graph area.
 * @x_cursor: Location to store the x value corresponding to cursor position.
 * @y_cursor: Location to store the y value corresponding to cursor position.
 *
 * Gets mouse cursor related values within a graph area.
 **/
void
gwy_graph_area_get_cursor(GwyGraphArea *area,
                          gdouble *x_cursor, gdouble *y_cursor)
{
    g_return_if_fail(GWY_IS_GRAPH_AREA(area));
    if (area->mouse_present) {
        *x_cursor = area->actual_cursor.x;
        *y_cursor = area->actual_cursor.y;

    }
    else {
        *x_cursor = 0;
        *y_cursor = 0;
    }
}

static gboolean
gwy_graph_area_leave_notify(GtkWidget *widget,
                            G_GNUC_UNUSED GdkEventCrossing *event)
{
    GwyGraphArea *area = GWY_GRAPH_AREA(widget);

    area->mouse_present = FALSE;

    return FALSE;
}

/**
 * gwy_graph_area_get_label:
 * @area: A graph area.
 *
 * Gets the label inside a graph area.
 *
 * Returns: The graph label widget within the graph area.
 **/
GtkWidget*
gwy_graph_area_get_label(GwyGraphArea *area)
{
    g_return_val_if_fail(GWY_IS_GRAPH_AREA(area), NULL);
    return GTK_WIDGET(area->lab);
}

/**
 * gwy_graph_area_set_x_range:
 * @area: A graph area.
 * @x_min: The minimum x value, in real coodrinates.
 * @x_max: The maximum x value, in real coodrinates.
 *
 * Sets the horizontal range a graph area displays.
 **/
void
gwy_graph_area_set_x_range(GwyGraphArea *area,
                           gdouble x_min,
                           gdouble x_max)
{
    g_return_if_fail(GWY_IS_GRAPH_AREA(area));

    gwy_debug("%p: %g, %g", area, x_min, x_max);
    if (x_min != area->x_min || x_max != area->x_max) {
        area->x_min = x_min;
        area->x_max = x_max;
        if (GTK_WIDGET_DRAWABLE(area))
            gtk_widget_queue_draw(GTK_WIDGET(area));
    }
}

/**
 * gwy_graph_area_set_y_range:
 * @area: A graph area.
 * @y_min: The minimum y value, in real coodrinates.
 * @y_max: The maximum y value, in real coodrinates.
 *
 * Sets the vertical range a graph area displays.
 **/
void
gwy_graph_area_set_y_range(GwyGraphArea *area,
                           gdouble y_min,
                           gdouble y_max)
{
    g_return_if_fail(GWY_IS_GRAPH_AREA(area));

    gwy_debug("%p: %g, %g", area, y_min, y_max);
    if (y_min != area->y_min || y_max != area->y_max) {
        area->y_min = y_min;
        area->y_max = y_max;
        if (GTK_WIDGET_DRAWABLE(area))
            gtk_widget_queue_draw(GTK_WIDGET(area));
    }
}

/**
 * gwy_graph_area_set_x_grid_data:
 * @area: A graph area.
 * @ndata: The number of points in @grid_data.
 * @grid_data: Array of grid line positions on the x-axis (in real values,
 *             not pixels).
 *
 * Sets the grid data on the x-axis of a graph area
 **/
void
gwy_graph_area_set_x_grid_data(GwyGraphArea *area,
                               guint ndata,
                               const gdouble *grid_data)
{
    g_return_if_fail(GWY_IS_GRAPH_AREA(area));

    g_array_set_size(area->x_grid_data, 0);
    g_array_append_vals(area->x_grid_data, grid_data, ndata);

    if (GTK_WIDGET_DRAWABLE(area))
        gtk_widget_queue_draw(GTK_WIDGET(area));
}

/**
 * gwy_graph_area_set_y_grid_data:
 * @area: A graph area.
 * @ndata: The number of points in @grid_data.
 * @grid_data: Array of grid line positions on the y-axis (in real values,
 *             not pixels).
 *
 * Sets the grid data on the y-axis of a graph area
 **/
void
gwy_graph_area_set_y_grid_data(GwyGraphArea *area,
                               guint ndata,
                               const gdouble *grid_data)
{
    g_return_if_fail(GWY_IS_GRAPH_AREA(area));

    g_array_set_size(area->y_grid_data, 0);
    g_array_append_vals(area->y_grid_data, grid_data, ndata);

    if (GTK_WIDGET_DRAWABLE(area))
        gtk_widget_queue_draw(GTK_WIDGET(area));
}

/**
 * gwy_graph_area_get_x_grid_data:
 * @area: A graph area.
 * @ndata: Location to store the number of returned positions.
 *
 * Gets the grid data on the x-axis of a graph area.
 *
 * Returns: Array of grid line positions (in real values, not pixels) owned
 *          by the graph area.
 **/
const gdouble*
gwy_graph_area_get_x_grid_data(GwyGraphArea *area,
                               guint *ndata)
{
    g_return_val_if_fail(GWY_IS_GRAPH_AREA(area), NULL);

    if (ndata)
        *ndata = area->x_grid_data->len;
    return (const gdouble*)area->x_grid_data->data;
}

/**
 * gwy_graph_area_get_y_grid_data:
 * @area: A graph area.
 * @ndata: Location to store the number of returned positions.
 *
 * Gets the grid data on the y-axis of a graph area.
 *
 * Returns: Array of grid line positions (in real values, not pixels) owned
 *          by the graph area.
 **/
const gdouble*
gwy_graph_area_get_y_grid_data(GwyGraphArea *area,
                               guint *ndata)
{
    g_return_val_if_fail(GWY_IS_GRAPH_AREA(area), NULL);

    if (ndata)
        *ndata = area->y_grid_data->len;
    return (const gdouble*)area->y_grid_data->data;
}


/**
 * gwy_graph_area_get_selection:
 * @area: A graph area.
 * @status_type: Graph status.  Value %GWY_GRAPH_STATUS_PLAIN mode (which has
 *               no selection associated) stands for the currentl selection
 *               mode.
 *
 * Gets the selection object corresponding to a status of a graph area.
 *
 * A selection object exists even for inactive status types (selection modes),
 * therefore also selections for other modes than the currently active one can
 * be requested.
 *
 * Returns: The requested selection.  It is %NULL only if @status_type is
 *          %GWY_GRAPH_STATUS_PLAIN and the current selection mode is
 *          %GWY_GRAPH_STATUS_PLAIN.
 **/
GwySelection*
gwy_graph_area_get_selection(GwyGraphArea *area,
                             GwyGraphStatusType status_type)
{
    g_return_val_if_fail(GWY_IS_GRAPH_AREA(area), NULL);

    if (status_type == GWY_GRAPH_STATUS_PLAIN)
        status_type = area->status;

    switch (status_type) {
        case GWY_GRAPH_STATUS_PLAIN:
        return NULL;

        case GWY_GRAPH_STATUS_XSEL:
        return area->xseldata;

        case GWY_GRAPH_STATUS_YSEL:
        return area->yseldata;

        case GWY_GRAPH_STATUS_POINTS:
        return area->pointsdata;

        case GWY_GRAPH_STATUS_ZOOM:
        return area->zoomdata;

        case GWY_GRAPH_STATUS_XLINES:
        return area->xlinesdata;

        case GWY_GRAPH_STATUS_YLINES:
        return area->ylinesdata;
    }

    g_return_val_if_reached(NULL);
}

static void
selection_changed(GwyGraphArea *area)
{
    gtk_widget_queue_draw(GTK_WIDGET(area));
}

/**
 * gwy_graph_area_set_status:
 * @area: A graph area.
 * @status_type: New graph area status.
 *
 * Sets the status of a graph area.
 *
 * When the area is inside a #GwyGraph, use gwy_graph_set_status() instead
 * (also see this function for details).
 **/
void
gwy_graph_area_set_status(GwyGraphArea *area, GwyGraphStatusType status_type)
{
    g_return_if_fail(GWY_IS_GRAPH_AREA(area));

    if (status_type == area->status)
        return;

    area->status = status_type;
    if (GTK_WIDGET_DRAWABLE(area))
        gtk_widget_queue_draw(GTK_WIDGET(area));
    g_object_notify(G_OBJECT(area), "status");
}

/**
 * gwy_graph_area_get_status:
 * @area: A graph area.
 *
 * Gets the status of a grap area.
 *
 * See gwy_graph_area_set_status().
 *
 * Returns: The current graph area status.
 **/
GwyGraphStatusType
gwy_graph_area_get_status(GwyGraphArea *area)
{
    g_return_val_if_fail(GWY_IS_GRAPH_AREA(area), 0);

    return area->status;
}

static void
gwy_graph_area_edit_curve_real(GwyGraphArea *area,
                               gint id)
{
    GwyGraphCurveModel *cmodel;
    gint n;

    if (id < 0) {
        if (area->area_dialog)
            gtk_widget_hide(area->area_dialog);
        return;
    }

    if (!area->area_dialog) {
        area->area_dialog = _gwy_graph_area_dialog_new();
        g_signal_connect(area->area_dialog, "response",
                         G_CALLBACK(gwy_graph_area_response), area);
    }
    g_return_if_fail(area->graph_model);
    n = gwy_graph_model_get_n_curves(area->graph_model);
    cmodel = gwy_graph_model_get_curve(area->graph_model, id);
    g_return_if_fail(cmodel);
    _gwy_graph_area_dialog_set_curve_data(area->area_dialog, cmodel);
    _gwy_graph_area_dialog_set_switching(area->area_dialog, id > 0, id < n-1);
    gtk_widget_show_all(area->area_dialog);
    gtk_window_present(GTK_WINDOW(area->area_dialog));
}

/**
 * gwy_graph_area_edit_curve:
 * @area: A graph area.
 * @id: The index of the curve to edit properties of.
 *
 * Invokes the curve property dialog for a curve.
 *
 * If the dialog is already displayed, it is switched to the requested curve.
 *
 * Since: 2.5
 **/
void
gwy_graph_area_edit_curve(GwyGraphArea *area,
                          gint id)
{
    g_return_if_fail(GWY_IS_GRAPH_AREA(area));
    g_signal_emit(area, graph_area_signals[EDIT_CURVE], 0, id);
}

/**
 * gwy_graph_area_export_vector:
 * @area: A graph area.
 * @x: Bounding box origin X-coordinate.
 * @y: Bounding box origin Y-coordinate.
 * @width: Bounding box width.
 * @height: Bounding box height.
 *
 * Creates PostScript representation of a graph area.
 *
 * Returns: A fragment of PostScript code representing the the graph area
 *          as a newly allocated #GString.
 **/
GString*
gwy_graph_area_export_vector(GwyGraphArea *area,
                             gint x, gint y,
                             gint width, gint height)
{
    static const gchar *symbols[] = {
        "Box",
        "Cross",
        "Circle",
        "Star",
        "Times",
        "TriU",
        "TriD",
        "Dia",
    };

    gint i, j, nc;
    GwyGraphCurveModel *curvemodel;
    GwyGraphModel *model;
    GString *out;
    gdouble xmult, ymult;
    GwyRGBA *color;
    gint pointsize;
    gint linesize;
    GwyGraphPointType pointtype;

    g_return_val_if_fail(GWY_IS_GRAPH_AREA(area), NULL);

    out = g_string_new("%%Area\n");

    model = area->graph_model;
    if ((area->x_max - area->x_min) == 0 || (area->y_max - area->y_min) == 0) {
        g_warning("Graph null range.\n");
        return out;
    }

    xmult = width/(area->x_max - area->x_min);
    ymult = height/(area->y_max - area->y_min);

    g_string_append_printf(out, "/box {\n"
                           "newpath\n"
                           "%d %d M\n"
                           "%d %d L\n"
                           "%d %d L\n"
                           "%d %d L\n"
                           "closepath\n"
                           "} def\n",
                           x, y,
                           x + width, y,
                           x + width, y + height,
                           x, y + height);

    g_string_append_printf(out, "gsave\n");
    g_string_append_printf(out, "box\n");
    g_string_append_printf(out, "clip\n");

    /*plot grid*/
    /*
    g_string_append_printf(out, "%d setlinewidth\n", 1);
    for (i = 0; i < area->x_grid_data->len; i++) {
        pvalue = &g_array_index(area->x_grid_data, gdouble, i);
        pos = (gint)((*pvalue)*ymult) + y;
        g_string_append_printf(out, "%d %d M\n", x, height - pos);
        g_string_append_printf(out, "%d %d L\n", x + width, height - pos);
        g_string_append_printf(out, "stroke\n");
    }

    for (i = 0; i < area->y_grid_data->len; i++) {
        pvalue = &g_array_index(area->y_grid_data, gdouble, i);
        pos = (gint)((*pvalue)*xmult) + x;
        g_string_append_printf(out, "%d %d M\n", pos, y);
        g_string_append_printf(out, "%d %d L\n", pos, y + height);
        g_string_append_printf(out, "stroke\n");
    }
    */



    nc = gwy_graph_model_get_n_curves(model);
    for (i = 0; i < nc; i++) {
        curvemodel = gwy_graph_model_get_curve(model, i);
        g_object_get(curvemodel,
                     "point-size", &pointsize,
                     "line-width", &linesize,
                     "point-type", &pointtype,
                     "color", &color,
                     NULL);
        /* FIXME */
        if (pointtype >= G_N_ELEMENTS(symbols)) {
            g_warning("Don't know how to draw point type #%u", pointtype);
            pointtype = 0;
        }
        g_string_append_printf(out, "/hpt %d def\n", pointsize);
        g_string_append_printf(out, "/vpt %d def\n", pointsize);
        g_string_append_printf(out, "/hpt2 hpt 2 mul def\n");
        g_string_append_printf(out, "/vpt2 vpt 2 mul def\n");
        g_string_append_printf(out, "%d setlinewidth\n", linesize);
        g_string_append_printf(out, "%f %f %f setrgbcolor\n",
                               color->r, color->g, color->b);
        gwy_rgba_free(color);

        for (j = 0; j < curvemodel->n - 1; j++) {
            if (curvemodel->mode == GWY_GRAPH_CURVE_LINE
                || curvemodel->mode == GWY_GRAPH_CURVE_LINE_POINTS)
            {
                if (j == 0)
                    g_string_append_printf(out, "%d %d M\n",
                                           (gint)(x + (curvemodel->xdata[j]
                                                       - area->x_min)*xmult),
                                           (gint)(y + (curvemodel->ydata[j]
                                                       - area->y_min)*ymult));
                else {
                    g_string_append_printf(out, "%d %d M\n",
                                           (gint)(x + (curvemodel->xdata[j-1]
                                                       - area->x_min)*xmult),
                                           (gint)(y + (curvemodel->ydata[j-1]
                                                       - area->y_min)*ymult));
                    g_string_append_printf(out, "%d %d L\n",
                                           (gint)(x + (curvemodel->xdata[j]
                                                       - area->x_min)*xmult),
                                           (gint)(y + (curvemodel->ydata[j]
                                                       - area->y_min)*ymult));
                }
            }
            if (curvemodel->mode == GWY_GRAPH_CURVE_POINTS
                || curvemodel->mode == GWY_GRAPH_CURVE_LINE_POINTS) {
                g_string_append_printf(out, "%d %d %s\n",
                                       (gint)(x + (curvemodel->xdata[j]
                                                   - area->x_min)*xmult),
                                       (gint)(y + (curvemodel->ydata[j]
                                                   - area->y_min)*ymult),
                                       symbols[curvemodel->point_type]);

            }
        }
        g_string_append_printf(out, "stroke\n");
    }
    g_string_append_printf(out, "grestore\n");

    /*plot boundary*/
    g_string_append_printf(out, "%d setlinewidth\n", 2);
    g_string_append_printf(out, "%d %d M\n", x, y);
    g_string_append_printf(out, "%d %d L\n", x + width, y);
    g_string_append_printf(out, "%d %d L\n", x + width, y + height);
    g_string_append_printf(out, "%d %d L\n", x, y + height);
    g_string_append_printf(out, "%d %d L\n", x, y);
    g_string_append_printf(out, "stroke\n");

    return out;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwygrapharea
 * @title: GwyGraphArea
 * @short_description: Layout for drawing graph curves
 *
 * #GwyGraphArea is the central part of #GwyGraph widget. It plots a set of
 * data curves with the given plot properties.
 *
 * It is recommended to use it within #GwyGraph, however, it can also be used
 * separately.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
