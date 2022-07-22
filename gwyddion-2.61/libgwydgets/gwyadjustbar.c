/*
 *  $Id: gwyadjustbar.c 23814 2021-06-08 13:17:39Z yeti-dn $
 *  Copyright (C) 2012-2017 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gdk/gdkkeysyms.h>
#include <glib-object.h>

#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libprocess/datafield.h>
#include <libdraw/gwyrgba.h>
#include <libgwydgets/gwydgettypes.h>
#include <libgwydgets/gwyadjustbar.h>

enum {
    PROP_0,
    PROP_ADJUSTMENT,
    PROP_SNAP_TO_TICKS,
    PROP_MAPPING,
    PROP_HAS_CHECK_BUTTON,
    N_PROPS,
};

enum {
    SGNL_CHANGE_VALUE,
    N_SIGNALS
};

typedef gdouble (*MappingFunc)(gdouble value);

struct _GwyAdjustBarPrivate {
    GdkWindow *input_window;
    GdkCursor *cursor_move;

    GtkAdjustment *adjustment;
    gulong adjustment_value_changed_id;
    gulong adjustment_changed_id;
    gdouble oldvalue; /* This is to avoid acting on no-change notifications. */
    gboolean snap_to_ticks;
    gboolean adjustment_ok;
    gboolean dragging;

    GtkWidget *check;
    gboolean bar_sensitive;

    GwyScaleMappingType mapping;
    MappingFunc map_value;
    MappingFunc map_position;
    gint x;
    gint length;
    gdouble a;
    gdouble b;
};

typedef struct _GwyAdjustBarPrivate AdjustBar;

static void     gwy_adjust_bar_dispose       (GObject *object);
static void     gwy_adjust_bar_finalize      (GObject *object);
static void     gwy_adjust_bar_set_property  (GObject *object,
                                              guint prop_id,
                                              const GValue *value,
                                              GParamSpec *pspec);
static void     gwy_adjust_bar_get_property  (GObject *object,
                                              guint prop_id,
                                              GValue *value,
                                              GParamSpec *pspec);
static void     gwy_adjust_bar_forall        (GtkContainer *container,
                                              gboolean include_internals,
                                              GtkCallback callback,
                                              gpointer cbdata);
static void     gwy_adjust_bar_realize       (GtkWidget *widget);
static void     gwy_adjust_bar_unrealize     (GtkWidget *widget);
static void     gwy_adjust_bar_map           (GtkWidget *widget);
static void     gwy_adjust_bar_unmap         (GtkWidget *widget);
static void     gwy_adjust_bar_size_request  (GtkWidget *widget,
                                              GtkRequisition *requisition);
static void     gwy_adjust_bar_size_allocate (GtkWidget *widget,
                                              GtkAllocation *allocation);
static gboolean gwy_adjust_bar_expose        (GtkWidget *widget,
                                              GdkEventExpose *expose);
static gboolean gwy_adjust_bar_enter_notify  (GtkWidget *widget,
                                              GdkEventCrossing *event);
static gboolean gwy_adjust_bar_leave_notify  (GtkWidget *widget,
                                              GdkEventCrossing *event);
static gboolean gwy_adjust_bar_scroll        (GtkWidget *widget,
                                              GdkEventScroll *event);
static gboolean gwy_adjust_bar_button_press  (GtkWidget *widget,
                                              GdkEventButton *event);
static gboolean gwy_adjust_bar_button_release(GtkWidget *widget,
                                              GdkEventButton *event);
static gboolean gwy_adjust_bar_motion_notify (GtkWidget *widget,
                                              GdkEventMotion *event);
static GType    gwy_adjust_bar_child_type    (GtkContainer *container);
static void     gwy_adjust_bar_change_value  (GwyAdjustBar *adjbar,
                                              gdouble newvalue);
static gboolean set_adjustment               (GwyAdjustBar *adjbar,
                                              GtkAdjustment *adjustment);
static gboolean set_snap_to_ticks            (GwyAdjustBar *adjbar,
                                              gboolean setting);
static gboolean set_mapping                  (GwyAdjustBar *adjbar,
                                              GwyScaleMappingType mapping);
static void     create_input_window          (GwyAdjustBar *adjbar);
static void     destroy_input_window         (GwyAdjustBar *adjbar);
static void     draw_bar                     (GwyAdjustBar *adjbar,
                                              cairo_t *cr);
static void     adjustment_changed           (GwyAdjustBar *adjbar,
                                              GtkAdjustment *adjustment);
static void     adjustment_value_changed     (GwyAdjustBar *adjbar,
                                              GtkAdjustment *adjustment);
static void     update_mapping               (GwyAdjustBar *adjbar);
static gdouble  map_value_to_position        (GwyAdjustBar *adjbar,
                                              gdouble value);
static gdouble  map_position_to_value        (GwyAdjustBar *adjbar,
                                              gdouble position);
static gdouble  map_both_linear              (gdouble value);
static void     change_value                 (GtkWidget *widget,
                                              gdouble newposition);
static void     ensure_cursors               (GwyAdjustBar *adjbar);
static void     discard_cursors              (GwyAdjustBar *adjbar);
static gdouble  snap_value                   (GwyAdjustBar *adjbar,
                                              gdouble value);

static const GtkBorder default_border = { 4, 4, 3, 3 };

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_TYPE(GwyAdjustBar, gwy_adjust_bar, GTK_TYPE_BIN);

static void
gwy_adjust_bar_class_init(GwyAdjustBarClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    GtkContainerClass *container_class = GTK_CONTAINER_CLASS(klass);
    guint i;

    g_type_class_add_private(klass, sizeof(AdjustBar));

    gobject_class->dispose = gwy_adjust_bar_dispose;
    gobject_class->finalize = gwy_adjust_bar_finalize;
    gobject_class->get_property = gwy_adjust_bar_get_property;
    gobject_class->set_property = gwy_adjust_bar_set_property;

    widget_class->realize = gwy_adjust_bar_realize;
    widget_class->unrealize = gwy_adjust_bar_unrealize;
    widget_class->map = gwy_adjust_bar_map;
    widget_class->unmap = gwy_adjust_bar_unmap;
    widget_class->size_request = gwy_adjust_bar_size_request;
    widget_class->size_allocate = gwy_adjust_bar_size_allocate;
    widget_class->expose_event = gwy_adjust_bar_expose;
    widget_class->enter_notify_event = gwy_adjust_bar_enter_notify;
    widget_class->leave_notify_event = gwy_adjust_bar_leave_notify;
    widget_class->scroll_event = gwy_adjust_bar_scroll;
    widget_class->button_press_event = gwy_adjust_bar_button_press;
    widget_class->button_release_event = gwy_adjust_bar_button_release;
    widget_class->motion_notify_event = gwy_adjust_bar_motion_notify;

    container_class->forall = gwy_adjust_bar_forall;
    container_class->child_type = gwy_adjust_bar_child_type;

    klass->change_value = gwy_adjust_bar_change_value;

    properties[PROP_ADJUSTMENT]
        = g_param_spec_object("adjustment",
                              "Adjustment",
                              "Adjustment representing the value.",
                              GTK_TYPE_ADJUSTMENT,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SNAP_TO_TICKS]
        = g_param_spec_boolean("snap-to-ticks",
                               "Snap to ticks",
                               "Whether only values that are multiples of step "
                               "size are allowed.",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAPPING]
        = g_param_spec_enum("mapping",
                            "Mapping",
                            "Mapping function between values and screen "
                            "positions.",
                            GWY_TYPE_SCALE_MAPPING_TYPE,
                            GWY_SCALE_MAPPING_SQRT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HAS_CHECK_BUTTON]
        = g_param_spec_boolean("has-check-button",
                               "Has check button",
                               "Whether the adjust bar has a check button.",
                               FALSE,
                               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    for (i = 1; i < N_PROPS; i++)
        g_object_class_install_property(gobject_class, i, properties[i]);

    /**
     * GwyAdjustBar::change-value:
     * @gwyadjustbar: The #GwyAdjustBar which received the signal.
     * @arg1: New value for @gwyadjustbar.
     *
     * The ::change-value signal is emitted when the user interactively
     * changes the value.
     *
     * It is an action signal.
     **/
    signals[SGNL_CHANGE_VALUE]
        = g_signal_new("change-value",
                       G_OBJECT_CLASS_TYPE(klass),
                       G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                       G_STRUCT_OFFSET(GwyAdjustBarClass, change_value),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__DOUBLE,
                       G_TYPE_NONE, 1, G_TYPE_DOUBLE);
}

static void
gwy_adjust_bar_init(GwyAdjustBar *adjbar)
{
    GtkWidget *label;
    AdjustBar *priv;

    priv = adjbar->priv = G_TYPE_INSTANCE_GET_PRIVATE(adjbar,
                                                      GWY_TYPE_ADJUST_BAR,
                                                      AdjustBar);
    priv->mapping = GWY_SCALE_MAPPING_SQRT;
    priv->bar_sensitive = TRUE;
    GTK_WIDGET_SET_FLAGS(adjbar, GTK_NO_WINDOW);
    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_container_add(GTK_CONTAINER(adjbar), label);
}

static void
gwy_adjust_bar_finalize(GObject *object)
{
    G_OBJECT_CLASS(gwy_adjust_bar_parent_class)->finalize(object);
}

static void
gwy_adjust_bar_dispose(GObject *object)
{
    GwyAdjustBar *adjbar = GWY_ADJUST_BAR(object);

    set_adjustment(adjbar, NULL);
    G_OBJECT_CLASS(gwy_adjust_bar_parent_class)->dispose(object);
}

static void
gwy_adjust_bar_set_property(GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
    GwyAdjustBar *adjbar = GWY_ADJUST_BAR(object);

    switch (prop_id) {
        case PROP_ADJUSTMENT:
        set_adjustment(adjbar, g_value_get_object(value));
        break;

        case PROP_SNAP_TO_TICKS:
        set_snap_to_ticks(adjbar, g_value_get_boolean(value));
        break;

        case PROP_MAPPING:
        set_mapping(adjbar, g_value_get_enum(value));
        break;

        case PROP_HAS_CHECK_BUTTON:
        gwy_adjust_bar_set_has_check_button(adjbar, g_value_get_boolean(value));
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_adjust_bar_get_property(GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
    AdjustBar *priv = GWY_ADJUST_BAR(object)->priv;

    switch (prop_id) {
        case PROP_ADJUSTMENT:
        g_value_set_object(value, priv->adjustment);
        break;

        case PROP_SNAP_TO_TICKS:
        g_value_set_boolean(value, priv->snap_to_ticks);
        break;

        case PROP_MAPPING:
        g_value_set_enum(value, priv->mapping);
        break;

        case PROP_HAS_CHECK_BUTTON:
        g_value_set_boolean(value, !!priv->check);
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_adjust_bar_forall(GtkContainer *container, gboolean include_internals,
                      GtkCallback callback, gpointer cbdata)
{
    if (include_internals) {
        AdjustBar *priv = GWY_ADJUST_BAR(container)->priv;
        if (priv->check)
            (*callback)(priv->check, cbdata);

    }
    GTK_CONTAINER_CLASS(gwy_adjust_bar_parent_class)->forall(container,
                                                             include_internals,
                                                             callback, cbdata);
}

static void
gwy_adjust_bar_realize(GtkWidget *widget)
{
    GwyAdjustBar *adjbar = GWY_ADJUST_BAR(widget);

    GTK_WIDGET_CLASS(gwy_adjust_bar_parent_class)->realize(widget);
    create_input_window(adjbar);
}

static void
gwy_adjust_bar_unrealize(GtkWidget *widget)
{
    GwyAdjustBar *adjbar = GWY_ADJUST_BAR(widget);
    AdjustBar *priv = adjbar->priv;

    discard_cursors(adjbar);
    destroy_input_window(adjbar);
    priv->adjustment_ok = FALSE;
    GTK_WIDGET_CLASS(gwy_adjust_bar_parent_class)->unrealize(widget);
}

static void
gwy_adjust_bar_map(GtkWidget *widget)
{
    GwyAdjustBar *adjbar = GWY_ADJUST_BAR(widget);
    AdjustBar *priv = adjbar->priv;
    GtkWidget *check = priv->check;

    GTK_WIDGET_CLASS(gwy_adjust_bar_parent_class)->map(widget);
    if (priv->input_window)
        gdk_window_show(priv->input_window);
    if (check
        && GTK_WIDGET_VISIBLE(check)
        && gtk_widget_get_child_visible(check)
        && !GTK_WIDGET_MAPPED(check))
        gtk_widget_map(check);
}

static void
gwy_adjust_bar_unmap(GtkWidget *widget)
{
    GwyAdjustBar *adjbar = GWY_ADJUST_BAR(widget);
    AdjustBar *priv = adjbar->priv;
    GtkWidget *check = priv->check;

    if (check)
        gtk_widget_unmap(check);
    if (priv->input_window)
        gdk_window_hide(priv->input_window);
    GTK_WIDGET_CLASS(gwy_adjust_bar_parent_class)->unmap(widget);
}

static void
gwy_adjust_bar_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    AdjustBar *priv = GWY_ADJUST_BAR(widget)->priv;
    GtkWidget *label;
    GtkRequisition child_req;
    gint border_width, spacing;

    requisition->width = default_border.left + default_border.right;
    requisition->height = default_border.top + default_border.bottom;

    border_width = gtk_container_get_border_width(GTK_CONTAINER(widget));
    requisition->width += 2*border_width;
    requisition->height += 2*border_width;

    label = GTK_BIN(widget)->child;
    if (label && GTK_WIDGET_VISIBLE(label)) {
        gtk_widget_size_request(label, &child_req);
        requisition->width += child_req.width;
        requisition->height += child_req.height;
    }

    if (priv->check) {
        gtk_widget_size_request(priv->check, &child_req);
        requisition->width += child_req.width;
        requisition->height = MAX(requisition->height, child_req.height);

        /* Reproduce checkbutton's indicator spacing internally.  It puts two
         * spacings between the check and label.  So use that instead of
         * our default border. */
        gtk_widget_style_get(priv->check, "indicator-spacing", &spacing, NULL);
        requisition->width += 2*spacing;
        requisition->width -= default_border.left;
    }
}

static void
gwy_adjust_bar_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    AdjustBar *priv = GWY_ADJUST_BAR(widget)->priv;
    GtkWidget *label;
    GtkAllocation child_allocation;
    GtkRequisition child_req;
    gint border_width, spacing, width, h;

    border_width = gtk_container_get_border_width(GTK_CONTAINER(widget));
    widget->allocation = *allocation;

    priv->x = 0;
    width = allocation->width;
    if (priv->check) {
        gtk_widget_get_child_requisition(priv->check, &child_req);
        gtk_widget_style_get(priv->check, "indicator-spacing", &spacing, NULL);
        priv->x = child_req.width + 2*spacing - default_border.left;
        priv->x = MAX(priv->x, 0);
        width = MAX(width - priv->x, 1);

        child_allocation.x = allocation->x;
        child_allocation.y = allocation->y + (allocation->height
                                              - child_req.height)/2;
        child_allocation.width = child_req.width;
        child_allocation.height = child_req.height;
        gtk_widget_size_allocate(priv->check, &child_allocation);
    }

    gwy_debug("ALLOCATION %dx%d at (%d,%d)",
              allocation->width, allocation->height,
              allocation->x, allocation->y);
    if (priv->input_window) {
        gwy_debug("INPUT WINDOW %dx%d at (%d,%d)",
                  width, allocation->height,
                  allocation->x + priv->x, allocation->y);
        gdk_window_move_resize(priv->input_window,
                               allocation->x + priv->x, allocation->y,
                               width, allocation->height);
    }

    label = GTK_BIN(widget)->child;
    if (label && GTK_WIDGET_VISIBLE(label)) {
        gtk_widget_get_child_requisition(label, &child_req);
        h = child_req.height + (2*border_width
                                + default_border.top + default_border.bottom);
        child_allocation.x = (allocation->x + priv->x
                              + border_width + default_border.left);
        child_allocation.y = (allocation->y
                              + MAX(allocation->height - h, 0)/2
                              + border_width + default_border.top);
        child_allocation.width = (width - 2*border_width
                                  - default_border.left - default_border.right);
        child_allocation.height = (allocation->height - 2*border_width
                                   - default_border.top - default_border.bottom);
        gtk_widget_size_allocate(label, &child_allocation);
    }

    update_mapping(GWY_ADJUST_BAR(widget));
}

static gboolean
gwy_adjust_bar_expose(GtkWidget *widget, GdkEventExpose *event)
{
    if (GTK_WIDGET_DRAWABLE(widget)) {
        GwyAdjustBar *adjbar = GWY_ADJUST_BAR(widget);
        cairo_t *cr;

        cr = gdk_cairo_create(gtk_widget_get_parent_window(widget));
#ifdef DEBUG
        {
            cairo_set_source_rgba(cr, 0.1, 0.9, 0.2, 1.0);
            cairo_rectangle(cr,
                            widget->allocation.x, widget->allocation.y,
                            widget->allocation.width, widget->allocation.height);
            cairo_fill(cr);
        }
#endif
        cairo_translate(cr,
                        widget->allocation.x + adjbar->priv->x,
                        widget->allocation.y);
        draw_bar(adjbar, cr);
        cairo_destroy(cr);
    }
    GTK_WIDGET_CLASS(gwy_adjust_bar_parent_class)->expose_event(widget, event);

    return FALSE;
}

static gboolean
gwy_adjust_bar_enter_notify(GtkWidget *widget,
                            G_GNUC_UNUSED GdkEventCrossing *event)
{
    AdjustBar *priv = GWY_ADJUST_BAR(widget)->priv;
    GtkStateType state = GTK_WIDGET_STATE(widget);

    if (!priv->bar_sensitive)
        return FALSE;
    ensure_cursors(GWY_ADJUST_BAR(widget));
    if (!(state & GTK_STATE_PRELIGHT))
        gtk_widget_set_state(widget, state | GTK_STATE_PRELIGHT);
    return FALSE;
}

static gboolean
gwy_adjust_bar_leave_notify(GtkWidget *widget,
                            G_GNUC_UNUSED GdkEventCrossing *event)
{
    AdjustBar *priv = GWY_ADJUST_BAR(widget)->priv;
    GtkStateType state = GTK_WIDGET_STATE(widget);

    if (!priv->bar_sensitive || priv->dragging)
        return FALSE;
    if (state & GTK_STATE_PRELIGHT)
        gtk_widget_set_state(widget, state & ~GTK_STATE_PRELIGHT);
    return FALSE;
}

static gboolean
gwy_adjust_bar_scroll(GtkWidget *widget, GdkEventScroll *event)
{
    GdkScrollDirection dir = event->direction;
    GwyAdjustBar *adjbar = GWY_ADJUST_BAR(widget);
    AdjustBar *priv = adjbar->priv;
    gdouble value, position, newposition, sposition;

    if (!priv->adjustment_ok)
        return TRUE;

    value = gtk_adjustment_get_value(priv->adjustment);
    position = map_value_to_position(adjbar, value);
    newposition = position;
    if (dir == GDK_SCROLL_UP || dir == GDK_SCROLL_RIGHT) {
        newposition += 1.0;
        if (priv->snap_to_ticks) {
            value += priv->adjustment->step_increment;
            value = fmin(value, priv->adjustment->upper);
            sposition = map_value_to_position(adjbar, value);
            newposition = fmax(newposition, sposition);
        }
    }
    else {
        newposition -= 1.0;
        if (priv->snap_to_ticks) {
            value -= priv->adjustment->step_increment;
            value = fmax(value, priv->adjustment->lower);
            sposition = map_value_to_position(adjbar, value);
            newposition = fmin(newposition, sposition);
        }
    }

    newposition = CLAMP(newposition, 0.0, priv->length);
    if (newposition != position)
        change_value(widget, newposition);
    return TRUE;
}

static gboolean
gwy_adjust_bar_button_press(GtkWidget *widget, GdkEventButton *event)
{
    AdjustBar *priv = GWY_ADJUST_BAR(widget)->priv;
    GtkWidget *child;

    if (!priv->bar_sensitive || event->button != 1)
        return FALSE;
    priv->dragging = TRUE;
    change_value(widget, event->x);
    child = GTK_BIN(widget)->child;
    if (child) {
        if (gtk_label_get_mnemonic_keyval(GTK_LABEL(child)) != GDK_VoidSymbol)
            gtk_widget_mnemonic_activate(child, FALSE);
        /* XXX: If the adjbar label does not have mnemonic we do not know
         * which widget we would like to give focus to. */
    }
    return TRUE;
}

static gboolean
gwy_adjust_bar_button_release(GtkWidget *widget, GdkEventButton *event)
{
    AdjustBar *priv = GWY_ADJUST_BAR(widget)->priv;

    if (!priv->bar_sensitive || event->button != 1)
        return FALSE;
    change_value(widget, event->x);
    priv->dragging = FALSE;
    return TRUE;
}

static gboolean
gwy_adjust_bar_motion_notify(GtkWidget *widget, GdkEventMotion *event)
{
    AdjustBar *priv = GWY_ADJUST_BAR(widget)->priv;

    if (!priv->bar_sensitive || !(event->state & GDK_BUTTON1_MASK))
        return FALSE;
    change_value(widget, event->x);
    return TRUE;
}

static GType
gwy_adjust_bar_child_type(GtkContainer *container)
{
    GtkWidget *child = GTK_BIN(container)->child;

    if (!child)
        return GTK_TYPE_LABEL;
    else
        return G_TYPE_NONE;
}

static void
gwy_adjust_bar_change_value(GwyAdjustBar *adjbar, gdouble newvalue)
{
    AdjustBar *priv = adjbar->priv;
    gdouble value;

    g_return_if_fail(priv->adjustment);
    if (!priv->adjustment_ok)
        return;

    value = gtk_adjustment_get_value(priv->adjustment);
    newvalue = snap_value(adjbar, newvalue);
    if (fabs(newvalue - value) <= 1e-12*fmax(fabs(newvalue), fabs(value)))
        return;

    gtk_adjustment_set_value(priv->adjustment, newvalue);
}

/**
 * gwy_adjust_bar_new:
 * @adjustment: The adjustment the adjust bar should use, or %NULL.
 * @label: Text of the adjustment bar label, or %NULL.
 *
 * Creates a new adjustment bar.
 *
 * The label text, if any, is set with mnemonic enabled.  However, you still
 * need to assign it to a widget (presumably a #GtkSpinButton) using
 * gtk_label_set_mnemonic_widget().
 *
 * Returns: A new adjustment bar.
 *
 * Since: 2.49
 **/
GtkWidget*
gwy_adjust_bar_new(GtkAdjustment *adjustment, const gchar *label)
{
    GwyAdjustBar *adjbar;

    if (adjustment) {
        adjbar = g_object_new(GWY_TYPE_ADJUST_BAR,
                              "adjustment", adjustment,
                              NULL);
    }
    else
        adjbar = g_object_new(GWY_TYPE_ADJUST_BAR, NULL);

    if (label) {
        GtkWidget *child = GTK_BIN(adjbar)->child;
        gtk_label_set_text_with_mnemonic(GTK_LABEL(child), label);
    }

    return GTK_WIDGET(adjbar);
}

/**
 * gwy_adjust_bar_set_adjustment:
 * @adjbar: An adjustment bar.
 * @adjustment: Adjustment to use for the value.
 *
 * Sets the adjustment that an adjustment bar visualises.
 *
 * Since: 2.49
 **/
void
gwy_adjust_bar_set_adjustment(GwyAdjustBar *adjbar,
                              GtkAdjustment *adjustment)
{
    g_return_if_fail(GWY_IS_ADJUST_BAR(adjbar));
    g_return_if_fail(GTK_IS_ADJUSTMENT(adjustment));
    if (!set_adjustment(adjbar, adjustment))
        return;

    g_object_notify(G_OBJECT(adjbar), "adjustment");
}

/**
 * gwy_adjust_bar_get_adjustment:
 * @adjbar: An adjustment bar.
 *
 * Obtains the adjustment that an adjustment bar visualises.
 *
 * Returns: The adjustment used by @adjbar.  If no adjustment was set
 *          and the default one is used, function returns %NULL.
 *
 * Since: 2.49
 **/
GtkAdjustment*
gwy_adjust_bar_get_adjustment(GwyAdjustBar *adjbar)
{
    g_return_val_if_fail(GWY_IS_ADJUST_BAR(adjbar), NULL);
    return adjbar->priv->adjustment;
}

/**
 * gwy_adjust_bar_set_snap_to_ticks:
 * @adjbar: An adjustment bar.
 * @setting: %TRUE to restrict values to multiples of step size,
 *           %FALSE to permit any values.
 *
 * Sets the snapping behaviour of an adjustment bar.
 *
 * Note the ‘multiples of step size’ condition in fact applies to the
 * difference from the minimum value.  The maximum adjustment value is always
 * permissible, even if it does not satisfy this condition.  Values modified by
 * the user (i.e.  emission of signal "change-value") are snapped, however,
 * values set explicitly gtk_adjustment_set_value() are kept intact.
 *
 * Setting this option to %TRUE immediately causes an adjustment value change
 * if it does not satisfy the condition.
 *
 * It is usually a poor idea to enable snapping for non-linear mappings.
 *
 * Since: 2.49
 **/
void
gwy_adjust_bar_set_snap_to_ticks(GwyAdjustBar *adjbar,
                                 gboolean setting)
{
    g_return_if_fail(GWY_IS_ADJUST_BAR(adjbar));
    if (!set_snap_to_ticks(adjbar, setting))
        return;

    g_object_notify(G_OBJECT(adjbar), "snap-to-ticks");
}

/**
 * gwy_adjust_bar_get_snap_to_ticks:
 * @adjbar: An adjustment bar.
 *
 * Sets the snapping behaviour of an adjustment bar.
 *
 * Returns: %TRUE if values are restricted to multiples of step size.
 *
 * Since: 2.49
 **/
gboolean
gwy_adjust_bar_get_snap_to_ticks(GwyAdjustBar *adjbar)
{
    g_return_val_if_fail(GWY_IS_ADJUST_BAR(adjbar), FALSE);
    return !!adjbar->priv->snap_to_ticks;
}

/**
 * gwy_adjust_bar_set_mapping:
 * @adjbar: An adjustment bar.
 * @mapping: Mapping function type between values and screen positions in the
 *           adjustment bar.
 *
 * Sets the mapping function type for an adjustment bar.
 *
 * Since: 2.49
 **/
void
gwy_adjust_bar_set_mapping(GwyAdjustBar *adjbar,
                           GwyScaleMappingType mapping)
{
    g_return_if_fail(GWY_IS_ADJUST_BAR(adjbar));
    if (!set_mapping(adjbar, mapping))
        return;

    g_object_notify(G_OBJECT(adjbar), "mapping");
}

/**
 * gwy_adjust_bar_get_mapping:
 * @adjbar: An adjustment bar.
 *
 * Gets the mapping function type of an adjustment bar.
 *
 * Returns: The type of mapping function between values and screen positions.
 *
 * Since: 2.49
 **/
GwyScaleMappingType
gwy_adjust_bar_get_mapping(GwyAdjustBar *adjbar)
{
    g_return_val_if_fail(GWY_IS_ADJUST_BAR(adjbar), GWY_SCALE_MAPPING_LINEAR);
    return adjbar->priv->mapping;
}

/**
 * gwy_adjust_bar_set_has_check_button:
 * @adjbar: An adjustment bar.
 * @setting: %TRUE to enable a check button; %FALSE to disable it.
 *
 * Sets whether an adjustment bar has a check button.
 *
 * Since: 2.49
 **/
void
gwy_adjust_bar_set_has_check_button(GwyAdjustBar *adjbar, gboolean setting)
{
    AdjustBar *priv;

    g_return_if_fail(GWY_IS_ADJUST_BAR(adjbar));
    priv = adjbar->priv;
    if (!setting == !priv->check)
        return;

    if (setting) {
        priv->check = gtk_check_button_new();
        gtk_widget_set_parent(priv->check, GTK_WIDGET(adjbar));
        gtk_widget_set_name(priv->check, "gwyadjbarcheck");
        /* FIXME: Should do even if we are hidden? */
        gtk_widget_show(priv->check);
    }
    else {
        gtk_widget_destroy(priv->check);
        priv->check = NULL;
    }

    if (GTK_WIDGET_VISIBLE(GTK_WIDGET(adjbar)))
        gtk_widget_queue_resize(GTK_WIDGET(adjbar));

    g_object_notify(G_OBJECT(adjbar), "has-check-button");
}

/**
 * gwy_adjust_bar_get_has_check_button:
 * @adjbar: An adjustment bar.
 *
 * Reports whether an adjustment bar has a check button.
 *
 * Returns: %TRUE if the adjustment bar has a check button.
 *
 * Since: 2.49
 **/
gboolean
gwy_adjust_bar_get_has_check_button(GwyAdjustBar *adjbar)
{
    g_return_val_if_fail(GWY_IS_ADJUST_BAR(adjbar), FALSE);
    return !!adjbar->priv->check;
}

/**
 * gwy_adjust_bar_get_label:
 * @adjbar: An adjustment bar.
 *
 * Gets the label widget inside an adjustment bar.
 *
 * Use gtk_label_set_mnemonic_widget() to set the mnemonic widget for the label
 * or change the label text.
 *
 * Returns: The label widget.
 *
 * Since: 2.49
 **/
GtkWidget*
gwy_adjust_bar_get_label(GwyAdjustBar *adjbar)
{
    g_return_val_if_fail(GWY_IS_ADJUST_BAR(adjbar), NULL);
    return GTK_BIN(adjbar)->child;
}

/**
 * gwy_adjust_bar_get_check_button:
 * @adjbar: An adjustment bar.
 *
 * Gets the check button of an adjustment bar.
 *
 * Connect to the "toggled" signal of the check button.  Modifying it is not
 * recommended.
 *
 * Returns: The check button widget, or %NULL if there is none.
 *
 * Since: 2.49
 **/
GtkWidget*
gwy_adjust_bar_get_check_button(GwyAdjustBar *adjbar)
{
    g_return_val_if_fail(GWY_IS_ADJUST_BAR(adjbar), NULL);
    return adjbar->priv->check;
}

/**
 * gwy_adjust_bar_set_bar_sensitive:
 * @adjbar: An adjustment bar.
 * @sensitive: %TRUE to make the widget's bar sensitive.
 *
 * Sets the sensitivity of an adjustment bar.
 *
 * The bar's sensitivity can be controlled separately.  This is useful when
 * @adjbar has a check button because otherwise the bar is the entire widget
 * and the function is not different from gtk_widget_set_sensitive().
 * However, if you want to enable and disable the adjustment bar via the check
 * button, use this function instead of gtk_widget_set_sensitive() which would
 * make insensitive also the check button.
 *
 * Since: 2.49
 **/
void
gwy_adjust_bar_set_bar_sensitive(GwyAdjustBar *adjbar, gboolean sensitive)
{
    GtkWidget *widget;
    AdjustBar *priv;

    g_return_if_fail(GWY_IS_ADJUST_BAR(adjbar));
    priv = adjbar->priv;
    if (!priv->bar_sensitive == !sensitive)
        return;

    priv->bar_sensitive = sensitive;
    if (sensitive) {
        gtk_widget_set_sensitive(GTK_BIN(adjbar)->child, TRUE);
        if (priv->input_window)
            ensure_cursors(adjbar);
    }
    else {
        if (priv->input_window) {
            gdk_window_set_cursor(priv->input_window, NULL);
            discard_cursors(adjbar);
        }
        gtk_widget_set_sensitive(GTK_BIN(adjbar)->child, FALSE);
    }

    widget = GTK_WIDGET(adjbar);
    if (GTK_WIDGET_DRAWABLE(widget))
        gtk_widget_queue_draw(widget);
}

/**
 * gwy_adjust_bar_get_bar_sensitive:
 * @adjbar: An adjustment bar.
 *
 * Reports whether an adjustment bar is sensitive.
 *
 * See gwy_adjust_bar_set_bar_sensitive() for discussion.
 *
 * Returns: %TRUE if the widget's bar is sensitive.
 *
 * Since: 2.49
 **/
gboolean
gwy_adjust_bar_get_bar_sensitive(GwyAdjustBar *adjbar)
{
    g_return_val_if_fail(GWY_IS_ADJUST_BAR(adjbar), FALSE);
    return adjbar->priv->bar_sensitive;
}

static gboolean
set_adjustment(GwyAdjustBar *adjbar,
               GtkAdjustment *adjustment)
{
    AdjustBar *priv = adjbar->priv;
    if (!gwy_set_member_object(adjbar, adjustment, GTK_TYPE_ADJUSTMENT,
                               &priv->adjustment,
                               "changed", &adjustment_changed,
                               &priv->adjustment_changed_id,
                               G_CONNECT_SWAPPED,
                               "value-changed", &adjustment_value_changed,
                               &priv->adjustment_value_changed_id,
                               G_CONNECT_SWAPPED,
                               NULL))
        return FALSE;

    priv->oldvalue = adjustment ? gtk_adjustment_get_value(adjustment) : 0.0;
    update_mapping(adjbar);
    gtk_widget_queue_draw(GTK_WIDGET(adjbar));
    return TRUE;
}

static gboolean
set_snap_to_ticks(GwyAdjustBar *adjbar,
                  gboolean setting)
{
    AdjustBar *priv = adjbar->priv;
    if (!setting == !priv->snap_to_ticks)
        return FALSE;

    priv->snap_to_ticks = !!setting;
    if (setting && priv->adjustment) {
        gdouble value = gtk_adjustment_get_value(priv->adjustment);
        gdouble snapped = snap_value(adjbar, value);
        if (fabs(snapped - value) > 1e-12*fmax(fabs(snapped), fabs(value)))
            gtk_adjustment_set_value(priv->adjustment, snapped);
    }

    return TRUE;
}

static gboolean
set_mapping(GwyAdjustBar *adjbar,
            GwyScaleMappingType mapping)
{
    AdjustBar *priv = adjbar->priv;
    if (mapping == priv->mapping)
        return FALSE;

    if (mapping > GWY_SCALE_MAPPING_LOG) {
        g_warning("Wrong scale mapping %u.", mapping);
        return FALSE;
    }

    priv->mapping = mapping;
    update_mapping(adjbar);
    gtk_widget_queue_draw(GTK_WIDGET(adjbar));
    return TRUE;
}

static void
create_input_window(GwyAdjustBar *adjbar)
{
    AdjustBar *priv = adjbar->priv;
    GtkWidget *widget = GTK_WIDGET(adjbar);
    GtkAllocation *allocation = &widget->allocation;
    GdkWindowAttr attributes;
    gint attributes_mask;

    g_return_if_fail(GTK_WIDGET_REALIZED(widget));
    if (priv->input_window)
        return;

    attributes.x = allocation->x + priv->x;
    attributes.y = allocation->y;
    attributes.width = priv->length;
    attributes.height = allocation->height;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.wclass = GDK_INPUT_ONLY;
    attributes.event_mask = (gtk_widget_get_events(widget)
                             | GDK_BUTTON_PRESS_MASK
                             | GDK_BUTTON_RELEASE_MASK
                             | GDK_ENTER_NOTIFY_MASK
                             | GDK_LEAVE_NOTIFY_MASK
                             | GDK_SCROLL_MASK
                             | GDK_POINTER_MOTION_MASK
                             | GDK_POINTER_MOTION_HINT_MASK);
    attributes_mask = GDK_WA_X | GDK_WA_Y;
    gwy_debug("CREATE INPUT WINDOW %dx%d at (%d,%d)",
              attributes.width, attributes.height, attributes.x, attributes.y);
    priv->input_window = gdk_window_new(gtk_widget_get_parent_window(widget),
                                        &attributes, attributes_mask);
    gdk_window_set_user_data(priv->input_window, widget);
}

static void
destroy_input_window(GwyAdjustBar *adjbar)
{
    AdjustBar *priv = adjbar->priv;

    if (!priv->input_window)
        return;
    gwy_debug("DESTROY INPUT WINDOW");
    gdk_window_destroy(priv->input_window);
    priv->input_window = NULL;
}

static void
draw_bar(GwyAdjustBar *adjbar, cairo_t *cr)
{
    AdjustBar *priv = adjbar->priv;
    GtkWidget *widget = GTK_WIDGET(adjbar);
    GtkStateType state = GTK_WIDGET_STATE(widget);
    gdouble height = widget->allocation.height;
    gdouble width = priv->length;
    gdouble val, barlength = 0.0;
    GwyRGBA base_color = { 0.3, 0.6, 1.0, 1.0 }, fcolor, bcolor;
    GwyRGBA bgcolor = { 1.0, 1.0, 1.0, 1.0 };

    if ((state & GTK_STATE_INSENSITIVE) || !priv->bar_sensitive) {
        base_color.a *= 0.4;
        bgcolor.a *= 0.4;
    }
    fcolor = bcolor = base_color;
    fcolor.a *= (state & GTK_STATE_PRELIGHT) ? 0.5 : 0.333;
    bgcolor.a *= (state & GTK_STATE_PRELIGHT) ? 0.5 : 0.333;

    if (priv->adjustment_ok) {
        val = gtk_adjustment_get_value(priv->adjustment);
        barlength = map_value_to_position(adjbar, val);
    }

    if (width > 2.0 && height > 2.0) {
        cairo_set_source_rgba(cr, bgcolor.r, bgcolor.g, bgcolor.b, bgcolor.a);
        cairo_rectangle(cr, 1.0, 1.0, width-2.0, height-2.0);
        cairo_fill(cr);
    }

    if (barlength < 0.5)
        return;

    cairo_set_line_width(cr, 1.0);
    if (barlength > 2.0) {
        cairo_rectangle(cr, 0.0, 0.0, barlength, height);
        cairo_set_source_rgba(cr, fcolor.r, fcolor.g, fcolor.b, fcolor.a);
        cairo_fill(cr);

        cairo_rectangle(cr, 0.5, 0.5, barlength-1.0, height-1.0);
        cairo_set_source_rgba(cr, bcolor.r, bcolor.g, bcolor.b, bcolor.a);
        cairo_stroke(cr);
    }
    else {
        cairo_rectangle(cr, 0, 0, barlength, height);
        cairo_set_source_rgba(cr, bcolor.r, bcolor.g, bcolor.b, bcolor.a);
        cairo_fill(cr);
    }
}

static void
adjustment_changed(GwyAdjustBar *adjbar,
                   G_GNUC_UNUSED GtkAdjustment *adjustment)
{
    update_mapping(adjbar);
    gtk_widget_queue_draw(GTK_WIDGET(adjbar));
}

static void
adjustment_value_changed(GwyAdjustBar *adjbar,
                         GtkAdjustment *adjustment)
{
    AdjustBar *priv = adjbar->priv;
    gdouble newvalue;

    if (!priv->adjustment_ok)
        return;

    newvalue = gtk_adjustment_get_value(adjustment);
    if (newvalue == priv->oldvalue)
        return;

    priv->oldvalue = newvalue;
    gtk_widget_queue_draw(GTK_WIDGET(adjbar));
}

static gdouble
ssqrt(gdouble x)
{
    return (x < 0.0) ? -sqrt(fabs(x)) : sqrt(x);
}

static gdouble
ssqr(gdouble x)
{
    return x*fabs(x);
}

static void
update_mapping(GwyAdjustBar *adjbar)
{
    AdjustBar *priv = adjbar->priv;
    gdouble lower, upper;

    priv->adjustment_ok = FALSE;
    if (!priv->adjustment)
        return;

    lower = priv->adjustment->lower;
    upper = priv->adjustment->upper;

    if (gwy_isinf(lower) || gwy_isnan(lower)
        || gwy_isinf(upper) || gwy_isnan(upper))
        return;

    if (priv->mapping == GWY_SCALE_MAPPING_LOG) {
        if (lower <= 0.0 || upper <= 0.0)
            return;
    }

    priv->length = GTK_WIDGET(adjbar)->allocation.width - priv->x;
    if (priv->length < 2)
        return;

    if (priv->mapping == GWY_SCALE_MAPPING_LINEAR)
        priv->map_value = priv->map_position = map_both_linear;
    else if (priv->mapping == GWY_SCALE_MAPPING_SQRT) {
        priv->map_value = ssqrt;
        priv->map_position = ssqr;
    }
    else if (priv->mapping == GWY_SCALE_MAPPING_LOG) {
        priv->map_value = log;
        priv->map_position = exp;
    }
    priv->b = priv->map_value(lower);
    priv->a = (priv->map_value(upper) - priv->b)/priv->length;
    if (gwy_isinf(priv->a) || gwy_isnan(priv->a) || !priv->a
        || gwy_isinf(priv->b) || gwy_isnan(priv->b))
        return;

    priv->adjustment_ok = TRUE;
}

static gdouble
map_value_to_position(GwyAdjustBar *adjbar, gdouble value)
{
    AdjustBar *priv = adjbar->priv;

    return (priv->map_value(value) - priv->b)/priv->a;
}

static gdouble
map_position_to_value(GwyAdjustBar *adjbar, gdouble position)
{
    AdjustBar *priv = adjbar->priv;

    return priv->map_position(priv->a*position + priv->b);
}

static gdouble
map_both_linear(gdouble value)
{
    return value;
}

static void
change_value(GtkWidget *widget, gdouble newposition)
{
    GwyAdjustBar *adjbar = GWY_ADJUST_BAR(widget);
    AdjustBar *priv = adjbar->priv;
    gdouble value, newvalue;

    if (!priv->adjustment_ok)
        return;

    value = gtk_adjustment_get_value(priv->adjustment);
    newposition = CLAMP(newposition, 0.0, priv->length);
    newvalue = map_position_to_value(adjbar, newposition);
    if (newvalue != value)
        g_signal_emit(adjbar, signals[SGNL_CHANGE_VALUE], 0, newvalue);
}

static void
ensure_cursors(GwyAdjustBar *adjbar)
{
    AdjustBar *priv = adjbar->priv;
    GdkDisplay *display;

    if (priv->cursor_move)
        return;

    display = gtk_widget_get_display(GTK_WIDGET(adjbar));
    priv->cursor_move = gdk_cursor_new_for_display(display,
                                                   GDK_SB_H_DOUBLE_ARROW);
    gdk_window_set_cursor(priv->input_window, priv->cursor_move);
}

static void
discard_cursors(GwyAdjustBar *adjbar)
{
    AdjustBar *priv = adjbar->priv;

    if (priv->cursor_move) {
        gdk_cursor_unref(priv->cursor_move);
        priv->cursor_move = NULL;
    }
}

static gdouble
snap_value(GwyAdjustBar *adjbar, gdouble value)
{
    AdjustBar *priv = adjbar->priv;
    gdouble step, lower, upper, m;

    if (!priv->adjustment || !priv->snap_to_ticks)
        return value;

    step = priv->adjustment->step_increment;
    if (!step)
        return value;

    lower = priv->adjustment->lower;
    upper = priv->adjustment->upper;
    m = 0.5*fmin(step, upper - lower);
    if (value >= upper - m)
        return upper;

    value = GWY_ROUND((value - lower)/step)*step + lower;
    if (value > upper)
        value -= step;
    if (value < lower)
        value = lower;

    return value;
}

/**
 * SECTION: gwyadjustbar
 * @title: GwyAdjustBar
 * @short_description: Compact adjustment visualisation and modification
 *
 * #GwyAdjustBar is a compact widget for visualisation and modification of the
 * value of an #GtkAdjustment.  It can contains a label with an overlaid bar
 * that can be clicked, dragged or modified by the scroll-wheel by the user.
 * Since the widget does not take keyboard focus, it should be paired with a
 * #GtkSpinButton, sharing the same adjustment.  This spin button would also be
 * the typical mnemonic widget for the adjustment bar.
 *
 * #GwyAdjustBar supports several different types of mapping between screen
 * positions and values of the underlying adjustment.  Nevertheless, the
 * default mapping (signed square root, %GWY_SCALE_MAPPING_SQRT) should fit
 * most situations.
 **/

/**
 * GwyAdjustBar:
 *
 * Adjustment bar widget visualising an adjustment.
 *
 * The #GwyAdjustBar struct contains private data only and should be accessed
 * using the functions below.
 *
 * Since: 2.49
 **/

/**
 * GwyAdjustBarClass:
 *
 * Class of adjustment bars visualising adjustments.
 *
 * Since: 2.49
 **/

/**
 * GwyScaleMappingType:
 * @GWY_SCALE_MAPPING_LINEAR: Linear mapping between values and screen
 *                            positions.  This recommended for signed additive
 *                            quantities of a limited range.
 * @GWY_SCALE_MAPPING_SQRT: Screen positions correspond to ‘signed square
 *                          roots’ of the value.  This is the
 *                          recommended general-purpose default mapping type as
 *                          it works with both signed and usigned quantities
 *                          and offers good sensitivity for both large and
 *                          small values.
 * @GWY_SCALE_MAPPING_LOG: Screen positions correspond to logarithms of values.
 *                         The adjustment range must contain only positive
 *                         values.  For quantities of extreme ranges this
 *                         mapping may be preferred to %GWY_SCALE_MAPPING_SQRT.
 *
 * Type of adjustment bar mapping functions.
 *
 * Since: 2.49
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
