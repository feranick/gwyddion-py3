/*
 *  $Id: stats_uncertainty.h 21458 2018-09-23 21:43:40Z yeti-dn $
 *  Copyright (C) 2003-2009 Anna Campbellova
 *  E-mail: acampbellova@cmi.cz.
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

#ifndef __GWY_STATS_UNCERTAINTY_H__
#define __GWY_STATS_UNCERTAINTY_H__ 1

#include <libgwyddion/gwymacros.h>
#include <libprocess/datafield.h>

G_BEGIN_DECLS

gdouble gwy_data_field_get_max_uncertainty                   (GwyDataField *data_field,
                                                              GwyDataField *uncz_field);
gdouble gwy_data_field_area_get_max_uncertainty              (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              GwyMaskingType mode,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height);
gdouble gwy_data_field_get_min_uncertainty                   (GwyDataField *data_field,
                                                              GwyDataField *uncz_field);
gdouble gwy_data_field_area_get_min_uncertainty              (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              GwyMaskingType mode,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height);
void    gwy_data_field_get_min_max_uncertainty               (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              gdouble *min_unc,
                                                              gdouble *max_unc);
void    gwy_data_field_area_get_min_max_uncertainty          (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height,
                                                              gdouble *min_unc,
                                                              gdouble *max_unc);
void    gwy_data_field_area_get_min_max_uncertainty_mask     (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              GwyMaskingType mode,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height,
                                                              gdouble *min_unc,
                                                              gdouble *max_unc);
gdouble gwy_data_field_get_avg_uncertainty                   (GwyDataField *data_field,
                                                              GwyDataField *uncz_field);
gdouble gwy_data_field_area_get_avg_uncertainty              (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height);
gdouble gwy_data_field_area_get_avg_uncertainty_mask         (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              GwyMaskingType mode,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height);
gdouble gwy_data_field_get_rms_uncertainty                   (GwyDataField *data_field,
                                                              GwyDataField *uncz_field);
gdouble gwy_data_field_area_get_rms_uncertainty              (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height);
gdouble gwy_data_field_area_get_rms_uncertainty_mask         (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              GwyMaskingType mode,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height);
void    gwy_data_field_get_stats_uncertainties               (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              gdouble *avg_unc,
                                                              gdouble *ra_unc,
                                                              gdouble *rms_unc,
                                                              gdouble *skew_unc,
                                                              gdouble *kurtosis_unc);
void    gwy_data_field_area_get_stats_uncertainties          (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height,
                                                              gdouble *avg_unc,
                                                              gdouble *ra_unc,
                                                              gdouble *rms_unc,
                                                              gdouble *skew_unc,
                                                              gdouble *kurtosis_unc);
void    gwy_data_field_area_get_stats_uncertainties_mask     (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              GwyMaskingType mode,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height,
                                                              gdouble *avg_unc,
                                                              gdouble *ra_unc,
                                                              gdouble *rms_unc,
                                                              gdouble *skew_unc,
                                                              gdouble *kurtosis_unc);
void    gwy_data_field_area_acf_uncertainty                  (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataLine *target_line,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height,
                                                              GwyOrientation orientation,
                                                              GwyInterpolationType interpolation,
                                                              gint nstats);
void    gwy_data_field_acf_uncertainty                       (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataLine *target_line,
                                                              GwyOrientation orientation,
                                                              GwyInterpolationType interpolation,
                                                              gint nstats);
void    gwy_data_field_area_hhcf_uncertainty                 (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataLine *target_line,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height,
                                                              GwyOrientation orientation,
                                                              GwyInterpolationType interpolation,
                                                              gint nstats);
void    gwy_data_field_hhcf_uncertainty                      (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataLine *target_line,
                                                              GwyOrientation orientation,
                                                              GwyInterpolationType interpolation,
                                                              gint nstats);
gdouble gwy_data_field_get_surface_area_uncertainty          (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *uncx_field,
                                                              GwyDataField *uncy_field);
gdouble gwy_data_field_area_get_surface_area_uncertainty     (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *uncx_field,
                                                              GwyDataField *uncy_field,
                                                              GwyDataField *mask,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height);
gdouble gwy_data_field_area_get_surface_area_mask_uncertainty(GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *uncx_field,
                                                              GwyDataField *uncy_field,
                                                              GwyDataField *mask,
                                                              GwyMaskingType mode,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height);
gdouble gwy_data_field_area_get_median_uncertainty           (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height);
gdouble gwy_data_field_area_get_median_uncertainty_mask      (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              GwyMaskingType mode,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height);
gdouble gwy_data_field_get_median_uncertainty                (GwyDataField *data_field,
                                                              GwyDataField *uncz_field);
void    gwy_data_field_area_dh_uncertainty                   (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              GwyDataLine *target_line,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height,
                                                              gint nstats);
void    gwy_data_field_dh_uncertainty                        (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataLine *target_line,
                                                              gint nstats);
void    gwy_data_field_area_get_normal_coeffs_uncertainty    (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *uncx_field,
                                                              GwyDataField *uncy_field,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height,
                                                              gdouble *nx,
                                                              gdouble *ny,
                                                              gdouble *nz,
                                                              gdouble *ux,
                                                              gdouble *uy,
                                                              gdouble *uz);
void    gwy_data_field_get_normal_coeffs_uncertainty         (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *uncx_field,
                                                              GwyDataField *uncy_field,
                                                              gdouble *nx,
                                                              gdouble *ny,
                                                              gdouble *nz,
                                                              gdouble *ux,
                                                              gdouble *uy,
                                                              gdouble *uz);
void    gwy_data_field_area_get_inclination_uncertainty      (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *uncx_field,
                                                              GwyDataField *uncy_field,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height,
                                                              gdouble *utheta,
                                                              gdouble *uphi);
void    gwy_data_field_get_inclination_uncertainty           (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *uncx_field,
                                                              GwyDataField *uncy_field,
                                                              gdouble *utheta,
                                                              gdouble *uphi);
gdouble gwy_data_field_area_get_projected_area_uncertainty   (gint nn,
                                                              GwyDataField *uncx_field,
                                                              GwyDataField *uncy_field);
void    gwy_data_field_area_cdh_uncertainty                  (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField *mask,
                                                              GwyDataLine *target_line,
                                                              gint col,
                                                              gint row,
                                                              gint width,
                                                              gint height,
                                                              gint nstats);
void    gwy_data_field_cdh_uncertainty                       (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataLine *target_line,
                                                              gint nstats);
gdouble gwy_data_field_get_xder_uncertainty                  (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField * uncx_field,
                                                              GwyDataField *uncy_field,
                                                              gint col,
                                                              gint row);
gdouble gwy_data_field_get_yder_uncertainty                  (GwyDataField *data_field,
                                                              GwyDataField *uncz_field,
                                                              GwyDataField * uncx_field,
                                                              GwyDataField *uncy_field,
                                                              gint col,
                                                              gint row);
void    gwy_data_line_acf_uncertainty                        (GwyDataLine *data_line,
                                                              GwyDataLine *uline,
                                                              GwyDataLine *target_line);
void    gwy_data_line_cumulate_uncertainty                   (GwyDataLine *uncz_line);
void    gwy_data_line_hhcf_uncertainty                       (GwyDataLine *data_line,
                                                              GwyDataLine *uline,
                                                              GwyDataLine *target_line);

G_END_DECLS

#endif /* __GWY_STATS_UNCERTAINTY_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
