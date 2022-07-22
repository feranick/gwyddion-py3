/*
 *  $Id: mfm_shift.c 23556 2021-04-21 08:00:51Z yeti-dn $
 *  Copyright (C) 2017 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/inttrans.h>
#include <libprocess/stats.h>
#include <libprocess/mfm.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "mfmops.h"
#include "preview.h"

#define MFM_SHIFT_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef struct {
    gdouble height;
    gboolean update;
} MfmShiftArgs;

typedef struct {
    MfmShiftArgs *args;
    GtkObject *height;
    GtkWidget *update;
    GwyDataField *result;
    GwyDataField *dfield;
    GtkWidget *view;
    GwyContainer *mydata;
} MfmShiftControls;

static gboolean module_register          (void);
static void     mfm_shift                 (GwyContainer *data,
                                          GwyRunType run);
static void     mfm_shift_do             (MfmShiftArgs *args,
                                          GwyDataField *dfield,
                                          GwyDataField *out);
static gboolean mfm_shift_dialog         (MfmShiftArgs *args,
                                          GwyContainer *data);

static void     mfm_shift_load_args      (GwyContainer *container,
                                          MfmShiftArgs *args);
static void     mfm_shift_save_args      (GwyContainer *container,
                                          MfmShiftArgs *args);
static void     mfm_shift_sanitize_args  (MfmShiftArgs *args);
static void     height_changed           (GtkAdjustment *adj,
                                          MfmShiftControls *controls);
static void     update_changed           (GtkToggleButton *button,
                                          MfmShiftControls *controls);
static void     preview                  (MfmShiftControls *controls,
                                          MfmShiftArgs *args);
static void     mfm_shift_dialog_update  (MfmShiftControls *controls,
                                          MfmShiftArgs *args);

static const MfmShiftArgs mfm_shift_defaults = {
    10, TRUE,
};


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Simulation of magnetic field z component change for another level"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2017",
};

GWY_MODULE_QUERY2(module_info, mfm_shift)

static gboolean
module_register(void)
{
    gwy_process_func_register("mfm_shift",
                              (GwyProcessFunc)&mfm_shift,
                              N_("/SPM M_odes/_Magnetic/_Field Shift in Z..."),
                              GWY_STOCK_MFM_FIELD_SHIFT,
                              MFM_SHIFT_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Compute stray field shift for another z level"));

    return TRUE;
}

static void
mfm_shift(GwyContainer *data, GwyRunType run)
{
    GwyDataField *dfield, *out;
    MfmShiftArgs args;
    gboolean ok;
    gint id, newid, datano;

    g_return_if_fail(run & MFM_SHIFT_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_CONTAINER_ID, &datano,
                                     0);
    g_return_if_fail(dfield);

    mfm_shift_load_args(gwy_app_settings_get(), &args);

    if (run == GWY_RUN_INTERACTIVE) {
        ok = mfm_shift_dialog(&args, data);
        mfm_shift_save_args(gwy_app_settings_get(), &args);
        if (!ok)
            return;
    }

    //do the computation

    out = gwy_data_field_new_alike(dfield, FALSE);
    mfm_shift_do(&args, dfield, out);

    newid = gwy_app_data_browser_add_data_field(out, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            0);

    gwy_app_set_data_field_title(data, newid, _("Shifted field"));
    gwy_app_channel_log_add_proc(data, id, newid);

    g_object_unref(out);
}

static gboolean
mfm_shift_dialog(MfmShiftArgs *args, GwyContainer *data)
{
    GtkWidget *dialog, *table, *hbox;
    MfmShiftControls controls;
    gint response, row, id, datano;

    controls.args = args;

    dialog = gtk_dialog_new_with_buttons(_("Stray Field Plane Shift"),
                                         NULL, 0, NULL);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                                 gwy_stock_like_button_new(_("_Update"),
                                                           GTK_STOCK_EXECUTE),
                                 RESPONSE_PREVIEW);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Reset"), RESPONSE_RESET);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);


    hbox = gtk_hbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), GTK_WIDGET(hbox),
                       FALSE, FALSE, 4);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &controls.dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_CONTAINER_ID, &datano,
                                     0);
    controls.result = gwy_data_field_new_alike(controls.dfield, TRUE);

    controls.mydata = gwy_container_new();
    gwy_container_set_object_by_name(controls.mydata, "/0/data", controls.result);
    gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    controls.view = gwy_create_preview(controls.mydata, 0, PREVIEW_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.view, FALSE, FALSE, 4);

    table = gtk_table_new(2, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 4);
    row = 0;

    controls.height = gtk_adjustment_new(args->height, -1000, 1000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("_Z shift by:"), "nm",
                            controls.height, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.height), "controls", &controls);
    g_signal_connect(controls.height, "value-changed",
                     G_CALLBACK(height_changed), &controls);
    row++;

    controls.update = gtk_check_button_new_with_mnemonic(_("I_nstant updates"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.update),
                                 args->update);
    gtk_table_attach(GTK_TABLE(table), controls.update,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls.update, "toggled",
                     G_CALLBACK(update_changed), &controls);
    row++;

    if (args->update)
        preview(&controls, args);

    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            return FALSE;
            break;

            case GTK_RESPONSE_OK:
            break;

            case RESPONSE_PREVIEW:
            preview(&controls, args);
            break;

            case RESPONSE_RESET:
            {
                *args = mfm_shift_defaults;
                mfm_shift_dialog_update(&controls, args);
            }
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);

    return TRUE;
}

static void
preview(MfmShiftControls *controls,
        MfmShiftArgs *args)
{
    mfm_shift_do(args, controls->dfield, controls->result);
    gwy_data_field_data_changed(controls->result);
}

static void
mfm_shift_do(MfmShiftArgs *args, GwyDataField *dfield, GwyDataField *out)
{
    gwy_data_field_mfm_shift_z(dfield, out, -args->height*1e-9);
}

static void
height_changed(GtkAdjustment *adj, MfmShiftControls *controls)
{
    controls->args->height = gtk_adjustment_get_value(adj);
    if (controls->args->update)
        preview(controls, controls->args);
}

static void
update_changed(GtkToggleButton *button, MfmShiftControls *controls)
{
    controls->args->update = gtk_toggle_button_get_active(button);
    if (controls->args->update)
        preview(controls, controls->args);
}

static void
mfm_shift_dialog_update(MfmShiftControls *controls,
                  MfmShiftArgs *args)
{
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->height), args->height);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->update),
                                 args->update);
}

static const gchar update_key[] = "/module/mfm_shift/update";
static const gchar height_key[] = "/module/mfm_shift/height";

static void
mfm_shift_sanitize_args(MfmShiftArgs *args)
{
    args->update = !!args->update;
}

static void
mfm_shift_load_args(GwyContainer *container,
              MfmShiftArgs *args)
{
    *args = mfm_shift_defaults;

    gwy_container_gis_boolean_by_name(container, update_key,
                                      &args->update);
    gwy_container_gis_double_by_name(container, height_key, &args->height);
    mfm_shift_sanitize_args(args);
}

static void
mfm_shift_save_args(GwyContainer *container,
              MfmShiftArgs *args)
{
    gwy_container_set_boolean_by_name(container, update_key,
                                      args->update);
    gwy_container_set_double_by_name(container, height_key, args->height);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
