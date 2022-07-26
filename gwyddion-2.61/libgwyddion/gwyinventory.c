/*
 *  $Id: gwyinventory.c 21682 2018-11-26 11:17:52Z yeti-dn $
 *  Copyright (C) 2003,2004 David Necas (Yeti), Petr Klapetek.
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
#include <stdlib.h>

#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libgwyddion/gwyinventory.h>
#include <libgwyddion/gwyserializable.h>

#define GWY_INVENTORY_TYPE_NAME "GwyInventory"

enum {
    ITEM_INSERTED,
    ITEM_DELETED,
    ITEM_UPDATED,
    ITEMS_REORDERED,
    DEFAULT_CHANGED,
    LAST_SIGNAL
};

typedef struct {
    gpointer p;
    guint i;
} ArrayItem;

static void          gwy_inventory_finalize            (GObject *object);
static inline guint  gwy_inventory_lookup              (GwyInventory *inventory,
                                                        const gchar *name);
static void          gwy_inventory_make_hash           (GwyInventory *inventory);
static GwyInventory* gwy_inventory_new_real            (const GwyInventoryItemType *itype,
                                                        guint nitems,
                                                        gpointer *items,
                                                        gboolean is_const);
static void          gwy_inventory_connect_to_item     (gpointer item,
                                                        GwyInventory *inventory);
static void          gwy_inventory_disconnect_from_item(gpointer item,
                                                        GwyInventory *inventory);
static void          gwy_inventory_reindex             (GwyInventory *inventory);
static void          gwy_inventory_item_updated_real   (GwyInventory *inventory,
                                                        guint i);
static void          gwy_inventory_item_changed        (GwyInventory *inventory,
                                                        gpointer item);
static gint          gwy_inventory_compare_indices     (gint *a,
                                                        gint *b,
                                                        GwyInventory *inventory);
static void          gwy_inventory_delete_nth_item_real(GwyInventory *inventory,
                                                        const gchar *name,
                                                        guint i);
static gchar*        gwy_inventory_invent_name         (GwyInventory *inventory,
                                                        const gchar *prefix);

static guint gwy_inventory_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(GwyInventory, gwy_inventory, G_TYPE_OBJECT)

static void
gwy_inventory_class_init(GwyInventoryClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_inventory_finalize;

    /**
     * GwyInventory::item-inserted:
     * @gwyinventory: The #GwyInventory which received the signal.
     * @arg1: Position an item was inserted at.
     *
     * The ::item-inserted signal is emitted when an item is inserted into
     * an inventory.
     **/
    gwy_inventory_signals[ITEM_INSERTED] =
        g_signal_new("item-inserted",
                     GWY_TYPE_INVENTORY,
                     G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                     G_STRUCT_OFFSET(GwyInventoryClass, item_inserted),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__UINT,
                     G_TYPE_NONE, 1, G_TYPE_UINT);

    /**
     * GwyInventory::item-deleted:
     * @gwyinventory: The #GwyInventory which received the signal.
     * @arg1: Position an item was deleted from.
     *
     * The ::item-deleted signal is emitted when an item is deleted from
     * an inventory.
     **/
    gwy_inventory_signals[ITEM_DELETED] =
        g_signal_new("item-deleted",
                     GWY_TYPE_INVENTORY,
                     G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                     G_STRUCT_OFFSET(GwyInventoryClass, item_deleted),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__UINT,
                     G_TYPE_NONE, 1, G_TYPE_UINT);

    /**
     * GwyInventory::item-updated:
     * @gwyinventory: The #GwyInventory which received the signal.
     * @arg1: Position of updated item.
     *
     * The ::item-updated signal is emitted when an item in an inventory
     * is updated.
     **/
    gwy_inventory_signals[ITEM_UPDATED] =
        g_signal_new("item-updated",
                     GWY_TYPE_INVENTORY,
                     G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                     G_STRUCT_OFFSET(GwyInventoryClass, item_updated),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__UINT,
                     G_TYPE_NONE, 1, G_TYPE_UINT);

    /**
     * GwyInventory::items-reordered:
     * @gwyinventory: The #GwyInventory which received the signal.
     * @arg1: New item order map as in #GtkTreeModel,
     *        @arg1[new_position] = old_position.
     *
     * The ::items-reordered signal is emitted when item in an inventory
     * are reordered.
     **/
    gwy_inventory_signals[ITEMS_REORDERED] =
        g_signal_new("items-reordered",
                     GWY_TYPE_INVENTORY,
                     G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                     G_STRUCT_OFFSET(GwyInventoryClass, items_reordered),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER,
                     G_TYPE_NONE, 1, G_TYPE_POINTER);

    /**
     * GwyInventory::default-changed:
     * @gwyinventory: The #GwyInventory which received the signal.
     *
     * The ::default-changed signal is emitted when either
     * default inventory item name changes or the presence of such an item
     * in the inventory changes.
     **/
    gwy_inventory_signals[DEFAULT_CHANGED] =
        g_signal_new("default-changed",
                     GWY_TYPE_INVENTORY,
                     G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                     G_STRUCT_OFFSET(GwyInventoryClass, default_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0);
}

static void
gwy_inventory_init(G_GNUC_UNUSED GwyInventory *inventory)
{
}

static void
gwy_inventory_finalize(GObject *object)
{
    GwyInventory *inventory = (GwyInventory*)object;

    if (inventory->is_watchable)
        g_ptr_array_foreach(inventory->items,
                            (GFunc)&gwy_inventory_disconnect_from_item,
                            inventory);
    if (inventory->default_key)
        g_string_free(inventory->default_key, TRUE);
    if (inventory->hash)
        g_hash_table_destroy(inventory->hash);
    if (inventory->idx)
        g_array_free(inventory->idx, TRUE);
    if (inventory->ridx)
        g_array_free(inventory->ridx, TRUE);
    if (inventory->is_object)
        g_ptr_array_foreach(inventory->items, (GFunc)&g_object_unref, NULL);
    g_ptr_array_free(inventory->items, TRUE);

    G_OBJECT_CLASS(gwy_inventory_parent_class)->finalize(object);
}

static inline guint
gwy_inventory_lookup(GwyInventory *inventory,
                     const gchar *name)
{
    if (G_UNLIKELY(!inventory->hash))
        gwy_inventory_make_hash(inventory);

    return GPOINTER_TO_UINT(g_hash_table_lookup(inventory->hash, name));
}

static void
gwy_inventory_make_hash(GwyInventory *inventory)
{
    const gchar* (*get_name)(gpointer);
    gint i;

    g_assert(!inventory->hash);
    gwy_debug("");
    inventory->hash = g_hash_table_new(g_str_hash, g_str_equal);

    get_name = inventory->item_type.get_name;
    for (i = 0; i < inventory->items->len; i++) {
        gpointer item;

        item = g_ptr_array_index(inventory->items, i);
        g_hash_table_insert(inventory->hash, (gpointer)get_name(item),
                            GUINT_TO_POINTER(i+1));
    }
}

/**
 * gwy_inventory_new:
 * @itype: Type of items the inventory will contain.
 *
 * Creates a new inventory.
 *
 * Returns: The newly created inventory.
 **/
GwyInventory*
gwy_inventory_new(const GwyInventoryItemType *itype)
{
    return gwy_inventory_new_real(itype, 0, NULL, FALSE);
}

/**
 * gwy_inventory_new_filled:
 * @itype: Type of items the inventory will contain.
 * @nitems: The number of pointers in @items.
 * @items: Item pointers to fill the newly created inventory with.
 *
 * Creates a new inventory and fills it with items.
 *
 * Returns: The newly created inventory.
 **/
GwyInventory*
gwy_inventory_new_filled(const GwyInventoryItemType *itype,
                         guint nitems,
                         gpointer *items)
{
    return gwy_inventory_new_real(itype, nitems, items, FALSE);
}

/**
 * gwy_inventory_new_from_array:
 * @itype: Type of items the inventory will contain.  Inventory keeps a copy
 *         of it, so it can be an automatic variable.
 * @item_size: Item size in bytes.
 * @nitems: The number of items in @items.
 * @items: An array with items.  It will be directly used as thus must
 *         exist through the whole lifetime of inventory.
 *
 * Creates a new inventory from static item array.
 *
 * The inventory is neither modifiable nor sortable, it simply serves as an
 * adaptor for the array @items.
 *
 * Returns: The newly created inventory.
 **/
GwyInventory*
gwy_inventory_new_from_array(const GwyInventoryItemType *itype,
                             guint item_size,
                             guint nitems,
                             gconstpointer items)
{
    gpointer *pitems;
    guint i;

    g_return_val_if_fail(items, NULL);
    g_return_val_if_fail(nitems, NULL);
    g_return_val_if_fail(item_size, NULL);

    pitems = g_newa(gpointer, nitems);
    for (i = 0; i < nitems; i++)
        pitems[i] = (gpointer)((const guchar*)items + i*item_size);

    return gwy_inventory_new_real(itype, nitems, pitems, TRUE);
}

static GwyInventory*
gwy_inventory_new_real(const GwyInventoryItemType *itype,
                       guint nitems,
                       gpointer *items,
                       gboolean is_const)
{
    GwyInventory *inventory;
    guint i;

    g_return_val_if_fail(itype, NULL);
    g_return_val_if_fail(itype->get_name, NULL);
    g_return_val_if_fail(items || !nitems, NULL);

    inventory = g_object_new(GWY_TYPE_INVENTORY, NULL);

    inventory->item_type = *itype;
    if (itype->type) {
        inventory->is_object = g_type_is_a(itype->type, G_TYPE_OBJECT);
        inventory->is_watchable = (itype->watchable_signal != NULL);
    }

    inventory->can_make_copies = itype->rename && itype->copy;
    inventory->is_sorted = (itype->compare != NULL);
    inventory->is_const = is_const;
    inventory->items = g_ptr_array_sized_new(nitems);

    for (i = 0; i < nitems; i++) {
        g_ptr_array_add(inventory->items, items[i]);
        if (inventory->is_sorted && i)
            inventory->is_sorted = (itype->compare(items[i-1], items[i]) < 0);
    }
    if (!is_const) {
        inventory->idx = g_array_sized_new(FALSE, FALSE, sizeof(guint),
                                           nitems);
        inventory->ridx = g_array_sized_new(FALSE, FALSE, sizeof(guint),
                                            nitems);
        for (i = 0; i < nitems; i++)
            g_array_append_val(inventory->idx, i);
        g_array_append_vals(inventory->ridx, inventory->idx->data, nitems);
    }
    if (inventory->is_object) {
        g_ptr_array_foreach(inventory->items, (GFunc)&g_object_ref, NULL);
        if (inventory->is_watchable)
            g_ptr_array_foreach(inventory->items,
                                (GFunc)&gwy_inventory_connect_to_item,
                                inventory);
    }

    return inventory;
}

static void
gwy_inventory_disconnect_from_item(gpointer item,
                                   GwyInventory *inventory)
{
    g_signal_handlers_disconnect_by_func(item, gwy_inventory_item_changed,
                                         inventory);
}

static void
gwy_inventory_connect_to_item(gpointer item,
                              GwyInventory *inventory)
{
    g_signal_connect_swapped(item, inventory->item_type.watchable_signal,
                             G_CALLBACK(gwy_inventory_item_changed), inventory);
}

/**
 * gwy_inventory_get_n_items:
 * @inventory: An inventory.
 *
 * Returns the number of items in an inventory.
 *
 * Returns: The number of items.
 **/
guint
gwy_inventory_get_n_items(GwyInventory *inventory)
{
    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), 0);
    return inventory->items->len;
}

/**
 * gwy_inventory_can_make_copies:
 * @inventory: An inventory.
 *
 * Returns whether an inventory can create new items itself.
 *
 * The prerequistie is that item type is a serializable object.  It enables
 * functions like gwy_inventory_new_item().
 *
 * Returns: %TRUE if inventory can create new items itself.
 **/
gboolean
gwy_inventory_can_make_copies(GwyInventory *inventory)
{
    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), FALSE);
    return inventory->can_make_copies;
}

/**
 * gwy_inventory_is_const:
 * @inventory: An inventory.
 *
 * Returns whether an inventory is an constant inventory.
 *
 * Not only you cannot modify a constant inventory, but functions like
 * gwy_inventory_get_item() may return pointers to constant memory.
 *
 * Returns: %TRUE if inventory is constant.
 **/
gboolean
gwy_inventory_is_const(GwyInventory *inventory)
{
    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), FALSE);
    return inventory->is_const;
}

/**
 * gwy_inventory_get_item_type:
 * @inventory: An inventory.
 *
 * Returns the type of item an inventory holds.
 *
 * Returns: The item type.  It is owned by inventory and must not be modified
 *          or freed.
 **/
const GwyInventoryItemType*
gwy_inventory_get_item_type(GwyInventory *inventory)
{
    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    return &inventory->item_type;
}

/**
 * gwy_inventory_get_item:
 * @inventory: An inventory.
 * @name: Item name.
 *
 * Looks up an item in an inventory.
 *
 * Returns: Item called @name, or %NULL if there is no such item.
 **/
gpointer
gwy_inventory_get_item(GwyInventory *inventory,
                       const gchar *name)
{
    guint i;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    if ((i = gwy_inventory_lookup(inventory, name)))
        return g_ptr_array_index(inventory->items, i-1);
    else
        return NULL;
}

/**
 * gwy_inventory_get_item_or_default:
 * @inventory: An inventory.
 * @name: Item name.
 *
 * Looks up an item in an inventory, eventually falling back to default.
 *
 * The lookup order is: item of requested name, default item (if set), any
 * inventory item, %NULL (can happen only when inventory is empty).
 *
 * Returns: Item called @name, or default item.
 **/
gpointer
gwy_inventory_get_item_or_default(GwyInventory *inventory,
                                  const gchar *name)
{
    guint i;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    if (name && (i = gwy_inventory_lookup(inventory, name)))
        return g_ptr_array_index(inventory->items, i-1);
    if (inventory->has_default
        && (i = gwy_inventory_lookup(inventory, inventory->default_key->str)))
        return g_ptr_array_index(inventory->items, i-1);

    if (inventory->items->len)
        return g_ptr_array_index(inventory->items, 0);
    return NULL;
}

/**
 * gwy_inventory_get_nth_item:
 * @inventory: An inventory.
 * @n: Item position.  It must be between zero and the number of items in
 *     inventory, inclusive.  If it is equal to the number of items, %NULL
 *     is returned.  In other words, inventory behaves like a %NULL-terminated
 *     array, you can simply iterate over it until gwy_inventory_get_nth_item()
 *     returns %NULL.
 *
 * Returns item on given position in an inventory.
 *
 * Returns: Item at given position.
 **/
gpointer
gwy_inventory_get_nth_item(GwyInventory *inventory,
                           guint n)
{
    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    g_return_val_if_fail(n <= inventory->items->len, NULL);
    if (G_UNLIKELY(n == inventory->items->len))
        return NULL;
    if (inventory->ridx)
        n = g_array_index(inventory->ridx, guint, n);

    return g_ptr_array_index(inventory->items, n);
}

/**
 * gwy_inventory_get_item_position:
 * @inventory: An inventory.
 * @name: Item name.
 *
 * Finds position of an item in an inventory.
 *
 * Returns: Item position, or (guint)-1 if there is no such item.
 **/
guint
gwy_inventory_get_item_position(GwyInventory *inventory,
                                const gchar *name)
{
    guint i;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), -1);
    if (!(i = gwy_inventory_lookup(inventory, name)))
        return (guint)-1;

    if (!inventory->idx)
        return i-1;

    if (inventory->needs_reindex)
        gwy_inventory_reindex(inventory);

    return g_array_index(inventory->idx, guint, i-1);
}

/**
 * gwy_inventory_reindex:
 * @inventory: An inventory.
 *
 * Updates @idx of @inventory to match @ridx.
 *
 * Note positions in hash are 1-based (to allow %NULL work as no-such-item),
 * but position in @items and @idx are 0-based.
 **/
static void
gwy_inventory_reindex(GwyInventory *inventory)
{
    guint i, n;

    gwy_debug("");
    g_return_if_fail(inventory->ridx);

    for (i = 0; i < inventory->items->len; i++) {
        n = g_array_index(inventory->ridx, guint, i);
        g_array_index(inventory->idx, guint, n) = i;
    }

    inventory->needs_reindex = FALSE;
}

/**
 * gwy_inventory_foreach:
 * @inventory: An inventory.
 * @function: A function to call on each item.  It must not modify @inventory.
 * @user_data: Data passed to @function.
 *
 * Calls a function on each item of an inventory, in order.
 *
 * @function's first argument is item position (transformed with
 * GUINT_TO_POINTER()), second is item pointer, and the last is @user_data.
 **/
void
gwy_inventory_foreach(GwyInventory *inventory,
                      GHFunc function,
                      gpointer user_data)
{
    gpointer item;
    guint i, j, n;

    g_return_if_fail(GWY_IS_INVENTORY(inventory));
    g_return_if_fail(function);

    n = inventory->items->len;
    if (inventory->ridx) {
        for (i = 0; i < n; i++) {
            j = g_array_index(inventory->ridx, guint, i);
            item = g_ptr_array_index(inventory->items, j);
            function(GUINT_TO_POINTER(i), item, user_data);
        }
    }
    else {
        for (i = 0; i < n; i++) {
            item = g_ptr_array_index(inventory->items, i);
            function(GUINT_TO_POINTER(i), item, user_data);
        }
    }
}

/**
 * gwy_inventory_find:
 * @inventory: An inventory.
 * @predicate: A function testing some item property.  It must not modify
 *             @inventory.
 * @user_data: Data passed to @predicate.
 *
 * Finds an inventory item using user-specified predicate function.
 *
 * @predicate is called for each item in @inventory (in order) until it returns
 * %TRUE.  Its arguments are the same as in gwy_inventory_foreach().
 *
 * Returns: The item for which @predicate returned %TRUE.  If there is no
 *          such item in the inventory, %NULL is returned.
 **/
gpointer
gwy_inventory_find(GwyInventory *inventory,
                   GHRFunc predicate,
                   gpointer user_data)
{
    gpointer item;
    guint i, j, n;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    g_return_val_if_fail(predicate, NULL);

    n = inventory->items->len;
    if (inventory->ridx) {
        for (i = 0; i < n; i++) {
            j = g_array_index(inventory->ridx, guint, i);
            item = g_ptr_array_index(inventory->items, j);
            if (predicate(GUINT_TO_POINTER(i), item, user_data))
                return item;
        }
    }
    else {
        for (i = 0; i < n; i++) {
            item = g_ptr_array_index(inventory->items, i);
            if (predicate(GUINT_TO_POINTER(i), item, user_data))
                return item;
        }
    }

    return NULL;
}

/**
 * gwy_inventory_get_default_item:
 * @inventory: An inventory.
 *
 * Returns the default item of an inventory.
 *
 * Returns: The default item.  If there is no default item, %NULL is returned.
 **/
gpointer
gwy_inventory_get_default_item(GwyInventory *inventory)
{
    guint i;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    if (!inventory->has_default)
        return NULL;

    if ((i = gwy_inventory_lookup(inventory, inventory->default_key->str)))
        return g_ptr_array_index(inventory->items, i-1);
    else
        return NULL;
}

/**
 * gwy_inventory_get_default_item_name:
 * @inventory: An inventory.
 *
 * Returns the name of the default item of an inventory.
 *
 * Returns: The default item name, %NULL if no default name is set.
 *          Item of this name may or may not exist in the inventory.
 **/
const gchar*
gwy_inventory_get_default_item_name(GwyInventory *inventory)
{
    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    if (!inventory->has_default)
        return NULL;

    return inventory->default_key->str;
}

/**
 * gwy_inventory_set_default_item_name:
 * @inventory: An inventory.
 * @name: Item name, pass %NULL to unset default item.
 *
 * Sets the default of an inventory.
 *
 * Item @name must already exist in the inventory.
 **/
void
gwy_inventory_set_default_item_name(GwyInventory *inventory,
                                    const gchar *name)
{
    gboolean emit_change = FALSE;

    g_return_if_fail(GWY_IS_INVENTORY(inventory));
    if (!name) {
        emit_change = inventory->has_default;
        inventory->has_default = FALSE;
    }
    else {
        if (!inventory->has_default) {
            emit_change = TRUE;
            inventory->has_default = TRUE;
        }

        if (!inventory->default_key) {
            inventory->default_key = g_string_new(name);
            emit_change = TRUE;
        }
        else {
            if (!gwy_strequal(inventory->default_key->str, name))
                emit_change = TRUE;
            g_string_assign(inventory->default_key, name);
        }
    }

    if (emit_change)
        g_signal_emit(inventory, gwy_inventory_signals[DEFAULT_CHANGED], 0);
}

/**
 * gwy_inventory_item_updated_real:
 * @inventory: An inventory.
 * @i: Storage position of updated item.
 *
 * Emits "item-updated" signal.
 **/
static void
gwy_inventory_item_updated_real(GwyInventory *inventory,
                                guint i)
{
    if (!inventory->idx) {
        g_signal_emit(inventory, gwy_inventory_signals[ITEM_UPDATED], 0, i);
        return;
    }

    if (inventory->needs_reindex)
        gwy_inventory_reindex(inventory);

    i = g_array_index(inventory->idx, guint, i);
    g_signal_emit(inventory, gwy_inventory_signals[ITEM_UPDATED], 0, i);
}

/**
 * gwy_inventory_item_updated:
 * @inventory: An inventory.
 * @name: Item name.
 *
 * Notifies inventory an item was updated.
 *
 * This function makes sense primarily for non-object items, as object items
 * can notify inventory via signals.
 **/
void
gwy_inventory_item_updated(GwyInventory *inventory,
                           const gchar *name)
{
    guint i;

    g_return_if_fail(GWY_IS_INVENTORY(inventory));
    if (!(i = gwy_inventory_lookup(inventory, name)))
        g_warning("Item `%s' does not exist", name);
    else
        gwy_inventory_item_updated_real(inventory, i-1);
}

/**
 * gwy_inventory_nth_item_updated:
 * @inventory: An inventory.
 * @n: Item position.
 *
 * Notifies inventory item on given position was updated.
 *
 * This function makes sense primarily for non-object items, as object items
 * can provide @watchable_signal.
 **/
void
gwy_inventory_nth_item_updated(GwyInventory *inventory,
                               guint n)
{
    g_return_if_fail(GWY_IS_INVENTORY(inventory));
    g_return_if_fail(n < inventory->items->len);

    g_signal_emit(inventory, gwy_inventory_signals[ITEM_UPDATED], 0, n);
}

/**
 * gwy_inventory_item_changed:
 * @inventory: An inventory.
 * @item: An item that has changed.
 *
 * Handles inventory item `changed' signal.
 **/
static void
gwy_inventory_item_changed(GwyInventory *inventory,
                           gpointer item)
{
    const gchar *name;
    guint i;

    name = inventory->item_type.get_name(item);
    i = gwy_inventory_lookup(inventory, name);
    g_assert(i);
    gwy_inventory_item_updated_real(inventory, i-1);
}

/**
 * gwy_inventory_insert_item:
 * @inventory: An inventory.
 * @item: An item to insert.
 *
 * Inserts an item into an inventory.
 *
 * Item of the same name must not exist yet.
 *
 * If the inventory is sorted, item is inserted to keep order.  If the
 * inventory is unsorted, item is simply added to the end.
 *
 * Returns: @item, for convenience.
 **/
gpointer
gwy_inventory_insert_item(GwyInventory *inventory,
                          gpointer item)
{
    const gchar *name;
    guint m;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    g_return_val_if_fail(!inventory->is_const, NULL);
    g_return_val_if_fail(item, NULL);

    name = inventory->item_type.get_name(item);
    if (gwy_inventory_lookup(inventory, name)) {
        g_warning("Item `%s' already exists", name);
        return NULL;
    }

    if (inventory->is_object)
        g_object_ref(item);

    /* Insert into index array */
    if (inventory->is_sorted && inventory->items->len) {
        gpointer mp;
        guint j0, j1;

        j0 = 0;
        j1 = inventory->items->len - 1;
        while (j1 - j0 > 1) {
            m = (j0 + j1 + 1)/2;
            mp = g_ptr_array_index(inventory->items,
                                   g_array_index(inventory->ridx, guint, m));
            if (inventory->item_type.compare(item, mp) >= 0)
                j0 = m;
            else
                j1 = m;
        }

        mp = g_ptr_array_index(inventory->items,
                               g_array_index(inventory->ridx, guint, j0));
        if (inventory->item_type.compare(item, mp) < 0)
            m = j0;
        else {
            mp = g_ptr_array_index(inventory->items,
                                   g_array_index(inventory->ridx, guint, j1));
            if (inventory->item_type.compare(item, mp) < 0)
                m = j1;
            else
                m = j1+1;
        }

        g_array_insert_val(inventory->ridx, m, inventory->items->len);
        inventory->needs_reindex = TRUE;
    }
    else {
        m = inventory->items->len;
        g_array_append_val(inventory->ridx, inventory->items->len);
    }

    g_array_append_val(inventory->idx, m);
    g_ptr_array_add(inventory->items, item);
    g_hash_table_insert(inventory->hash, (gpointer)name,
                        GUINT_TO_POINTER(inventory->items->len));

    if (inventory->is_watchable)
        gwy_inventory_connect_to_item(item, inventory);

    g_signal_emit(inventory, gwy_inventory_signals[ITEM_INSERTED], 0, m);
    if (inventory->has_default
        && gwy_strequal(name, inventory->default_key->str))
        g_signal_emit(inventory, gwy_inventory_signals[DEFAULT_CHANGED], 0);

    return item;
}

/**
 * gwy_inventory_insert_nth_item:
 * @inventory: An inventory.
 * @item: An item to insert.
 * @n: Position to insert @item to.
 *
 * Inserts an item to an explicit position in an inventory.
 *
 * Item of the same name must not exist yet.
 *
 * Returns: @item, for convenience.
 **/
gpointer
gwy_inventory_insert_nth_item(GwyInventory *inventory,
                              gpointer item,
                              guint n)
{
    const gchar *name;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    g_return_val_if_fail(!inventory->is_const, NULL);
    g_return_val_if_fail(item, NULL);
    g_return_val_if_fail(n <= inventory->items->len, NULL);

    name = inventory->item_type.get_name(item);
    if (gwy_inventory_lookup(inventory, name)) {
        g_warning("Item `%s' already exists", name);
        return NULL;
    }

    if (inventory->is_object)
        g_object_ref(item);

    g_array_insert_val(inventory->ridx, n, inventory->items->len);
    inventory->needs_reindex = TRUE;

    g_array_append_val(inventory->idx, n);    /* value does not matter */
    g_ptr_array_add(inventory->items, item);
    g_hash_table_insert(inventory->hash, (gpointer)name,
                        GUINT_TO_POINTER(inventory->items->len));

    if (inventory->is_sorted) {
        gpointer mp;

        if (n > 0) {
            mp = g_ptr_array_index(inventory->items,
                                   g_array_index(inventory->ridx, guint, n-1));
            if (inventory->item_type.compare(item, mp) < 0)
                inventory->is_sorted = FALSE;
        }
        if (inventory->is_sorted
            && n+1 < inventory->items->len) {
            mp = g_ptr_array_index(inventory->items,
                                   g_array_index(inventory->ridx, guint, n+1));
            if (inventory->item_type.compare(item, mp) > 0)
                inventory->is_sorted = FALSE;
        }
    }

    if (inventory->is_watchable)
        gwy_inventory_connect_to_item(item, inventory);

    g_signal_emit(inventory, gwy_inventory_signals[ITEM_INSERTED], 0, n);
    if (inventory->has_default
        && gwy_strequal(name, inventory->default_key->str))
        g_signal_emit(inventory, gwy_inventory_signals[DEFAULT_CHANGED], 0);

    return item;
}

/**
 * gwy_inventory_restore_order:
 * @inventory: An inventory.
 *
 * Assures an inventory is sorted.
 **/
void
gwy_inventory_restore_order(GwyInventory *inventory)
{
    guint i;
    gint *new_order;

    g_return_if_fail(GWY_IS_INVENTORY(inventory));
    g_return_if_fail(!inventory->is_const);
    if (inventory->is_sorted || !inventory->item_type.compare)
        return;

    /* Make sure old order is remembered in @idx */
    if (inventory->needs_reindex)
        gwy_inventory_reindex(inventory);
    g_array_sort_with_data(inventory->ridx,
                           (GCompareDataFunc)gwy_inventory_compare_indices,
                           inventory);

    new_order = g_newa(gint, inventory->items->len);

    /* Fill new_order with indices: new_order[new_position] = old_position */
    for (i = 0; i < inventory->ridx->len; i++)
        new_order[i] = g_array_index(inventory->idx, guint,
                                     g_array_index(inventory->ridx, guint, i));
    inventory->needs_reindex = TRUE;
    inventory->is_sorted = TRUE;

    g_signal_emit(inventory, gwy_inventory_signals[ITEMS_REORDERED], 0,
                  new_order);
}

/**
 * gwy_inventory_forget_order:
 * @inventory: An inventory.
 *
 * Forces an inventory to be unsorted.
 *
 * Item positions don't change, but future gwy_inventory_insert_item() won't
 * try to insert items in order.
 **/
void
gwy_inventory_forget_order(GwyInventory *inventory)
{
    g_return_if_fail(GWY_IS_INVENTORY(inventory));
    g_return_if_fail(!inventory->is_const);
    inventory->is_sorted = FALSE;
}

static gint
gwy_inventory_compare_indices(gint *a,
                              gint *b,
                              GwyInventory *inventory)
{
    gpointer pa, pb;

    pa = g_ptr_array_index(inventory->items, *a);
    pb = g_ptr_array_index(inventory->items, *b);
    return inventory->item_type.compare(pa, pb);
}

/**
 * gwy_inventory_delete_nth_item_real:
 * @inventory: An inventory.
 * @name: Item name (to avoid double lookups from gwy_inventory_delete_item()).
 * @i: Storage position of item to remove.
 *
 * Removes an item from an inventory given its physical position.
 *
 * A kind of g_array_remove_index_fast(), but updating references.
 **/
static void
gwy_inventory_delete_nth_item_real(GwyInventory *inventory,
                                   const gchar *name,
                                   guint i)
{
    gpointer mp, lp;
    guint n, last;
    gboolean emit_change = FALSE;

    mp = g_ptr_array_index(inventory->items, i);
    if (inventory->item_type.is_fixed
        && inventory->item_type.is_fixed(mp)) {
        g_warning("Cannot delete fixed item `%s'", name);
        return;
    }
    if (inventory->has_default
        && gwy_strequal(name, inventory->default_key->str))
        emit_change = TRUE;

    if (inventory->is_watchable)
        gwy_inventory_disconnect_from_item(mp, inventory);

    if (inventory->needs_reindex)
        gwy_inventory_reindex(inventory);

    if (inventory->item_type.dismantle)
        inventory->item_type.dismantle(mp);

    n = g_array_index(inventory->idx, guint, i);
    last = inventory->items->len - 1;

    /* Move last item of @items to position of removed item */
    if (inventory->hash)
        g_hash_table_remove(inventory->hash, name);
    if (i < last) {
        lp = g_ptr_array_index(inventory->items, last);
        g_ptr_array_index(inventory->items, i) = lp;
        name = inventory->item_type.get_name(lp);
        if (inventory->hash)
            g_hash_table_insert(inventory->hash, (gpointer)name,
                                GUINT_TO_POINTER(i+1));
        g_array_index(inventory->ridx, guint,
                      g_array_index(inventory->idx, guint, last)) = i;
    }
    g_array_remove_index(inventory->ridx, n);
    g_ptr_array_set_size(inventory->items, last);
    g_array_set_size(inventory->idx, last);
    inventory->needs_reindex = TRUE;

    if (inventory->is_object)
        g_object_unref(mp);

    g_signal_emit(inventory, gwy_inventory_signals[ITEM_DELETED], 0, n);
    if (emit_change)
        g_signal_emit(inventory, gwy_inventory_signals[DEFAULT_CHANGED], 0);
}

/**
 * gwy_inventory_delete_item:
 * @inventory: An inventory.
 * @name: Name of item to delete.
 *
 * Deletes an item from an inventory.
 *
 * Returns: %TRUE if item was deleted.
 **/
gboolean
gwy_inventory_delete_item(GwyInventory *inventory,
                          const gchar *name)
{
    guint i;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), FALSE);
    g_return_val_if_fail(!inventory->is_const, FALSE);
    if (!(i = gwy_inventory_lookup(inventory, name))) {
        g_warning("Item `%s' does not exist", name);
        return FALSE;
    }

    gwy_inventory_delete_nth_item_real(inventory, name, i-1);

    return TRUE;
}

/**
 * gwy_inventory_delete_nth_item:
 * @inventory: An inventory.
 * @n: Position of @item to delete.
 *
 * Deletes an item on given position from an inventory.
 *
 * Returns: %TRUE if item was deleted.
 **/
gboolean
gwy_inventory_delete_nth_item(GwyInventory *inventory,
                              guint n)
{
    const gchar *name;
    guint i;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), FALSE);
    g_return_val_if_fail(!inventory->is_const, FALSE);
    g_return_val_if_fail(n < inventory->items->len, FALSE);

    i = g_array_index(inventory->ridx, guint, n);
    name = inventory->item_type.get_name(g_ptr_array_index(inventory->items,
                                                           i));
    gwy_inventory_delete_nth_item_real(inventory, name, i);

    return TRUE;
}

/**
 * gwy_inventory_rename_item:
 * @inventory: An inventory.
 * @name: Name of item to rename.
 * @newname: New name of item.
 *
 * Renames an inventory item.
 *
 * If an item of name @newname is already present in @inventory, the rename
 * will fail.
 *
 * Returns: The item, for convenience.
 **/
gpointer
gwy_inventory_rename_item(GwyInventory *inventory,
                          const gchar *name,
                          const gchar *newname)
{
    gpointer mp;
    guint i;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    g_return_val_if_fail(!inventory->is_const, NULL);
    g_return_val_if_fail(newname, NULL);
    g_return_val_if_fail(inventory->item_type.rename, NULL);

    if (!(i = gwy_inventory_lookup(inventory, name))) {
        g_warning("Item `%s' does not exist", name);
        return NULL;
    }
    mp = g_ptr_array_index(inventory->items, i-1);
    if (inventory->item_type.is_fixed
        && inventory->item_type.is_fixed(mp)) {
        g_warning("Cannot rename fixed item `%s'", name);
        return NULL;
    }
    if (gwy_strequal(name, newname))
        return mp;

    if (gwy_inventory_lookup(inventory, newname)) {
        g_warning("Item `%s' already exists", newname);
        return NULL;
    }

    g_hash_table_remove(inventory->hash, name);
    inventory->item_type.rename(mp, newname);
    g_hash_table_insert(inventory->hash,
                        (gpointer)inventory->item_type.get_name(mp),
                        GUINT_TO_POINTER(i));

    if (inventory->needs_reindex)
        gwy_inventory_reindex(inventory);
    if (inventory->is_sorted) {
        inventory->is_sorted = FALSE;
        gwy_inventory_restore_order(inventory);
    }

    g_signal_emit(inventory, gwy_inventory_signals[ITEM_UPDATED], 0,
                  g_array_index(inventory->idx, guint, i-1));
    if (inventory->has_default
        && (gwy_strequal(name, inventory->default_key->str)
            || gwy_strequal(newname, inventory->default_key->str)))
        g_signal_emit(inventory, gwy_inventory_signals[DEFAULT_CHANGED], 0);

    return mp;
}

/**
 * gwy_inventory_new_item:
 * @inventory: An inventory.
 * @name: Name of item to duplicate, may be %NULL to use default item (the
 *        same happens when @name does not exist).
 * @newname: Name of new item, it must not exist yet.  It may be %NULL, the
 *           new name is based on @name then.
 *
 * Creates a new item as a copy of existing one and inserts it to inventory.
 *
 * The newly created item can be called differently than @newname if that
 * already exists.
 *
 * Returns: The newly added item.
 **/
gpointer
gwy_inventory_new_item(GwyInventory *inventory,
                       const gchar *name,
                       const gchar *newname)
{
    guint i = 0;
    gpointer item = NULL;
    gchar *freeme = NULL;

    g_return_val_if_fail(GWY_IS_INVENTORY(inventory), NULL);
    g_return_val_if_fail(!inventory->is_const, NULL);
    g_return_val_if_fail(inventory->can_make_copies, NULL);

    /* Find which item we should base copy on */
    if (!name && inventory->has_default)
        name = inventory->default_key->str;

    if ((!name || !(i = gwy_inventory_lookup(inventory, name)))
        && inventory->items->len)
        i = 1;
    if (i) {
        item = g_ptr_array_index(inventory->items, i-1);
        name = inventory->item_type.get_name(item);
    }

    if (!name || !item) {
        g_warning("No default item to base new item on");
        return NULL;
    }

    /* Find new name */
    if (!newname)
        newname = freeme = gwy_inventory_invent_name(inventory, name);
    else if (gwy_inventory_lookup(inventory, newname))
        newname = freeme = gwy_inventory_invent_name(inventory, newname);

    /* Create new item */
    item = inventory->item_type.copy(item);
    inventory->item_type.rename(item, newname);
    gwy_inventory_insert_item(inventory, item);
    g_free(freeme);

    return item;
}

/**
 * gwy_inventory_invent_name:
 * @inventory: An inventory.
 * @prefix: Name prefix.
 *
 * Finds a name of form "prefix number" that does not identify any item in
 * an inventory yet.
 *
 * Returns: The invented name as a string that is owned by this function and
 *          valid only until next call to it.
 **/
static gchar*
gwy_inventory_invent_name(GwyInventory *inventory,
                          const gchar *prefix)
{
    const gchar *p, *last;
    GString *str;
    gint n, i;

    str = g_string_new(prefix ? prefix : _("Untitled"));
    if (!gwy_inventory_lookup(inventory, str->str))
        return g_string_free(str, FALSE);

    last = str->str + MAX(str->len-1, 0);
    for (p = last; p >= str->str; p--) {
        if (!g_ascii_isdigit(*p))
            break;
    }
    if (p == last || (p >= str->str && !g_ascii_isspace(*p)))
        p = last;
    while (p >= str->str && g_ascii_isspace(*p))
        p--;
    g_string_truncate(str, p+1 - str->str);

    g_string_append_c(str, ' ');
    n = str->len;
    for (i = 1; i < 10000; i++) {
        g_string_append_printf(str, "%d", i);
        if (!gwy_inventory_lookup(inventory, str->str))
            return g_string_free(str, FALSE);

        g_string_truncate(str, n);
    }
    g_string_free(str, TRUE);
    g_assert_not_reached();
    return NULL;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyinventory
 * @title: GwyInventory
 * @short_description: Ordered item inventory, indexed by both name and
 *                     position.
 * @see_also: #GwyContainer
 *
 * #GwyInventory is a uniform container that offers both hash table and array
 * (sorted or unsorted) interfaces.  Both types of read access are fast,
 * operations that modify it may be slower.  Inventory can also maintain a
 * notion of default item.
 *
 * #GwyInventory can be used both as an actual container for some data, or just
 * wrap a static array with a the same interface so the actual storage is
 * opaque to inventory user.  The former kind of inventories can be created
 * with gwy_inventory_new() or gwy_inventory_new_filled(); constant inventory
 * is created with gwy_inventory_new_from_array().  Contantess of an inventory
 * can be tested with gwy_inventory_is_const().
 *
 * Possible operations with data items stored in an inventory are specified
 * upon inventory creation with #GwyInventoryItemType structure.  Not all
 * fields are mandatory, with items allowing more operations the inventory is
 * more capable too.  For example, if items offer a method to make copies,
 * gwy_inventory_new_item() can be used to directly create new items in the
 * inventory (this capability can be tested with
 * gwy_inventory_can_make_copies()).
 *
 * Item can have `traits', that is data that can be obtained generically. They
 * are similar to #GObject properties.  Actually, if items are objects, they
 * should simply map object properties to traits.  But it is possible to define
 * traits for simple structures too.
 **/

/**
 * GwyInventory:
 *
 * The #GwyInventory struct contains private data only and should be accessed
 * using the functions below.
 **/

/**
 * GwyInventoryItemType:
 * @type: Object type, if item is object or other type with registered GType.
 *        May be zero to indicate an unregistered item type.
 *        If items are objects, inventory takes a reference on them.
 * @watchable_signal: Item signal name to watch, used only for objects.
 *                    When item emits this signal, inventory emits
 *                    "item-updated" signal for it.
 *                    May be %NULL to indicate no signal should be watched,
 *                    you can still emit "item-updated" with
 *                    gwy_inventory_item_updated() or
 *                    gwy_inventory_nth_item_updated().
 * @is_fixed: If not %NULL and returns %TRUE for some item, such an item
 *            cannot be removed from inventory, fixed items can be only
 *            added.  This is checked each time an attempt is made to remove
 *            an item.
 * @get_name: Returns item name (the string is owned by item and it is assumed
 *            to exist until item ceases to exist or is renamed).  This
 *            function is obligatory.
 * @compare: Item comparation function for sorting.
 *           If %NULL, inventory never attempts to keep any item order
 *           and gwy_inventory_restore_order() does nothing.
 *           Otherwise inventory is sorted unless sorting is (temporarily)
 *           disabled with gwy_inventory_forget_order() or it was created
 *           with gwy_inventory_new_filled() and the initial array was
 *           not sorted according to @compare.
 * @rename: Function to rename an item.  If not %NULL, calls to
 *          gwy_inventory_rename_item() are possible.  Note items must not
 *          be renamed by any other means than this method, because when
 *          an item is renamed and inventory does not know it, very bad
 *          things will happen and you will lose all your good karma.
 * @dismantle: Called on item before it's removed from inventory.  May be
 *             %NULL.
 * @copy: Method to create a copy of an item.  If this function and @rename are
 *        defined, calls to gwy_inventory_new_item() are possible.  Inventory
 *        sets the copy's name immediately after creation, so it normally
 *        does not matter which name @copy gives it.
 * @get_traits: Function to get item traits.  It returns array of item trait
 *              #GTypes (keeping its ownership) and if @nitems is not %NULL,
 *              it stores the length of returned array there.
 * @get_trait_name: Returns name of @i-th trait (keeping ownership of the
 *                  returned string).  It is not obligatory, but advisable to
 *                  give traits names.
 * @get_trait_value: Sets @value to value of @i-th trait of item.
 *
 * Infromation about a #GwyInventory item type.
 *
 * Note only one of the fields must be always defined: @get_name.  All the
 * others give inventory (and thus inventory users) some additional powers over
 * items.  They may be set to %NULL or 0 if particular item type does not
 * (want to) support this operation.
 *
 * The three trait methods are not used by #GwyInventory itself, but allows
 * #GwyInventoryStore to generically map item properties to virtual columns
 * of a #GtkTreeModel.  If items are objects, you will usually want to
 * directly map some or all #GObject properties to item traits.  If they are
 * plain C structs or something else, you can easily export their data members
 * as virtual #GtkTreeModel columns by defining traits for them.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
