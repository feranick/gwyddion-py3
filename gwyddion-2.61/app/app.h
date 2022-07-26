/*
 *  $Id: app.h 23936 2021-08-09 09:19:54Z yeti-dn $
 *  Copyright (C) 2003-2019 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_APP_APP_H__
#define __GWY_APP_APP_H__

#include <gtk/gtkwindow.h>
#include <libgwydgets/gwydataview.h>
#include <app/gwytool.h>

G_BEGIN_DECLS

void         gwy_app_switch_tool                (const gchar *toolname);
const gchar* gwy_app_current_tool_name          (void);
GwyTool*     gwy_app_current_tool               (void);
void         gwy_app_add_main_accel_group       (GtkWindow *window);
void         gwy_app_save_window_position       (GtkWindow *window,
                                                 const gchar *prefix,
                                                 gboolean position,
                                                 gboolean size);
void         gwy_app_restore_window_position    (GtkWindow *window,
                                                 const gchar *prefix,
                                                 gboolean grow_only);
GtkWidget*   gwy_app_main_window_get            (void);
void         gwy_app_main_window_set            (GtkWidget *window);
void         gwy_app_data_view_change_mask_color(GwyDataView *data_view);
gboolean     gwy_app_quit                       (void);
void         gwy_app_init_widget_styles         (void);
void         gwy_app_init_i18n                  (void);
gboolean     gwy_app_init_common                (GError **error,
                                                 ...);
void         gwy_app_init_nongui                (const gchar *option,
                                                 ...);

G_END_DECLS

#endif /* __GWY_APP_APP_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
