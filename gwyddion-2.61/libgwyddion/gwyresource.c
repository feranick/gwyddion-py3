/*
 *  $Id: gwyresource.c 21692 2018-11-26 13:29:49Z yeti-dn $
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

#include "config.h"
#include <string.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyenum.h>
#include <libgwyddion/gwyinventory.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libgwyddion/gwyresource.h>

#define MAGIC_HEADER "Gwyddion resource "

enum {
    DATA_CHANGED,
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_NAME,
    PROP_IS_CONST,
    PROP_IS_PREFERRED,
    PROP_LAST
};

/* Use a static propery -> trait map.  We could do it generically, too.
 * g_param_spec_pool_list() is ugly and slow is the minor problem, the major
 * is that it does g_hash_table_foreach() so we would get different orders
 * on different invocations and have to sort it. */
/* Threads: only modified in class init functon.  */
static GType gwy_resource_trait_types[PROP_LAST-1];          /* Threads: OK */
static const gchar *gwy_resource_trait_names[PROP_LAST-1];   /* Threads: OK */
static guint gwy_resource_ntraits = 0;                       /* Threads: OK */

static void         gwy_resource_finalize       (GObject *object);
static void         gwy_resource_set_property   (GObject *object,
                                                 guint prop_id,
                                                 const GValue *value,
                                                 GParamSpec *pspec);
static void         gwy_resource_get_property   (GObject *object,
                                                 guint prop_id,
                                                 GValue *value,
                                                 GParamSpec *pspec);
static gboolean     gwy_resource_get_is_const   (gconstpointer item);
static const gchar* gwy_resource_get_item_name  (gpointer item);
static gboolean     gwy_resource_compare        (gconstpointer item1,
                                                 gconstpointer item2);
static void         gwy_resource_rename_impl    (gpointer item,
                                                 const gchar *new_name);
static const GType* gwy_resource_get_traits     (gint *ntraits);
static const gchar* gwy_resource_get_trait_name (gint i);
static void         gwy_resource_get_trait_value(gpointer item,
                                                 gint i,
                                                 GValue *value);
static GwyResource* gwy_resource_parse_real     (const gchar *text,
                                                 GType expected_type,
                                                 gboolean is_const);
static void         gwy_resource_modified       (GwyResource *resource);
static void         gwy_resource_class_load_dir (const gchar *path,
                                                 GwyResourceClass *klass,
                                                 gboolean is_system);

static guint resource_signals[LAST_SIGNAL] = { 0 };

G_LOCK_DEFINE_STATIC(all_resources);
static GSList *all_resources = NULL;          /* Threads: protected by lock */

static const GwyInventoryItemType gwy_resource_item_type = {
    0,
    "data-changed",
    &gwy_resource_get_is_const,
    &gwy_resource_get_item_name,
    &gwy_resource_compare,
    &gwy_resource_rename_impl,
    NULL,
    NULL,  /* copy needs particular class */
    &gwy_resource_get_traits,
    &gwy_resource_get_trait_name,
    &gwy_resource_get_trait_value,
};

G_DEFINE_ABSTRACT_TYPE(GwyResource, gwy_resource, G_TYPE_OBJECT)

static void
gwy_resource_class_init(GwyResourceClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GParamSpec *pspec;

    gobject_class->finalize = gwy_resource_finalize;
    gobject_class->get_property = gwy_resource_get_property;
    gobject_class->set_property = gwy_resource_set_property;

    klass->item_type = gwy_resource_item_type;
    klass->item_type.type = G_TYPE_FROM_CLASS(klass);
    klass->data_changed = gwy_resource_modified;

    pspec = g_param_spec_string("name",
                                "Name",
                                "Resource name",
                                NULL, /* What is the default value good for? */
                                G_PARAM_READABLE);
    g_object_class_install_property(gobject_class, PROP_NAME, pspec);
    gwy_resource_trait_types[gwy_resource_ntraits] = pspec->value_type;
    gwy_resource_trait_names[gwy_resource_ntraits] = pspec->name;
    gwy_resource_ntraits++;

    pspec = g_param_spec_boolean("is-preferred",
                                 "Is preferred",
                                 "Whether a resource is preferred",
                                 FALSE,
                                 G_PARAM_READWRITE);
    g_object_class_install_property(gobject_class, PROP_IS_PREFERRED, pspec);
    gwy_resource_trait_types[gwy_resource_ntraits] = pspec->value_type;
    gwy_resource_trait_names[gwy_resource_ntraits] = pspec->name;
    gwy_resource_ntraits++;

    pspec = g_param_spec_boolean("is-const",
                                 "Is constant",
                                 "Whether a resource is constant (system)",
                                 FALSE,
                                 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property(gobject_class, PROP_IS_CONST, pspec);
    gwy_resource_trait_types[gwy_resource_ntraits] = pspec->value_type;
    gwy_resource_trait_names[gwy_resource_ntraits] = pspec->name;
    gwy_resource_ntraits++;

    /**
     * GwyResource::data-changed:
     * @gwyresource: The #GwyResource which received the signal.
     *
     * The ::data-changed signal is emitted when resource data changes.
     */
    resource_signals[DATA_CHANGED]
        = g_signal_new("data-changed",
                       G_OBJECT_CLASS_TYPE(gobject_class),
                       G_SIGNAL_RUN_FIRST,
                       G_STRUCT_OFFSET(GwyResourceClass, data_changed),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__VOID,
                       G_TYPE_NONE, 0);
}

static void
gwy_resource_init(GwyResource *resource)
{
    resource->name = g_string_new(NULL);
}

static void
gwy_resource_finalize(GObject *object)
{
    GwyResource *resource = (GwyResource*)object;

    gwy_debug("%s", resource->name->str);
    if (resource->use_count)
        g_critical("Resource %p with nonzero use_count is finalized.", object);
    g_string_free(resource->name, TRUE);

    G_OBJECT_CLASS(gwy_resource_parent_class)->finalize(object);
}

static void
gwy_resource_set_property(GObject *object,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
    GwyResource *resource = GWY_RESOURCE(object);

    switch (prop_id) {
        case PROP_IS_CONST:
        resource->is_const = g_value_get_boolean(value);
        break;

        case PROP_IS_PREFERRED:
        gwy_resource_set_is_preferred(resource, g_value_get_boolean(value));
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_resource_get_property(GObject *object,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
    GwyResource *resource = GWY_RESOURCE(object);

    switch (prop_id) {
        case PROP_NAME:
        g_value_set_string(value, resource->name->str);
        break;

        case PROP_IS_CONST:
        g_value_set_boolean(value, resource->is_const);
        break;

        case PROP_IS_PREFERRED:
        g_value_set_boolean(value, resource->is_preferred);
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static const gchar*
gwy_resource_get_item_name(gpointer item)
{
    GwyResource *resource = (GwyResource*)item;
    return resource->name->str;
}

static gboolean
gwy_resource_get_is_const(gconstpointer item)
{
    GwyResource *resource = (GwyResource*)item;
    return resource->is_const;
}

static gboolean
gwy_resource_compare(gconstpointer item1,
                     gconstpointer item2)
{
    GwyResource *resource1 = (GwyResource*)item1;
    GwyResource *resource2 = (GwyResource*)item2;

    return strcmp(resource1->name->str, resource2->name->str);
}

static void
gwy_resource_rename_impl(gpointer item,
                         const gchar *new_name)
{
    GwyResource *resource = (GwyResource*)item;

    g_return_if_fail(!resource->is_const);

    g_string_assign(resource->name, new_name);
    g_object_notify(G_OBJECT(item), "name");
}

static const GType*
gwy_resource_get_traits(gint *ntraits)
{
    if (ntraits)
        *ntraits = gwy_resource_ntraits;

    return gwy_resource_trait_types;
}

static const gchar*
gwy_resource_get_trait_name(gint i)
{
    g_return_val_if_fail(i >= 0 && i < gwy_resource_ntraits, NULL);
    return gwy_resource_trait_names[i];
}

static void
gwy_resource_get_trait_value(gpointer item,
                             gint i,
                             GValue *value)
{
    g_return_if_fail(i >= 0 && i < gwy_resource_ntraits);
    g_value_init(value, gwy_resource_trait_types[i]);
    g_object_get_property(G_OBJECT(item), gwy_resource_trait_names[i], value);
}

/**
 * gwy_resource_get_name:
 * @resource: A resource.
 *
 * Returns resource name.
 *
 * Returns: Name of @resource.  The string is owned by @resource and must not
 *          be modfied or freed.
 **/
const gchar*
gwy_resource_get_name(GwyResource *resource)
{
    g_return_val_if_fail(GWY_IS_RESOURCE(resource), NULL);
    return resource->name->str;
}

/**
 * gwy_resource_get_is_modifiable:
 * @resource: A resource.
 *
 * Returns whether a resource is modifiable.
 *
 * Returns: %TRUE if resource is modifiable, %FALSE if it's fixed (system)
 *          resource.
 **/
gboolean
gwy_resource_get_is_modifiable(GwyResource *resource)
{
    g_return_val_if_fail(GWY_IS_RESOURCE(resource), FALSE);
    return !resource->is_const;
}

/**
 * gwy_resource_get_is_preferred:
 * @resource: A resource.
 *
 * Returns whether a resource is preferred.
 *
 * Returns: %TRUE if resource is preferred, %FALSE otherwise.
 **/
gboolean
gwy_resource_get_is_preferred(GwyResource *resource)
{
    g_return_val_if_fail(GWY_IS_RESOURCE(resource), FALSE);
    return resource->is_preferred;
}

/**
 * gwy_resource_set_is_preferred:
 * @resource: A resource.
 * @is_preferred: %TRUE to make @resource preferred, %FALSE to make it not
 *                preferred.
 *
 * Sets preferability of a resource.
 **/
void
gwy_resource_set_is_preferred(GwyResource *resource,
                              gboolean is_preferred)
{
    g_return_if_fail(GWY_IS_RESOURCE(resource));
    resource->is_preferred = !!is_preferred;
    g_object_notify(G_OBJECT(resource), "is-preferred");
}

/**
 * gwy_resource_class_get_name:
 * @klass: A resource class.
 *
 * Gets the name of resource class.
 *
 * This is an simple identifier usable for example as directory name.
 *
 * Returns: Resource class name, as a constant string that must not be modified
 *          nor freed.
 **/
const gchar*
gwy_resource_class_get_name(GwyResourceClass *klass)
{
    g_return_val_if_fail(GWY_IS_RESOURCE_CLASS(klass), NULL);
    return klass->name;
}

/**
 * gwy_resource_class_get_inventory:
 * @klass: A resource class.
 *
 * Gets inventory which holds resources of a resource class.
 *
 * Returns: Resource class inventory.
 **/
GwyInventory*
gwy_resource_class_get_inventory(GwyResourceClass *klass)
{
    g_return_val_if_fail(GWY_IS_RESOURCE_CLASS(klass), NULL);
    return klass->inventory;
}

/**
 * gwy_resource_class_get_item_type:
 * @klass: A resource class.
 *
 * Gets inventory item type for a resource class.
 *
 * Returns: Inventory item type.
 **/
const GwyInventoryItemType*
gwy_resource_class_get_item_type(GwyResourceClass *klass)
{
    g_return_val_if_fail(GWY_IS_RESOURCE_CLASS(klass), NULL);
    return &klass->item_type;
}

/**
 * gwy_resource_use:
 * @resource: A resource.
 *
 * Starts using a resource.
 *
 * Call to this function is necessary to use a resource properly.
 * It makes the resource to create any auxiliary structures that consume
 * considerable amount of memory and perform other initialization to
 * ready-to-use form.
 *
 * When a resource is no longer used, it should be released with
 * gwy_resource_release().
 *
 * In addition, it calls g_object_ref() on the resource.
 *
 * Resources usually exist through almost whole program lifetime from
 * #GObject perspective, but from the viewpoint of use this method is the
 * constructor and gwy_resource_release() is the destructor.
 **/
void
gwy_resource_use(GwyResource *resource)
{
    g_return_if_fail(GWY_IS_RESOURCE(resource));
    gwy_debug("%s %p<%s> %d",
              G_OBJECT_TYPE_NAME(resource),
              resource, resource->name->str,
              resource->use_count);

    g_object_ref(resource);
    if (!resource->use_count++) {
        void (*method)(GwyResource*);

        method = GWY_RESOURCE_GET_CLASS(resource)->use;
        if (method)
            method(resource);
    }
}

/**
 * gwy_resource_release:
 * @resource: A resource.
 *
 * Releases a resource.
 *
 * When the number of resource uses drops to zero, it frees all auxiliary data
 * and returns back to `latent' form.  In addition, it calls g_object_unref()
 * on it.  See gwy_resource_use() for more.
 **/
void
gwy_resource_release(GwyResource *resource)
{
    g_return_if_fail(GWY_IS_RESOURCE(resource));
    gwy_debug("%s %p<%s> %d",
              G_OBJECT_TYPE_NAME(resource),
              resource, resource->name->str,
              resource->use_count);
    g_return_if_fail(resource->use_count);

    if (!--resource->use_count) {
        void (*method)(GwyResource*);

        method = GWY_RESOURCE_GET_CLASS(resource)->release;
        if (method)
            method(resource);
    }
    g_object_unref(resource);
}

/**
 * gwy_resource_is_used:
 * @resource: A resource.
 *
 * Tells whether a resource is currently in use.
 *
 * See gwy_resource_use() for details.
 *
 * Returns: %TRUE if resource is in use, %FALSE otherwise.
 **/
gboolean
gwy_resource_is_used(GwyResource *resource)
{
    g_return_val_if_fail(GWY_IS_RESOURCE(resource), FALSE);
    return resource->use_count > 0;
}

/**
 * gwy_resource_dump:
 * @resource: A resource.
 *
 * Dumps a resource to a textual (human readable) form.
 *
 * Returns: Textual resource representation.
 **/
GString*
gwy_resource_dump(GwyResource *resource)
{
    void (*method)(GwyResource*, GString*);
    GString *str;

    g_return_val_if_fail(GWY_IS_RESOURCE(resource), NULL);
    method = GWY_RESOURCE_GET_CLASS(resource)->dump;
    g_return_val_if_fail(method, NULL);

    str = g_string_new(MAGIC_HEADER);
    g_string_append(str, G_OBJECT_TYPE_NAME(resource));
    g_string_append_c(str, '\n');
    method(resource, str);

    return str;
}

/**
 * gwy_resource_parse:
 * @text: Textual resource representation.
 * @expected_type: Resource object type.  If not 0, only resources of give type
 *                 are allowed.  Zero value means any #GwyResource is allowed.
 *
 * Reconstructs a resource from human readable form.
 *
 * Returns: Newly created resource (or %NULL).
 **/
GwyResource*
gwy_resource_parse(const gchar *text,
                   GType expected_type)
{
    return gwy_resource_parse_real(text, expected_type, FALSE);
}

static GwyResource*
gwy_resource_parse_real(const gchar *text,
                        GType expected_type,
                        gboolean is_const)
{
    GwyResourceClass *klass;
    GwyResource *resource = NULL;
    GType type;
    gchar *name = NULL;
    guint len;

    if (!g_str_has_prefix(text, MAGIC_HEADER)) {
        g_warning("Wrong resource magic header");
        return NULL;
    }

    text += sizeof(MAGIC_HEADER) - 1;
    len = strspn(text, G_CSET_a_2_z G_CSET_A_2_Z G_CSET_DIGITS);
    name = g_strndup(text, len);
    text = strchr(text + len, '\n');
    if (!text) {
        g_warning("Truncated resource header");
        goto fail;
    }
    text++;
    type = g_type_from_name(name);
    if (!type
        || (expected_type && type != expected_type)
        || !g_type_is_a(type, GWY_TYPE_RESOURCE)
        || !G_TYPE_IS_INSTANTIATABLE(type)
        || G_TYPE_IS_ABSTRACT(type)) {
        g_warning("Wrong resource type `%s'", name);
        goto fail;
    }
    klass = GWY_RESOURCE_CLASS(g_type_class_peek_static(type));
    g_return_val_if_fail(klass && klass->parse, NULL);

    resource = klass->parse(text, is_const);
    if (resource)
        g_string_assign(resource->name, name);

fail:
    g_free(name);

    return resource;
}

/**
 * gwy_resource_data_changed:
 * @resource: A resource.
 *
 * Emits signal "data-changed" on a resource.
 *
 * It can be called only on non-constant resources.  The default handler
 * sets @is_modified flag on the resource.
 *
 * Mostly useful in resource implementation.
 **/
void
gwy_resource_data_changed(GwyResource *resource)
{
    g_return_if_fail(GWY_IS_RESOURCE(resource));
    g_signal_emit(resource, resource_signals[DATA_CHANGED], 0);
}

static void
gwy_resource_modified(GwyResource *resource)
{
    if (resource->is_const)
        g_warning("Constant resource was modified");
    resource->is_modified = TRUE;
}

/**
 * gwy_resource_data_saved:
 * @resource: A resource.
 *
 * Clears @is_modified flag of a resource.
 *
 * Since: 2.8
 **/
void
gwy_resource_data_saved(GwyResource *resource)
{
    g_return_if_fail(GWY_IS_RESOURCE(resource));
    if (resource->is_const)
        g_warning("Constant resource being passed to data_saved()");
    resource->is_modified = FALSE;
}

/**
 * gwy_resource_class_load:
 * @klass: A resource class.
 *
 * Loads resources of a resources class from disk.
 *
 * Resources are loaded from system directory (and marked constant) and from
 * user directory (marked modifiable).
 **/
void
gwy_resource_class_load(GwyResourceClass *klass)
{
    gpointer type;
    gchar *path, *datadir;

    g_return_if_fail(GWY_IS_RESOURCE_CLASS(klass));
    g_return_if_fail(klass->inventory);

    gwy_inventory_forget_order(klass->inventory);

    type = GSIZE_TO_POINTER(G_TYPE_FROM_CLASS(klass));
    G_LOCK(all_resources);
    if (!g_slist_find(all_resources, type))
        all_resources = g_slist_prepend(all_resources, type);
    G_UNLOCK(all_resources);

    datadir = gwy_find_self_dir("data");
    path = g_build_filename(datadir, klass->name, NULL);
    g_free(datadir);
    gwy_resource_class_load_dir(path, klass, TRUE);
    g_free(path);

    path = g_build_filename(gwy_get_user_dir(), klass->name, NULL);
    gwy_resource_class_load_dir(path, klass, FALSE);
    g_free(path);

    gwy_inventory_restore_order(klass->inventory);
}

static void
gwy_resource_class_load_dir(const gchar *path,
                            GwyResourceClass *klass,
                            gboolean is_system)
{
    GDir *dir;
    GwyResource *resource;
    GError *err = NULL;
    const gchar *name;
    gchar *filename, *text;

    if (!(dir = g_dir_open(path, 0, NULL)))
        return;

    while ((name = g_dir_read_name(dir))) {
        if (gwy_filename_ignore(name))
            continue;

        if (gwy_inventory_get_item(klass->inventory, name)) {
            g_warning("Ignoring duplicite %s `%s'", klass->name, name);
            continue;
        }
        /* FIXME */
        filename = g_build_filename(path, name, NULL);
        if (!g_file_get_contents(filename, &text, NULL, &err)) {
            g_warning("Cannot read `%s': %s", filename, err->message);
            g_clear_error(&err);
            g_free(filename);
            continue;
        }
        g_free(filename);

        resource = gwy_resource_parse_real(text, G_TYPE_FROM_CLASS(klass),
                                           is_system);
        if (resource) {
            g_string_assign(resource->name, name);
            resource->is_modified = FALSE;
            gwy_inventory_insert_item(klass->inventory, resource);
            g_object_unref(resource);
        }
        g_free(text);
    }

    g_dir_close(dir);
}

/**
 * gwy_resource_build_filename:
 * @resource: A resource.
 *
 * Builds file name a resource should be saved to.
 *
 * If the resource has not been newly created, renamed, or system it was
 * probably loaded from file of the same name.
 *
 * Returns: Resource file name as a newly allocated string that must be freed
 *          by caller.
 **/
gchar*
gwy_resource_build_filename(GwyResource *resource)
{
    GwyResourceClass *klass;

    g_return_val_if_fail(GWY_IS_RESOURCE(resource), NULL);
    if (resource->is_const)
        g_warning("Filename of a constant resource `%s' should not be needed",
                  resource->name->str);

    klass = GWY_RESOURCE_GET_CLASS(resource);
    return g_build_filename(gwy_get_user_dir(),
                            klass->name, resource->name->str, NULL);
}

/**
 * gwy_resource_delete:
 * @resource: A resource.
 *
 * Deletes a resource, including removal from disk.
 *
 * The method deletes the resource both in the inventory and on disk.
 * Constant resources cannot be deleted.
 *
 * Returns: %TRUE if the removal succeeded.
 *
 * Since: 2.51
 **/
gboolean
gwy_resource_delete(GwyResource *resource)
{
    GwyInventory *inventory;
    gchar *filename;
    gint retval;

    g_return_val_if_fail(GWY_IS_RESOURCE(resource), FALSE);

    if (resource->is_const)
        return FALSE;

    inventory = GWY_RESOURCE_GET_CLASS(resource)->inventory;

    filename = gwy_resource_build_filename(resource);
    retval = g_remove(filename);
    if (retval == 0)
        gwy_inventory_delete_item(inventory, resource->name->str);
    else {
        /* FIXME: GUIze this */
        g_warning("Cannot delete resource file: %s", filename);
    }
    g_free(filename);

    return retval == 0;
}

/**
 * gwy_resource_rename:
 * @resource: A resource.
 * @newname: New resource name.
 *
 * Renames a resource, including renaming it on disk.
 *
 * The method renames the resource both in the inventory and on disk. The
 * renaming must not conflict with an existing resource, constant resources
 * cannot be renamed, etc.  It is OK to rename a resource to the same name
 * (nothing happens then).
 *
 * Returns: %TRUE if the renaming succeeded.
 *
 * Since: 2.51
 **/
gboolean
gwy_resource_rename(GwyResource *resource,
                    const gchar *newname)
{
    GwyInventory *inventory;
    gchar *oldname, *oldfilename, *newfilename;
    gpointer item;
    gint retval;

    g_return_val_if_fail(GWY_IS_RESOURCE(resource), FALSE);
    g_return_val_if_fail(newname, FALSE);

    if (strchr(newname, '/') || strchr(newname, '\\')) {
        g_warning("Refusing to rename resource to name with special chars.");
        return FALSE;
    }

    if (gwy_strequal(newname, resource->name->str))
        return TRUE;
    if (resource->is_const)
        return FALSE;

    inventory = GWY_RESOURCE_GET_CLASS(resource)->inventory;
    item = gwy_inventory_get_item(inventory, newname);
    if (item)
        return FALSE;

    oldname = g_strdup(resource->name->str);
    oldfilename = gwy_resource_build_filename(resource);
    gwy_inventory_rename_item(inventory, oldname, newname);
    newfilename = gwy_resource_build_filename(resource);

    retval = g_rename(oldfilename, newfilename);
    if (retval != 0) {
        /* FIXME: GUIze this */
        g_warning("Cannot rename resource file: %s to %s",
                  oldfilename, newfilename);
        gwy_inventory_rename_item(inventory, newname, oldname);
    }
    g_free(newfilename);
    g_free(oldfilename);
    g_free(oldname);

    return retval == 0;
}

/**
 * gwy_resource_class_mkdir:
 * @klass: A resource class.
 *
 * Creates directory for user resources if it does not exist.
 *
 * Returns: %TRUE if the directory exists or has been successfully created.
 *          %FALSE if it doesn't exist and cannot be created, consult errno
 *          for reason.
 **/
gboolean
gwy_resource_class_mkdir(GwyResourceClass *klass)
{
    gchar *path;
    gint ok;

    g_return_val_if_fail(GWY_IS_RESOURCE_CLASS(klass), FALSE);

    path = g_build_filename(gwy_get_user_dir(), klass->name, NULL);
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        g_free(path);
        return TRUE;
    }

    ok = !g_mkdir(path, 0700);
    g_free(path);

    return ok;
}

/**
 * gwy_resource_classes_finalize:
 *
 * Destroys the inventories of all resource classes.
 *
 * This function makes the affected resource classes unusable.  Its purpose is
 * to faciliate reference leak debugging by destroying a large number of
 * objects that normally live forever.
 *
 * Note static resource classes that never called gwy_resource_class_load()
 * are excluded.
 *
 * Since: 2.8
 **/
void
gwy_resource_classes_finalize(void)
{
    GSList *l;

    G_LOCK(all_resources);
    for (l = all_resources; l; l = g_slist_next(l)) {
        GwyResourceClass *klass;

        klass = g_type_class_ref((GType)GPOINTER_TO_SIZE(all_resources->data));
        GWY_OBJECT_UNREF(klass->inventory);
    }

    g_slist_free(all_resources);
    all_resources = NULL;
    G_UNLOCK(all_resources);
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyresource
 * @title: GwyResource
 * @short_description: Built-in and/or user supplied application resources
 * @see_also: #GwyInventory
 *
 * #GwyResource is a base class for various application resources.  It defines
 * common interface: questioning resource name (gwy_resource_get_name()),
 * modifiability (gwy_resource_get_is_modifiable()), loading resources from
 * files and saving them.
 **/

/**
 * GwyResource:
 *
 * The #GwyResource struct contains private data only and should be accessed
 * using the functions below.
 **/

/**
 * GwyResourceClass:
 * @inventory: Inventory with resources.
 * @name: Resource class name, usable as resource directory name for on-disk
 *        resources.
 * @item_type: Inventory item type.  Most fields are pre-filled, but namely
 *             @type and @copy must be filled by particular resource type.
 * @data_changed: "data-changed" signal method.
 * @use: gwy_resource_use() virtual method.
 * @release: gwy_resource_release() virtual method.
 * @dump: gwy_resource_dump() virtual method, it only cares about resource
 *        data itself, the envelope is handled by #GwyResource.
 * @parse: gwy_resource_parse() virtual method, in only cares about resource
 *         data itself, the envelope is handled by #GwyResource.
 *
 * Resource class.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
