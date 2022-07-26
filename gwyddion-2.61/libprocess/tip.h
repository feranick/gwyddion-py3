/*
 *  $Id: tip.h 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2003-2016 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_PROCESS_TIP_H__
#define __GWY_PROCESS_TIP_H__

#include <libprocess/datafield.h>
#include <libprocess/gwyprocesstypes.h>

G_BEGIN_DECLS

typedef struct _GwyTipModelPreset GwyTipModelPreset;

#ifndef GWY_DISABLE_DEPRECATED
typedef void (*GwyTipModelFunc)(GwyDataField *tip,
                                gdouble height,
                                gdouble radius,
                                gdouble rotation,
                                gdouble *params);

typedef void (*GwyTipGuessFunc)(GwyDataField *data,
                                gdouble height,
                                gdouble radius,
                                gdouble *params,
                                gint *xres,
                                gint *yres);

struct _GwyTipModelPreset {
    const gchar *tip_name;
    const gchar *group_name;
    GwyTipModelFunc func;
    GwyTipGuessFunc guess;
    gint nparams;
};
#endif

#define GWY_TYPE_TIP_MODEL_PRESET (gwy_tip_model_preset_get_type())

GType                    gwy_tip_model_preset_get_type         (void)                             G_GNUC_CONST;
gint                     gwy_tip_model_get_npresets            (void);
const GwyTipModelPreset* gwy_tip_model_get_preset              (gint preset_id);
const GwyTipModelPreset* gwy_tip_model_get_preset_by_name      (const gchar *name);
gint                     gwy_tip_model_get_preset_id           (const GwyTipModelPreset* preset);
const gchar*             gwy_tip_model_get_preset_tip_name     (const GwyTipModelPreset* preset);
const gchar*             gwy_tip_model_get_preset_group_name   (const GwyTipModelPreset* preset);
gint                     gwy_tip_model_get_preset_nparams      (const GwyTipModelPreset* preset);
const GwyTipParamType*   gwy_tip_model_get_preset_params       (const GwyTipModelPreset* preset);
void                     gwy_tip_model_preset_create           (const GwyTipModelPreset* preset,
                                                                GwyDataField *tip,
                                                                const gdouble *params);
void                     gwy_tip_model_preset_create_for_zrange(const GwyTipModelPreset* preset,
                                                                GwyDataField *tip,
                                                                gdouble zrange,
                                                                gboolean square,
                                                                const gdouble *params);

GwyDataField*   gwy_tip_dilation(GwyDataField *tip,
                                 GwyDataField *surface,
                                 GwyDataField *result,
                                 GwySetFractionFunc set_fraction,
                                 GwySetMessageFunc set_message);

GwyDataField*   gwy_tip_erosion(GwyDataField *tip,
                                GwyDataField *surface,
                                GwyDataField *result,
                                GwySetFractionFunc set_fraction,
                                GwySetMessageFunc set_message);

GwyDataField*   gwy_tip_cmap(GwyDataField *tip,
                             GwyDataField *surface,
                             GwyDataField *result,
                             GwySetFractionFunc set_fraction,
                             GwySetMessageFunc set_message);


GwyDataField*   gwy_tip_estimate_partial(GwyDataField *tip,
                                         GwyDataField *surface,
                                         gdouble threshold,
                                         gboolean use_edges,
                                         gint *count,
                                         GwySetFractionFunc set_fraction,
                                         GwySetMessageFunc set_message);

GwyDataField*   gwy_tip_estimate_full(GwyDataField *tip,
                                      GwyDataField *surface,
                                      gdouble threshold,
                                      gboolean use_edges,
                                      gint *count,
                                      GwySetFractionFunc set_fraction,
                                      GwySetMessageFunc set_message);

G_END_DECLS

#endif /* __GWY_PROCESS_TIP__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
