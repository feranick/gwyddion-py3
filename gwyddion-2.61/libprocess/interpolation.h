/*
 *  $Id: interpolation.h 20678 2017-12-18 18:26:55Z yeti-dn $
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

#ifndef __GWY_PROCESS_INTERPOLATION_H__
#define __GWY_PROCESS_INTERPOLATION_H__

#include <glib.h>
#include <libprocess/gwyprocessenums.h>

G_BEGIN_DECLS

gdouble gwy_interpolation_get_dval(gdouble x,
                                   gdouble x1_,
                                   gdouble y1_,
                                   gdouble x2_,
                                   gdouble y2_,
                                   GwyInterpolationType interpolation) G_GNUC_CONST;

gdouble
gwy_interpolation_get_dval_of_equidists(gdouble x,
                                        gdouble *data,
                                        GwyInterpolationType interpolation) G_GNUC_PURE;

gdouble
gwy_interpolation_interpolate_1d(gdouble x,
                                 const gdouble *coeff,
                                 GwyInterpolationType interpolation) G_GNUC_PURE;

gdouble
gwy_interpolation_interpolate_2d(gdouble x,
                                 gdouble y,
                                 gint rowstride,
                                 const gdouble *coeff,
                                 GwyInterpolationType interpolation) G_GNUC_PURE;

gboolean
gwy_interpolation_has_interpolating_basis(GwyInterpolationType interpolation) G_GNUC_CONST;

gint
gwy_interpolation_get_support_size(GwyInterpolationType interpolation) G_GNUC_CONST;

void
gwy_interpolation_resolve_coeffs_1d(gint n,
                                    gdouble *data,
                                    GwyInterpolationType interpolation);

void
gwy_interpolation_resolve_coeffs_2d(gint width,
                                    gint height,
                                    gint rowstride,
                                    gdouble *data,
                                    GwyInterpolationType interpolation);

void
gwy_interpolation_resample_block_1d(gint length,
                                    gdouble *data,
                                    gint newlength,
                                    gdouble *newdata,
                                    GwyInterpolationType interpolation,
                                    gboolean preserve);

void
gwy_interpolation_resample_block_2d(gint width,
                                    gint height,
                                    gint rowstride,
                                    gdouble *data,
                                    gint newwidth,
                                    gint newheight,
                                    gint newrowstride,
                                    gdouble *newdata,
                                    GwyInterpolationType interpolation,
                                    gboolean preserve);

void
gwy_interpolation_shift_block_1d(gint length,
                                 gdouble *data,
                                 gdouble offset,
                                 gdouble *newdata,
                                 GwyInterpolationType interpolation,
                                 GwyExteriorType exterior,
                                 gdouble fill_value,
                                 gboolean preserve);

G_END_DECLS

#endif /* __GWY_PROCESS_INTERPOLATION_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
