/*
 *  $Id: volumize.c 21302 2018-08-08 14:33:39Z yeti-dn $
 *  Copyright (C) 2015-2016 David Necas (Yeti), Petr Klapetek.
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

#include "config.h"
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/stats.h>
#include <libprocess/brick.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define VOLUMIZE_RUN_MODES (GWY_RUN_IMMEDIATE)
#define MAXPIX 600

static gboolean  module_register            (void);
static void      volumize                   (GwyContainer *data,
                                             GwyRunType run);
static GwyBrick* create_brick_from_datafield(GwyDataField *dfield);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Converts datafield to 3D volume data."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.2",
    "David Nečas (Yeti) & Petr Klapetek",
    "2013",
};

GWY_MODULE_QUERY2(module_info, volumize)

static gboolean
module_register(void)
{
    gwy_process_func_register("volumize",
                              (GwyProcessFunc)&volumize,
                              N_("/_Basic Operations/Volumize"),
                              GWY_STOCK_VOLUMIZE,
                              VOLUMIZE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Convert datafield to 3D data"));

    return TRUE;
}

static void
volumize(GwyContainer *data, GwyRunType run)
{
    GwyDataField *dfield = NULL;
    GwyBrick *brick;
    gint newid;

    g_return_if_fail(run & VOLUMIZE_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield, 0);

    brick = create_brick_from_datafield(dfield);
    dfield = gwy_data_field_duplicate(dfield);
    gwy_brick_sum_plane(brick, dfield, 0, 0, 0,
                        gwy_brick_get_xres(brick), gwy_brick_get_yres(brick),
                        -1, FALSE);

    newid = gwy_app_data_browser_add_brick(brick, dfield, data, TRUE);
    g_object_unref(brick);
    g_object_unref(dfield);
    gwy_app_volume_log_add(data, -1, newid, "proc::volumize", NULL);
}

static GwyBrick*
create_brick_from_datafield(GwyDataField *dfield)
{
    gint xres, yres, zres;
    gint col, row, lev;
    gdouble ratio, *bdata, *ddata;
    gdouble xreal, yreal, zreal, offset;
    gboolean freeme = FALSE;
    GwyDataField *lowres;
    GwyBrick *brick;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    zres = MAX(xres, yres);

    if (xres*yres > MAXPIX*MAXPIX) {
        ratio = (MAXPIX*MAXPIX)/(gdouble)(xres*yres);
        lowres = gwy_data_field_new_alike(dfield, TRUE);
        gwy_data_field_copy(dfield, lowres, TRUE);
        xres *= ratio;
        yres *= ratio;
        freeme = TRUE;
        gwy_data_field_resample(lowres, xres, yres, GWY_INTERPOLATION_BILINEAR);
    }
    else
        lowres = dfield;

    zres = MAX(xres, yres);

    xreal = gwy_data_field_get_xreal(dfield);
    yreal = gwy_data_field_get_yreal(dfield);
    offset = gwy_data_field_get_min(lowres);
    zreal = gwy_data_field_get_max(lowres) - offset;

    brick = gwy_brick_new(xres, yres, zres, xreal, yreal, zreal, TRUE);

    gwy_si_unit_assign(gwy_brick_get_si_unit_x(brick),
                       gwy_data_field_get_si_unit_xy(dfield));
    gwy_si_unit_assign(gwy_brick_get_si_unit_y(brick),
                       gwy_data_field_get_si_unit_xy(dfield));
    gwy_si_unit_assign(gwy_brick_get_si_unit_z(brick),
                       gwy_data_field_get_si_unit_z(dfield));

    ddata = gwy_data_field_get_data(lowres);
    bdata = gwy_brick_get_data(brick);

    for (lev = 0; lev < zres; lev++) {
        for (row = 0; row < yres; row++) {
            for (col = 0; col < xres; col++) {
                if (ddata[col + xres*row] >= lev*zreal/zres + offset)
                    bdata[col + xres*row + xres*yres*lev] = 1;

            }
        }
    }
    if (freeme)
        GWY_OBJECT_UNREF(lowres);

    return brick;

}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
