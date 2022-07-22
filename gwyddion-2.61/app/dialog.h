/*
 *  $Id: dialog.h 23730 2021-05-17 15:48:27Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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

#ifndef __GWY_DIALOG_H__
#define __GWY_DIALOG_H__

#include <gtk/gtk.h>
#include <app/param-table.h>

G_BEGIN_DECLS

typedef enum {
    GWY_DIALOG_CANCEL      = 0,
    GWY_DIALOG_PROCEED     = 1,
    GWY_DIALOG_HAVE_RESULT = 2,
} GwyDialogOutcome;

typedef enum {
    GWY_PREVIEW_NONE         = 0,
    GWY_PREVIEW_IMMEDIATE    = 1,
    GWY_PREVIEW_UPON_REQUEST = 2,
} GwyPreviewType;

typedef enum {
    GWY_RESPONSE_RESET       = 1,
    GWY_RESPONSE_UPDATE      = 2,
    GWY_RESPONSE_CLEAR       = 3,
} GwyResponseType;

typedef void (*GwyDialogPreviewFunc)(gpointer user_data);

#define GWY_TYPE_DIALOG            (gwy_dialog_get_type())
#define GWY_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_DIALOG, GwyDialog))
#define GWY_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_DIALOG, GwyDialogClass))
#define GWY_IS_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_DIALOG))
#define GWY_IS_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_DIALOG))
#define GWY_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_DIALOG, GwyDialogClass))

typedef struct _GwyDialog      GwyDialog;
typedef struct _GwyDialogClass GwyDialogClass;

struct _GwyDialog {
    GtkDialog parent;
    struct _GwyDialogPrivate *priv;
};

struct _GwyDialogClass {
    GtkDialogClass parent;
};

GType            gwy_dialog_get_type                 (void)                         G_GNUC_CONST;
GtkWidget*       gwy_dialog_new                      (const gchar *title);
void             gwy_dialog_add_buttons              (GwyDialog *dialog,
                                                      gint response_id,
                                                      ...);
void             gwy_dialog_add_content              (GwyDialog *dialog,
                                                      GtkWidget *child,
                                                      gboolean expand,
                                                      gboolean fill,
                                                      gint padding);
void             gwy_dialog_add_param_table          (GwyDialog *dialog,
                                                      GwyParamTable *partable);
void             gwy_dialog_remove_param_table       (GwyDialog *dialog,
                                                      GwyParamTable *partable);
void             gwy_dialog_set_preview_func         (GwyDialog *dialog,
                                                      GwyPreviewType prevtype,
                                                      GwyDialogPreviewFunc preview,
                                                      gpointer user_data,
                                                      GDestroyNotify destroy);
void             gwy_dialog_set_instant_updates_param(GwyDialog *dialog,
                                                      gint id);
GwyDialogOutcome gwy_dialog_run                      (GwyDialog *dialog);
void             gwy_dialog_invalidate               (GwyDialog *dialog);
void             gwy_dialog_have_result              (GwyDialog *dialog);

G_END_DECLS

#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
