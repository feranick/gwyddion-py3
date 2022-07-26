/*
 *  $Id: file.h 24584 2022-02-05 10:48:18Z yeti-dn $
 *  Copyright (C) 2003-2006 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_APP_FILE_H__
#define __GWY_APP_FILE_H__

#include <libgwyddion/gwycontainer.h>
#include <gtk/gtkwidget.h>

G_BEGIN_DECLS

const gchar*  gwy_app_get_current_directory(void);
void          gwy_app_set_current_directory(const gchar *directory);

GwyContainer* gwy_app_file_load            (const gchar *filename_utf8,
                                            const gchar *filename_sys,
                                            const gchar *name);
void          gwy_app_file_open            (void);
void          gwy_app_file_merge           (void);
gboolean      gwy_app_file_write           (GwyContainer *data,
                                            const gchar *filename_utf8,
                                            const gchar *filename_sys,
                                            const gchar *name);
void          gwy_app_file_save            (void);
void          gwy_app_file_save_as         (void);
void          gwy_app_file_close           (void);
gboolean      gwy_app_file_confirm_overwrite (GtkWidget *chooser);

G_END_DECLS

#endif /* __GWY_APP_FILE_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
