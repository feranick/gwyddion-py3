/*
 *  $Id: grain_makesel.c 24796 2022-04-28 15:20:59Z yeti-dn $
 *  Copyright (C) 2014-2021 David Necas (Yeti), Petr Klapetek, Sven Neumann.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, neumann@jpk.com.
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

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/grains.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_IMMEDIATE

static gboolean module_register       (void);
static void     grain_inscribe_discs  (GwyContainer *data,
                                       GwyRunType runtype);
static void     grain_exscribe_circles(GwyContainer *data,
                                       GwyRunType runtype);
static void     grain_inscribe_rects  (GwyContainer *data,
                                       GwyRunType runtype);
static void     grain_exscribe_bboxes (GwyContainer *data,
                                       GwyRunType runtype);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Creates selections visualizing grains."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, grain_makesel)

static gboolean
module_register(void)
{
    gwy_process_func_register("grain_inscribe_discs",
                              (GwyProcessFunc)&grain_inscribe_discs,
                              N_("/_Grains/Select _Inscribed Discs"),
                              GWY_STOCK_GRAIN_INSCRIBED_CIRCLE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Create a selection visualizing discs inscribed into grains"));
    gwy_process_func_register("grain_exscribe_circles",
                              (GwyProcessFunc)&grain_exscribe_circles,
                              N_("/_Grains/Select _Circumscribed Circles"),
                              GWY_STOCK_GRAIN_EXSCRIBED_CIRCLE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Create a selection visualizing grain circumcircles"));
    gwy_process_func_register("grain_inscribe_rects",
                              (GwyProcessFunc)&grain_inscribe_rects,
                              N_("/_Grains/Select Inscribed _Rectangles"),
                              GWY_STOCK_GRAIN_INSCRIBED_BOX,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Create a selection visualizing rectangles inscribed into grains"));
    gwy_process_func_register("grain_exscribe_bboxes",
                              (GwyProcessFunc)&grain_exscribe_bboxes,
                              N_("/_Grains/Select _Bounding Boxes"),
                              GWY_STOCK_GRAIN_BOUNDING_BOX,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Create a selection visualizing grain bounding boxes"));

    return TRUE;
}

static GwySelection*
create_selection(const gchar *typename, guint *ngrains)
{
    GParamSpecInt *pspec;
    GObjectClass *klass;
    GType type;

    type = g_type_from_name(typename);
    g_return_val_if_fail(type, NULL);

    klass = g_type_class_ref(type);
    pspec = (GParamSpecInt*)g_object_class_find_property(klass, "max-objects");
    g_return_val_if_fail(G_IS_PARAM_SPEC_UINT(pspec), NULL);

    if ((gint)*ngrains > pspec->maximum) {
        g_warning("Too many grains for %s, only first %d will be shown.", typename, pspec->maximum);
        *ngrains = pspec->maximum;
    }
    return g_object_new(type, "max-objects", *ngrains, NULL);
}

static void
make_circles(GwyContainer *data, gint id, GwyDataField *field, gdouble **rxydata, guint ngrains)
{
    gdouble xoffset = gwy_data_field_get_xoffset(field), yoffset = gwy_data_field_get_yoffset(field);
    GwySelection *selection;
    gchar *key;
    guint i;

    selection = create_selection("GwySelectionEllipse", &ngrains);
    for (i = 1; i <= ngrains; i++) {
        gdouble r = rxydata[0][i], x = rxydata[1][i] - xoffset, y = rxydata[2][i] - yoffset;
        gdouble xy[4] = { x - r, y - r, x + r, y + r };
        gwy_selection_set_object(selection, i-1, xy);
    }

    key = g_strdup_printf("/%d/select/ellipse", id);
    gwy_container_set_object_by_name(data, key, selection);
    g_object_unref(selection);
    g_free(key);
}

/* FIXME: It would be nice to have something like that also for minimum and maximum bounding dimensions. */
static void
grain_inscribe_discs(GwyContainer *data, GwyRunType runtype)
{
    static const GwyGrainQuantity quantities[] = {
        GWY_GRAIN_VALUE_INSCRIBED_DISC_R, GWY_GRAIN_VALUE_INSCRIBED_DISC_X, GWY_GRAIN_VALUE_INSCRIBED_DISC_Y,
    };

    GwyDataField *field, *mfield;
    guint ngrains, i;
    gint *grains;
    gdouble *inscd;
    gdouble *values[3];
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);

    grains = g_new0(gint, mfield->xres * mfield->yres);
    ngrains = gwy_data_field_number_grains(mfield, grains);
    inscd = g_new(gdouble, 3*(ngrains + 1));
    for (i = 0; i < 3; i++)
        values[i] = inscd + i*(ngrains + 1);

    gwy_data_field_grains_get_quantities(field, values, quantities, 3, ngrains, grains);
    g_free(grains);

    make_circles(data, id, field, values, ngrains);
    g_free(inscd);
}

static void
grain_exscribe_circles(GwyContainer *data, GwyRunType runtype)
{
    static const GwyGrainQuantity quantities[] = {
        GWY_GRAIN_VALUE_CIRCUMCIRCLE_R, GWY_GRAIN_VALUE_CIRCUMCIRCLE_X, GWY_GRAIN_VALUE_CIRCUMCIRCLE_Y,
    };

    GwyDataField *field, *mfield;
    guint ngrains, i;
    gint *grains;
    gdouble *circc;
    gdouble *values[3];
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);

    grains = g_new0(gint, mfield->xres * mfield->yres);
    ngrains = gwy_data_field_number_grains(mfield, grains);
    circc = g_new(gdouble, 3*(ngrains + 1));
    for (i = 0; i < 3; i++)
        values[i] = circc + i*(ngrains + 1);

    gwy_data_field_grains_get_quantities(field, values, quantities, 3, ngrains, grains);
    g_free(grains);

    make_circles(data, id, field, values, ngrains);
    g_free(circc);
}

static void
make_boxes(GwyContainer *data, gint id, GwyDataField *field, const gint *boxes, guint ngrains)
{
    gdouble dx = gwy_data_field_get_dx(field), dy = gwy_data_field_get_dy(field);
    GwySelection *selection;
    gchar *key;
    guint i;

    selection = create_selection("GwySelectionRectangle", &ngrains);
    for (i = 1; i <= ngrains; i++) {
        const gint *bb = boxes + 4*i;
        gdouble x = dx*bb[0], y = dy*bb[1], w = dx*bb[2], h = dy*bb[3];
        gdouble xy[4] = { x, y, x + w, y + h };
        gwy_selection_set_object(selection, i-1, xy);
    }

    key = g_strdup_printf("/%d/select/rectangle", id);
    gwy_container_set_object_by_name(data, key, selection);
    g_object_unref(selection);
    g_free(key);
}

static void
grain_inscribe_rects(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *field, *mfield;
    guint ngrains;
    gint *grains, *iboxes;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);

    grains = g_new0(gint, mfield->xres * mfield->yres);
    ngrains = gwy_data_field_number_grains(mfield, grains);
    iboxes = g_new(gint, 4*(ngrains + 1));

    gwy_data_field_get_grain_inscribed_boxes(mfield, ngrains, grains, iboxes);
    g_free(grains);

    make_boxes(data, id, field, iboxes, ngrains);
    g_free(iboxes);
}

static void
grain_exscribe_bboxes(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *field, *mfield;
    guint ngrains;
    gint *grains, *bboxes;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);

    grains = g_new0(gint, mfield->xres * mfield->yres);
    ngrains = gwy_data_field_number_grains(mfield, grains);
    bboxes = g_new(gint, 4*(ngrains + 1));

    gwy_data_field_get_grain_bounding_boxes(mfield, ngrains, grains, bboxes);
    g_free(grains);

    make_boxes(data, id, field, bboxes, ngrains);
    g_free(bboxes);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
