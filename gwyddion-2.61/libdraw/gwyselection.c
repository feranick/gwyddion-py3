/*
 *  $Id: gwyselection.c 21680 2018-11-26 10:39:39Z yeti-dn $
 *  Copyright (C) 2003-2018 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyserializable.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libdraw/gwyselection.h>


enum {
    PROP_0,
    PROP_OBJECT_SIZE,
    PROP_MAX_OBJECTS
};

enum {
    CHANGED,
    FINISHED,
    LAST_SIGNAL
};

static void        gwy_selection_finalize               (GObject *object);
static void        gwy_selection_set_property           (GObject *object,
                                                         guint prop_id,
                                                         const GValue *value,
                                                         GParamSpec *pspec);
static void        gwy_selection_get_property           (GObject*object,
                                                         guint prop_id,
                                                         GValue *value,
                                                         GParamSpec *pspec);
static void        gwy_selection_serializable_init      (GwySerializableIface *iface);
static void        gwy_selection_clear_default          (GwySelection *selection);
static gboolean    gwy_selection_get_object_default     (GwySelection *selection,
                                                         gint i,
                                                         gdouble *data);
static gint        gwy_selection_set_object_default     (GwySelection *selection,
                                                         gint i,
                                                         const gdouble *data);
static void        gwy_selection_delete_object_default  (GwySelection *selection,
                                                         gint i);
static gint        gwy_selection_get_data_default       (GwySelection *selection,
                                                         gdouble *data);
static void        gwy_selection_set_data_default       (GwySelection *selection,
                                                         gint nselected,
                                                         const gdouble *data);
static void        gwy_selection_set_max_objects_default(GwySelection *selection,
                                                         guint max_objects);
static void        gwy_selection_crop_default           (GwySelection *selection,
                                                         gdouble xmin,
                                                         gdouble ymin,
                                                         gdouble xmax,
                                                         gdouble ymax);
static void        gwy_selection_move_default           (GwySelection *selection,
                                                         gdouble vx,
                                                         gdouble vy);
static GByteArray* gwy_selection_serialize_default      (GObject *obj,
                                                         GByteArray *buffer);
static gsize       gwy_selection_get_size_default       (GObject *obj);
static GObject*    gwy_selection_deserialize_default    (const guchar *buffer,
                                                         gsize size,
                                                         gsize *position);
static GObject*    gwy_selection_duplicate_default      (GObject *object);
static void        gwy_selection_clone_default          (GObject *source,
                                                         GObject *copy);

static guint selection_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_EXTENDED
    (GwySelection, gwy_selection, G_TYPE_OBJECT, G_TYPE_FLAG_ABSTRACT,
     GWY_IMPLEMENT_SERIALIZABLE(gwy_selection_serializable_init))

static void
gwy_selection_class_init(GwySelectionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_selection_finalize;
    gobject_class->get_property = gwy_selection_get_property;
    gobject_class->set_property = gwy_selection_set_property;

    klass->clear = gwy_selection_clear_default;
    klass->get_object = gwy_selection_get_object_default;
    klass->set_object = gwy_selection_set_object_default;
    klass->delete_object = gwy_selection_delete_object_default;
    klass->get_data = gwy_selection_get_data_default;
    klass->set_data = gwy_selection_set_data_default;
    klass->set_max_objects = gwy_selection_set_max_objects_default;
    klass->crop = gwy_selection_crop_default;
    klass->move = gwy_selection_move_default;

    g_object_class_install_property
        (gobject_class,
         PROP_OBJECT_SIZE,
         g_param_spec_uint("object-size",
                           "Object size",
                           "Number of coordinates in one selection object",
                           0, 1024, 0, G_PARAM_READABLE));

    g_object_class_install_property
        (gobject_class,
         PROP_MAX_OBJECTS,
         g_param_spec_uint("max-objects",
                           "Max. objects",
                           "Maximum number of objects that can be selected",
                           0, 65536, 1, G_PARAM_READWRITE));

    /**
     * GwySelection::changed:
     * @gwyselection: The #GwySelection which received the signal.
     * @arg1: Changed object position hint.  If the value is nonnegative, only
     *        this object has changed.  If it's negative, the selection has
     *        to be treated as completely changed.
     *
     * The ::changed signal is emitted whenever selection changes.
     **/
    selection_signals[CHANGED]
        = g_signal_new("changed",
                       G_OBJECT_CLASS_TYPE(gobject_class),
                       G_SIGNAL_RUN_FIRST,
                       G_STRUCT_OFFSET(GwySelectionClass, changed),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__INT,
                       G_TYPE_NONE, 1, G_TYPE_INT);

    /**
     * GwySelection::finished:
     * @gwyselection: The #GwySelection which received the signal.
     *
     * The ::finished signal is emitted when selection is finished.
     *
     * What exactly finished means is defined by corresponding
     * #GwyVectorLayer, but normally it involves user stopped changing
     * a selection object. Selections never emit this signal themselves.
     **/
    selection_signals[FINISHED]
        = g_signal_new("finished",
                       G_OBJECT_CLASS_TYPE(gobject_class),
                       G_SIGNAL_RUN_FIRST,
                       G_STRUCT_OFFSET(GwySelectionClass, finished),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__VOID,
                       G_TYPE_NONE, 0);
}

static void
gwy_selection_serializable_init(GwySerializableIface *iface)
{
    iface->serialize = gwy_selection_serialize_default;
    iface->deserialize = gwy_selection_deserialize_default;
    iface->get_size = gwy_selection_get_size_default;
    iface->duplicate = gwy_selection_duplicate_default;
    iface->clone = gwy_selection_clone_default;
}

static void
gwy_selection_init(GwySelection *selection)
{
    selection->objects = g_array_new(FALSE, FALSE, sizeof(gdouble));
}

static void
gwy_selection_finalize(GObject *object)
{
    GwySelection *selection = (GwySelection*)object;

    g_array_free(selection->objects, TRUE);
    G_OBJECT_CLASS(gwy_selection_parent_class)->finalize(object);
}

static void
gwy_selection_set_property(GObject *object,
                           guint prop_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
    GwySelection *selection = GWY_SELECTION(object);

    switch (prop_id) {
        case PROP_MAX_OBJECTS:
        gwy_selection_set_max_objects(selection, g_value_get_uint(value));
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_selection_get_property(GObject *object,
                           guint prop_id,
                           GValue *value,
                           GParamSpec *pspec)
{
    GwySelection *selection = GWY_SELECTION(object);

    switch (prop_id) {
        case PROP_MAX_OBJECTS:
        g_value_set_uint(value, gwy_selection_get_max_objects(selection));
        break;

        case PROP_OBJECT_SIZE:
        g_value_set_uint(value, gwy_selection_get_object_size(selection));
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/**
 * gwy_selection_get_object_size:
 * @selection: A selection.
 *
 * Gets the number of coordinates that make up a one selection object.
 *
 * Returns: The number of coordinates in one selection object.
 **/
guint
gwy_selection_get_object_size(GwySelection *selection)
{
    g_return_val_if_fail(GWY_IS_SELECTION(selection), 0);
    return GWY_SELECTION_GET_CLASS(selection)->object_size;
}

/**
 * gwy_selection_clear:
 * @selection: A selection.
 *
 * Clears a selection.
 **/
void
gwy_selection_clear(GwySelection *selection)
{
    g_return_if_fail(GWY_IS_SELECTION(selection));
    GWY_SELECTION_GET_CLASS(selection)->clear(selection);
}

/**
 * gwy_selection_crop:
 * @selection: A selection.
 * @xmin: Minimum x-coordinate.
 * @ymin: Minimum y-coordinate.
 * @xmax: Maximum x-coordinate.
 * @ymax: Maximum y-coordinate.
 *
 * Limits objects in a selection to a rectangle.
 *
 * Objects that are fully outside specified rectangle are removed.  Objects
 * partially outside may be removed or cut, depending on what makes sense for
 * the specific selection type.  If the selection class does not implement this
 * method then all objects are removed.
 *
 * Since: 2.16
 **/
void
gwy_selection_crop(GwySelection *selection,
                   gdouble xmin,
                   gdouble ymin,
                   gdouble xmax,
                   gdouble ymax)
{
    g_return_if_fail(GWY_IS_SELECTION(selection));
    GWY_SELECTION_GET_CLASS(selection)->crop(selection, xmin, ymin, xmax, ymax);
}

/**
 * gwy_selection_move:
 * @selection: A selection.
 * @vx: Value to add to all x-coordinates.
 * @vy: Value to add to all y-coordinates.
 *
 * Moves entire selection in plane by given vector.
 *
 * If a selection class does not implement this operation the selection remains
 * unchanged.  Bult-in selection classes generally implement this operation if
 * it is meaningful.  For some, such as GwySelectionLattice, it is not
 * meaningful and moving GwySelectionLattice thus does not do anything.
 *
 * Since: 2.43
 **/
void
gwy_selection_move(GwySelection *selection,
                   gdouble vx,
                   gdouble vy)
{
    g_return_if_fail(GWY_IS_SELECTION(selection));
    GWY_SELECTION_GET_CLASS(selection)->move(selection, vx, vy);
}

/**
 * gwy_selection_filter:
 * @selection: A selection.
 * @filter: Function returning %TRUE for objects that should be kept, %FALSE
 *          for objects that should be removed.
 * @data: User data passed to @filter.
 *
 * Removes selection objects matching certain criteria.
 *
 * Since: 2.16
 **/
void
gwy_selection_filter(GwySelection *selection,
                     GwySelectionFilterFunc filter,
                     gpointer data)
{
    GwySelection *sel;
    guint len, i, object_size;
    gdouble *xy;

    /* Be careful to work with non-default implementations.  Do not assume we
     * know much about the internal structure. */
    sel = gwy_selection_duplicate(selection);
    gwy_selection_clear(sel);
    len = gwy_selection_get_data(selection, NULL);
    object_size = gwy_selection_get_object_size(selection);
    xy = g_newa(gdouble, object_size);
    for (i = 0; i < len; i++) {
        if (filter(selection, i, data)) {
            gwy_selection_get_object(selection, i, xy);
            gwy_selection_set_object(sel, -1, xy);
        }
    }
    /* This is the only place we emit a signal on @selection. */
    gwy_serializable_clone(G_OBJECT(sel), G_OBJECT(selection));
    g_object_unref(sel);
}

/**
 * gwy_selection_get_object:
 * @selection: A selection.
 * @i: Index of object to get.
 * @data: Array to store selection object data to.  Object data is an
 *        array of coordinates whose precise meaning is defined by particular
 *        selection types.
 *
 * Gets one selection object.
 *
 * Returns: %TRUE if there was such an object and @data was filled.
 **/
gboolean
gwy_selection_get_object(GwySelection *selection,
                         gint i,
                         gdouble *data)
{
    g_return_val_if_fail(GWY_IS_SELECTION(selection), FALSE);
    return GWY_SELECTION_GET_CLASS(selection)->get_object(selection, i, data);
}

/**
 * gwy_selection_set_object:
 * @selection: A selection.
 * @i: Index of object to set.
 * @data: Object selection data.  It's an array of coordinates whose precise
 *        meaning is defined by particular selection types.
 *
 * Sets one selection object.
 *
 * This method can be also used to append objects (if the maximum number is
 * not exceeded).  Since there cannot be holes in the object list, @i must be
 * then equal to either the number of selected objects or special value -1
 * meaning append to end.
 *
 * Returns: The index of actually set object (useful namely when @i is -1).
 **/
gint
gwy_selection_set_object(GwySelection *selection,
                         gint i,
                         const gdouble *data)
{
    g_return_val_if_fail(GWY_IS_SELECTION(selection), -1);
    return GWY_SELECTION_GET_CLASS(selection)->set_object(selection, i, data);
}

/**
 * gwy_selection_delete_object:
 * @selection: A selection.
 * @i: Index of object to delete.
 *
 * Deletes a one selection object.
 *
 * Since there cannot be holes in the object list, the rest of selection
 * objects is moved to close the gap.
 **/
void
gwy_selection_delete_object(GwySelection *selection,
                            gint i)
{
    g_return_if_fail(GWY_IS_SELECTION(selection));
    GWY_SELECTION_GET_CLASS(selection)->delete_object(selection, i);
}

/**
 * gwy_selection_get_data:
 * @selection: A selection.
 * @data: Array to store selection data to.  Selection data is an
 *        array of coordinates whose precise meaning is defined by particular
 *        selection types.  It may be %NULL.
 *
 * Gets selection data.
 *
 * Returns: The number of selected objects.  This is *not* the required size
 *          of @data, which must be at least gwy_selection_get_object_size()
 *          times larger.
 **/
gint
gwy_selection_get_data(GwySelection *selection,
                       gdouble *data)
{
    g_return_val_if_fail(GWY_IS_SELECTION(selection), 0);
    return GWY_SELECTION_GET_CLASS(selection)->get_data(selection, data);
}

/**
 * gwy_selection_set_data:
 * @selection: A selection.
 * @nselected: The number of selected objects.
 * @data: Selection data, that is an array @nselected *
 *        gwy_selection_get_object_size() long with selected object
 *        coordinates.
 *
 * Sets selection data.
 **/
void
gwy_selection_set_data(GwySelection *selection,
                       gint nselected,
                       const gdouble *data)
{
    g_return_if_fail(GWY_IS_SELECTION(selection));
    GWY_SELECTION_GET_CLASS(selection)->set_data(selection, nselected, data);
}

/**
 * gwy_selection_get_max_objects:
 * @selection: A selection.
 *
 * Gets the maximum number of selected objects.
 *
 * Returns: The maximum number of selected objects;
 **/
guint
gwy_selection_get_max_objects(GwySelection *selection)
{
    guint object_size;

    g_return_val_if_fail(GWY_IS_SELECTION(selection), 0);
    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    return selection->objects->len/object_size;
}

/**
 * gwy_selection_set_max_objects:
 * @selection: A selection.
 * @max_objects: The maximum number of objects allowed to select.  Note
 *               particular selection types may allow only specific values.
 *
 * Sets the maximum number of objects allowed to select.
 *
 * When selection reaches this number of selected objects, it emits
 * "finished" signal.
 **/
void
gwy_selection_set_max_objects(GwySelection *selection,
                              guint max_objects)
{
    g_return_if_fail(GWY_IS_SELECTION(selection));
    GWY_SELECTION_GET_CLASS(selection)->set_max_objects(selection, max_objects);
}

/**
 * gwy_selection_is_full:
 * @selection: A selection.
 *
 * Checks whether the maximum number of objects is selected.
 *
 * Returns: %TRUE when the maximum possible number of objects is selected,
 *          %FALSE otherwise.
 **/
gboolean
gwy_selection_is_full(GwySelection *selection)
{
    guint object_size;

    g_return_val_if_fail(GWY_IS_SELECTION(selection), FALSE);
    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    return selection->n == selection->objects->len/object_size;
}

/**
 * gwy_selection_changed:
 * @selection: A selection.
 * @i: Index of object that changed.  Use -1 when not applicable, e.g., when
 *     complete selection was changed, cleared, or truncated.
 *
 * Emits "changed" signal on a selection.
 **/
void
gwy_selection_changed(GwySelection *selection,
                      gint i)
{
    g_return_if_fail(GWY_IS_SELECTION(selection));
    g_signal_emit(selection, selection_signals[CHANGED], 0, i);
}

/**
 * gwy_selection_finished:
 * @selection: A selection.
 *
 * Emits "finished" signal on a selection.
 **/
void
gwy_selection_finished(GwySelection *selection)
{
    g_return_if_fail(GWY_IS_SELECTION(selection));
    g_signal_emit(selection, selection_signals[FINISHED], 0);
}

static void
gwy_selection_clear_default(GwySelection *selection)
{
    if (!selection->n)
        return;

    selection->n = 0;
    g_signal_emit(selection, selection_signals[CHANGED], 0, -1);
}

static gboolean
gwy_selection_get_object_default(GwySelection *selection,
                                 gint i,
                                 gdouble *data)
{
    guint object_size;

    if (i < 0 || i >= selection->n)
        return FALSE;
    if (!data)
        return TRUE;

    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    gwy_assign(data, (gdouble*)selection->objects->data + i*object_size,
               object_size);
    return TRUE;
}

static gint
gwy_selection_set_object_default(GwySelection *selection,
                                 gint i,
                                 const gdouble *data)
{
    guint object_size, max_len;

    gwy_debug("%p: setting object %d, n=%d", selection, i, selection->n);
    if (i < 0)
        i = selection->n;
    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    max_len = selection->objects->len/object_size;
    g_return_val_if_fail(i < max_len, -1);
    if (i > selection->n) {
        g_warning("Disontinuous selections are not supported.  "
                  "Moving object to first feasible position.");
        i = MIN(selection->n, max_len-1);
    }

    selection->n = MAX(selection->n, i+1);
    gwy_assign((gdouble*)selection->objects->data + i*object_size, data,
               object_size);

    g_signal_emit(selection, selection_signals[CHANGED], 0, i);
    return i;
}

static void
gwy_selection_delete_object_default(GwySelection *selection,
                                    gint i)
{
    guint object_size, len;

    g_return_if_fail(i >= 0 && i < selection->n);
    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    len = selection->objects->len;
    g_array_remove_range(selection->objects, i*object_size, object_size);
    g_array_set_size(selection->objects, len);
    selection->n--;

    g_signal_emit(selection, selection_signals[CHANGED], 0, -1);
}

static gint
gwy_selection_get_data_default(GwySelection *selection,
                               gdouble *data)
{
    guint object_size;

    if (data && selection->n) {
        object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
        gwy_assign(data, selection->objects->data, selection->n*object_size);
    }

    return selection->n;
}

static void
gwy_selection_set_data_default(GwySelection *selection,
                               gint nselected,
                               const gdouble *data)
{
    guint object_size, max_len;

    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    max_len = selection->objects->len/object_size;
    if (nselected > max_len) {
        g_warning("nselected larger than max. number of objects");
        nselected = max_len;
    }

    if (nselected) {
        g_return_if_fail(data);
        gwy_assign((gdouble*)selection->objects->data, data,
                   nselected*object_size);
    }
    selection->n = nselected;
    g_signal_emit(selection, selection_signals[CHANGED], 0, -1);
}

static void
gwy_selection_set_max_objects_default(GwySelection *selection,
                                      guint max_objects)
{
    guint object_size;

    gwy_debug("%d", max_objects);
    g_return_if_fail(max_objects >= 1);
    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    if (max_objects*object_size == selection->objects->len)
        return;

    g_array_set_size(selection->objects, max_objects*object_size);

    if (max_objects < selection->n) {
        selection->n = max_objects;
        g_object_notify(G_OBJECT(selection), "max-objects");
        g_signal_emit(selection, selection_signals[CHANGED], 0, -1);
    }
    else
        g_object_notify(G_OBJECT(selection), "max-objects");
}

static void
gwy_selection_crop_default(GwySelection *selection,
                           G_GNUC_UNUSED gdouble xmin,
                           G_GNUC_UNUSED gdouble ymin,
                           G_GNUC_UNUSED gdouble xmax,
                           G_GNUC_UNUSED gdouble ymax)
{
    /* If the selection class does not implement crop, we have to remove all
     * objects. */
    gwy_selection_clear(selection);
}

static void
gwy_selection_move_default(G_GNUC_UNUSED GwySelection *selection,
                           G_GNUC_UNUSED gdouble vx,
                           G_GNUC_UNUSED gdouble vy)
{
    /* If the selection class does not implement move we do nothing. */
}

static GByteArray*
gwy_selection_serialize_default(GObject *obj,
                                GByteArray *buffer)
{
    GwySelection *selection;
    gint object_size;

    g_return_val_if_fail(GWY_IS_SELECTION(obj), NULL);

    selection = GWY_SELECTION(obj);
    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    {
        guint32 len = selection->n * object_size;
        guint32 max = selection->objects->len/object_size;
        gpointer pdata = len ? &selection->objects->data : NULL;
        const gchar *name = G_OBJECT_TYPE_NAME(obj);
        GwySerializeSpec spec[] = {
            { 'i', "max", &max, NULL, },
            { 'D', "data", pdata, &len, },
        };

        return gwy_serialize_pack_object_struct(buffer, name,
                                                G_N_ELEMENTS(spec), spec);
    }
}

static gsize
gwy_selection_get_size_default(GObject *obj)
{
    GwySelection *selection;
    gint object_size;

    g_return_val_if_fail(GWY_IS_SELECTION(obj), 0);

    selection = GWY_SELECTION(obj);
    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    {
        guint32 len = selection->n * object_size;
        guint32 max = selection->objects->len/object_size;
        gpointer pdata = len ? &selection->objects->data : NULL;
        const gchar *name = G_OBJECT_TYPE_NAME(obj);
        GwySerializeSpec spec[] = {
            { 'i', "max", &max, NULL, },
            { 'D', "data", pdata, &len, },
        };

        return gwy_serialize_get_struct_size(name, G_N_ELEMENTS(spec), spec);
    }
}

static GObject*
gwy_selection_deserialize_default(const guchar *buffer,
                                  gsize size,
                                  gsize *position)
{
    gdouble *data = NULL;
    guint32 len = 0, max = 0;
    GwySerializeSpec spec[] = {
        { 'i', "max", &max, NULL },
        { 'D', "data", &data, &len, },
    };
    gsize typenamesize;
    GType type;
    gint object_size;
    const gchar *typename;
    GwySelection *selection;

    g_return_val_if_fail(buffer, NULL);

    typenamesize = gwy_serialize_check_string(buffer, size, *position, NULL);
    if (!typenamesize)
        return NULL;
    typename = (const gchar*)(buffer + *position);

    if (!(type = g_type_from_name(typename))
        || !g_type_is_a(type, GWY_TYPE_SELECTION)
        || !G_TYPE_IS_INSTANTIATABLE(type))
        return NULL;

    if (!gwy_serialize_unpack_object_struct(buffer, size, position, typename,
                                            G_N_ELEMENTS(spec), spec)) {
        g_free(data);
        return NULL;
    }

    selection = g_object_new(type, NULL);
    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    g_array_set_size(selection->objects, 0);
    if (data && len) {
        if (len % object_size)
            g_warning("Selection data size not multiple of object size. "
                      "Ignoring it.");
        else {
            g_array_append_vals(selection->objects, data, len);
            selection->n = len/object_size;
        }
        g_free(data);
    }
    if (max > selection->n)
        g_array_set_size(selection->objects, max*object_size);

    return (GObject*)selection;
}

static GObject*
gwy_selection_duplicate_default(GObject *object)
{
    GwySelection *selection, *duplicate;
    guint object_size;

    g_return_val_if_fail(GWY_IS_SELECTION(object), NULL);
    selection = GWY_SELECTION(object);
    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;
    duplicate = g_object_new(G_TYPE_FROM_INSTANCE(object), NULL);
    g_array_set_size(duplicate->objects, 0);
    g_array_append_vals(duplicate->objects,
                        selection->objects->data, selection->n*object_size);
    duplicate->n = selection->n;
    g_array_set_size(duplicate->objects, selection->objects->len);

    return (GObject*)duplicate;
}

static void
gwy_selection_clone_default(GObject *source, GObject *copy)
{
    GwySelection *selection, *clone;
    gint object_size;

    g_return_if_fail(GWY_IS_SELECTION(source));
    g_return_if_fail(GWY_IS_SELECTION(copy));
    /* is-a relation is cheched by gwy_serlizable_clone() */

    selection = GWY_SELECTION(source);
    clone = GWY_SELECTION(copy);
    object_size = GWY_SELECTION_GET_CLASS(selection)->object_size;

    g_array_set_size(clone->objects, 0);
    g_array_append_vals(clone->objects,
                        selection->objects->data, selection->n*object_size);
    clone->n = selection->n;
    g_array_set_size(clone->objects, selection->objects->len);

    g_signal_emit(clone, selection_signals[CHANGED], 0, -1);
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyselection
 * @title: GwySelection
 * @short_description: Data selection base class
 * @see_also: #GwyVectorLayer -- uses #GwySelection for selections,
 *            #GwyGraphArea -- uses #GwySelection for selections
 *
 * #GwySelection is an abstract class representing data selections.  Particular
 * selection types are defined by vector layer modules.
 *
 * Selections behave as flat arrays of coordinates.  They are however logically
 * split into selection objects (points, lines, rectangles), characteristic for
 * each selection type. For example, to describe a horizontal line one needs
 * only one coordinate, for a point two coordinates are needed, rectangle or
 * arbitrary line need four.  gwy_selection_get_object_size() can be used to
 * generically determine the number of coordinates used to describe a one
 * selection object.
 *
 * The number of selection objects in a selection can vary,
 * gwy_selection_set_max_objects() sets the maximum possible number.  Functions
 * for getting and setting individual selection objects
 * (gwy_selection_get_object(), gwy_selection_set_object()) or complete
 * selection (gwy_selection_get_data(), gwy_selection_set_data()) are
 * available.  The method gwy_selection_set_data() with %NULL second argument
 * is also used to determine the number of selected object.
 **/

/**
 * GwySelection:
 * @objects: Array of object coordinates whose meaning is defined by each
 *           selection type (subclass).  Default #GwySelection virtual methods
 *           assume each selection object is of the same size, stored in
 *           class @object_size field at selection class init time.  The size
 *           of array (multiplied with @object_size) determines maximum number
 *           of selectable objects.
 * @n: The number of actually selected objects.
 *
 * The #GwySelection struct describes an abstract selection as a collection
 * of coordinates.  It should not be accessed directly except selection
 * class implementation.
 **/

/**
 * GwySelectionClass:
 * @object_size: The number of coordinates that form one selection object.
 * @clear: The gwy_selection_clear() virtual method.
 * @get_object: The gwy_selection_get_object() virtual method.
 * @set_object: The gwy_selection_set_object() virtual method.
 * @delete_object: The gwy_selection_delete_object() virtual method.
 * @get_data: The gwy_selection_get_data() virtual method.
 * @set_data: The gwy_selection_set_data() virtual method.
 * @set_max_objects: The gwy_selection_set_max_objects() virtual method.
 * @changed: The "changed" signal virtual method.
 * @finished: The "finished" signal virtual method.
 *
 * The virtual methods and data memebers of #GwySelection<!-- -->s.
 *
 * Typically, the only field subclasses set in their class init method is
 * @object_size.  The methods are implemented generically in #GwySelection
 * and need not be overriden.
 **/

/**
 * gwy_selection_duplicate:
 * @selection: An selection to duplicate.
 *
 * Convenience macro doing gwy_serializable_duplicate() with all the necessary
 * typecasting.
 **/

/**
 * GwySelectionFilterFunc:
 * @selection: A selection.
 * @i: Index of object to consider.
 * @data: User data passed to gwy_selection_filter().
 *
 * Type of selection filtering function.
 *
 * Returns: %TRUE for objects that should be kept, %FALSE for objects that
 *          should be removed.
 *
 * Since: 2.16
 **/

/**
 * gwy_selection_assign:
 * @dest: Target selection.
 * @source: Source selection.
 *
 * Convenience macro making one selection identical to another.
 *
 * This is just a gwy_serializable_clone() wrapper with all the necessary
 * typecasting.
 *
 * Since: 2.52
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
