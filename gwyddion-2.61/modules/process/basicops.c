/*
 *  $Id: basicops.c 24158 2021-09-20 13:29:40Z yeti-dn $
 *  Copyright (C) 2003-2018 David Necas (Yeti), Petr Klapetek.
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

#include "config.h"
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_IMMEDIATE

static gboolean module_register           (void);
static void     flip_horizontally         (GwyContainer *data,
                                           GwyRunType runtype);
static void     flip_vertically           (GwyContainer *data,
                                           GwyRunType runtype);
static void     flip_diagonally           (GwyContainer *data,
                                           GwyRunType runtype);
static void     invert_value              (GwyContainer *data,
                                           GwyRunType runtype);
static void     rotate_clockwise_90       (GwyContainer *data,
                                           GwyRunType runtype);
static void     rotate_counterclockwise_90(GwyContainer *data,
                                           GwyRunType runtype);
static void     rotate_180                (GwyContainer *data,
                                           GwyRunType runtype);
static void     square_samples            (GwyContainer *data,
                                           GwyRunType runtype);
static void     null_offsets              (GwyContainer *data,
                                           GwyRunType runtype);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Basic operations like flipping, value inversion, and rotation by multiples of 90 degrees."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY2(module_info, basicops)

static gboolean
module_register(void)
{
    gwy_process_func_register("invert_value",
                              (GwyProcessFunc)&invert_value,
                              N_("/_Basic Operations/_Invert Value"),
                              GWY_STOCK_VALUE_INVERT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Invert values about mean"));
    gwy_process_func_register("flip_horizontally",
                              (GwyProcessFunc)&flip_horizontally,
                              N_("/_Basic Operations/Flip _Horizontally"),
                              GWY_STOCK_FLIP_HORIZONTALLY,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Flip data horizontally"));
    gwy_process_func_register("flip_vertically",
                              (GwyProcessFunc)&flip_vertically,
                              N_("/_Basic Operations/Flip _Vertically"),
                              GWY_STOCK_FLIP_VERTICALLY,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Flip data vertically"));
    gwy_process_func_register("flip_diagonally",
                              (GwyProcessFunc)&flip_diagonally,
                              N_("/_Basic Operations/Flip Dia_gonally"),
                              GWY_STOCK_FLIP_DIAGONALLY,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Flip data diagonally"));
    gwy_process_func_register("rotate_180",
                              (GwyProcessFunc)&rotate_180,
                              N_("/_Basic Operations/Flip _Both"),
                              GWY_STOCK_ROTATE_180,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Flip data both horizontally and vertically"));
    gwy_process_func_register("rotate_90_cw",
                              (GwyProcessFunc)&rotate_clockwise_90,
                              N_("/_Basic Operations/Rotate C_lockwise"),
                              GWY_STOCK_ROTATE_90_CW,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Rotate data 90 degrees clockwise"));
    gwy_process_func_register("rotate_90_ccw",
                              (GwyProcessFunc)&rotate_counterclockwise_90,
                              N_("/_Basic Operations/Rotate _Counterclockwise"),
                              GWY_STOCK_ROTATE_90_CCW,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Rotate data 90 degrees counterclockwise"));
    gwy_process_func_register("square_samples",
                              (GwyProcessFunc)&square_samples,
                              N_("/_Basic Operations/S_quare Samples"),
                              GWY_STOCK_SQUARE_SAMPLES,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Resample data with non-1:1 aspect ratio to square samples"));
    gwy_process_func_register("null_offsets",
                              (GwyProcessFunc)&null_offsets,
                              N_("/_Basic Operations/_Null Offsets"),
                              GWY_STOCK_NULL_OFFSETS,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Null horizontal offsets, moving the origin to the upper left corner"));

    return TRUE;
}

static inline guint
compress_quarks(GwyDataField **fields, GQuark *quarks, guint nitems)
{
    guint i, n;

    for (i = n = 0; i < nitems; i++) {
        if (fields[i]) {
            fields[n] = fields[i];
            quarks[n] = quarks[i];
            n++;
        }
    }

    return n;
}

static inline guint
get_fields_and_quarks(GwyDataField **fields, GQuark *quarks, gint *id)
{
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, fields + 0,
                                     GWY_APP_MASK_FIELD, fields + 1,
                                     GWY_APP_SHOW_FIELD, fields + 2,
                                     GWY_APP_DATA_FIELD_KEY, quarks + 0,
                                     GWY_APP_MASK_FIELD_KEY, quarks + 1,
                                     GWY_APP_SHOW_FIELD_KEY, quarks + 2,
                                     GWY_APP_DATA_FIELD_ID, id,
                                     0);
    return compress_quarks(fields, quarks, 3);
}

static void
flip_horizontally(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfields[3];
    GQuark quarks[3];
    gint id, n, i;

    g_return_if_fail(runtype & RUN_MODES);
    n = get_fields_and_quarks(dfields, quarks, &id);
    gwy_app_undo_qcheckpointv(data, n, quarks);
    for (i = 0; i < n; i++) {
        gwy_data_field_invert(dfields[i], FALSE, TRUE, FALSE);
        gwy_data_field_data_changed(dfields[i]);
    }
    gwy_app_data_clear_selections(data, id);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
flip_vertically(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfields[3];
    GQuark quarks[3];
    gint id, n, i;

    g_return_if_fail(runtype & RUN_MODES);
    n = get_fields_and_quarks(dfields, quarks, &id);
    gwy_app_undo_qcheckpointv(data, n, quarks);
    for (i = 0; i < n; i++) {
        gwy_data_field_invert(dfields[i], TRUE, FALSE, FALSE);
        gwy_data_field_data_changed(dfields[i]);
    }
    gwy_app_data_clear_selections(data, id);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
flip_diagonally(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfields[3], *newfield;
    GQuark quarks[3];
    gint id, n, i;

    g_return_if_fail(runtype & RUN_MODES);
    n = get_fields_and_quarks(dfields, quarks, &id);
    gwy_app_undo_qcheckpointv(data, n, quarks);
    for (i = 0; i < n; i++) {
        newfield = gwy_data_field_new_alike(dfields[i], FALSE);
        gwy_data_field_flip_xy(dfields[i], newfield, FALSE);
        gwy_container_set_object(data, quarks[i], newfield);
        g_object_unref(newfield);
    }
    gwy_app_data_clear_selections(data, id);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
invert_value(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfields[2];
    GQuark quarks[2];
    gint id, n, i;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, dfields + 0,
                                     GWY_APP_SHOW_FIELD, dfields + 1,
                                     GWY_APP_DATA_FIELD_KEY, quarks + 0,
                                     GWY_APP_SHOW_FIELD_KEY, quarks + 1,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    n = compress_quarks(dfields, quarks, G_N_ELEMENTS(dfields));
    gwy_app_undo_qcheckpointv(data, n, quarks);
    for (i = 0; i < n; i++) {
        if (dfields[i]) {
            gwy_data_field_invert(dfields[i], FALSE, FALSE, TRUE);
            gwy_data_field_data_changed(dfields[i]);
        }
    }
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
rotate_clockwise_90(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfields[3], *newfield;
    GQuark quarks[3];
    gint id, n, i;

    g_return_if_fail(runtype & RUN_MODES);
    n = get_fields_and_quarks(dfields, quarks, &id);
    gwy_app_undo_qcheckpointv(data, n, quarks);
    for (i = 0; i < n; i++) {
        newfield = gwy_data_field_new_rotated_90(dfields[i], TRUE);
        gwy_container_set_object(data, quarks[i], newfield);
        g_object_unref(newfield);
    }
    gwy_app_data_clear_selections(data, id);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
rotate_counterclockwise_90(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfields[3], *newfield;
    GQuark quarks[3];
    gint id, n, i;

    g_return_if_fail(runtype & RUN_MODES);
    n = get_fields_and_quarks(dfields, quarks, &id);
    gwy_app_undo_qcheckpointv(data, n, quarks);
    for (i = 0; i < n; i++) {
        newfield = gwy_data_field_new_rotated_90(dfields[i], FALSE);
        gwy_container_set_object(data, quarks[i], newfield);
        g_object_unref(newfield);
    }
    gwy_app_data_clear_selections(data, id);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
rotate_180(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfields[3];
    GQuark quarks[3];
    gint id, n, i;

    g_return_if_fail(runtype & RUN_MODES);
    n = get_fields_and_quarks(dfields, quarks, &id);
    gwy_app_undo_qcheckpointv(data, n, quarks);
    for (i = 0; i < n; i++) {
        gwy_data_field_invert(dfields[i], TRUE, TRUE, FALSE);
        gwy_data_field_data_changed(dfields[i]);
    }
    gwy_app_data_clear_selections(data, id);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
square_samples(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfield, *dfields[3];
    gdouble xreal, yreal, qx, qy;
    gint oldid, newid, xres, yres;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, dfields + 0,
                                     GWY_APP_MASK_FIELD, dfields + 1,
                                     GWY_APP_SHOW_FIELD, dfields + 2,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    dfield = dfields[0];
    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    xreal = gwy_data_field_get_xreal(dfield);
    yreal = gwy_data_field_get_yreal(dfield);
    qx = xres/xreal;
    qy = yres/yreal;
    if (fabs(log(qx/qy)) > 1.0/hypot(xres, yres)) {
        /* Resample */
        if (qx < qy)
            xres = MAX(GWY_ROUND(xreal*qy), 1);
        else
            yres = MAX(GWY_ROUND(yreal*qx), 1);

        dfields[0] = gwy_data_field_new_resampled(dfields[0], xres, yres, GWY_INTERPOLATION_BSPLINE);
        if (dfields[1])
            dfields[1] = gwy_data_field_new_resampled(dfields[1], xres, yres, GWY_INTERPOLATION_ROUND);
        if (dfields[2])
            dfields[2] = gwy_data_field_new_resampled(dfields[2], xres, yres, GWY_INTERPOLATION_BSPLINE);
    }
    else {
        /* Ratios are equal, just duplicate */
        dfields[0] = gwy_data_field_duplicate(dfields[0]);
        if (dfields[1])
            dfields[1] = gwy_data_field_duplicate(dfields[1]);
        if (dfields[2])
            dfields[2] = gwy_data_field_duplicate(dfields[2]);
    }


    newid = gwy_app_data_browser_add_data_field(dfields[0], data, TRUE);
    g_object_unref(dfields[0]);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

    if (dfields[1]) {
        gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), dfields[1]);
        g_object_unref(dfields[1]);
    }
    if (dfields[2]) {
        gwy_container_set_object(data, gwy_app_get_show_key_for_id(newid), dfields[2]);
        g_object_unref(dfields[2]);
    }

    gwy_app_channel_log_add_proc(data, oldid, newid);
}

static void
null_offsets(GwyContainer *data,
             GwyRunType runtype)
{
    GwyDataField *dfields[3];
    GQuark quarks[3];
    gint id, n, i;

    g_return_if_fail(runtype & RUN_MODES);
    get_fields_and_quarks(dfields, quarks, &id);
    for (i = 0; i < G_N_ELEMENTS(dfields); i++) {
        if (dfields[i]
            && !gwy_data_field_get_xoffset(dfields[i])
            && !gwy_data_field_get_yoffset(dfields[i])) {
            quarks[i] = 0;
            dfields[i] = NULL;
        }
    }

    n = compress_quarks(dfields, quarks, G_N_ELEMENTS(dfields));
    if (!n)
        return;

    gwy_app_undo_qcheckpointv(data, n, quarks);
    for (i = 0; i < n; i++) {
        gwy_data_field_set_xoffset(dfields[i], 0.0);
        gwy_data_field_set_yoffset(dfields[i], 0.0);
        gwy_data_field_data_changed(dfields[i]);
    }
    gwy_app_data_clear_selections(data, id);
    gwy_app_channel_log_add_proc(data, id, id);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
