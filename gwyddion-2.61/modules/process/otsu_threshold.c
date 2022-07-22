/*
 *  $Id: otsu_threshold.c 24432 2021-10-27 12:57:58Z yeti-dn $
 *  Copyright (C) 2013 Brazilian Nanotechnology National Laboratory
 *  E-mail: Vinicius Barboza <vinicius.barboza@lnnano.cnpem.br>
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
#include <libgwyddion/gwymath.h>
#include <libprocess/grains.h>
#include <libprocess/filters.h>
#include <libgwymodule/gwymodule.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES GWY_RUN_IMMEDIATE

static gboolean module_register(void);
static void     otsu_threshold (GwyContainer *data,
                                GwyRunType run);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Automated threshold using Otsu's method on heights."),
    "Vinicius Barboza <vinicius.barboza@lnnano.cnpem.br>",
    "1.2",
    "Brazilian Nanotechnology National Laboratory",
    "2013",
};

GWY_MODULE_QUERY2(module_info, otsu_threshold)

static gboolean
module_register(void)
{
    gwy_process_func_register("otsu-threshold",
                              (GwyProcessFunc)&otsu_threshold,
                              N_("/_Grains/_Mark by Otsu's"),
                              GWY_STOCK_GRAINS_OTSU,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Automated threshold using Otsu's method on heights."));
    return TRUE;
}

static void
otsu_threshold(GwyContainer *data,
               GwyRunType run)
{
    GwyDataField *dfield, *mfield;
    GQuark mquark;
    gdouble thresh;
    gint id;

    /* Running smoothly */
    g_return_if_fail(run & RUN_MODES);

    /* Getting fields and quarks */
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);

    /* Setting checkopint */
    gwy_app_undo_qcheckpointv(data, 1, &mquark);

    /* Checking for mask and creating a new one */
    if (!mfield) {
        mfield = gwy_data_field_new_alike(dfield, TRUE);
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mfield), NULL);
        gwy_container_set_object(data, mquark, mfield);
        g_object_unref(mfield);
    }

    /* Copying the information to the mask field */
    gwy_data_field_copy(dfield, mfield, FALSE);

    /* Apply threshold to mask field */
    thresh = gwy_data_field_otsu_threshold(mfield);
    gwy_data_field_threshold(mfield, thresh, 0.0, 1.0);
    gwy_data_field_data_changed(mfield);
    gwy_app_channel_log_add_proc(data, id, id);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
