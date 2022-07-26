/*
 *  $Id: arithmetic.h 24254 2021-10-06 11:25:18Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_PROCESS_ARITHMETIC_H__
#define __GWY_PROCESS_ARITHMETIC_H__

#include <libprocess/dataline.h>
#include <libprocess/datafield.h>
#include <libprocess/brick.h>
#include <libprocess/lawn.h>
#include <libprocess/gwyprocessenums.h>

G_BEGIN_DECLS

void gwy_data_field_sum_fields        (GwyDataField *result,
                                       GwyDataField *operand1,
                                       GwyDataField *operand2);
void gwy_data_field_subtract_fields   (GwyDataField *result,
                                       GwyDataField *operand1,
                                       GwyDataField *operand2);
void gwy_data_field_divide_fields     (GwyDataField *result,
                                       GwyDataField *operand1,
                                       GwyDataField *operand2);
void gwy_data_field_multiply_fields   (GwyDataField *result,
                                       GwyDataField *operand1,
                                       GwyDataField *operand2);
void gwy_data_field_min_of_fields     (GwyDataField *result,
                                       GwyDataField *operand1,
                                       GwyDataField *operand2);
void gwy_data_field_max_of_fields     (GwyDataField *result,
                                       GwyDataField *operand1,
                                       GwyDataField *operand2);
void gwy_data_field_hypot_of_fields   (GwyDataField *result,
                                       GwyDataField *operand1,
                                       GwyDataField *operand2);
void gwy_data_field_linear_combination(GwyDataField *result,
                                       gdouble coeff1,
                                       GwyDataField *operand1,
                                       gdouble coeff2,
                                       GwyDataField *operand2,
                                       gdouble constant);

GwyDataCompatibilityFlags
gwy_data_field_check_compatibility(GwyDataField *data_field1,
                                   GwyDataField *data_field2,
                                   GwyDataCompatibilityFlags check);
GwyDataCompatibilityFlags
gwy_data_line_check_compatibility(GwyDataLine *data_line1,
                                  GwyDataLine *data_line2,
                                  GwyDataCompatibilityFlags check);

GwyDataCompatibilityFlags
gwy_brick_check_compatibility(GwyBrick *brick1,
                              GwyBrick *brick2,
                              GwyDataCompatibilityFlags check);

GwyDataCompatibilityFlags
gwy_lawn_check_compatibility(GwyLawn *lawn1,
                             GwyLawn *lawn2,
                             GwyDataCompatibilityFlags check);

GwyDataCompatibilityFlags
gwy_data_field_check_compatibility_with_brick_xy(GwyDataField *data_field,
                                                 GwyBrick *brick,
                                                 GwyDataCompatibilityFlags check);

GwyDataCompatibilityFlags
gwy_data_line_check_compatibility_with_brick_z(GwyDataLine *data_line,
                                               GwyBrick *brick,
                                               GwyDataCompatibilityFlags check);

GwyDataCompatibilityFlags
gwy_data_field_check_compatibility_with_lawn(GwyDataField *data_field,
                                             GwyLawn *lawn,
                                             GwyDataCompatibilityFlags check);

GwyDataField* gwy_data_field_extend(GwyDataField *data_field,
                                    guint left,
                                    guint right,
                                    guint up,
                                    guint down,
                                    GwyExteriorType exterior,
                                    gdouble fill_value,
                                    gboolean keep_offsets);

G_END_DECLS

#endif /* __GWY_PROCESS_ARITHMETIC_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
