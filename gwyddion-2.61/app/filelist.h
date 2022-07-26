/*
 *  $Id: filelist.h 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2004 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_APP_FILE_LIST_H__
#define __GWY_APP_FILE_LIST_H__

#include <gtk/gtkwidget.h>
#include <libgwyddion/gwycontainer.h>

G_BEGIN_DECLS

GtkWidget* gwy_app_recent_file_list_new     (void);
void       gwy_app_recent_file_list_update  (GwyContainer *data,
                                             const gchar *filename_utf8,
                                             const gchar *filename_sys,
                                             gint hint);
gboolean   gwy_app_recent_file_list_load    (const gchar *filename);
gboolean   gwy_app_recent_file_list_save    (const gchar *filename);
void       gwy_app_recent_file_list_free    (void);
GdkPixbuf* gwy_app_recent_file_get_thumbnail(const gchar *filename_utf8);

G_END_DECLS

#endif
/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

