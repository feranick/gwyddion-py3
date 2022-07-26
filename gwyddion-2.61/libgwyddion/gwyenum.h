/*
 *  $Id: gwyenum.h 24147 2021-09-16 15:56:25Z yeti-dn $
 *  Copyright (C) 2005 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __GWY_ENUM_H__
#define __GWY_ENUM_H__

#include <glib-object.h>
#include <libgwyddion/gwyinventory.h>

G_BEGIN_DECLS

#define GWY_TYPE_ENUM                  (gwy_enum_get_type())

typedef struct {
    /*<public>*/
    const gchar *name;
    gint value;
} GwyEnum;

GType         gwy_enum_get_type        (void)                      G_GNUC_CONST;
gint          gwy_string_to_enum       (const gchar *str,
                                        const GwyEnum *enum_table,
                                        gint n);
const gchar*  gwy_enum_to_string       (gint enumval,
                                        const GwyEnum *enum_table,
                                        gint n);
gchar*        gwy_enuml_to_string      (gint enumval,
                                        ...)                       G_GNUC_NULL_TERMINATED;
gint          gwy_string_to_flags      (const gchar *str,
                                        const GwyEnum *enum_table,
                                        gint n,
                                        const gchar *delimiter);
gchar*        gwy_flags_to_string      (gint enumval,
                                        const GwyEnum *enum_table,
                                        gint n,
                                        const gchar *glue);
gint          gwy_enum_sanitize_value  (gint enumval,
                                        GType enum_type);
void          gwy_enum_freev           (GwyEnum *enum_table);
GwyInventory* gwy_enum_inventory_new   (const GwyEnum *enum_table,
                                        gint n);
GwyEnum*      gwy_enum_fill_from_struct(GwyEnum *enum_table,
                                        gint n,
                                        gconstpointer items,
                                        guint item_size,
                                        gint name_offset,
                                        gint value_offset);

G_END_DECLS

#endif /* __GWY_ENUM_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
