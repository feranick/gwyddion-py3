/*
 *  $Id: inttrans.h 24765 2022-04-14 15:03:32Z yeti-dn $
 *  Copyright (C) 2003-2022 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_PROCESS_INTTRANS_H__
#define __GWY_PROCESS_INTTRANS_H__

#include <libprocess/datafield.h>
#include <libprocess/cwt.h>

G_BEGIN_DECLS

gint gwy_fft_find_nice_size         (gint size);

void gwy_data_line_fft               (GwyDataLine *rsrc,
                                      GwyDataLine *isrc,
                                      GwyDataLine *rdest,
                                      GwyDataLine *idest,
                                      GwyWindowingType windowing,
                                      GwyTransformDirection direction,
                                      GwyInterpolationType interpolation,
                                      gboolean preserverms,
                                      gint level);
void gwy_data_line_part_fft          (GwyDataLine *rsrc,
                                      GwyDataLine *isrc,
                                      GwyDataLine *rdest,
                                      GwyDataLine *idest,
                                      gint from,
                                      gint len,
                                      GwyWindowingType windowing,
                                      GwyTransformDirection direction,
                                      GwyInterpolationType interpolation,
                                      gboolean preserverms,
                                      gint level);
void gwy_data_line_fft_raw           (GwyDataLine *rsrc,
                                      GwyDataLine *isrc,
                                      GwyDataLine *rdest,
                                      GwyDataLine *idest,
                                      GwyTransformDirection direction);
void gwy_data_line_zoom_fft          (GwyDataLine *rsrc,
                                      GwyDataLine *isrc,
                                      GwyDataLine *rdest,
                                      GwyDataLine *idest,
                                      gint m,
                                      gdouble f0,
                                      gdouble f1);
void gwy_data_field_1dfft            (GwyDataField *rin,
                                      GwyDataField *iin,
                                      GwyDataField *rout,
                                      GwyDataField *iout,
                                      GwyOrientation orientation,
                                      GwyWindowingType windowing,
                                      GwyTransformDirection direction,
                                      GwyInterpolationType interpolation,
                                      gboolean preserverms,
                                      gint level);
void gwy_data_field_area_1dfft       (GwyDataField *rin,
                                      GwyDataField *iin,
                                      GwyDataField *rout,
                                      GwyDataField *iout,
                                      gint col,
                                      gint row,
                                      gint width,
                                      gint height,
                                      GwyOrientation orientation,
                                      GwyWindowingType windowing,
                                      GwyTransformDirection direction,
                                      GwyInterpolationType interpolation,
                                      gboolean preserverms,
                                      gint level);
void gwy_data_field_1dfft_raw        (GwyDataField *rin,
                                      GwyDataField *iin,
                                      GwyDataField *rout,
                                      GwyDataField *iout,
                                      GwyOrientation orientation,
                                      GwyTransformDirection direction);
void gwy_data_field_2dfft            (GwyDataField *rin,
                                      GwyDataField *iin,
                                      GwyDataField *rout,
                                      GwyDataField *iout,
                                      GwyWindowingType windowing,
                                      GwyTransformDirection direction,
                                      GwyInterpolationType interpolation,
                                      gboolean preserverms,
                                      gint level);
void gwy_data_field_area_2dfft       (GwyDataField *rin,
                                      GwyDataField *iin,
                                      GwyDataField *rout,
                                      GwyDataField *iout,
                                      gint col,
                                      gint row,
                                      gint width,
                                      gint height,
                                      GwyWindowingType windowing,
                                      GwyTransformDirection direction,
                                      GwyInterpolationType interpolation,
                                      gboolean preserverms,
                                      gint level);
void gwy_data_field_2dfft_raw        (GwyDataField *rin,
                                      GwyDataField *iin,
                                      GwyDataField *rout,
                                      GwyDataField *iout,
                                      GwyTransformDirection direction);
void gwy_data_field_2dfft_humanize   (GwyDataField *data_field);
void gwy_data_field_2dfft_dehumanize (GwyDataField *data_field);
void gwy_data_field_fft_postprocess  (GwyDataField *data_field,
                                      gboolean humanize);
void gwy_data_field_fft_filter_1d    (GwyDataField *data_field,
                                      GwyDataField *result_field,
                                      GwyDataLine *weights,
                                      GwyOrientation orientation,
                                      GwyInterpolationType interpolation);

void gwy_data_field_cwt             (GwyDataField *data_field,
                                     GwyInterpolationType interpolation,
                                     gdouble scale,
                                     Gwy2DCWTWaveletType wtype);


G_END_DECLS

#endif /* __GWY_PROCESS_INTTRANS_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
