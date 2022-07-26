/*
 *  $Id: gwycombobox.h 24329 2021-10-11 15:08:54Z yeti-dn $
 *  Copyright (C) 2003 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_COMBO_BOX_H__
#define __GWY_COMBO_BOX_H__

#include <gtk/gtkcombobox.h>
#include <libgwyddion/gwyenum.h>
#include <libgwyddion/gwysiunit.h>
#include <libprocess/lawn.h>
#include <libgwydgets/gwygraphmodel.h>

G_BEGIN_DECLS

GtkWidget* gwy_enum_combo_box_new            (const GwyEnum *entries,
                                              gint nentries,
                                              GCallback callback,
                                              gpointer cbdata,
                                              gint active,
                                              gboolean translate);
GtkWidget* gwy_enum_combo_box_newl           (GCallback callback,
                                              gpointer cbdata,
                                              gint active,
                                              ...);
GtkWidget* gwy_combo_box_metric_unit_new     (GCallback callback,
                                              gpointer cbdata,
                                              gint from,
                                              gint to,
                                              GwySIUnit *unit,
                                              gint active);
void       gwy_combo_box_metric_unit_set_unit(GtkComboBox *combo,
                                              gint from,
                                              gint to,
                                              GwySIUnit *unit);
GtkWidget* gwy_combo_box_graph_curve_new     (GCallback callback,
                                              gpointer cbdata,
                                              GwyGraphModel *gmodel,
                                              gint current);
GtkWidget* gwy_combo_box_lawn_curve_new      (GCallback callback,
                                              gpointer cbdata,
                                              GwyLawn *lawn,
                                              gint current);
GtkWidget* gwy_combo_box_lawn_segment_new    (GCallback callback,
                                              gpointer cbdata,
                                              GwyLawn *lawn,
                                              gint current);
void       gwy_enum_combo_box_set_active     (GtkComboBox *combo,
                                              gint active);
gint       gwy_enum_combo_box_get_active     (GtkComboBox *combo);
void       gwy_enum_combo_box_update_int     (GtkComboBox *combo,
                                              gint *integer);

G_END_DECLS

#endif /* __GWY_COMBO_BOX_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
