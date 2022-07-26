/*
 *  $Id: gwy3dlabel.h 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2005 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_3D_LABEL_H__
#define __GWY_3D_LABEL_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GWY_TYPE_3D_LABEL            (gwy_3d_label_get_type())
#define GWY_3D_LABEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_3D_LABEL, Gwy3DLabel))
#define GWY_3D_LABEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_3D_LABEL, Gwy3DLabelClass))
#define GWY_IS_3D_LABEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_3D_LABEL))
#define GWY_IS_3D_LABEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_3D_LABEL))
#define GWY_3D_LABEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_3D_LABEL, Gwy3DLabelClass))

typedef struct _Gwy3DLabel                Gwy3DLabel;
typedef struct _Gwy3DLabelClass           Gwy3DLabelClass;

struct _Gwy3DLabel {
    GObject parent_instance;

    /* public read-only */
    gdouble delta_x;
    gdouble delta_y;
    gdouble rotation;
    gdouble size;
    gboolean fixed_size;

    gdouble double1;
    gint int1;
    gpointer reserved1;

    /* private */
    gchar *default_text;
    GString *text;
};

struct _Gwy3DLabelClass {
    GObjectClass parent_class;

    void (*reserved1)(void);
    void (*reserved2)(void);
};

GType          gwy_3d_label_get_type                (void) G_GNUC_CONST;
Gwy3DLabel*    gwy_3d_label_new                     (const gchar *default_text);
void           gwy_3d_label_set_text                (Gwy3DLabel *label,
                                                     const gchar *text);
const gchar*   gwy_3d_label_get_text                (Gwy3DLabel *label);
gchar*         gwy_3d_label_expand_text             (Gwy3DLabel *label,
                                                     GHashTable *variables);
void           gwy_3d_label_reset                   (Gwy3DLabel *label);
void           gwy_3d_label_reset_text              (Gwy3DLabel *label);
gdouble        gwy_3d_label_user_size               (Gwy3DLabel *label,
                                                     gdouble user_size);

G_END_DECLS

#endif  /* __GWY_3D_LABEL_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
