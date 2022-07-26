/*
 *  $Id: correlation.h 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2003-2017 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_PROCESS_CORRELATION_H__
#define __GWY_PROCESS_CORRELATION_H__

#include <libprocess/datafield.h>
#include <libprocess/gwyprocesstypes.h>

G_BEGIN_DECLS

gdouble gwy_data_field_get_correlation_score(GwyDataField *data_field,
                                             GwyDataField *kernel_field,
                                             gint col,
                                             gint row,
                                             gint kernel_col,
                                             gint kernel_row,
                                             gint kernel_width,
                                             gint kernel_height);

gdouble gwy_data_field_get_weighted_correlation_score(GwyDataField *data_field,
                                     GwyDataField *kernel_field,
                                     GwyDataField *weight_field,
                                     gint col,
                                     gint row,
                                     gint kernel_col,
                                     gint kernel_row,
                                     gint kernel_width,
                                     gint kernel_height);

void gwy_data_field_crosscorrelate(GwyDataField *data_field1,
                                   GwyDataField *data_field2,
                                   GwyDataField *x_dist,
                                   GwyDataField *y_dist,
                                   GwyDataField *score,
                                   gint search_width,
                                   gint search_height,
                                   gint window_width,
                                   gint window_height);

GwyComputationState* gwy_data_field_crosscorrelate_init(GwyDataField *data_field1,
                                                        GwyDataField *data_field2,
                                                        GwyDataField *x_dist,
                                                        GwyDataField *y_dist,
                                                        GwyDataField *score,
                                                        gint search_width,
                                                        gint search_height,
                                                        gint window_width,
                                                        gint window_height);

void gwy_data_field_crosscorrelate_set_weights(GwyComputationState *state, 
                                               GwyWindowingType type);

void gwy_data_field_crosscorrelate_iteration(GwyComputationState *state);
void gwy_data_field_crosscorrelate_finalize(GwyComputationState *state);

void gwy_data_field_correlate(GwyDataField *data_field,
                              GwyDataField *kernel_field,
                              GwyDataField *score,
                              GwyCorrelationType method);

GwyComputationState *gwy_data_field_correlate_init(GwyDataField *data_field,
                                                   GwyDataField *kernel_field,
                                                   GwyDataField *score);
void gwy_data_field_correlate_iteration(GwyComputationState *state);
void gwy_data_field_correlate_finalize(GwyComputationState *state);

void gwy_data_field_correlation_search(GwyDataField *dfield,
                                       GwyDataField *kernel,
                                       GwyDataField *kernel_weight,
                                       GwyDataField *target,
                                       GwyCorrSearchType method,
                                       gdouble regcoeff,
                                       GwyExteriorType exterior,
                                       gdouble fill_value);

G_END_DECLS

#endif /* __GWY_PROCESS_CORRELATION__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
