/*
 *  $Id: gwygraphareadialog.h 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2003-2007 David Necas (Yeti), Petr Klapetek.
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

/*< private_header >*/

#ifndef __GWY_GRAPH_AREA_DIALOG_H__
#define __GWY_GRAPH_AREA_DIALOG_H__

#include <gtk/gtkdialog.h>
#include <libgwydgets/gwygraphbasics.h>
#include <libgwydgets/gwygraphcurvemodel.h>

G_BEGIN_DECLS

#define GWY_TYPE_GRAPH_AREA_DIALOG            (_gwy_graph_area_dialog_get_type())
#define GWY_GRAPH_AREA_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_GRAPH_AREA_DIALOG, GwyGraphAreaDialog))
#define GWY_GRAPH_AREA_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_GRAPH_AREA_DIALOG, GwyGraphAreaDialogClass))
#define GWY_IS_GRAPH_AREA_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_GRAPH_AREA_DIALOG))
#define GWY_IS_GRAPH_AREA_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_GRAPH_AREA_DIALOG))
#define GWY_GRAPH_AREA_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_GRAPH_AREA_DIALOG, GwyGraphAreaDialogClass))

typedef struct _GwyGraphAreaDialog      GwyGraphAreaDialog;
typedef struct _GwyGraphAreaDialogClass GwyGraphAreaDialogClass;

typedef enum {
    GWY_GRAPH_AREA_DIALOG_RESPONSE_PREV = 1,
    GWY_GRAPH_AREA_DIALOG_RESPONSE_NEXT = 2,
} GwyGraphAreaDialogResponse;

struct _GwyGraphAreaDialog {
    GtkDialog dialog;

    GtkWidget *curvetype_menu;
    GtkWidget *color_button;
    GtkWidget *pointtype_menu;
    GtkWidget *linestyle_menu;
    GtkObject *pointsize;
    GtkObject *thickness;

    GtkWidget *sci_text;

    GtkWidget *color_dialog;
    GtkWidget *color_selector;
    GwyRGBA old_color;

    GtkWidget *prev;
    GtkWidget *next;
    gboolean prev_possible;
    gboolean next_possible;

    GwyGraphCurveModel *curve_model;
};

struct _GwyGraphAreaDialogClass {
    GtkDialogClass parent_class;
};

G_GNUC_INTERNAL
GType      _gwy_graph_area_dialog_get_type (void) G_GNUC_CONST;

G_GNUC_INTERNAL
GtkWidget*  _gwy_graph_area_dialog_new     (void);

G_GNUC_INTERNAL
void        _gwy_graph_area_dialog_set_curve_data(GtkWidget *dialog,
                                                  GwyGraphCurveModel *cmodel);

G_GNUC_INTERNAL
void        _gwy_graph_area_dialog_set_switching(GtkWidget *dialog,
                                                 gboolean prev_possible,
                                                 gboolean next_possible);

/* Helpers */
G_GNUC_INTERNAL
GtkTreeModel* _gwy_graph_get_point_type_store(GtkWidget *widget);

G_GNUC_INTERNAL
GtkTreeModel* _gwy_graph_get_line_style_store(GtkWidget *widget);

G_END_DECLS

#endif /* __GWY_GRAPH_AREA_DIALOG_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
