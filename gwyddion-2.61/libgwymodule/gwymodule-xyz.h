/*
 *  $Id: gwymodule-xyz.h 20677 2017-12-18 18:22:52Z yeti-dn $
 *  Copyright (C) 2003-2004,2013,2016 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_MODULE_XYZ_H__
#define __GWY_MODULE_XYZ_H__

#include <libgwyddion/gwycontainer.h>
#include <libgwymodule/gwymoduleenums.h>
#include <libgwymodule/gwymoduleloader.h>

G_BEGIN_DECLS

typedef void (*GwyXYZFunc)(GwyContainer *data,
                           GwyRunType run,
                           const gchar *name);

gboolean     gwy_xyz_func_register            (const gchar *name,
                                               GwyXYZFunc func,
                                               const gchar *menu_path,
                                               const gchar *stock_id,
                                               GwyRunType run,
                                               guint sens_mask,
                                               const gchar *tooltip);
void         gwy_xyz_func_run                 (const gchar *name,
                                               GwyContainer *data,
                                               GwyRunType run);
gboolean     gwy_xyz_func_exists              (const gchar *name);
GwyRunType   gwy_xyz_func_get_run_types       (const gchar *name);
const gchar* gwy_xyz_func_get_menu_path       (const gchar *name);
const gchar* gwy_xyz_func_get_stock_id        (const gchar *name);
const gchar* gwy_xyz_func_get_tooltip         (const gchar *name);
guint        gwy_xyz_func_get_sensitivity_mask(const gchar *name);
void         gwy_xyz_func_foreach             (GFunc function,
                                               gpointer user_data);
const gchar* gwy_xyz_func_current             (void);

G_END_DECLS

#endif /* __GWY_MODULE_XYZ_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
