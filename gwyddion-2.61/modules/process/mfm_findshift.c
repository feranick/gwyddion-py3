/*
 *  $Id: mfm_findshift.c 21040 2018-05-01 16:08:49Z yeti-dn $
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

#define MFM_FINDSHIFT_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef struct {
    gdouble start;
    gdouble stop;
    GwyAppDataId op1;
    GwyAppDataId op2;
} MfmFindshiftArgs;

typedef struct {
    MfmFindshiftArgs *args;
    GtkObject *start;
    GtkObject *stop;
    GtkWidget *chooser_op2;
} MfmFindshiftControls;

static gboolean module_register             (void);
static void     mfm_findshift               (GwyContainer *data,
                                             GwyRunType run);
static gboolean mfm_findshift_dialog        (MfmFindshiftArgs *args);

static void     mfm_findshift_load_args     (GwyContainer *container,
                                             MfmFindshiftArgs *args);
static void     mfm_findshift_save_args     (GwyContainer *container,
                                             MfmFindshiftArgs *args);
static void     mfm_findshift_sanitize_args (MfmFindshiftArgs *args);
static void     start_changed               (GtkAdjustment *adj,
                                             MfmFindshiftControls *controls);
static void     stop_changed                (GtkAdjustment *adj,
                                             MfmFindshiftControls *controls);
static void     mfm_findshift_dialog_update (MfmFindshiftControls *controls,
                                             MfmFindshiftArgs *args);
static void     mfm_findshift_data_changed  (GwyDataChooser *chooser,
                                             GwyAppDataId *object);
static gboolean mfm_findshift_data_filter   (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);


static GwyAppDataId op2_id = GWY_APP_DATA_ID_NONE;

static const MfmFindshiftArgs mfm_findshift_defaults = {
    10, 20, GWY_APP_DATA_ID_NONE, GWY_APP_DATA_ID_NONE,
};


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Lift height difference estimation from data blur"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.2",
    "David Nečas (Yeti) & Petr Klapetek",
    "2017",
};

GWY_MODULE_QUERY2(module_info, mfm_findshift)

static gboolean
module_register(void)
{
    gwy_process_func_register("mfm_findshift",
                              (GwyProcessFunc)&mfm_findshift,
                              N_("/SPM M_odes/_Magnetic/_Estimate Shift in Z..."),
                              GWY_STOCK_MFM_FIELD_FIND_SHIFT,
                              MFM_FINDSHIFT_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Estimate lift height difference in MFM data"));

    return TRUE;
}

static void
mfm_findshift(GwyContainer *data, GwyRunType run)
{
    GwyDataField *dfield1, *dfield2, *out;
    GtkWidget *dialog;
    MfmFindshiftArgs args;
    gboolean ok;
    gint newid;
    GQuark quark;
    GwyContainer *mydata;
    gdouble minshift;

    g_return_if_fail(run & MFM_FINDSHIFT_RUN_MODES);

    mfm_findshift_load_args(gwy_app_settings_get(), &args);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_ID, &args.op1.id,
                                     GWY_APP_CONTAINER_ID, &args.op1.datano,
                                     0);

    if (run == GWY_RUN_INTERACTIVE) {
        ok = mfm_findshift_dialog(&args);
        mfm_findshift_save_args(gwy_app_settings_get(), &args);
        if (!ok)
            return;
    }

    mydata = gwy_app_data_browser_get(args.op1.datano);
    quark = gwy_app_get_data_key_for_id(args.op1.id);
    dfield1 = GWY_DATA_FIELD(gwy_container_get_object(mydata, quark));

    mydata = gwy_app_data_browser_get(args.op2.datano);
    quark = gwy_app_get_data_key_for_id(args.op2.id);
    dfield2 = GWY_DATA_FIELD(gwy_container_get_object(mydata, quark));

    minshift = gwy_data_field_mfm_find_shift_z(dfield1, dfield2,
                                               -1e-9*args.start,
                                               -1e-9*args.stop);

    dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data,
                                                                    args.op1.id),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_INFO,
                                    GTK_BUTTONS_CLOSE,
                                    "%s %g nm",
                                    _("Estimated shift:"), -minshift/1e-9);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    out = gwy_data_field_new_alike(dfield1, FALSE);
    gwy_data_field_mfm_shift_z(dfield1, out, minshift);
    gwy_data_field_subtract_fields(out, dfield2, out);

    newid = gwy_app_data_browser_add_data_field(out, data, TRUE);
    gwy_app_sync_data_items(data, data, args.op1.id, newid, FALSE,
                        GWY_DATA_ITEM_GRADIENT,
                        GWY_DATA_ITEM_MASK_COLOR,
                        0);

    gwy_app_set_data_field_title(data, newid, _("Shifted field difference"));
    gwy_app_channel_log_add_proc(data, args.op1.id, newid);

    g_object_unref(out);
}


static gboolean
mfm_findshift_dialog(MfmFindshiftArgs *args)
{
    GtkWidget *dialog, *table;
    GwyDataChooser *chooser;
    MfmFindshiftControls controls;
    gint response, row;

    controls.args = args;

    dialog = gtk_dialog_new_with_buttons(_("Estimate Lift Height Shift"),
                                         NULL, 0, NULL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Reset"), RESPONSE_RESET);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);

    table = gtk_table_new(7, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), table,
                       FALSE, FALSE, 4);
    row = 0;

    controls.chooser_op2 = gwy_data_chooser_new_channels();
    chooser = GWY_DATA_CHOOSER(controls.chooser_op2);
    g_object_set_data(G_OBJECT(chooser), "dialog", dialog);
    gwy_data_chooser_set_active_id(chooser, &args->op2);
    gwy_data_chooser_set_filter(chooser,
                                mfm_findshift_data_filter, &args->op1, NULL);
    g_signal_connect(chooser, "changed",
                     G_CALLBACK(mfm_findshift_data_changed), &args->op2);
    mfm_findshift_data_changed(chooser, &args->op2);
    gwy_table_attach_adjbar(table, row, _("Data to compare:"), NULL,
                            GTK_OBJECT(chooser), GWY_HSCALE_WIDGET_NO_EXPAND);
    gtk_table_set_row_spacing(GTK_TABLE(table), row, 8);
    row++;

    controls.start = gtk_adjustment_new(args->start, -1000, 1000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Search _from:"), "nm",
                            controls.start, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.start), "controls", &controls);
    g_signal_connect(controls.start, "value-changed",
                     G_CALLBACK(start_changed), &controls);
    row++;

    controls.stop = gtk_adjustment_new(args->stop, -1000, 1000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Search _to:"), "nm",
                            controls.stop, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.stop), "controls", &controls);
    g_signal_connect(controls.stop, "value-changed",
                     G_CALLBACK(stop_changed), &controls);
    row++;

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

            case RESPONSE_RESET:
            {
                *args = mfm_findshift_defaults;
                mfm_findshift_dialog_update(&controls, args);
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
start_changed(GtkAdjustment *adj, MfmFindshiftControls *controls)
{
    controls->args->start = gtk_adjustment_get_value(adj);
}

static void
stop_changed(GtkAdjustment *adj, MfmFindshiftControls *controls)
{
    controls->args->stop = gtk_adjustment_get_value(adj);
}

static void
mfm_findshift_data_changed(GwyDataChooser *chooser,
                           GwyAppDataId *object)
{
    GtkWidget *dialog;

    gwy_data_chooser_get_active_id(chooser, object);
    gwy_debug("data: %d %d", object->datano, object->id);

    dialog = g_object_get_data(G_OBJECT(chooser), "dialog");
    g_assert(GTK_IS_DIALOG(dialog));
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK,
                                      object->datano);
}

static gboolean
mfm_findshift_data_filter(GwyContainer *data,
                          gint id,
                          gpointer user_data)
{

    GwyAppDataId *object = (GwyAppDataId*)user_data;
    GwyDataField *op1, *op2;
    GQuark quark;

    quark = gwy_app_get_data_key_for_id(id);
    op1 = GWY_DATA_FIELD(gwy_container_get_object(data, quark));

    data = gwy_app_data_browser_get(object->datano);
    quark = gwy_app_get_data_key_for_id(object->id);
    op2 = GWY_DATA_FIELD(gwy_container_get_object(data, quark));

    /* It does not make sense to crosscorrelate with itself */
    if (op1 == op2)
        return FALSE;

    return !gwy_data_field_check_compatibility(op1, op2,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL
                                               | GWY_DATA_COMPATIBILITY_VALUE);
}

static void
mfm_findshift_dialog_update(MfmFindshiftControls *controls,
                            MfmFindshiftArgs *args)
{
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->start), args->start);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->stop), args->stop);

}

static const gchar start_key[] = "/module/mfm_findshift/start";
static const gchar stop_key[]  = "/module/mfm_findshift/stop";

static void
mfm_findshift_sanitize_args(MfmFindshiftArgs *args)
{
    gwy_app_data_id_verify_channel(&args->op2);
}

static void
mfm_findshift_load_args(GwyContainer *container,
              MfmFindshiftArgs *args)
{
    *args = mfm_findshift_defaults;

    gwy_container_gis_double_by_name(container, start_key, &args->start);
    gwy_container_gis_double_by_name(container, stop_key, &args->stop);
    args->op2 = op2_id;

    mfm_findshift_sanitize_args(args);
}

static void
mfm_findshift_save_args(GwyContainer *container,
              MfmFindshiftArgs *args)
{
    op2_id = args->op2;
    gwy_container_set_double_by_name(container, start_key, args->start);
    gwy_container_set_double_by_name(container, stop_key, args->stop);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
