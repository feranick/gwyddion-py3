/*
 *  $Id: gwyselectiongraphline.c 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2006-2015 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwyserializable.h>
#include <libprocess/gwyprocessenums.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwyselectiongraphline.h>

#define GWY_SELECTION_GRAPH_LINE_TYPE_NAME "GwySelectionGraphLine"

enum {
    OBJECT_SIZE = 1
};

enum {
    PROP_0,
    PROP_ORIENTATION
};

typedef struct {
    GwyOrientation orientation;
} GwySelectionGraphLinePriv;

static void        gwy_selection_graph_line_serializable_init(GwySerializableIface *iface);
static void        gwy_selection_graph_line_set_property     (GObject *object,
                                                              guint prop_id,
                                                              const GValue *value,
                                                              GParamSpec *pspec);
static void        gwy_selection_graph_line_get_property     (GObject *object,
                                                              guint prop_id,
                                                              GValue *value,
                                                              GParamSpec *pspec);
static gboolean    gwy_selection_graph_line_crop_object      (GwySelection *selection,
                                                              gint i,
                                                              gpointer user_data);
static void        gwy_selection_graph_line_crop             (GwySelection *selection,
                                                              gdouble xmin,
                                                              gdouble ymin,
                                                              gdouble xmax,
                                                              gdouble ymax);
static void        gwy_selection_graph_line_move             (GwySelection *selection,
                                                              gdouble vx,
                                                              gdouble vy);
static GByteArray* gwy_selection_graph_line_serialize        (GObject *serializable,
                                                              GByteArray *buffer);
static GObject*    gwy_selection_graph_line_deserialize      (const guchar *buffer,
                                                              gsize size,
                                                              gsize *position);
static GObject*    gwy_selection_graph_line_duplicate        (GObject *object);
static void        gwy_selection_graph_line_clone            (GObject *source,
                                                              GObject *copy);
static void        gwy_selection_graph_line_set_orientation  (GwySelectionGraphLine *selection,
                                                              GwyOrientation orientation);

static GwySerializableIface *gwy_selection_graph_line_serializable_parent_iface;

G_DEFINE_TYPE_EXTENDED
    (GwySelectionGraphLine, gwy_selection_graph_line, GWY_TYPE_SELECTION, 0,
     GWY_IMPLEMENT_SERIALIZABLE(gwy_selection_graph_line_serializable_init))

static void
gwy_selection_graph_line_class_init(GwySelectionGraphLineClass *klass)
{
    GwySelectionClass *sel_class = GWY_SELECTION_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->set_property = gwy_selection_graph_line_set_property;
    gobject_class->get_property = gwy_selection_graph_line_get_property;

    sel_class->object_size = OBJECT_SIZE;
    sel_class->crop = gwy_selection_graph_line_crop;
    sel_class->move = gwy_selection_graph_line_move;
    g_type_class_add_private(klass, sizeof(GwySelectionGraphLinePriv));

    /**
     * GwySelectionGraphLine:orientation:
     *
     * The :orientation property represents the orientation of the selected
     * lines.
     *
     * The orientation is %GWY_ORIENTATION_HORIZONTAL for selections along the
     * @x-axis, i.e. vertical lines, and %GWY_ORIENTATION_VERTICAL for
     * selections along the @y-axis, i.e. horizontal lines.
     *
     * Since: 2.43
     **/
    g_object_class_install_property
        (gobject_class,
         PROP_ORIENTATION,
         g_param_spec_enum("orientation",
                           "Orientation",
                           "Orientation of selected lines",
                           GWY_TYPE_ORIENTATION,
                           GWY_ORIENTATION_HORIZONTAL,
                           G_PARAM_READABLE | G_PARAM_WRITABLE));
}

static void
gwy_selection_graph_line_serializable_init(GwySerializableIface *iface)
{
    gwy_selection_graph_line_serializable_parent_iface
        = g_type_interface_peek_parent(iface);

    iface->serialize = gwy_selection_graph_line_serialize;
    iface->deserialize = gwy_selection_graph_line_deserialize;
    iface->duplicate = gwy_selection_graph_line_duplicate;
    iface->clone = gwy_selection_graph_line_clone;
}

static void
gwy_selection_graph_line_init(GwySelectionGraphLine *selection)
{
    GwySelectionGraphLinePriv *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE(selection,
                                       GWY_TYPE_SELECTION_GRAPH_LINE,
                                       GwySelectionGraphLinePriv);

    /* Set max. number of objects to one */
    g_array_set_size(GWY_SELECTION(selection)->objects, OBJECT_SIZE);
    priv->orientation = GWY_ORIENTATION_HORIZONTAL;
}

static void
gwy_selection_graph_line_set_property(GObject *object,
                                      guint prop_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
    GwySelectionGraphLine *selection = GWY_SELECTION_GRAPH_LINE(object);

    switch (prop_id) {
        case PROP_ORIENTATION:
        gwy_selection_graph_line_set_orientation(selection,
                                                 g_value_get_enum(value));
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_selection_graph_line_get_property(GObject *object,
                                      guint prop_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
    GwySelectionGraphLine *selection = GWY_SELECTION_GRAPH_LINE(object);
    GwySelectionGraphLinePriv *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE(selection,
                                       GWY_TYPE_SELECTION_GRAPH_LINE,
                                       GwySelectionGraphLinePriv);

    switch (prop_id) {
        case PROP_ORIENTATION:
        g_value_set_enum(value, priv->orientation);
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static gboolean
gwy_selection_graph_line_crop_object(GwySelection *selection,
                                     gint i,
                                     gpointer user_data)
{
    const gdouble *minmax = (const gdouble*)user_data;
    GwySelectionGraphLinePriv *priv;
    gdouble xy[OBJECT_SIZE];

    priv = G_TYPE_INSTANCE_GET_PRIVATE(selection,
                                       GWY_TYPE_SELECTION_GRAPH_LINE,
                                       GwySelectionGraphLinePriv);

    gwy_selection_get_object(selection, i, xy);
    if (priv->orientation == GWY_ORIENTATION_VERTICAL)
        return xy[0] >= minmax[0] && xy[0] <= minmax[2];
    else
        return xy[0] >= minmax[1] && xy[0] <= minmax[3];
}

static void
gwy_selection_graph_line_crop(GwySelection *selection,
                              gdouble xmin,
                              gdouble ymin,
                              gdouble xmax,
                              gdouble ymax)
{
    gdouble minmax[4] = { xmin, ymin, xmax, ymax };

    gwy_selection_filter(selection, gwy_selection_graph_line_crop_object,
                         minmax);
}

static void
gwy_selection_graph_line_move(GwySelection *selection,
                              gdouble vx,
                              gdouble vy)
{
    GwySelectionGraphLinePriv *priv;
    gdouble *data = (gdouble*)selection->objects->data;
    guint i, n = selection->objects->len/OBJECT_SIZE;

    priv = G_TYPE_INSTANCE_GET_PRIVATE(selection,
                                       GWY_TYPE_SELECTION_GRAPH_LINE,
                                       GwySelectionGraphLinePriv);
    if (priv->orientation == GWY_ORIENTATION_VERTICAL) {
        for (i = 0; i < n; i++)
            data[OBJECT_SIZE*i] += vy;
    }
    else {
        for (i = 0; i < n; i++)
            data[OBJECT_SIZE*i] += vx;
    }
}

static GByteArray*
gwy_selection_graph_line_serialize(GObject *serializable,
                                   GByteArray *buffer)
{
    GwySelection *selection;
    GwySelectionGraphLinePriv *priv;

    g_return_val_if_fail(GWY_IS_SELECTION_GRAPH_LINE(serializable), NULL);

    selection = GWY_SELECTION(serializable);
    priv = G_TYPE_INSTANCE_GET_PRIVATE(selection,
                                       GWY_TYPE_SELECTION_GRAPH_LINE,
                                       GwySelectionGraphLinePriv);

    {
        guint32 len = selection->n * OBJECT_SIZE;
        guint32 max = selection->objects->len/OBJECT_SIZE;
        guint32 orientation = priv->orientation;
        gpointer pdata = len ? &selection->objects->data : NULL;
        GwySerializeSpec spec[] = {
            { 'i', "max", &max, NULL, },
            { 'i', "orientation", &orientation, NULL, },
            { 'D', "data", pdata, &len, },
        };

        return gwy_serialize_pack_object_struct(buffer,
                                                GWY_SELECTION_GRAPH_LINE_TYPE_NAME,
                                                G_N_ELEMENTS(spec), spec);
    }
}

static GObject*
gwy_selection_graph_line_deserialize(const guchar *buffer,
                                     gsize size,
                                     gsize *position)
{
    gdouble *data = NULL;
    guint32 len = 0, max = 0, orientation = GWY_ORIENTATION_HORIZONTAL;
    GwySerializeSpec spec[] = {
        { 'i', "max", &max, NULL },
        { 'i', "orientation", &orientation, NULL, },
        { 'D', "data", &data, &len, },
    };
    GwySelection *selection;
    GwySelectionGraphLinePriv *priv;

    g_return_val_if_fail(buffer, NULL);

    if (!gwy_serialize_unpack_object_struct(buffer, size, position,
                                            GWY_SELECTION_GRAPH_LINE_TYPE_NAME,
                                            G_N_ELEMENTS(spec), spec)) {
        g_free(data);
        return NULL;
    }

    selection = g_object_new(GWY_TYPE_SELECTION_GRAPH_LINE, NULL);
    priv = G_TYPE_INSTANCE_GET_PRIVATE(selection,
                                       GWY_TYPE_SELECTION_GRAPH_LINE,
                                       GwySelectionGraphLinePriv);
    priv->orientation = orientation;
    g_array_set_size(selection->objects, 0);
    if (data && len) {
        if (len % OBJECT_SIZE)
            g_warning("Selection data size not multiple of object size. "
                      "Ignoring it.");
        else {
            g_array_append_vals(selection->objects, data, len);
            selection->n = len/OBJECT_SIZE;
        }
        g_free(data);
    }
    if (max > selection->n)
        g_array_set_size(selection->objects, max*OBJECT_SIZE);

    return (GObject*)selection;
}

static GObject*
gwy_selection_graph_line_duplicate(GObject *object)
{
    GObject *copy;
    GwySelectionGraphLinePriv *priv, *copypriv;

    copy = gwy_selection_graph_line_serializable_parent_iface->duplicate(object);
    priv = G_TYPE_INSTANCE_GET_PRIVATE(object,
                                       GWY_TYPE_SELECTION_GRAPH_LINE,
                                       GwySelectionGraphLinePriv);
    copypriv = G_TYPE_INSTANCE_GET_PRIVATE(copy,
                                           GWY_TYPE_SELECTION_GRAPH_LINE,
                                           GwySelectionGraphLinePriv);
    copypriv->orientation = priv->orientation;

    return copy;
}

static void
gwy_selection_graph_line_clone(GObject *source,
                               GObject *copy)
{
    GwySelectionGraphLinePriv *srcpriv, *copypriv;

    srcpriv = G_TYPE_INSTANCE_GET_PRIVATE(source,
                                          GWY_TYPE_SELECTION_GRAPH_LINE,
                                          GwySelectionGraphLinePriv);
    copypriv = G_TYPE_INSTANCE_GET_PRIVATE(copy,
                                           GWY_TYPE_SELECTION_GRAPH_LINE,
                                           GwySelectionGraphLinePriv);
    copypriv->orientation = srcpriv->orientation;
    /* Must do this at the end, it emits a signal. */
    gwy_selection_graph_line_serializable_parent_iface->clone(source, copy);
}

static void
gwy_selection_graph_line_set_orientation(GwySelectionGraphLine *selection,
                                         GwyOrientation orientation)
{
    GwySelectionGraphLinePriv *priv;

    g_return_if_fail(orientation == GWY_ORIENTATION_HORIZONTAL
                     || orientation == GWY_ORIENTATION_VERTICAL);

    priv = G_TYPE_INSTANCE_GET_PRIVATE(selection,
                                       GWY_TYPE_SELECTION_GRAPH_LINE,
                                       GwySelectionGraphLinePriv);
    if (orientation == priv->orientation)
        return;

    gwy_selection_clear(GWY_SELECTION(selection));
    priv->orientation = orientation;
    g_object_notify(G_OBJECT(selection), "orientation");
}

/**
 * gwy_selection_graph_line_new:
 *
 * Creates a new line-wise graph selection.
 *
 * Returns: A new selection object.
 *
 * Since: 2.1
 **/
GwySelection*
gwy_selection_graph_line_new(void)
{
    return (GwySelection*)g_object_new(GWY_TYPE_SELECTION_GRAPH_LINE, NULL);
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyselectiongraphline
 * @title: GwySelectionGraphLine
 * @short_description: Single coordinate graph selection
 *
 * #GwySelectionGraphLine is used to represent signle coodinate (@x or @y)
 * graph selections.  Selection data consists of signle coordinates.
 *
 * If you obtain the selection from a graph widget it has the "orientation"
 * property set for information.  The orientation should be kept intact in this
 * case as changing it is not meaningful.  The graph keeps two distinct
 * horizontal and vertical selection objects.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
