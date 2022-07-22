/*
 *  $Id: logging.h 20677 2017-12-18 18:22:52Z yeti-dn $
 *  Copyright (C) 2016 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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

#ifndef __GWY_APP_LOGGING_H__
#define __GWY_APP_LOGGING_H__

#include <gtk/gtk.h>
#include <libgwydgets/gwydataview.h>

G_BEGIN_DECLS

typedef enum {
    GWY_APP_LOGGING_TO_FILE    = (1 << 0),
    GWY_APP_LOGGING_TO_CONSOLE = (1 << 1),
} GwyAppLoggingFlags;

void           gwy_app_setup_logging      (GwyAppLoggingFlags flags);
GtkTextBuffer* gwy_app_get_log_text_buffer(void);

G_END_DECLS

#endif /* __GWY_APP_LOGGING_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
