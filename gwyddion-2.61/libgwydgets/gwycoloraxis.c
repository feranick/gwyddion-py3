/*
 *  $Id: gwycoloraxis.c 24535 2021-11-22 17:15:18Z yeti-dn $
 *  Copyright (C) 2003-2014 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libgwydgets/gwycoloraxis.h>
#include <libgwydgets/gwydgettypes.h>

enum {
    PROP_0,
    PROP_ORIENTATION,
    PROP_TICKS_STYLE,
    PROP_SI_UNIT,
    PROP_GRADIENT,
    PROP_LABELS_VISIBLE,
    PROP_LAST
};

enum { MIN_TICK_DISTANCE = 30 };

struct _GwyColorAxisPrivate {
    GwyColorAxisMapFunc map_ticks;
    gpointer map_ticks_data;
};

typedef struct _GwyColorAxisPrivate GwyColorAxisPrivate;

static void     gwy_color_axis_set_property (GObject *object,
                                             guint prop_id,
                                             const GValue *value,
                                             GParamSpec *pspec);
static void     gwy_color_axis_get_property (GObject *object,
                                             guint prop_id,
                                             GValue *value,
                                             GParamSpec *pspec);
static void     gwy_color_axis_destroy      (GtkObject *object);
static void     gwy_color_axis_realize      (GtkWidget *widget);
static void     gwy_color_axis_unrealize    (GtkWidget *widget);
static void     gwy_color_axis_size_request (GtkWidget *widget,
                                             GtkRequisition *requisition);
static void     gwy_color_axis_size_allocate(GtkWidget *widget,
                                             GtkAllocation *allocation);
static gboolean gwy_color_axis_expose       (GtkWidget *widget,
                                             GdkEventExpose *event);
static void     gwy_color_axis_adjust       (GwyColorAxis *axis,
                                             gint width,
                                             gint height);
static void     gwy_color_axis_draw_labels  (GwyColorAxis *axis, gint prec);
static gint     gwy_color_axis_step_to_prec (gdouble d);
static void     gwy_color_axis_update       (GwyColorAxis *axis);
static void     gwy_color_axis_changed      (GwyColorAxis *axis);

G_DEFINE_TYPE(GwyColorAxis, gwy_color_axis, GTK_TYPE_WIDGET)

static void
gwy_color_axis_class_init(GwyColorAxisClass *klass)
{
    GObjectClass *gobject_class;
    GtkObjectClass *object_class;
    GtkWidgetClass *widget_class;

    gobject_class = G_OBJECT_CLASS(klass);
    object_class = GTK_OBJECT_CLASS(klass);
    widget_class = GTK_WIDGET_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GwyColorAxisPrivate));

    gobject_class->get_property = gwy_color_axis_get_property;
    gobject_class->set_property = gwy_color_axis_set_property;

    object_class->destroy = gwy_color_axis_destroy;

    widget_class->realize = gwy_color_axis_realize;
    widget_class->expose_event = gwy_color_axis_expose;
    widget_class->size_request = gwy_color_axis_size_request;
    widget_class->unrealize = gwy_color_axis_unrealize;
    widget_class->size_allocate = gwy_color_axis_size_allocate;

    g_object_class_install_property
        (gobject_class,
         PROP_ORIENTATION,
         g_param_spec_enum("orientation",
                           "Orientation",
                           "Axis orientation",
                           GTK_TYPE_ORIENTATION,
                           GTK_ORIENTATION_VERTICAL,
                           G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_TICKS_STYLE,
         g_param_spec_enum("ticks-style",
                           "Ticks style",
                           "The style of axis ticks",
                           GWY_TYPE_TICKS_STYLE,
                           GWY_TICKS_STYLE_AUTO,
                           G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_SI_UNIT,
         g_param_spec_object("si-unit",
                             "SI unit",
                             "SI unit to display in labels",
                             GWY_TYPE_SI_UNIT,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_GRADIENT,
         g_param_spec_string("gradient",
                             "Gradient",
                             "Name of color gradient the axis displays",
                             NULL,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_LABELS_VISIBLE,
         g_param_spec_boolean("labels-visible",
                              "Labels visible",
                              "Whether minimum and maximum labels are visible",
                              TRUE,
                              G_PARAM_READWRITE));
}

static void
gwy_color_axis_init(GwyColorAxis *axis)
{
    axis->priv = G_TYPE_INSTANCE_GET_PRIVATE(axis, GWY_TYPE_COLOR_AXIS,
                                             GwyColorAxisPrivate);
    axis->orientation = GTK_ORIENTATION_VERTICAL;
    axis->tick_length = 6;
    axis->stripe_width = 10;
    axis->labels_visible = TRUE;
    axis->ticks_style = GWY_TICKS_STYLE_AUTO;

    axis->min = 0.0;
    axis->max = 1.0;
    axis->inverted = FALSE;

    axis->siunit = gwy_si_unit_new(NULL);

    axis->gradient = gwy_gradients_get_gradient(NULL);
    axis->gradient_id
        = g_signal_connect_swapped(axis->gradient, "data-changed",
                                   G_CALLBACK(gwy_color_axis_update), axis);
    gwy_resource_use(GWY_RESOURCE(axis->gradient));
}

static void
gwy_color_axis_set_property(GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
    GwyColorAxis *axis = GWY_COLOR_AXIS(object);

    switch (prop_id) {
        case PROP_ORIENTATION:
        /* Constr-only */
        axis->orientation = g_value_get_enum(value);
        break;

        case PROP_TICKS_STYLE:
        gwy_color_axis_set_ticks_style(axis, g_value_get_enum(value));
        break;

        case PROP_SI_UNIT:
        gwy_color_axis_set_si_unit(axis, g_value_get_object(value));
        break;

        case PROP_GRADIENT:
        gwy_color_axis_set_gradient(axis, g_value_get_string(value));
        break;

        case PROP_LABELS_VISIBLE:
        gwy_color_axis_set_labels_visible(axis, g_value_get_boolean(value));
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_color_axis_get_property(GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
    GwyColorAxis *axis = GWY_COLOR_AXIS(object);

    switch (prop_id) {
        case PROP_ORIENTATION:
        g_value_set_enum(value, axis->orientation);
        break;

        case PROP_TICKS_STYLE:
        g_value_set_enum(value, axis->ticks_style);
        break;

        case PROP_SI_UNIT:
        g_value_set_object(value, axis->siunit);
        break;

        case PROP_GRADIENT:
        g_value_set_string(value, gwy_color_axis_get_gradient(axis));
        break;

        case PROP_LABELS_VISIBLE:
        g_value_set_boolean(value, axis->labels_visible);
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/**
 * gwy_color_axis_new_with_range:
 * @orientation: The orientation of the axis.
 * @min: The minimum.
 * @max: The maximum.
 *
 * Creates a new color axis.
 *
 * Returns: The newly created color axis as a #GtkWidget.
 **/
GtkWidget*
gwy_color_axis_new_with_range(GtkOrientation orientation,
                              gdouble min,
                              gdouble max)
{
    GwyColorAxis *axis;
    GtkWidget *widget;

    widget = gwy_color_axis_new(orientation);
    axis = GWY_COLOR_AXIS(widget);
    if (max < min)
        axis->inverted = TRUE;
    axis->min = MIN(min, max);
    axis->max = MAX(min, max);

    return widget;
}

/**
 * gwy_color_axis_new:
 * @orientation: The orientation of the axis.
 *
 * Creates a new color axis.
 *
 * Returns: The newly created color axis as a #GtkWidget.
 **/
GtkWidget*
gwy_color_axis_new(GtkOrientation orientation)
{
    return g_object_new(GWY_TYPE_COLOR_AXIS, "orientation", orientation, NULL);
}

static void
gwy_color_axis_destroy(GtkObject *object)
{
    GwyColorAxis *axis;

    axis = (GwyColorAxis*)object;
    if (axis->gradient_id) {
        g_signal_handler_disconnect(axis->gradient, axis->gradient_id);
        axis->gradient_id = 0;
    }
    GWY_OBJECT_UNREF(axis->siunit);
    if (axis->gradient) {
        gwy_resource_release(GWY_RESOURCE(axis->gradient));
        axis->gradient = NULL;
    }
    GWY_OBJECT_UNREF(axis->stripe);

    GTK_OBJECT_CLASS(gwy_color_axis_parent_class)->destroy(object);
}

static void
gwy_color_axis_unrealize(GtkWidget *widget)
{
    if (GTK_WIDGET_CLASS(gwy_color_axis_parent_class)->unrealize)
        GTK_WIDGET_CLASS(gwy_color_axis_parent_class)->unrealize(widget);
}

static void
gwy_color_axis_realize(GtkWidget *widget)
{
    GwyColorAxis *axis;
    GdkWindowAttr attributes;
    gint attributes_mask;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(GWY_IS_COLOR_AXIS(widget));

    GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);
    axis = GWY_COLOR_AXIS(widget);

    attributes.x = widget->allocation.x;
    attributes.y = widget->allocation.y;
    attributes.width = widget->allocation.width;
    attributes.height = widget->allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.event_mask = gtk_widget_get_events(widget)
                            | GDK_EXPOSURE_MASK
                            | GDK_BUTTON_PRESS_MASK
                            | GDK_BUTTON_RELEASE_MASK
                            | GDK_POINTER_MOTION_MASK
                            | GDK_POINTER_MOTION_HINT_MASK;
    attributes.visual = gtk_widget_get_visual(widget);
    attributes.colormap = gtk_widget_get_colormap(widget);

    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
    widget->window = gdk_window_new(gtk_widget_get_parent_window(widget),
                                    &attributes, attributes_mask);
    gdk_window_set_user_data(widget->window, widget);

    widget->style = gtk_style_attach(widget->style, widget->window);
    gtk_style_set_background(widget->style, widget->window, GTK_STATE_NORMAL);

    /*compute axis*/
    gwy_color_axis_update(axis);
}

static void
gwy_color_axis_size_request(GtkWidget *widget,
                            GtkRequisition *requisition)
{
    GwyColorAxis *axis;

    axis = GWY_COLOR_AXIS(widget);

    /* XXX */
    if (axis->orientation == GTK_ORIENTATION_VERTICAL) {
        requisition->width = 80;
        requisition->height = 100;
    }
    else {
        requisition->width = 100;
        requisition->height = 50;
    }
}

static void
gwy_color_axis_size_allocate(GtkWidget *widget,
                              GtkAllocation *allocation)
{
    GwyColorAxis *axis;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(GWY_IS_COLOR_AXIS(widget));
    g_return_if_fail(allocation != NULL);

    widget->allocation = *allocation;

    axis = GWY_COLOR_AXIS(widget);
    if (GTK_WIDGET_REALIZED(widget)) {
        gdk_window_move_resize(widget->window,
                               allocation->x, allocation->y,
                               allocation->width, allocation->height);
    }
    gwy_color_axis_adjust(axis, allocation->width, allocation->height);
}

static void
gwy_color_axis_adjust(GwyColorAxis *axis, gint width, gint height)
{
    gint i, j, rowstride, palsize, dval, x, y, pali;
    guchar *pixels, *line;
    const guchar *samples, *s;
    gdouble cor;

    g_return_if_fail(axis->orientation == GTK_ORIENTATION_VERTICAL
                     || axis->orientation == GTK_ORIENTATION_HORIZONTAL);
    GWY_OBJECT_UNREF(axis->stripe);

    if (axis->orientation == GTK_ORIENTATION_VERTICAL)
        axis->stripe = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                                      axis->stripe_width, height);
    else
        axis->stripe = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                                      width, axis->stripe_width);

    /*render stripe according to orientation*/
    pixels = gdk_pixbuf_get_pixels(axis->stripe);
    rowstride = gdk_pixbuf_get_rowstride(axis->stripe);
    samples = gwy_gradient_get_samples(axis->gradient, &palsize);

    if (axis->orientation == GTK_ORIENTATION_VERTICAL) {
        cor = (palsize - 1.0)/height;
        for (i = 0; i < height; i++) {
            line = pixels + (axis->inverted
                             ? (height-1-i)*rowstride : i*rowstride);
            dval = (gint)((height-i-1)*cor + 0.5);
            for (j = 0; j < axis->stripe_width*height; j += height) {
                s = samples + 4*dval;
                *(line++) = *(s++);
                *(line++) = *(s++);
                *(line++) = *s;
            }
        }
    }
    else {
        for (y = 0; y < axis->stripe_width; ++y) {
            if (axis->inverted) {
                line = pixels + (y + 1)*rowstride - 4;
                for (x = 0; x < width; ++x) {
                    pali = x/((gdouble)(width))*palsize;
                    if (pali >= palsize)
                        pali = palsize - 1;
                    if (pali < 0)
                        pali = 0;
                    s = samples + 4*pali;
                    *(line++) = *(s++);
                    *(line++) = *(s++);
                    *(line++) = *s;
                    line -= 4;
                }
            }
            else {
                line = pixels + y*rowstride;
                for (x = 0; x < width; ++x) {
                    pali = x/((gdouble)(width))*palsize;
                    if (pali >= palsize)
                        pali = palsize - 1;
                    if (pali < 0)
                        pali = 0;
                    s = samples + 4*pali;
                    *(line++) = *(s++);
                    *(line++) = *(s++);
                    *(line++) = *s;
                }
            }
        }
    }
}

/* auxilliary function to compute decimal points from tickdist */
static gint
gwy_color_axis_step_to_prec(gdouble d)
{
    gdouble resd = log10(7.5) - log10(d);
    if (resd != resd)
        return 1;
    if (resd > 1e20)
        return 1;
    if (resd < 1.0)
        resd = 1.0;
    return (gint)floor(resd);
}

static void
gwy_color_axis_draw_labels_ticks(GwyColorAxis *axis)
{
    GtkWidget *widget;
    gint xthickness, ythickness, width, height, swidth, off, tlength, size,
         pos, prec = 1, mintdist = MIN_TICK_DISTANCE;
    guint txlen, i;
    gdouble scale, x, x2, m, tickdist, max;
    GString *strlabel;
    PangoLayout *layoutlabel;
    PangoRectangle rectlabel;
    GArray *tickx;
    GdkGC *gc;
    GwySIValueFormat *format = NULL;

    gwy_debug("labels_visible: %d", axis->labels_visible);
    gwy_debug("ticks_style: %d", axis->ticks_style);
    widget = GTK_WIDGET(axis);
    if (!axis->labels_visible) {
        axis->labelb_size = 1;
        axis->labele_size = 1;
        /*return;*/
    }

    xthickness = widget->style->xthickness;
    ythickness = widget->style->ythickness;
    width = widget->allocation.width;
    height = widget->allocation.height;
    swidth = axis->stripe_width;
    off = swidth + 1
        + ((axis->orientation == GTK_ORIENTATION_VERTICAL)
           ? xthickness : ythickness);
    tlength = axis->tick_length;

    /* Draw frame around false color scale and boundary marks */
    gc = widget->style->fg_gc[GTK_WIDGET_STATE(widget)];
    if (axis->orientation == GTK_ORIENTATION_VERTICAL) {
        gdk_draw_rectangle(widget->window, gc, FALSE, 0, 0, swidth, height - 1);
        gdk_draw_line(widget->window, gc,
                      swidth, 0, swidth + tlength, 0);
        gdk_draw_line(widget->window, gc,
                      swidth, height - 1, swidth + tlength, height - 1);
        size = height;
    }
    else {
        gdk_draw_rectangle(widget->window, gc, FALSE, 0, 0, width - 1, swidth);
        gdk_draw_line(widget->window, gc,
                      0, swidth, 0, swidth + tlength);
        gdk_draw_line(widget->window, gc,
                      width - 1, swidth, width - 1, swidth + tlength);
        size = width;
    }

    /* Don't attempt to draw anything if rounding errors are too large or
     * scale calculation can overflow */
    x = axis->max - axis->min;
    max = MAX(fabs(axis->min), fabs(axis->max));
    if (x < 1e-15*max || x <= 1e4*G_MINDOUBLE || max >= 1e-4*G_MAXDOUBLE)
        return; /*  */


    strlabel = g_string_new(NULL);
    layoutlabel = gtk_widget_create_pango_layout(widget, "");
    format = gwy_si_unit_get_format(axis->siunit, GWY_SI_UNIT_FORMAT_VFMARKUP,
                                    max, NULL);

    if (axis->ticks_style == GWY_TICKS_STYLE_AUTO
        || axis->ticks_style == GWY_TICKS_STYLE_UNLABELLED) {
        scale = size/(axis->max - axis->min);
        x = MIN_TICK_DISTANCE/scale;
        m = pow10(floor(log10(x)));
        x /= m;
        gwy_debug("scale: %g x: %g m: %g", scale, x, m);
        if (x == 1.0)
            x = 1.0;
        else if (x <= 2.0)
            x = 2.0;
        else if (x <= 5.0)
            x = 5.0;
        else
            x = 10.0;
        tickdist = x*m;
        x = floor(axis->min/tickdist)*tickdist;
        max = ceil(axis->max/tickdist)*tickdist;
        /* if labels_visible, then compute precision */
        if (axis->labels_visible) {
            prec = gwy_color_axis_step_to_prec(tickdist/format->magnitude);
            gwy_debug("precision 1: %d; %g", prec, tickdist/format->magnitude);
            /* if horizontal, then recompute tickdist */
            if (axis->orientation == GTK_ORIENTATION_HORIZONTAL) {
                /* compute maximum size of all labels */
                x2 = x;
                while (x2 <= max) {
                    /* Spaces here in format string cause gap size between tick
                     * labels in horizontal color axis */
                    g_string_printf(strlabel, "%3.*f  ", prec,
                                    x2/format->magnitude);
                    pango_layout_set_markup(layoutlabel, strlabel->str,
                                            strlabel->len);
                    pango_layout_get_pixel_extents(layoutlabel, NULL,
                                                   &rectlabel);
                    if (rectlabel.width > mintdist)
                        mintdist = rectlabel.width;
                    x2 += tickdist;
                }
                /* tickdist recomputation */
                x = mintdist/scale;
                m = pow10(floor(log10(x)));
                x /= m;
                gwy_debug("scale: %g x: %g m: %g", scale, x, m);
                if (x == 1.0)
                    x = 1.0;
                else if (x <= 2.0)
                    x = 2.0;
                else if (x <= 5.0)
                    x = 5.0;
                else
                    x = 10.0;
                tickdist = x*m;
                x = floor(axis->min/tickdist)*tickdist;
                max = ceil(axis->max/tickdist)*tickdist;
                prec = gwy_color_axis_step_to_prec(tickdist/format->magnitude);
                gwy_debug("precision 2: %d; %g", prec,
                          tickdist/format->magnitude);
            }
            /* draw min and max label; non-drawing when not labels_visible
             * dealt with inside the function */
            gwy_color_axis_draw_labels(axis, prec);
        }
        gwy_debug("tickdist: %g x: %g max: %g", tickdist, x, max);
        tickx = g_array_new(FALSE, FALSE, sizeof(gdouble));
        while (x <= max) {
            g_array_append_val(tickx, x);
            x += tickdist;
        }
        txlen = tickx->len;
        if (axis->priv->map_ticks && txlen) {
            gdouble *tmx = g_new(gdouble, txlen);

            axis->priv->map_ticks(axis,
                                  (const gdouble*)tickx->data, tmx, txlen,
                                  axis->priv->map_ticks_data);
            g_array_set_size(tickx, 0);
            for (i = 0; i < txlen; i++) {
                x = axis->min + (axis->max - axis->min)*tmx[i];
                g_array_append_val(tickx, x);
            }
            g_free(tmx);
        }
        if (axis->orientation == GTK_ORIENTATION_VERTICAL) {
            for (i = 0; i < txlen; i++) {
                x = g_array_index(tickx, gdouble, i);
                pos = size-1 - GWY_ROUND((x - axis->min)*scale);
                if (pos > axis->labelb_size && pos < size-1-axis->labele_size) {
                    gdk_draw_line(widget->window, gc, swidth, pos,
                                  swidth + tlength/2, pos);
                    /* draw only if labels_visible */
                    if (axis->ticks_style != GWY_TICKS_STYLE_AUTO
                        || !axis->labels_visible)
                        continue;

                    g_string_printf(strlabel, "%3.*f", prec,
                                    x/format->magnitude);
                    pango_layout_set_markup(layoutlabel, strlabel->str,
                                            strlabel->len);
                    pango_layout_get_pixel_extents(layoutlabel, NULL,
                                                   &rectlabel);
                    /* prevent drawing over maximum label */
                    if (pos-rectlabel.height > axis->labelb_size)
                        gdk_draw_layout(widget->window, gc, off,
                                        pos-rectlabel.height, layoutlabel);
                }
            }
            g_array_free(tickx, TRUE);
        }
        else {
            for (i = 0; i < txlen; i++) {
                x = g_array_index(tickx, gdouble, i);
                pos = GWY_ROUND((x - axis->min)*scale);
                if (pos > axis->labelb_size && pos < size-1-axis->labele_size) {
                    gdk_draw_line(widget->window, gc, pos, swidth, pos,
                                  swidth + tlength/2);
                    /* draw only if labels_visible */
                    if (axis->ticks_style != GWY_TICKS_STYLE_AUTO
                        || !axis->labels_visible)
                        continue;

                    g_string_printf(strlabel, "%3.*f", prec,
                                    x/format->magnitude);
                    pango_layout_set_markup(layoutlabel, strlabel->str,
                                            strlabel->len);
                    pango_layout_get_pixel_extents(layoutlabel, NULL,
                                                   &rectlabel);
                    /* prevent drawing over maximum label */
                    if (pos+rectlabel.width < size-1-axis->labele_size)
                        gdk_draw_layout(widget->window, gc, pos, off,
                                        layoutlabel);
                }
            }
        }
    }
    else if (axis->ticks_style == GWY_TICKS_STYLE_NONE) {
        gwy_color_axis_draw_labels(axis, 1);
    }
    else if (axis->ticks_style == GWY_TICKS_STYLE_CENTER) {
        gwy_color_axis_draw_labels(axis, 1);
        x2 = (axis->max+axis->min)*0.5;
        if (axis->orientation == GTK_ORIENTATION_VERTICAL) {
            pos = height/2;
            gdk_draw_line(widget->window, gc,
                          swidth, height/2, swidth + tlength/2, height/2);
            if (axis->labels_visible) {
                g_string_printf(strlabel, "%3.1f", x2/format->magnitude);
                pango_layout_set_markup(layoutlabel, strlabel->str,
                                        strlabel->len);
                pango_layout_get_pixel_extents(layoutlabel, NULL, &rectlabel);
                /* prevent drawing over maximum label */
                if (pos-rectlabel.height > axis->labelb_size)
                    gdk_draw_layout(widget->window, gc, off,
                                    pos-rectlabel.height, layoutlabel);
            }
        }
        else {
            pos = width/2;
            gdk_draw_line(widget->window, gc,
                          width/2, swidth, width/2, swidth + tlength/2);
            if (axis->labels_visible) {
                g_string_printf(strlabel, "%3.1f  ", x2/format->magnitude);
                pango_layout_set_markup(layoutlabel, strlabel->str,
                                        strlabel->len);
                pango_layout_get_pixel_extents(layoutlabel, NULL, &rectlabel);
                /* prevent drawing over maximum label */
                if (pos+rectlabel.width < size-1-axis->labele_size)
                    gdk_draw_layout(widget->window, gc, pos, off, layoutlabel);
            }
        }
    }
    GWY_SI_VALUE_FORMAT_FREE(format);
    g_string_free(strlabel, TRUE);
    g_object_unref(layoutlabel);
}

static gboolean
gwy_color_axis_expose(GtkWidget *widget,
                      GdkEventExpose *event)
{
    GwyColorAxis *axis;
    GdkGC *gc;

    g_return_val_if_fail(widget != NULL, FALSE);
    g_return_val_if_fail(GWY_IS_COLOR_AXIS(widget), FALSE);
    g_return_val_if_fail(event != NULL, FALSE);

    if (event->count > 0)
        return FALSE;

    axis = GWY_COLOR_AXIS(widget);
    gc = widget->style->fg_gc[GTK_WIDGET_STATE(widget)];

    gdk_draw_pixbuf(widget->window, gc, axis->stripe,
                    0, 0,
                    0, 0,
                    gdk_pixbuf_get_width(axis->stripe),
                    gdk_pixbuf_get_height(axis->stripe),
                    GDK_RGB_DITHER_NONE, 0, 0);
    gwy_color_axis_draw_labels_ticks(axis);
    return FALSE;
}

static void
gwy_color_axis_draw_labels(GwyColorAxis *axis, gint prec)
{
    GtkWidget *widget;
    PangoLayout *layoutmax, *layoutmin, *layoutunits;
    GwySIValueFormat *format = NULL;
    GString *strmin, *strmax, *strunits;
    GdkGC *gc;
    PangoRectangle rectmin, rectmax, rectunits;
    gint xthickness, ythickness, width, height, swidth, off;
    gdouble max, max_label_aux;

    gwy_debug("labels_visible: %d", axis->labels_visible);
    widget = GTK_WIDGET(axis);

    if (!axis->labels_visible) {
        axis->labelb_size = 1;
        axis->labele_size = 1;
        return;
    }

    xthickness = widget->style->xthickness;
    ythickness = widget->style->ythickness;
    width = widget->allocation.width;
    height = widget->allocation.height;
    swidth = axis->stripe_width;
    off = swidth + 1
          + ((axis->orientation == GTK_ORIENTATION_VERTICAL)
             ? xthickness : ythickness);

    /* Compute minimum and maximum numbers */
    strmin = g_string_new(NULL);
    strmax = g_string_new(NULL);
    max = MAX(fabs(axis->min), fabs(axis->max));
    format = gwy_si_unit_get_format(axis->siunit,
        GWY_SI_UNIT_FORMAT_VFMARKUP, max, NULL);
    /* min label */
    if (max == 0) {
        g_string_assign(strmin, "0.0");
    }
    else {
        g_string_printf(strmin, "%3.*f  ", prec,
                        axis->min/format->magnitude);
    }
    layoutmin = gtk_widget_create_pango_layout(widget, "");
    pango_layout_set_markup(layoutmin, strmin->str, strmin->len);
    pango_layout_get_pixel_extents(layoutmin, NULL, &rectmin);

    /* max label */
    if (max == 0)
        max_label_aux = 0.0;
    else
        max_label_aux = axis->max/format->magnitude;
    layoutmax = gtk_widget_create_pango_layout(widget, "");
    if (axis->orientation == GTK_ORIENTATION_VERTICAL) {
        g_string_printf(strmax, "%3.*f %s", prec, max_label_aux, format->units);
        /* Measure text */
        pango_layout_set_markup(layoutmax, strmax->str, strmax->len);
        pango_layout_get_pixel_extents(layoutmax, NULL, &rectmax);
        if (rectmax.width + off > width) {
            /* twoline layout */
            g_string_printf(strmax, "%3.*f\n%s", prec,
                        axis->max/format->magnitude, format->units);
            pango_layout_set_markup(layoutmax, strmax->str, strmax->len);
            pango_layout_get_pixel_extents(layoutmax, NULL, &rectmax);
        }
    }
    else {
        g_string_printf(strmax, "%3.*f", prec, max_label_aux);
        pango_layout_set_markup(layoutmax, strmax->str, strmax->len);
        pango_layout_get_pixel_extents(layoutmax, NULL, &rectmax);
    }

    /* Draw text */
    gc = widget->style->text_gc[GTK_WIDGET_STATE(widget)];

    if (axis->orientation == GTK_ORIENTATION_VERTICAL) {
        gdk_draw_layout(widget->window, gc, off, ythickness, layoutmax);
        axis->labelb_size = rectmax.height;
    }
    else {
        gdk_draw_layout(widget->window, gc,
                        width - rectmax.width - xthickness, off, layoutmax);
        axis->labele_size = rectmax.width;
    }

    if (axis->orientation == GTK_ORIENTATION_VERTICAL) {
        gdk_draw_layout(widget->window, gc,
                        off, height - rectmin.height - ythickness,
                        layoutmin);
        axis->labele_size = rectmin.height;
    }
    else {
        gdk_draw_layout(widget->window, gc,
                        xthickness, off,
                        layoutmin);
        axis->labelb_size = rectmin.width;
    }

    if (axis->orientation == GTK_ORIENTATION_HORIZONTAL) {
        /* Draw units separately */
        strunits = g_string_new(NULL);
        layoutunits = gtk_widget_create_pango_layout(widget, "");
        g_string_printf(strunits, "%s", format->units);
        pango_layout_set_markup(layoutunits, strunits->str, strunits->len);
        pango_layout_get_pixel_extents(layoutunits, NULL, &rectunits);
        gdk_draw_layout(widget->window, gc, width-rectunits.width-xthickness,
            off+rectmax.height, layoutunits);
        g_object_unref(layoutunits);
        g_string_free(strunits, TRUE);
    }

    g_object_unref(layoutmin);
    g_object_unref(layoutmax);
    g_string_free(strmin, TRUE);
    g_string_free(strmax, TRUE);
    GWY_SI_VALUE_FORMAT_FREE(format);
}

/**
 * gwy_color_axis_get_range:
 * @axis: A color axis.
 * @min: Location to store the range maximum (or %NULL).
 * @max: Location to store the range minimum (or %NULL).
 *
 * Gets the range of a color axis.
 **/
void
gwy_color_axis_get_range(GwyColorAxis *axis,
                         gdouble *min,
                         gdouble *max)
{
    g_return_if_fail(GWY_IS_COLOR_AXIS(axis));
    if (min)
        *min = axis->inverted ? axis->max : axis->min;
    if (max)
        *max = axis->inverted ? axis->min : axis->max;
}

/**
 * gwy_color_axis_set_range:
 * @axis: A color axis.
 * @min: The range minimum.
 * @max: The range maximum.
 *
 * Sets the range of a color axis.
 **/
void
gwy_color_axis_set_range(GwyColorAxis *axis,
                         gdouble min,
                         gdouble max)
{
    gboolean inverted = max < min;

    g_return_if_fail(GWY_IS_COLOR_AXIS(axis));

    if ((!axis->inverted && axis->min == min && axis->max == max)
        || (axis->inverted && axis->min == max && axis->max == min))
        return;

    axis->min = MIN(min, max);
    axis->max = MAX(min, max);

    if (axis->inverted != inverted) {
        axis->inverted = inverted;
        gwy_color_axis_update(axis);
    }
    else
        gwy_color_axis_changed(axis);
}

/**
 * gwy_color_axis_get_gradient:
 * @axis: A color axis.
 *
 * Gets the color gradient a color axis uses.
 *
 * Returns: The color gradient.
 **/
const gchar*
gwy_color_axis_get_gradient(GwyColorAxis *axis)
{
    g_return_val_if_fail(GWY_IS_COLOR_AXIS(axis), NULL);

    return gwy_resource_get_name(GWY_RESOURCE(axis->gradient));
}

/**
 * gwy_color_axis_set_gradient:
 * @axis: A color axis.
 * @gradient: Name of gradient @axis should use.  It should exist.
 *
 * Sets the color gradient a color axis should use.
 **/
void
gwy_color_axis_set_gradient(GwyColorAxis *axis,
                            const gchar *gradient)
{
    GwyGradient *grad;

    g_return_if_fail(GWY_IS_COLOR_AXIS(axis));

    grad = gwy_gradients_get_gradient(gradient);
    if (grad == axis->gradient)
        return;

    g_signal_handler_disconnect(axis->gradient, axis->gradient_id);
    gwy_resource_release(GWY_RESOURCE(axis->gradient));
    axis->gradient = grad;
    gwy_resource_use(GWY_RESOURCE(axis->gradient));
    axis->gradient_id
        = g_signal_connect_swapped(axis->gradient, "data-changed",
                                   G_CALLBACK(gwy_color_axis_update), axis);

    gwy_color_axis_update(axis);
}

/**
 * gwy_color_axis_get_si_unit:
 * @axis: A color axis.
 *
 * Gets the SI unit a color axis displays.
 *
 * Returns: The SI unit.
 **/
GwySIUnit*
gwy_color_axis_get_si_unit(GwyColorAxis *axis)
{
    g_return_val_if_fail(GWY_IS_COLOR_AXIS(axis), NULL);

    return axis->siunit;
}

/**
 * gwy_color_axis_set_si_unit:
 * @axis: A color axis.
 * @unit: A SI unit to display next to minimum and maximum value.
 *
 * Sets the SI unit a color axis displays.
 **/
void
gwy_color_axis_set_si_unit(GwyColorAxis *axis,
                           GwySIUnit *unit)
{
    gboolean not_equal = TRUE;
    GwySIUnit *old;

    g_return_if_fail(GWY_IS_COLOR_AXIS(axis));
    g_return_if_fail(GWY_IS_SI_UNIT(unit));

    if (axis->siunit != unit) {
        not_equal = !gwy_si_unit_equal(unit, axis->siunit);
        old = axis->siunit;
        g_object_ref(unit);
        axis->siunit = unit;
        GWY_OBJECT_UNREF(old);
    }
    if (not_equal)
        gwy_color_axis_changed(axis);
}

/**
 * gwy_color_axis_get_ticks_style:
 * @axis: A color axis.
 *
 * Gets ticks style of a color axis.
 *
 * Returns: The ticks style.
 **/
GwyTicksStyle
gwy_color_axis_get_ticks_style(GwyColorAxis *axis)
{
    g_return_val_if_fail(GWY_IS_COLOR_AXIS(axis), GWY_TICKS_STYLE_NONE);

    return axis->ticks_style;
}

/**
 * gwy_color_axis_set_ticks_style:
 * @axis: A color axis.
 * @ticks_style: The ticks style to use.
 *
 * Sets the ticks style of a color axis.
 **/
void
gwy_color_axis_set_ticks_style(GwyColorAxis *axis,
                               GwyTicksStyle ticks_style)
{
    g_return_if_fail(GWY_IS_COLOR_AXIS(axis));
    g_return_if_fail(ticks_style <= GWY_TICKS_STYLE_UNLABELLED);

    if (axis->ticks_style == ticks_style)
        return;

    axis->ticks_style = ticks_style;
    gwy_color_axis_changed(axis);
}

/**
 * gwy_color_axis_get_labels_visible:
 * @axis: A color axis.
 *
 * Gets the visibility of labels of a color axis.
 *
 * Returns: %TRUE if labels are displayed, %FALSE if they are omitted.
 **/
gboolean
gwy_color_axis_get_labels_visible(GwyColorAxis *axis)
{
    g_return_val_if_fail(GWY_IS_COLOR_AXIS(axis), FALSE);

    return axis->labels_visible;
}

/**
 * gwy_color_axis_set_labels_visible:
 * @axis: A color axis.
 * @labels_visible: %TRUE to display labels with minimum and maximum values,
 *                  %FALSE to display no labels.
 *
 * Sets the visibility of labels of a color axis.
 **/
void
gwy_color_axis_set_labels_visible(GwyColorAxis *axis,
                                  gboolean labels_visible)
{
    g_return_if_fail(GWY_IS_COLOR_AXIS(axis));
    labels_visible = !!labels_visible;

    if (axis->labels_visible == labels_visible)
        return;

    axis->labels_visible = labels_visible;
    gwy_color_axis_changed(axis);
}

static void
gwy_color_axis_update(GwyColorAxis *axis)
{
    gwy_color_axis_adjust(axis,
                          GTK_WIDGET(axis)->allocation.width,
                          GTK_WIDGET(axis)->allocation.height);
    gwy_color_axis_changed(axis);
}

static void
gwy_color_axis_changed(GwyColorAxis *axis)
{
    if (GTK_WIDGET_DRAWABLE(axis))
        gtk_widget_queue_draw(GTK_WIDGET(axis));
}

/**
 * gwy_color_axis_set_tick_map_func:
 * @axis: A color axis.
 * @func: Tick mapping function.
 * @user_data: Data to pass to @func.
 *
 * Set the tick mapping function for a color axis.
 *
 * The axis calculates tick positions as for the linear axis and then places
 * them non-linearly using @func.  Hence a mapping function should be used with
 * ticks mode %GWY_TICKS_STYLE_UNLABELLED because minimum tick spacing is not
 * guaranteed.
 *
 * Since: 2.39
 **/
void
gwy_color_axis_set_tick_map_func(GwyColorAxis *axis,
                                 GwyColorAxisMapFunc func,
                                 gpointer user_data)
{
    GwyColorAxisPrivate *priv;

    g_return_if_fail(GWY_IS_COLOR_AXIS(axis));
    priv = axis->priv;

    if (priv->map_ticks == func && priv->map_ticks_data == user_data)
        return;

    priv->map_ticks = func;
    priv->map_ticks_data = user_data;
    gwy_color_axis_changed(axis);
}

/************************** Documentation ****************************/

/**
 * SECTION:gwycoloraxis
 * @title: GwyColorAxis
 * @short_description: Simple axis with a false color scale
 * @see_also: #GwyAxis -- Axis for use in graphs,
 *            #GwyRuler -- Horizontal and vertical rulers
 **/

/**
 * GwyColorAxisMapFunc:
 * @axis: A color axis.
 * @z: Array of length @n of tick values.
 * @mapped: Array of length @n where values mapped to [0,1] should be placed.
 * @n: Length of @z and @mapped.
 * @user_data: Data passed to gwy_color_axis_set_tick_map_func().
 *
 * Type of color axis non-linear tick mapping function.
 *
 * Since: 2.39
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
