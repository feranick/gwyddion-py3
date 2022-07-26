/*
 *  $Id: gwygraphcurvemodel.h 24581 2022-02-05 10:24:24Z yeti-dn $
 *  Copyright (C) 2004-2016 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_GRAPH_CURVE_MODEL_H__
#define __GWY_GRAPH_CURVE_MODEL_H__

#include <libdraw/gwyrgba.h>
#include <libgwydgets/gwydgetenums.h>
#include <libprocess/dataline.h>
#include <libprocess/gwycaldata.h>

G_BEGIN_DECLS

#define GWY_TYPE_GRAPH_CURVE_MODEL             (gwy_graph_curve_model_get_type())
#define GWY_GRAPH_CURVE_MODEL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_GRAPH_CURVE_MODEL, GwyGraphCurveModel))
#define GWY_GRAPH_CURVE_MODEL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_GRAPH_CURVE_MODEL, GwyGraphCurveModelClass))
#define GWY_IS_GRAPH_CURVE_MODEL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_GRAPH_CURVE_MODEL))
#define GWY_IS_GRAPH_CURVE_MODEL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_GRAPH_CURVE_MODEL))
#define GWY_GRAPH_CURVE_MODEL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_GRAPH_CURVE_MODEL, GwyGraphCurveModelClass))


typedef struct _GwyGraphCurveModel GwyGraphCurveModel;
typedef struct _GwyGraphCurveModelClass GwyGraphCurveModelClass;

struct _GwyGraphCurveModel {
    GObject parent_instance;

    /* data */
    gint n;
    gdouble *xdata;
    gdouble *ydata;

    /* range cache */
    guint32 cached;
    gdouble cache[4];

    /* like GwyGraphAreaCurveParams, but with proper field types */
    GString *description;
    GwyRGBA color;
    GwyGraphCurveType mode;

    GwyGraphPointType point_type;
    gint point_size;

    GdkLineStyle line_style;
    gint line_width;

    /* reserved stuff */
    GwyGraphPointType enum1;
    gint int1;
    GwyGraphPointType enum2;
    gint int2;
    gint int3;
    gint int4;
    gdouble *cache2;
    GwyCurveCalibrationData *calibration;
    gpointer reserved3;
    gpointer reserved4;
};

struct _GwyGraphCurveModelClass {
    GObjectClass parent_class;

    void (*data_changed)(GwyGraphCurveModel *model);

    void (*reserved1)(void);
    void (*reserved2)(void);
    void (*reserved3)(void);
};

#define gwy_graph_curve_model_duplicate(gcmodel) \
        (GWY_GRAPH_CURVE_MODEL(gwy_serializable_duplicate(G_OBJECT(gcmodel))))

GType                    gwy_graph_curve_model_get_type              (void)                                        G_GNUC_CONST;
GwyGraphCurveModel*      gwy_graph_curve_model_new                   (void);
GwyGraphCurveModel*      gwy_graph_curve_model_new_alike             (GwyGraphCurveModel *gcmodel);
void                     gwy_graph_curve_model_set_data              (GwyGraphCurveModel *gcmodel,
                                                                      const gdouble *xdata,
                                                                      const gdouble *ydata,
                                                                      gint n);
void                     gwy_graph_curve_model_set_data_interleaved  (GwyGraphCurveModel *gcmodel,
                                                                      const gdouble *xydata,
                                                                      gint n);
void                     gwy_graph_curve_model_set_data_from_dataline(GwyGraphCurveModel *gcmodel,
                                                                      GwyDataLine *dline,
                                                                      gint from_index,
                                                                      gint to_index);
void                     gwy_graph_curve_model_enforce_order         (GwyGraphCurveModel *gcmodel);
gboolean                 gwy_graph_curve_model_is_ordered            (GwyGraphCurveModel *gcmodel);
const gdouble*           gwy_graph_curve_model_get_xdata             (GwyGraphCurveModel *gcmodel);
const gdouble*           gwy_graph_curve_model_get_ydata             (GwyGraphCurveModel *gcmodel);
gint                     gwy_graph_curve_model_get_ndata             (GwyGraphCurveModel *gcmodel);
gboolean                 gwy_graph_curve_model_get_x_range           (GwyGraphCurveModel *gcmodel,
                                                                      gdouble *x_min,
                                                                      gdouble *x_max);
gboolean                 gwy_graph_curve_model_get_y_range           (GwyGraphCurveModel *gcmodel,
                                                                      gdouble *y_min,
                                                                      gdouble *y_max);
gboolean                 gwy_graph_curve_model_get_ranges            (GwyGraphCurveModel *gcmodel,
                                                                      gboolean x_logscale,
                                                                      gboolean y_logscale,
                                                                      gdouble *x_min,
                                                                      gdouble *x_max,
                                                                      gdouble *y_min,
                                                                      gdouble *y_max);
GwyCurveCalibrationData* gwy_graph_curve_model_get_calibration_data  (GwyGraphCurveModel *gcmodel);
void                     gwy_graph_curve_model_set_calibration_data  (GwyGraphCurveModel *gcmodel,
                                                                      const GwyCurveCalibrationData *calibration);

G_END_DECLS

#endif /* __GWY_GRAPH_CURVE_MODEL_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
