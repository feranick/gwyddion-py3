/*
 *  $Id: fraccor.c 20677 2017-12-18 18:22:52Z yeti-dn $
 *  Copyright (C) 2004 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymacros.h>
#include <libprocess/datafield.h>
#include <libprocess/fractals.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define FRACCOR_RUN_MODES GWY_RUN_IMMEDIATE

static gboolean module_register(void);
static void     fraccor        (GwyContainer *data,
                                GwyRunType run);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Removes data under mask using fractal interpolation."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.3",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, fraccor)

static gboolean
module_register(void)
{
    gwy_process_func_register("fraccor",
                              (GwyProcessFunc)&fraccor,
                              N_("/_Correct Data/_Fractal Correction"),
                              GWY_STOCK_FRACTAL_CORRECTION,
                              FRACCOR_RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Interpolate data under mask with fractal "
                                 "interpolation"));

    return TRUE;
}

static void
fraccor(GwyContainer *data, GwyRunType run)
{
    GwyDataField *dfield, *mfield;
    GQuark dquark;
    gint id;

    g_return_if_fail(run & FRACCOR_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_KEY, &dquark,
                                     GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(dfield && dquark && mfield);

    gwy_app_undo_qcheckpointv(data, 1, &dquark);
    gwy_data_field_fractal_correction(dfield, mfield, GWY_INTERPOLATION_LINEAR);
    gwy_data_field_data_changed(dfield);
    gwy_app_channel_log_add_proc(data, id, id);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
