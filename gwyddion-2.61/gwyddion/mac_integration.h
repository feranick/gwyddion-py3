/*
 *  $Id: mac_integration.h 21345 2018-08-27 18:48:04Z yeti-dn $
 *  Copyright (C) 2009 Andrey Gruzdev.
 *  E-mail: gruzdev@ntmdt.ru.
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

#ifndef __MAC_INTEGRATION_H__
#define __MAC_INTEGRATION_H__

#include <gtk/gtk.h>

void     gwy_osx_init_handler        (int *argc);
void     gwy_osx_remove_handler      (void);
gboolean gwy_osx_open_files          (void);
void     gwy_osx_set_locale          (void);
void     gwy_osx_get_menu_from_widget(GtkWidget *container);

#endif
