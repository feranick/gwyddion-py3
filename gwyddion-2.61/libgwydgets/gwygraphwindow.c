/*
 *  $Id: gwygraphwindow.c 24710 2022-03-21 17:35:20Z yeti-dn $
 *  Copyright (C) 2004-2016 David Necas (Yeti), Petr Klapetek.
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
#include <gdk/gdkkeysyms.h>

#include <libprocess/datafield.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwydgets/gwygraphwindow.h>
#include <libgwydgets/gwygraphcurves.h>
#include <libgwydgets/gwygraphdata.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwystatusbar.h>
#include "gwygraphwindowmeasuredialog.h"

#define ZOOM_FACTOR 1.3195

enum {
    DEFAULT_WIDTH = 550,
    DEFAULT_HEIGHT = 390,
};

static void     gwy_graph_window_destroy         (GtkObject *object);
static void     gwy_graph_window_finalize        (GObject *object);
static void     gwy_graph_window_model_changed   (GwyGraphWindow *graph_window);
static gboolean gwy_graph_window_key_pressed     (GtkWidget *widget,
                                                  GdkEventKey *event);
static gboolean gwy_graph_cursor_motion          (GwyGraphWindow *graphwindow);
static void     gwy_graph_window_measure         (GwyGraphWindow *graphwindow);
static void     gwy_graph_window_zoom_in         (GwyGraphWindow *graphwindow);
static void     gwy_graph_window_zoom_to_fit     (GwyGraphWindow *graphwindow);
static void     gwy_graph_window_x_log           (GwyGraphWindow *graphwindow);
static void     gwy_graph_window_y_log           (GwyGraphWindow *graphwindow);
static void     gwy_graph_window_zoom_finished   (GwyGraphWindow *graphwindow);
static void     gwy_graph_window_resize          (GwyGraphWindow *graphwindow,
                                                  gint zoomtype);
static void     gwy_graph_window_measure_finished(GwyGraphWindow *graphwindow,
                                                  gint response);
static void     graph_title_changed              (GwyGraphWindow *graphwindow);
static void   gwy_graph_window_curves_row_activated(GwyGraphWindow *graphwindow,
                                                    GtkTreePath *path,
                                                    GtkTreeViewColumn *column);

/* These are actually class data.  To put them to Class struct someone would
 * have to do class_ref() and live with this reference to the end of time. */
static GtkTooltips *tooltips = NULL;
static gboolean tooltips_set = FALSE;

G_DEFINE_TYPE(GwyGraphWindow, gwy_graph_window, GTK_TYPE_WINDOW)

static void
gwy_graph_window_class_init(GwyGraphWindowClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    GtkObjectClass *object_class = GTK_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_graph_window_finalize;

    object_class->destroy = gwy_graph_window_destroy;

    widget_class->key_press_event = gwy_graph_window_key_pressed;
}

static void
gwy_graph_window_init(G_GNUC_UNUSED GwyGraphWindow *graphwindow)
{
    if (!tooltips_set && !tooltips) {
        tooltips = gtk_tooltips_new();
        g_object_ref(tooltips);
        gtk_object_sink(GTK_OBJECT(tooltips));
    }
}

static void
gwy_graph_window_finalize(GObject *object)
{
    gtk_widget_destroy(GTK_WIDGET(GWY_GRAPH_WINDOW(object)->measure_dialog));
    G_OBJECT_CLASS(gwy_graph_window_parent_class)->finalize(object);
}

static void
gwy_graph_window_destroy(GtkObject *object)
{
    GwyGraphWindow *graph_window = GWY_GRAPH_WINDOW(object);

    if (graph_window->data) {
        GwyGraphData *graph_data = GWY_GRAPH_DATA(graph_window->data);
        GwyGraphModel *gmodel = gwy_graph_data_get_model(graph_data);
        if (gmodel)
            g_signal_handlers_disconnect_by_func(gmodel,
                                                 graph_title_changed,
                                                 graph_window);
        graph_window->data = NULL;
    }

    GTK_OBJECT_CLASS(gwy_graph_window_parent_class)->destroy(object);
}

/**
 * gwy_graph_window_new:
 * @graph: A GwyGraph object containing the graph.
 *
 * Creates a new window showing @graph.
 *
 * Returns: A newly created graph window as #GtkWidget.
 **/
GtkWidget*
gwy_graph_window_new(GwyGraph *graph)
{
    GwyGraphWindow *graphwindow;
    GwyGraphArea *area;
    GwySelection *selection;
    GtkScrolledWindow *swindow;
    GtkWidget *vbox, *hbox;

    gwy_debug("");
    g_return_val_if_fail(GWY_IS_GRAPH(graph), NULL);

    graphwindow = (GwyGraphWindow*)g_object_new(GWY_TYPE_GRAPH_WINDOW, NULL);
    gtk_window_set_wmclass(GTK_WINDOW(graphwindow), "data",
                           g_get_application_name());
    gtk_window_set_default_size(GTK_WINDOW(graphwindow),
                                DEFAULT_WIDTH, DEFAULT_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(graphwindow), TRUE);

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(GTK_WINDOW(graphwindow)), vbox);

    graphwindow->graph = GTK_WIDGET(graph);
    graphwindow->last_status = gwy_graph_get_status(graph);

    /* Add notebook with graph and text matrix */
    graphwindow->notebook = gtk_notebook_new();

    graph_title_changed(graphwindow);

    gtk_notebook_append_page(GTK_NOTEBOOK(graphwindow->notebook),
                             GTK_WIDGET(graph),
                             gtk_label_new(_("Graph")));

    swindow = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
    graphwindow->data = gwy_graph_data_new(NULL);
    gtk_container_add(GTK_CONTAINER(swindow), graphwindow->data);
    gtk_notebook_append_page(GTK_NOTEBOOK(graphwindow->notebook),
                             GTK_WIDGET(swindow),
                             gtk_label_new(_("Data")));

    swindow = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
    gtk_scrolled_window_set_policy(swindow,
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    graphwindow->curves = gwy_graph_curves_new(NULL);
    g_signal_connect_swapped(graphwindow->curves, "row-activated",
                             G_CALLBACK(gwy_graph_window_curves_row_activated),
                             graphwindow);

    gtk_container_add(GTK_CONTAINER(swindow), graphwindow->curves);
    gtk_notebook_append_page(GTK_NOTEBOOK(graphwindow->notebook),
                             GTK_WIDGET(swindow),
                             gtk_label_new(_("Curves")));

    gtk_container_add(GTK_CONTAINER(vbox), graphwindow->notebook);

    /* Add buttons */
    hbox = gtk_hbox_new(FALSE, 0);

    graphwindow->button_measure_points = gtk_toggle_button_new();
    gtk_container_add(GTK_CONTAINER(graphwindow->button_measure_points),
                      gtk_image_new_from_stock(GWY_STOCK_GRAPH_MEASURE,
                                               GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_box_pack_start(GTK_BOX(hbox), graphwindow->button_measure_points,
                       FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(graphwindow->button_measure_points,
                                 _("Measure distances in graph"));
    g_signal_connect_swapped(graphwindow->button_measure_points, "clicked",
                             G_CALLBACK(gwy_graph_window_measure),
                             graphwindow);

    graphwindow->button_zoom_in = gtk_toggle_button_new();
    gtk_container_add(GTK_CONTAINER(graphwindow->button_zoom_in),
                      gtk_image_new_from_stock(GWY_STOCK_GRAPH_ZOOM_IN,
                                               GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_box_pack_start(GTK_BOX(hbox), graphwindow->button_zoom_in,
                       FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(graphwindow->button_zoom_in,
                                 _("Zoom in by mouse selection"));
    g_signal_connect_swapped(graphwindow->button_zoom_in, "toggled",
                             G_CALLBACK(gwy_graph_window_zoom_in),
                             graphwindow);

    graphwindow->button_zoom_to_fit = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(graphwindow->button_zoom_to_fit),
                      gtk_image_new_from_stock(GWY_STOCK_GRAPH_ZOOM_FIT,
                                               GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_box_pack_start(GTK_BOX(hbox), graphwindow->button_zoom_to_fit,
                       FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(graphwindow->button_zoom_to_fit,
                                 _("Zoom out to full curve"));
    g_signal_connect_swapped(graphwindow->button_zoom_to_fit, "clicked",
                             G_CALLBACK(gwy_graph_window_zoom_to_fit),
                             graphwindow);

    graphwindow->button_x_log = gtk_toggle_button_new();
    gtk_container_add(GTK_CONTAINER(graphwindow->button_x_log),
                      gtk_image_new_from_stock(GWY_STOCK_LOGSCALE_HORIZONTAL,
                                               GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_box_pack_start(GTK_BOX(hbox), graphwindow->button_x_log,
                       FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(graphwindow->button_x_log,
                                 _("Toggle logarithmic x axis"));
    g_signal_connect_swapped(graphwindow->button_x_log, "clicked",
                             G_CALLBACK(gwy_graph_window_x_log),
                             graphwindow);

    graphwindow->button_y_log = gtk_toggle_button_new();
    gtk_container_add(GTK_CONTAINER(graphwindow->button_y_log),
                      gtk_image_new_from_stock(GWY_STOCK_LOGSCALE_VERTICAL,
                                               GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_box_pack_start(GTK_BOX(hbox), graphwindow->button_y_log,
                       FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(graphwindow->button_y_log,
                                 _("Toggle logarithmic y axis"));
    g_signal_connect_swapped(graphwindow->button_y_log, "clicked",
                             G_CALLBACK(gwy_graph_window_y_log),
                             graphwindow);

    graphwindow->statusbar = gwy_statusbar_new();
    gtk_widget_set_name(graphwindow->statusbar, "gwyflatstatusbar");
    gtk_box_pack_end(GTK_BOX(hbox), graphwindow->statusbar, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    graphwindow->measure_dialog = _gwy_graph_window_measure_dialog_new(graph);
    g_signal_connect_swapped(graphwindow->measure_dialog, "response",
                             G_CALLBACK(gwy_graph_window_measure_finished),
                             graphwindow);

    g_signal_connect_swapped(gwy_graph_get_area(graph), "motion-notify-event",
                             G_CALLBACK(gwy_graph_cursor_motion), graphwindow);

    area = GWY_GRAPH_AREA(gwy_graph_get_area(graph));
    selection = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_ZOOM);
    g_signal_connect_swapped(selection, "finished",
                             G_CALLBACK(gwy_graph_window_zoom_finished),
                             graphwindow);

    g_signal_connect_swapped(graph, "notify::model",
                             G_CALLBACK(gwy_graph_window_model_changed),
                             graphwindow);
    gwy_graph_window_model_changed(graphwindow);

    return GTK_WIDGET(graphwindow);
}

/**
 * gwy_graph_window_get_graph:
 * @graphwindow: A graph window.
 *
 * Gets the graph widget a graph window currently shows.
 *
 * Returns: The currently shown #GwyGraph.
 **/
GtkWidget*
gwy_graph_window_get_graph(GwyGraphWindow *graphwindow)
{
    g_return_val_if_fail(GWY_IS_GRAPH_WINDOW(graphwindow), NULL);

    return graphwindow->graph;
}

/**
 * gwy_graph_window_get_graph_data:
 * @graphwindow: A graph window.
 *
 * Gets the graph data widget of a graph window.
 *
 * Returns: The #GwyGraphData widget of this graph window.  Its model and
 *          column layout must be considered private.
 *
 * Since: 2.5
 **/
GtkWidget*
gwy_graph_window_get_graph_data(GwyGraphWindow *graphwindow)
{
    g_return_val_if_fail(GWY_IS_GRAPH_WINDOW(graphwindow), NULL);

    return graphwindow->data;
}

/**
 * gwy_graph_window_get_graph_curves:
 * @graphwindow: A graph window.
 *
 * Gets the graph curves widget of a graph window.
 *
 * Returns: The #GwyGraphCurves widget of this graph window.  Its model and
 *          column layout must be considered private.
 *
 * Since: 2.5
 **/
GtkWidget*
gwy_graph_window_get_graph_curves(GwyGraphWindow *graphwindow)
{
    g_return_val_if_fail(GWY_IS_GRAPH_WINDOW(graphwindow), NULL);

    return graphwindow->curves;
}

/**
 * gwy_graph_window_class_set_tooltips:
 * @tips: #GtkTooltips object #GwyGraphWindow should use for setting tooltips.
 *        A %NULL value disables tooltips altogether.
 *
 * Sets the tooltips object to use for adding tooltips to graph window parts.
 *
 * This function does not do anything useful.  Do not use it.
 *
 * This is a class method.  It affects only newly created graph windows,
 * existing graph windows will continue to use the tooltips they were
 * constructed with.
 *
 * If no class tooltips object is set before first #GwyGraphWindow is created,
 * the class instantiates one on its own.  You can normally obtain it with
 * gwy_graph_window_class_get_tooltips() then.  The class takes a reference on
 * the tooltips in either case.
 **/
void
gwy_graph_window_class_set_tooltips(GtkTooltips *tips)
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
 * gwy_graph_window_class_get_tooltips:
 *
 * Gets the tooltips object used for adding tooltips to Graph window parts.
 *
 * This function does not do anything useful.  Do not use it.
 *
 * Returns: The #GtkTooltips object. (Do not free).
 **/
GtkTooltips*
gwy_graph_window_class_get_tooltips(void)
{
    return tooltips;
}

static void
gwy_graph_window_update_log_button(GwyGraphWindow *graph_window,
                                   GtkWidget *button,
                                   GwyGraphModel *gmodel,
                                   const gchar *prop_name,
                                   gpointer callback)
{
    gboolean logscale = FALSE;
    guint id;

    id = g_signal_handler_find(button,
                               G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                               0, 0, NULL, callback, graph_window);
    if (gmodel)
        g_object_get(gmodel, prop_name, &logscale, NULL);
    g_signal_handler_block(button, id);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), logscale);
    g_signal_handler_unblock(button, id);
}

static void
gwy_graph_window_model_changed(GwyGraphWindow *graph_window)
{
    GwyGraphModel *gmodel, *oldmodel;

    /* Retrieve the old model. FIXME: This is dirty. */
    oldmodel = gwy_graph_data_get_model(GWY_GRAPH_DATA(graph_window->data));

    if (oldmodel)
        g_signal_handlers_disconnect_by_func(oldmodel,
                                             graph_title_changed, graph_window);

    gmodel = gwy_graph_get_model(GWY_GRAPH(graph_window->graph));
    gwy_graph_data_set_model(GWY_GRAPH_DATA(graph_window->data), gmodel);
    gwy_graph_curves_set_model(GWY_GRAPH_CURVES(graph_window->curves), gmodel);

    gwy_graph_window_update_log_button(graph_window, graph_window->button_x_log,
                                       gmodel, "x-logarithmic",
                                       gwy_graph_window_x_log);
    gwy_graph_window_update_log_button(graph_window, graph_window->button_y_log,
                                       gmodel, "y-logarithmic",
                                       gwy_graph_window_y_log);

    if (gmodel)
        g_signal_connect_swapped(gmodel, "notify::title",
                                 G_CALLBACK(graph_title_changed), graph_window);

    graph_title_changed(graph_window);
}

static void
gwy_graph_window_copy_to_clipboard(GwyGraphWindow *graph_window)
{
    GtkClipboard *clipboard;
    GdkDisplay *display;
    GdkPixbuf *pixbuf;
    GdkAtom atom;

    display = gtk_widget_get_display(GTK_WIDGET(graph_window));
    atom = gdk_atom_intern("CLIPBOARD", FALSE);
    clipboard = gtk_clipboard_get_for_display(display, atom);
    pixbuf = gwy_graph_export_pixmap(GWY_GRAPH(graph_window->graph),
                                     FALSE, TRUE, TRUE);
    gtk_clipboard_set_image(clipboard, pixbuf);
    g_object_unref(pixbuf);
}

static gboolean
gwy_graph_window_key_pressed(GtkWidget *widget,
                             GdkEventKey *event)
{
    enum {
        important_mods = GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_RELEASE_MASK
    };
    GwyGraphWindow *graph_window;
    gboolean (*method)(GtkWidget*, GdkEventKey*);
    guint state, key;

    gwy_debug("state = %u, keyval = %u", event->state, event->keyval);
    graph_window = GWY_GRAPH_WINDOW(widget);
    state = event->state & important_mods;
    key = event->keyval;
    if (!state && (key == GDK_minus || key == GDK_KP_Subtract)) {
        gwy_graph_window_resize(graph_window, -1);
        return TRUE;
    }
    else if (!state && (key == GDK_equal || key == GDK_KP_Equal
                        || key == GDK_plus || key == GDK_KP_Add)) {
        gwy_graph_window_resize(graph_window, 1);
        return TRUE;
    }
    else if (!state && (key == GDK_Z || key == GDK_z || key == GDK_KP_Divide)) {
        gwy_graph_window_resize(graph_window, 0);
        return TRUE;
    }
    else if (state == GDK_CONTROL_MASK && (key == GDK_C || key == GDK_c)) {
        gwy_graph_window_copy_to_clipboard(graph_window);
        return TRUE;
    }

    method = GTK_WIDGET_CLASS(gwy_graph_window_parent_class)->key_press_event;
    return method ? method(widget, event) : FALSE;
}

static gboolean
gwy_graph_cursor_motion(GwyGraphWindow *graphwindow)
{
    GwyGraph *graph;
    GwyAxis *axis;
    gdouble x, y;
    GString *str;
    gchar *p;

    str = g_string_new(NULL);
    graph = GWY_GRAPH(graphwindow->graph);
    gwy_graph_area_get_cursor(GWY_GRAPH_AREA(gwy_graph_get_area(graph)),
                              &x, &y);

    axis = gwy_graph_get_axis(graph, GTK_POS_TOP);
    if (gwy_axis_is_logarithmic(axis)) {
        GwySIUnit *unit = axis->unit;
        if (unit) {
            gchar *u = gwy_si_unit_get_string(unit,
                                              GWY_SI_UNIT_FORMAT_VFMARKUP);
            g_string_append_printf(str, "%.5g %s", x, u);
            g_free(u);
        }
        else
            g_string_append_printf(str, "%.5g", x);
    }
    else {
        g_string_append_printf(str, "%.4f %s",
                               x/gwy_axis_get_magnification(axis),
                               gwy_axis_get_magnification_string(axis));
    }

    g_string_append(str, ", ");

    axis = gwy_graph_get_axis(graph, GTK_POS_LEFT);
    if (gwy_axis_is_logarithmic(axis)) {
        GwySIUnit *unit = axis->unit;
        if (unit) {
            gchar *u = gwy_si_unit_get_string(unit,
                                              GWY_SI_UNIT_FORMAT_VFMARKUP);
            g_string_append_printf(str, "%.5g %s", y, u);
            g_free(u);
        }
        else
            g_string_append_printf(str, "%.5g", y);
    }
    else {
        g_string_append_printf(str, "%.4f %s",
                               y/gwy_axis_get_magnification(axis),
                               gwy_axis_get_magnification_string(axis));
    }

    while ((p = strstr(str->str, "e+"))) {
        guint i = p - str->str;
        g_string_erase(str, i, 2);
        g_string_insert(str, i, "×10<sup>");
        i += strlen("×10<sup>");
        while (str->str[i] == '0' && g_ascii_isdigit(str->str[i+1]))
            g_string_erase(str, i, 1);
        while (g_ascii_isdigit(str->str[i]))
            i++;
        g_string_insert(str, i, "</sup>");
    }

    while ((p = strstr(str->str, "e-"))) {
        guint i = p - str->str;
        g_string_erase(str, i, 2);
        g_string_insert(str, i, "×10<sup>-");
        i += strlen("×10<sup>-");
        while (str->str[i] == '0' && g_ascii_isdigit(str->str[i+1]))
            g_string_erase(str, i, 1);
        while (g_ascii_isdigit(str->str[i]))
            i++;
        g_string_insert(str, i, "</sup>");
    }

    gwy_statusbar_set_markup(GWY_STATUSBAR(graphwindow->statusbar), str->str);
    g_string_free(str, TRUE);

    return FALSE;
}

static void
gwy_graph_window_measure(GwyGraphWindow *graphwindow)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(graphwindow->button_measure_points))) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(graphwindow->button_zoom_in), FALSE);
        gwy_graph_set_status(GWY_GRAPH(graphwindow->graph),
                             GWY_GRAPH_STATUS_XLINES);
        gtk_widget_queue_draw(graphwindow->graph);
        gtk_widget_show_all(graphwindow->measure_dialog);
    }
    else {
        gwy_graph_window_measure_finished(graphwindow, 0);
    }
}


static void
gwy_graph_window_measure_finished(GwyGraphWindow *graphwindow, gint response)
{
    GwyGraphArea *area;

    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(graphwindow->graph)));

    if (response == GWY_GRAPH_WINDOW_MEASURE_RESPONSE_CLEAR) {
        GwyGraphStatusType status;
        status = gwy_graph_area_get_status(area);
        gwy_selection_clear(gwy_graph_area_get_selection(area, status));
        return;
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(graphwindow->button_measure_points), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(graphwindow->button_zoom_in), FALSE);
    gwy_graph_set_status(GWY_GRAPH(graphwindow->graph), GWY_GRAPH_STATUS_PLAIN);

    gtk_widget_queue_draw(graphwindow->graph);
    gtk_widget_hide(graphwindow->measure_dialog);
}

static void
gwy_graph_window_zoom_in(GwyGraphWindow *graphwindow)
{
    GwyGraph *graph;

    graph = GWY_GRAPH(graphwindow->graph);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(graphwindow->button_zoom_in))) {
        graphwindow->last_status = gwy_graph_get_status(graph);
        gwy_graph_set_status(graph, GWY_GRAPH_STATUS_ZOOM);
    }
    else
        gwy_graph_set_status(graph, graphwindow->last_status);
}

static void
gwy_graph_window_zoom_to_fit(GwyGraphWindow *graphwindow)
{
    GwyGraph *graph;
    GwyGraphModel *model;

    graph = GWY_GRAPH(graphwindow->graph);
    model = gwy_graph_get_model(graph);
    g_object_set(model,
                 "x-min-set", FALSE, "x-max-set", FALSE,
                 "y-min-set", FALSE, "y-max-set", FALSE,
                 NULL);
}

static void
gwy_graph_window_zoom_finished(GwyGraphWindow *graphwindow)
{
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(graphwindow->button_zoom_in),
                                 FALSE);
    gwy_graph_set_status(GWY_GRAPH(graphwindow->graph),
                         graphwindow->last_status);
}

static void
gwy_graph_window_resize(GwyGraphWindow *graphwindow, gint zoomtype)
{
    GtkWindow *window = GTK_WINDOW(graphwindow);
    GtkWidget *widget = GTK_WIDGET(graphwindow);
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
gwy_graph_window_x_log(GwyGraphWindow *graphwindow)
{
    GwyGraphModel *model;
    gboolean state;

    model = gwy_graph_get_model(GWY_GRAPH(graphwindow->graph));
    g_object_get(model, "x-logarithmic", &state, NULL);
    g_object_set(model, "x-logarithmic", !state, NULL);
}

static void
gwy_graph_window_y_log(GwyGraphWindow *graphwindow)
{
    GwyGraphModel *model;
    gboolean state;

    model = gwy_graph_get_model(GWY_GRAPH(graphwindow->graph));
    g_object_get(model, "y-logarithmic", &state, NULL);
    g_object_set(model, "y-logarithmic", !state, NULL);
}

static void
graph_title_changed(GwyGraphWindow *graphwindow)
{
    GwyGraphModel *gmodel;
    gchar *title;

    gmodel = gwy_graph_get_model(GWY_GRAPH(graphwindow->graph));
    g_object_get(gmodel, "title", &title, NULL);

    /* FIXME: Can it be NULL? */
    if (title)
        gtk_window_set_title(GTK_WINDOW(graphwindow), title);
    else
        gtk_window_set_title(GTK_WINDOW(graphwindow), _("Untitled"));

    g_free(title);
}

static void
gwy_graph_window_curves_row_activated(GwyGraphWindow *graphwindow,
                                      GtkTreePath *path,
                                      G_GNUC_UNUSED GtkTreeViewColumn *column)
{
    GwyGraphArea *area;
    const gint *indices;

    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(graphwindow->graph)));
    indices = gtk_tree_path_get_indices(path);
    gwy_graph_area_edit_curve(area, indices[0]);
}

/************************** Documentation ****************************/

/**
 * SECTION:gwygraphwindow
 * @title: GwyGraphWindow
 * @short_description: Graph display window
 *
 * #GwyGraphWindow encapsulates a #GwyGraph together with other controls and
 * graph data view.
 * You can create a graph window for a graph with gwy_graph_window_new().
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
