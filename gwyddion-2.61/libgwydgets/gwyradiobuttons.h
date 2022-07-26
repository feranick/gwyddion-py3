/*
 *  $Id: gwyradiobuttons.h 21106 2018-05-27 10:20:06Z yeti-dn $
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

#ifndef __GWY_RADIO_BUTTONS_H__
#define __GWY_RADIO_BUTTONS_H__

#include <gtk/gtktable.h>
#include <libgwyddion/gwyenum.h>

G_BEGIN_DECLS

GSList*    gwy_radio_buttons_create         (const GwyEnum *entries,
                                             gint nentries,
                                             GCallback callback,
                                             gpointer cbdata,
                                             gint current);
GSList*    gwy_radio_buttons_createl        (GCallback callback,
                                             gpointer cbdata,
                                             gint current,
                                             ...);
gint       gwy_radio_buttons_attach_to_table(GSList *group,
                                             GtkTable *table,
                                             gint colspan,
                                             gint row);
gboolean   gwy_radio_buttons_set_current    (GSList *group,
                                             gint current);
gint       gwy_radio_buttons_get_current    (GSList *group);
GtkWidget* gwy_radio_buttons_find           (GSList *group,
                                             gint value);
void       gwy_radio_buttons_set_sensitive  (GSList *group,
                                             gboolean sensitive);
gint       gwy_radio_button_get_value       (GtkWidget *button);
void       gwy_radio_button_set_value       (GtkWidget *button,
                                             gint value);

G_END_DECLS

#endif /* __GWY_RADIO_BUTTONS_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

