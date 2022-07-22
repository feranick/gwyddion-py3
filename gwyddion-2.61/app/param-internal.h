/*
 *  $Id: param-internal.h 24330 2021-10-11 15:45:25Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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

/*< private_header >*/

#ifndef __GWY_PARAM_INTERNAL_H__
#define __GWY_PARAM_INTERNAL_H__

#include "dialog.h"

G_BEGIN_DECLS

#define gwy_param_fallback_color { 0.0, 0.0, 0.0, 1.0 }

typedef enum {
    GWY_PARAM_NONE     = 0,
    GWY_PARAM_BOOLEAN,
    GWY_PARAM_INT,
    GWY_PARAM_ENUM,
    GWY_PARAM_FLAGS,
    GWY_PARAM_REPORT_TYPE,
    GWY_PARAM_RANDOM_SEED,
    GWY_PARAM_ACTIVE_PAGE,
    GWY_PARAM_DOUBLE,
    GWY_PARAM_STRING,
    GWY_PARAM_COLOR,
    GWY_PARAM_IMAGE_ID,
    GWY_PARAM_GRAPH_ID,
    GWY_PARAM_VOLUME_ID,
    GWY_PARAM_XYZ_ID,
    GWY_PARAM_CURVE_MAP_ID,
    GWY_PARAM_GRAPH_CURVE,
    GWY_PARAM_LAWN_CURVE,
    GWY_PARAM_LAWN_SEGMENT,
    GWY_PARAM_UNIT,
    GWY_PARAM_RESOURCE,
} GwyParamType;

typedef struct {
    gboolean default_value;
    gboolean is_instant_updates;
    gint seed_id;
} GwyParamDefBoolean;

typedef struct {
    gint minimum;
    gint maximum;
    gint default_value;
} GwyParamDefInt;

typedef struct {
    gint randomize_id;
} GwyParamDefRandomSeed;

typedef struct {
    GType gtype;
    guint nvalues;
    const GwyEnum *table;
    gint default_value_index;
} GwyParamDefEnum;

typedef struct {
    GType gtype;
    guint nvalues;
    const GwyEnum *table;
    guint allset;
    guint default_value;
} GwyParamDefFlags;

typedef struct {
    gdouble minimum;
    gdouble maximum;
    gdouble default_value;
    gboolean is_percentage : 1;
    gboolean is_angle : 1;
    gboolean angle_positive : 1;
    guint angle_folding : 4;
} GwyParamDefDouble;

typedef struct {
    GwyRectifyStringFunc rectify;
    gchar *default_value;
    GwyParamStringFlags flags;
} GwyParamDefString;

typedef struct {
    const gchar *default_value;
} GwyParamDefUnit;

typedef struct {
    GwyRGBA default_value;
    gboolean has_alpha : 1;
    gboolean is_mask : 1;
} GwyParamDefColor;

typedef struct {
    gboolean is_target_graph;
} GwyParamDefDataId;

typedef struct {
    GwyResultsExportStyle style;
    GwyResultsReportType default_value;
} GwyParamDefReportType;

typedef struct {
    gint this_one_may_actually_have_no_definition_data;
} GwyParamDefActivePage;

typedef struct {
    GwyInventory *inventory;
    const gchar *default_value;
} GwyParamDefResource;

typedef struct {
    const gchar *name;
    const gchar *desc;
    GwyParamType type;
    gint id;
    union {
        GwyParamDefBoolean b;
        GwyParamDefInt i;
        GwyParamDefEnum e;
        GwyParamDefFlags f;
        GwyParamDefDouble d;
        GwyParamDefString s;
        GwyParamDefColor c;
        GwyParamDefDataId di;
        GwyParamDefReportType rt;
        GwyParamDefRandomSeed rs;
        GwyParamDefActivePage ap;
        GwyParamDefUnit si;
        GwyParamDefResource res;
    } def;
} GwyParamDefItem;

G_GNUC_INTERNAL
const GwyParamDefItem* _gwy_param_def_item(GwyParamDef *pardef,
                                           gint i);

G_GNUC_INTERNAL
guint _gwy_param_def_size(GwyParamDef *pardef);

G_GNUC_INTERNAL
gint _gwy_param_def_index(GwyParamDef *pardef,
                          gint id);

G_GNUC_INTERNAL
void _gwy_param_def_use(GwyParamDef *pardef,
                        GwyParams *params);

G_GNUC_INTERNAL
gint _gwy_param_def_rectify_enum(const GwyParamDefItem *def,
                                 gint value);

G_GNUC_INTERNAL
guint _gwy_param_def_rectify_flags(const GwyParamDefItem *def,
                                   guint value);

G_GNUC_INTERNAL
gint _gwy_param_def_rectify_int(const GwyParamDefItem *def,
                                gint value);

G_GNUC_INTERNAL
gint _gwy_param_def_rectify_random_seed(const GwyParamDefItem *def,
                                        gint value);

G_GNUC_INTERNAL
gdouble _gwy_param_def_rectify_double(const GwyParamDefItem *def,
                                      gdouble value);

G_GNUC_INTERNAL
GwyRGBA _gwy_param_def_rectify_color(const GwyParamDefItem *def,
                                     GwyRGBA value);

G_GNUC_INTERNAL
GwyResultsReportType _gwy_param_def_rectify_report_type(const GwyParamDefItem *def,
                                                        GwyResultsReportType value);

G_GNUC_INTERNAL
gchar* _gwy_param_def_rectify_string(const GwyParamDefItem *def,
                                     const gchar *value);

G_GNUC_INTERNAL
gchar* _gwy_param_def_rectify_unit(const GwyParamDefItem *def,
                                   const gchar *value);

G_GNUC_INTERNAL
const gchar* _gwy_param_def_rectify_resource(const GwyParamDefItem *def,
                                             const gchar *value);

G_GNUC_INTERNAL
gboolean _gwy_params_curve_get_use_string(GwyParams *params,
                                          gint id);

G_GNUC_INTERNAL
void _gwy_param_table_in_update(GwyParamTable *partable,
                                gboolean is_in_update);

G_GNUC_INTERNAL
void _gwy_param_table_set_parent_dialog(GwyParamTable *partable,
                                        GwyDialog *dialog);

G_GNUC_INTERNAL
void _gwy_param_table_proceed(GwyParamTable *partable);

G_GNUC_INTERNAL
void _gwy_dialog_param_table_update_started(GwyDialog *dialog);

G_GNUC_INTERNAL
void _gwy_dialog_param_table_update_finished(GwyDialog *dialog);

G_GNUC_UNUSED
static inline gboolean
param_type_is_data_id(GwyParamType type)
{
    return (type == GWY_PARAM_IMAGE_ID
            || type == GWY_PARAM_GRAPH_ID
            || type == GWY_PARAM_VOLUME_ID
            || type == GWY_PARAM_XYZ_ID
            || type == GWY_PARAM_CURVE_MAP_ID);
}

G_GNUC_UNUSED
static inline gboolean
param_type_is_curve_no(GwyParamType type)
{
    return (type == GWY_PARAM_GRAPH_CURVE || type == GWY_PARAM_LAWN_CURVE || type == GWY_PARAM_LAWN_SEGMENT);
}

G_END_DECLS

#endif

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
