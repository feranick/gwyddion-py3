/*
 *  $Id: gwyprocess.h 23915 2021-08-03 08:31:13Z yeti-dn $
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

#ifndef __GWY_GWYPROCESS_H__
#define __GWY_GWYPROCESS_H__

#include <libprocess/gwyprocessenums.h>
#include <libprocess/gwyprocesstypes.h>

#include <libprocess/arithmetic.h>
#include <libprocess/brick.h>
#include <libprocess/cdline.h>
#include <libprocess/correct.h>
#include <libprocess/correlation.h>
#include <libprocess/cwt.h>
#include <libprocess/datafield.h>
#include <libprocess/dataline.h>
#include <libprocess/dwt.h>
#include <libprocess/elliptic.h>
#include <libprocess/filters.h>
#include <libprocess/fractals.h>
#include <libprocess/grains.h>
#include <libprocess/gwygrainvalue.h>
#include <libprocess/hough.h>
#include <libprocess/interpolation.h>
#include <libprocess/lawn.h>
#include <libprocess/simplefft.h>
#include <libprocess/spectra.h>
#include <libprocess/linestats.h>
#include <libprocess/inttrans.h>
#include <libprocess/spline.h>
#include <libprocess/stats.h>
#include <libprocess/level.h>
#include <libprocess/tip.h>
#include <libprocess/mfm.h>
#include <libprocess/synth.h>
#include <libprocess/surface.h>
#include <libprocess/peaks.h>
#include <libprocess/triangulation.h>
#include <libprocess/gwyshapefitpreset.h>
#include <libprocess/gwycalibration.h>
#include <libprocess/gwycaldata.h>
#include <libprocess/stats_uncertainty.h>

G_BEGIN_DECLS

void gwy_process_type_init(void);

G_END_DECLS

#endif /* __GWY_GWYPROCESS_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
