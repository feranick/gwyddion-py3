/*
 *  $Id: params.h 24646 2022-03-08 14:26:48Z yeti-dn $
 *  Copyright (C) 2021-2022 David Necas (Yeti).
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

#ifndef __GWY_PARAMS_H__
#define __GWY_PARAMS_H__

#include <libgwyddion/gwyresource.h>
#include <libprocess/gwyprocessenums.h>
#include <libprocess/datafield.h>
#include <libprocess/brick.h>
#include <libprocess/surface.h>
#include <libprocess/lawn.h>
#include <libdraw/gwyrgba.h>
#include <libgwydgets/gwygraphmodel.h>
#include <app/datachooser.h>
#include <app/param-def.h>

G_BEGIN_DECLS

#define GWY_TYPE_PARAMS            (gwy_params_get_type())
#define GWY_PARAMS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_PARAMS, GwyParams))
#define GWY_PARAMS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_PARAMS, GwyParamsClass))
#define GWY_IS_PARAMS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_PARAMS))
#define GWY_IS_PARAMS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_PARAMS))
#define GWY_PARAMS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_PARAMS, GwyParamsClass))

typedef struct _GwyParams      GwyParams;
typedef struct _GwyParamsClass GwyParamsClass;

struct _GwyParams {
    GObject parent;
    struct _GwyParamsPrivate *priv;
};

struct _GwyParamsClass {
    GObjectClass parent;
};

GType                gwy_params_get_type          (void)                        G_GNUC_CONST;
GwyParams*           gwy_params_new               (void);
void                 gwy_params_set_def           (GwyParams *params,
                                                   GwyParamDef *pardef);
GwyParamDef*         gwy_params_get_def           (GwyParams *params);
GwyParams*           gwy_params_new_from_settings (GwyParamDef *pardef);
void                 gwy_params_load_from_settings(GwyParams *params);
GwyParams*           gwy_params_duplicate         (GwyParams *params);
void                 gwy_params_save_to_settings  (GwyParams *params);
void                 gwy_params_reset_all         (GwyParams *params,
                                                   const gchar *prefix);
gboolean             gwy_params_reset             (GwyParams *params,
                                                   gint id);
gboolean             gwy_params_get_boolean       (GwyParams *params,
                                                   gint id);
gboolean             gwy_params_set_boolean       (GwyParams *params,
                                                   gint id,
                                                   gboolean value);
gint                 gwy_params_get_enum          (GwyParams *params,
                                                   gint id);
GwyMaskingType       gwy_params_get_masking       (GwyParams *params,
                                                   gint id,
                                                   GwyDataField **mask);
gboolean             gwy_params_set_enum          (GwyParams *params,
                                                   gint id,
                                                   gint value);
guint                gwy_params_get_flags         (GwyParams *params,
                                                   gint id);
gboolean             gwy_params_set_flags         (GwyParams *params,
                                                   gint id,
                                                   guint value);
gboolean             gwy_params_set_flag          (GwyParams *params,
                                                   gint id,
                                                   guint flag,
                                                   gboolean value);
GwyResultsReportType gwy_params_get_report_type   (GwyParams *params,
                                                   gint id);
gboolean             gwy_params_set_report_type   (GwyParams *params,
                                                   gint id,
                                                   GwyResultsReportType value);
gint                 gwy_params_get_int           (GwyParams *params,
                                                   gint id);
gboolean             gwy_params_set_int           (GwyParams *params,
                                                   gint id,
                                                   gint value);
gdouble              gwy_params_get_double        (GwyParams *params,
                                                   gint id);
gboolean             gwy_params_set_double        (GwyParams *params,
                                                   gint id,
                                                   gdouble value);
const gchar*         gwy_params_get_string        (GwyParams *params,
                                                   gint id);
gboolean             gwy_params_set_string        (GwyParams *params,
                                                   gint id,
                                                   const gchar *value);
gboolean             gwy_params_set_unit          (GwyParams *params,
                                                   gint id,
                                                   const gchar *value);
GwySIUnit*           gwy_params_get_unit          (GwyParams *params,
                                                   gint id,
                                                   gint *power10);
gboolean             gwy_params_set_resource      (GwyParams *params,
                                                   gint id,
                                                   const gchar *value);
GwyResource*         gwy_params_get_resource      (GwyParams *params,
                                                   gint id);
GwyRGBA              gwy_params_get_color         (GwyParams *params,
                                                   gint id);
gboolean             gwy_params_set_color         (GwyParams *params,
                                                   gint id,
                                                   GwyRGBA value);
GwyAppDataId         gwy_params_get_data_id       (GwyParams *params,
                                                   gint id);
gboolean             gwy_params_data_id_is_none   (GwyParams *params,
                                                   gint id);
GwyDataField*        gwy_params_get_image         (GwyParams *params,
                                                   gint id);
GwyDataField*        gwy_params_get_mask          (GwyParams *params,
                                                   gint id);
GwyGraphModel*       gwy_params_get_graph         (GwyParams *params,
                                                   gint id);
GwyBrick*            gwy_params_get_volume        (GwyParams *params,
                                                   gint id);
GwySurface*          gwy_params_get_xyz           (GwyParams *params,
                                                   gint id);
GwyLawn*             gwy_params_get_curve_map     (GwyParams *params,
                                                   gint id);
gboolean             gwy_params_set_image_id      (GwyParams *params,
                                                   gint id,
                                                   GwyAppDataId value);
gboolean             gwy_params_set_graph_id      (GwyParams *params,
                                                   gint id,
                                                   GwyAppDataId value);
gboolean             gwy_params_set_volume_id     (GwyParams *params,
                                                   gint id,
                                                   GwyAppDataId value);
gboolean             gwy_params_set_xyz_id        (GwyParams *params,
                                                   gint id,
                                                   GwyAppDataId value);
gboolean             gwy_params_set_curve_map_id  (GwyParams *params,
                                                   gint id,
                                                   GwyAppDataId value);
gboolean             gwy_params_set_curve         (GwyParams *params,
                                                   gint id,
                                                   gint value);
gint                 gwy_params_randomize_seed    (GwyParams *params,
                                                   gint id);

G_END_DECLS

#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
