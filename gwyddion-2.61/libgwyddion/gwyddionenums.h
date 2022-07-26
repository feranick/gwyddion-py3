/*
 *  $Id: gwyddionenums.h 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2005-2016 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWYDDION_ENUMS_H__
#define __GWYDDION_ENUMS_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    GWY_SI_UNIT_FORMAT_NONE      = 0,
    GWY_SI_UNIT_FORMAT_PLAIN     = 1,
    GWY_SI_UNIT_FORMAT_MARKUP    = 2,
    GWY_SI_UNIT_FORMAT_VFMARKUP  = 3,
    GWY_SI_UNIT_FORMAT_TEX       = 4,
    GWY_SI_UNIT_FORMAT_VFTEX     = 5,
    GWY_SI_UNIT_FORMAT_UNICODE   = 6,
    GWY_SI_UNIT_FORMAT_VFUNICODE = 7,
} GwySIUnitFormatStyle;

typedef enum {
    GWY_NLFIT_PARAM_ANGLE  = 1 << 0,
    GWY_NLFIT_PARAM_ABSVAL = 1 << 1,
} GwyNLFitParamFlags;

typedef enum {
    GWY_PERCENTILE_INTERPOLATION_LINEAR   = 0,
    GWY_PERCENTILE_INTERPOLATION_LOWER    = 1,
    GWY_PERCENTILE_INTERPOLATION_HIGHER   = 2,
    GWY_PERCENTILE_INTERPOLATION_NEAREST  = 3,
    GWY_PERCENTILE_INTERPOLATION_MIDPOINT = 4,
} GwyPercentileInterpolationType;

G_END_DECLS

#endif /*__GWYDDION_ENUMS_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
