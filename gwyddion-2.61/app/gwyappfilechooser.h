/*
 *  $Id: gwyappfilechooser.h 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2006 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_APP_FILE_CHOOSER_H__
#define __GWY_APP_FILE_CHOOSER_H__

/*< private_header >*/

#include <gtk/gtkfilechooserdialog.h>

G_BEGIN_DECLS

#define GWY_TYPE_APP_FILE_CHOOSER             (_gwy_app_file_chooser_get_type())
#define GWY_APP_FILE_CHOOSER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_APP_FILE_CHOOSER, GwyAppFileChooser))
#define GWY_APP_FILE_CHOOSER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_APP_FILE_CHOOSER, GwyAppFileChooserClass))
#define GWY_IS_APP_FILE_CHOOSER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_APP_FILE_CHOOSER))
#define GWY_IS_APP_FILE_CHOOSER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_APP_FILE_CHOOSER))
#define GWY_APP_FILE_CHOOSER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_APP_FILE_CHOOSER, GwyAppFileChooserClass))

typedef struct _GwyAppFileChooser      GwyAppFileChooser;
typedef struct _GwyAppFileChooserClass GwyAppFileChooserClass;

struct _GwyAppFileChooser {
    GtkFileChooserDialog parent_instance;

    GQuark type_key;
    const gchar *prefix;
    gchar *filetype;

    GtkWidget *type_list;

    /* Filtering */
    GtkWidget *expander;
    GtkFileFilter *no_filter;
    GtkFileFilter *filter;
    GtkWidget *loadable_check;
    gboolean only_loadable;
    GString *glob;
    gboolean glob_casesens;
    GtkWidget *glob_entry;
    GtkWidget *glob_case_check;
    GPatternSpec *pattern;

    /* Preview */
    GtkWidget *preview;
    GtkWidget *preview_filename;
    GtkWidget *preview_type;
    GObject *renderer_fileinfo;

    guint full_preview_id;
    gboolean make_thumbnail;
    gchar *preview_name_sys;
};

struct _GwyAppFileChooserClass {
    GtkFileChooserDialogClass parent_class;
};

G_GNUC_INTERNAL
GType      _gwy_app_file_chooser_get_type         (void) G_GNUC_CONST;


G_GNUC_INTERNAL
GtkWidget* _gwy_app_file_chooser_get              (GtkFileChooserAction action);

G_GNUC_INTERNAL
gchar*     _gwy_app_file_chooser_get_selected_type (GwyAppFileChooser *chooser);

G_END_DECLS

#endif /* __GWY_APP_FILE_CHOOSER_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
