/*
 *  $Id: gwyruler.h 20678 2017-12-18 18:26:55Z yeti-dn $
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

/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * GwyRuler is based on GtkRuler (instead of subclassing) since GtkRuler
 * can be subject of removal from Gtk+ in some unspecified point in the future.
 */

#ifndef __GWY_RULER_H__
#define __GWY_RULER_H__

#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>

#include <libgwyddion/gwysiunit.h>
#include <libgwydgets/gwydgetenums.h>

G_BEGIN_DECLS

#define GWY_TYPE_RULER            (gwy_ruler_get_type())
#define GWY_RULER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_RULER, GwyRuler))
#define GWY_RULER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_RULER, GwyRulerClass))
#define GWY_IS_RULER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_RULER))
#define GWY_IS_RULER_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_RULER))
#define GWY_RULER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_RULER, GwyRulerClass))


typedef struct _GwyRuler        GwyRuler;
typedef struct _GwyRulerClass   GwyRulerClass;

struct _GwyRuler {
    GtkWidget widget;

    PangoLayout *layout;
    GdkPixmap *backing_store;
    GdkGC *non_gr_exp_gc;
    gint xsrc, ysrc;
    gint hthickness, vthickness, height, pixelsize;
    GwySIUnit *units;
    GwyUnitsPlacement units_placement;

    gdouble lower;    /* The upper limit of the ruler (in physical units) */
    gdouble upper;    /* The lower limit of the ruler */
    gdouble position;    /* The position of the mark on the ruler */
    gdouble max_size;    /* The maximum size of the ruler */

    GwySIValueFormat *vformat;
    gpointer reserved2;
};

struct _GwyRulerClass {
    GtkWidgetClass parent_class;

    /* Virtual methods */
    void (*prepare_sizes) (GwyRuler *ruler);
    void (*draw_frame) (GwyRuler *ruler);
    void (*draw_layout) (GwyRuler *ruler,
                         gint hpos,
                         gint vpos);
    void (*draw_tick) (GwyRuler *ruler,
                       gint pos,
                       gint length);
    void (*draw_pos)   (GwyRuler *ruler);

    void (*reserved1)(void);
    void (*reserved2)(void);
};


GType             gwy_ruler_get_type            (void) G_GNUC_CONST;
void              gwy_ruler_set_range           (GwyRuler      *ruler,
                                                 gdouble        lower,
                                                 gdouble        upper,
                                                 gdouble        position,
                                                 gdouble        max_size);

void              gwy_ruler_draw_pos            (GwyRuler *ruler);
void              gwy_ruler_get_range           (GwyRuler *ruler,
                                                 gdouble  *lower,
                                                 gdouble  *upper,
                                                 gdouble  *position,
                                                 gdouble  *max_size);
void              gwy_ruler_set_si_unit         (GwyRuler *ruler,
                                                 GwySIUnit *units);
GwySIUnit*        gwy_ruler_get_si_unit         (GwyRuler *ruler);
GwyUnitsPlacement gwy_ruler_get_units_placement (GwyRuler *ruler);
void              gwy_ruler_set_units_placement (GwyRuler *ruler,
                                                 GwyUnitsPlacement placement);

G_END_DECLS

#endif /* __GWY_RULER_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
