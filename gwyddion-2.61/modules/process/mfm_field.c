/*
 *  $Id: mfm_field.c 23556 2021-04-21 08:00:51Z yeti-dn $
 *  Copyright (C) 2003-2018 David Necas (Yeti), Petr Klapetek.
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

#define MFM_FIELD_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    GWY_MFM_FIELD_OUTPUT_FIELD   = 0,
    GWY_MFM_FIELD_OUTPUT_FORCE   = 1,
    GWY_MFM_FIELD_OUTPUT_FORCE_DX = 2,
    GWY_MFM_FIELD_OUTPUT_FORCE_DDX  = 3,
    GWY_MFM_FIELD_OUTPUT_MEFF  = 4
} GwyMfmFieldOutputType;


typedef struct {
    GwyMfmFieldOutputType out;
    GwyMFMProbeType probe;
    gboolean walls;
    gdouble height;
    gdouble thickness;
    gdouble sigma;
    gdouble mtip;
    gdouble bx;
    gdouble by;
    gdouble length;
    gdouble wall_a;
    gdouble wall_kn;
    gdouble angle;
    gboolean update;
} MfmFieldArgs;

typedef struct {
    MfmFieldArgs *args;
    GtkWidget *out;
    GtkWidget *probe;
    GtkWidget *walls;
    GtkWidget *update;
    GtkWidget *widthlabel;
    GtkWidget *widthvalue;
    GtkObject *height;
    GtkObject *thickness;
    GtkObject *sigma;
    GtkObject *mtip;
    GtkObject *bx;
    GtkObject *by;
    GtkObject *length;
    GtkObject *wall_a;
    GtkObject *wall_kn;
    GtkObject *angle;
    GwyDataField *result;
    GwyDataField *mfield;
    GtkWidget *view;
    GwyContainer *mydata;

} MfmFieldControls;

static gboolean module_register          (void);
static void     mfm_field                 (GwyContainer *data,
                                          GwyRunType run);
static void     mfm_field_do             (MfmFieldArgs *args,
                                          GwyDataField *mfield,
                                          GwyDataField *out);
static gboolean mfm_field_dialog         (MfmFieldArgs *args,
                                          GwyContainer *data);
static void     walls_changed            (GtkToggleButton *button,
                                          MfmFieldControls *controls);
static void     update_changed           (GtkToggleButton *button,
                                          MfmFieldControls *controls);
static void     update_sensitivity       (MfmFieldControls *controls);
static void     preview                  (MfmFieldControls *controls,
                                          MfmFieldArgs *args);
static void     mfm_field_load_args      (GwyContainer *container,
                                          MfmFieldArgs *args);
static void     mfm_field_save_args      (GwyContainer *container,
                                          MfmFieldArgs *args);
static void     mfm_field_sanitize_args  (MfmFieldArgs *args);
static void     sigma_changed            (GtkAdjustment *adj,
                                          MfmFieldControls *controls);
static void     height_changed           (GtkAdjustment *adj,
                                          MfmFieldControls *controls);
static void     thickness_changed        (GtkAdjustment *adj,
                                          MfmFieldControls *controls);
static void     wall_a_changed           (GtkAdjustment *adj,
                                          MfmFieldControls *controls);
static void     wall_kn_changed           (GtkAdjustment *adj,
                                          MfmFieldControls *controls);
static void     bx_changed               (GtkAdjustment *adj,
                                          MfmFieldControls *controls);
static void     by_changed               (GtkAdjustment *adj,
                                          MfmFieldControls *controls);
static void     mtip_changed             (GtkAdjustment *adj,
                                          MfmFieldControls *controls);
static void     length_changed           (GtkAdjustment *adj,
                                          MfmFieldControls *controls);
static void     angle_changed            (GtkAdjustment *adj,
                                          MfmFieldControls *controls);
static void     probe_changed            (GtkComboBox *combo,
                                          MfmFieldControls *controls);
static void     out_changed              (GtkComboBox *combo,
                                          MfmFieldControls *controls);
static void     mfm_field_dialog_update  (MfmFieldControls *controls,
                                          MfmFieldArgs *args);
static void     update_dw                (MfmFieldControls *controls);

static const MfmFieldArgs mfm_field_defaults = {
    GWY_MFM_FIELD_OUTPUT_FIELD,
    GWY_MFM_PROBE_CHARGE,
    FALSE,
    100, 100, 1,
    1, 10, 10, 1000,
    28, 540, 0,
    FALSE,
};


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Simulation of magnetic field above perpendicular media"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.2",
    "David Nečas (Yeti) & Petr Klapetek",
    "2017",
};

GWY_MODULE_QUERY2(module_info, mfm_field)

static gboolean
module_register(void)
{
    gwy_process_func_register("mfm_field",
                              (GwyProcessFunc)&mfm_field,
                              N_("/SPM M_odes/_Magnetic/_Perpendicular Media Field..."),
                              GWY_STOCK_MFM_PERPENDICULAR,
                              MFM_FIELD_RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Compute stray field above perpendicular magnetic medium"));

    return TRUE;
}

static void
mfm_field(GwyContainer *data, GwyRunType run)
{
    GwyDataField *mfield, *out;
    MfmFieldArgs args;
    gboolean ok;
    gint id, newid, datano;
    g_return_if_fail(run & MFM_FIELD_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_CONTAINER_ID, &datano,
                                     0);
    g_return_if_fail(mfield);

    mfm_field_load_args(gwy_app_settings_get(), &args);

    if (run == GWY_RUN_INTERACTIVE) {
        ok = mfm_field_dialog(&args, data);
        mfm_field_save_args(gwy_app_settings_get(), &args);
        if (!ok)
            return;
    }

    //do the computation

    out = gwy_data_field_new_alike(mfield, FALSE);
    mfm_field_do(&args, mfield, out);

    if (args.out == GWY_MFM_FIELD_OUTPUT_MEFF) {
        newid = gwy_app_data_browser_add_data_field(out, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

        gwy_app_set_data_field_title(data, newid, "Meff");
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    if (args.out == GWY_MFM_FIELD_OUTPUT_FIELD) {
        newid = gwy_app_data_browser_add_data_field(out, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

        gwy_app_set_data_field_title(data, newid, "Hz");
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    else if (args.out == GWY_MFM_FIELD_OUTPUT_FORCE) {
        newid = gwy_app_data_browser_add_data_field(out, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

        gwy_app_set_data_field_title(data, newid, "Fz");
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    else if (args.out == GWY_MFM_FIELD_OUTPUT_FORCE_DX) {
        newid = gwy_app_data_browser_add_data_field(out, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

        gwy_app_set_data_field_title(data, newid, "dFz/dz");
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    else if (args.out == GWY_MFM_FIELD_OUTPUT_FORCE_DDX) {
        newid = gwy_app_data_browser_add_data_field(out, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

        gwy_app_set_data_field_title(data, newid, "d²Fz/dz²");
        gwy_app_channel_log_add_proc(data, id, newid);

    }

    g_object_unref(out);
}


static gboolean
mfm_field_dialog(MfmFieldArgs *args, GwyContainer *data)
{
    static const GwyEnum mfm_field_outputs[] = {
        {
            "M<sub>eff</sub>",
            GWY_MFM_FIELD_OUTPUT_MEFF,
        },
        {
            "H<sub>z</sub>",
            GWY_MFM_FIELD_OUTPUT_FIELD,
        },
        {
            "F<sub>z</sub>",
            GWY_MFM_FIELD_OUTPUT_FORCE,
        },
        {
            "dF<sub>z</sub>/dz",
            GWY_MFM_FIELD_OUTPUT_FORCE_DX,
        },
        {
            "d<sup>2</sup>F<sub>z</sub>/dz<sup>2</sup>",
            GWY_MFM_FIELD_OUTPUT_FORCE_DDX,
        },
    };
    static const GwyEnum mfm_field_probes[] = {
        { N_("Point charge"), GWY_MFM_PROBE_CHARGE, },
        { N_("Bar"),          GWY_MFM_PROBE_BAR,    },
    };
    GtkWidget *dialog, *table, *hbox, *spin;
    MfmFieldControls controls;
    gint response, row, id, datano;

    controls.args = args;

    dialog = gtk_dialog_new_with_buttons(_("Perpendicular Media Stray Field"),
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

    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &controls.mfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_CONTAINER_ID, &datano,
                                     0);
    controls.result = gwy_data_field_new_alike(controls.mfield, TRUE);

    controls.mydata = gwy_container_new();
    gwy_container_set_object_by_name(controls.mydata, "/0/data", controls.result);
    gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    controls.view = gwy_create_preview(controls.mydata, 0, PREVIEW_SIZE, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.view, FALSE, FALSE, 4);

    table = gtk_table_new(13, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), table,
                       FALSE, FALSE, 4);
    row = 0;

    controls.height = gtk_adjustment_new(args->height, 0, 1000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Output plane height:"), "nm",
                                   controls.height, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_object_set_data(G_OBJECT(controls.height), "controls", &controls);
    g_signal_connect(controls.height, "value-changed",
                     G_CALLBACK(height_changed), &controls);
    row++;

    controls.thickness = gtk_adjustment_new(args->thickness, 0, 1000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Film thickness:"), "nm",
                                   controls.thickness, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_object_set_data(G_OBJECT(controls.thickness), "controls", &controls);
    g_signal_connect(controls.thickness, "value-changed",
                     G_CALLBACK(thickness_changed), &controls);
    row++;

    controls.sigma = gtk_adjustment_new(args->sigma, 1, 1000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Magnetic charge:"), "kA/m",
                                   controls.sigma, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_object_set_data(G_OBJECT(controls.sigma), "controls", &controls);
    g_signal_connect(controls.sigma, "value-changed",
                     G_CALLBACK(sigma_changed), &controls);
    row++;

    controls.angle = gtk_adjustment_new(args->angle, 0, 20, 0.1, 1, 0);
    spin = gwy_table_attach_adjbar(table, row, _("Cantilever _angle:"), "deg",
                                   controls.angle, GWY_HSCALE_LINEAR);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_object_set_data(G_OBJECT(controls.angle), "controls", &controls);
    g_signal_connect(controls.angle, "value-changed",
                     G_CALLBACK(angle_changed), &controls);
    row++;


    controls.walls
        = gtk_check_button_new_with_mnemonic(_("Include domain _walls"));
    gtk_table_attach(GTK_TABLE(table), controls.walls,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.walls),
                                 args->walls);
    g_signal_connect(controls.walls, "toggled",
                     G_CALLBACK(walls_changed), &controls);
    row++;

    controls.wall_a = gtk_adjustment_new(args->wall_a, 1, 1000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Exchange constant:"), "pJ/m",
                                   controls.wall_a, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_object_set_data(G_OBJECT(controls.wall_a), "controls", &controls);
    g_signal_connect(controls.wall_a, "value-changed",
                     G_CALLBACK(wall_a_changed), &controls);
    row++;

    controls.wall_kn = gtk_adjustment_new(args->wall_kn, 1, 1000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row,
                                   _("_Uniaxial anisotropy:"), "kJ/m³",
                                   controls.wall_kn, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_object_set_data(G_OBJECT(controls.wall_kn), "controls", &controls);
    g_signal_connect(controls.wall_kn, "value-changed",
                     G_CALLBACK(wall_kn_changed), &controls);
    row++;

    controls.widthlabel = gtk_label_new(_("Domain wall width:"));
    gtk_misc_set_alignment(GTK_MISC(controls.widthlabel), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.widthlabel,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    controls.widthvalue = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.widthvalue), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.widthvalue,
                     1, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;


    controls.out = gwy_enum_combo_box_new(mfm_field_outputs,
                                          G_N_ELEMENTS(mfm_field_outputs),
                                          G_CALLBACK(out_changed),
                                          &controls, args->out, TRUE);
    gwy_table_attach_adjbar(table, row, _("Output _type:"), NULL,
                            GTK_OBJECT(controls.out),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.probe = gwy_enum_combo_box_new(mfm_field_probes,
                                            G_N_ELEMENTS(mfm_field_probes),
                                            G_CALLBACK(probe_changed),
                                            &controls, args->probe, TRUE);
    gwy_table_attach_adjbar(table, row, _("_Probe type:"), NULL,
                            GTK_OBJECT(controls.probe),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    controls.mtip = gtk_adjustment_new(args->mtip, 1, 10000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Tip _magnetization:"), "kA/m",
                            controls.mtip, GWY_HSCALE_LOG);
    g_object_set_data(G_OBJECT(controls.mtip), "controls", &controls);
    g_signal_connect(controls.mtip, "value-changed",
                     G_CALLBACK(mtip_changed), &controls);
    row++;

    controls.bx = gtk_adjustment_new(args->bx, 1, 1000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Bar width _x:"), "nm",
                            controls.bx, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.bx), "controls", &controls);
    g_signal_connect(controls.bx, "value-changed",
                     G_CALLBACK(bx_changed), &controls);
    row++;

    controls.by = gtk_adjustment_new(args->by, 1, 1000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Bar width _y:"), "nm",
                            controls.by, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.by), "controls", &controls);
    g_signal_connect(controls.by, "value-changed",
                     G_CALLBACK(by_changed), &controls);
    row++;

    controls.length = gtk_adjustment_new(args->length, 1, 10000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Bar length (_z):"), "nm",
                            controls.length, GWY_HSCALE_LOG);
    g_object_set_data(G_OBJECT(controls.length), "controls", &controls);
    g_signal_connect(controls.length, "value-changed",
                     G_CALLBACK(length_changed), &controls);
    row++;

    controls.update = gtk_check_button_new_with_mnemonic(_("I_nstant updates"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.update),
                                 args->update);
    gtk_table_attach(GTK_TABLE(table), controls.update,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls.update, "toggled",
                     G_CALLBACK(update_changed), &controls);
    row++;

    update_sensitivity(&controls);
    update_dw(&controls);

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
                *args = mfm_field_defaults;
                mfm_field_dialog_update(&controls, args);
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
sigma_changed(GtkAdjustment *adj, MfmFieldControls *controls)
{
    controls->args->sigma = gtk_adjustment_get_value(adj);
}

static void
mfm_field_do(MfmFieldArgs *args, GwyDataField *mfield, GwyDataField *out)
{
    GwyDataField  *fz=NULL, *fza, *fzb, *fzc;
    gdouble dd = 1.0e-9;
    gdouble wall_delta = G_PI*sqrt(args->wall_a*1e-12/(args->wall_kn*1e3));

    /* FIXME: This could be done directly if we had a function equivalent to
     * gwy_data_field_mfm_perpendicular_stray_field() which calculated
     * derivatives by Z. */
    if (args->out == GWY_MFM_FIELD_OUTPUT_FIELD || args->out == GWY_MFM_FIELD_OUTPUT_MEFF || args->out == GWY_MFM_FIELD_OUTPUT_FORCE) {
        gwy_data_field_mfm_perpendicular_stray_field(mfield,
                                                     out,
                                                     args->height*1e-9,
                                                     args->thickness*1e-9,
                                                     args->sigma*1e3,
                                                     args->walls,
                                                     wall_delta);

        if (args->angle > 0)
            gwy_data_field_mfm_perpendicular_stray_field_angle_correction(out,
                                                                          args->angle,
                                                                          GWY_ORIENTATION_HORIZONTAL);

        if (args->out == GWY_MFM_FIELD_OUTPUT_MEFF) 
            gwy_data_field_multiply(out, 2.0);


        if (args->out == GWY_MFM_FIELD_OUTPUT_FORCE) {
            fz = gwy_data_field_new_alike(out, TRUE);
            gwy_data_field_mfm_perpendicular_medium_force(out, fz,
                                                          args->probe,
                                                          args->mtip*1e3,
                                                          args->bx*1e-9,
                                                          args->by*1e-9,
                                                          args->length*1e-9);
            gwy_data_field_copy(fz, out, FALSE);
            gwy_object_unref(fz);
        }
    }
    else if (args->out == GWY_MFM_FIELD_OUTPUT_FORCE_DX) {
        //this is done numerically now
        fza = gwy_data_field_new_alike(out, TRUE);
        fzb = gwy_data_field_new_alike(out, TRUE);
        fz = gwy_data_field_new_alike(out, TRUE);

        gwy_data_field_mfm_perpendicular_stray_field(mfield, out,
                                                     args->height*1e-9-dd,
                                                     args->thickness*1e-9,
                                                     args->sigma*1e3,
                                                     args->walls,
                                                     wall_delta);
        gwy_data_field_mfm_perpendicular_medium_force(out, fza,
                                                      args->probe,
                                                      args->mtip*1e3,
                                                      args->bx*1e-9,
                                                      args->by*1e-9,
                                                      args->length*1e-9);

        gwy_data_field_mfm_perpendicular_stray_field(mfield, out,
                                                     args->height*1e-9+dd,
                                                     args->thickness*1e-9,
                                                     args->sigma*1e3,
                                                     args->walls,
                                                     wall_delta);
        gwy_data_field_mfm_perpendicular_medium_force(out, fzb,
                                                      args->probe,
                                                      args->mtip*1e3,
                                                      args->bx*1e-9,
                                                      args->by*1e-9,
                                                      args->length*1e-9);

        gwy_data_field_subtract_fields(fz, fza, fzb);
        gwy_data_field_multiply(fz, 0.5/dd);

        gwy_object_unref(fza);
        gwy_object_unref(fzb);

        gwy_data_field_copy(fz, out, FALSE);
        gwy_object_unref(fz);

    }
    else if (args->out == GWY_MFM_FIELD_OUTPUT_FORCE_DDX) //this is done numerically now
    {
        fza = gwy_data_field_new_alike(out, TRUE);
        fzb = gwy_data_field_new_alike(out, TRUE);
        fzc = gwy_data_field_new_alike(out, TRUE);
        fz = gwy_data_field_new_alike(out, TRUE);


        gwy_data_field_mfm_perpendicular_stray_field(mfield, out,
                                                     args->height*1e-9-dd,
                                                     args->thickness*1e-9,
                                                     args->sigma*1e3,
                                                     args->walls,
                                                     wall_delta);
        gwy_data_field_mfm_perpendicular_medium_force(out, fza,
                                                      args->probe,
                                                      args->mtip*1e3,
                                                      args->bx*1e-9,
                                                      args->by*1e-9,
                                                      args->length*1e-9);

        gwy_data_field_mfm_perpendicular_stray_field(mfield, out,
                                                     args->height*1e-9,
                                                     args->thickness*1e-9,
                                                     args->sigma*1e3,
                                                     args->walls,
                                                     wall_delta);
        gwy_data_field_mfm_perpendicular_medium_force(out, fzb,
                                                      args->probe,
                                                      args->mtip*1e3,
                                                      args->bx*1e-9,
                                                      args->by*1e-9,
                                                      args->length*1e-9);

        gwy_data_field_mfm_perpendicular_stray_field(mfield, out,
                                                     args->height*1e-9+dd,
                                                     args->thickness*1e-9,
                                                     args->sigma*1e3,
                                                     args->walls,
                                                     wall_delta);
        gwy_data_field_mfm_perpendicular_medium_force(out, fzc,
                                                      args->probe,
                                                      args->mtip*1e3,
                                                      args->bx*1e-9,
                                                      args->by*1e-9,
                                                      args->length*1e-9);

        gwy_data_field_multiply(fzb, 2.0);
        gwy_data_field_sum_fields(fz, fza, fzc);
        gwy_data_field_subtract_fields(fz, fz, fzb);

        gwy_data_field_multiply(fz, 1.0/(dd*dd));

        gwy_object_unref(fza);
        gwy_object_unref(fzb);
        gwy_object_unref(fzc);

        gwy_data_field_copy(fz, out, FALSE);
        gwy_object_unref(fz);

    }
}


static void
preview(MfmFieldControls *controls,
        MfmFieldArgs *args)
{
    mfm_field_do(args, controls->mfield, controls->result);
    gwy_data_field_data_changed(controls->result);
}


static void
wall_a_changed(GtkAdjustment *adj, MfmFieldControls *controls)
{
    controls->args->wall_a = gtk_adjustment_get_value(adj);
    update_dw(controls);
    if (controls->args->update) preview(controls, controls->args);
}

static void
wall_kn_changed(GtkAdjustment *adj, MfmFieldControls *controls)
{
    controls->args->wall_kn = gtk_adjustment_get_value(adj);
    update_dw(controls);
    if (controls->args->update) preview(controls, controls->args);
}

static void
height_changed(GtkAdjustment *adj, MfmFieldControls *controls)
{
    controls->args->height = gtk_adjustment_get_value(adj);
    if (controls->args->update) preview(controls, controls->args);
}

static void
thickness_changed(GtkAdjustment *adj, MfmFieldControls *controls)
{
    controls->args->thickness = gtk_adjustment_get_value(adj);
    if (controls->args->update) preview(controls, controls->args);
}

static void
mtip_changed(GtkAdjustment *adj, MfmFieldControls *controls)
{
    controls->args->mtip = gtk_adjustment_get_value(adj);
    if (controls->args->update) preview(controls, controls->args);
}

static void
bx_changed(GtkAdjustment *adj, MfmFieldControls *controls)
{
    controls->args->bx = gtk_adjustment_get_value(adj);
    if (controls->args->update) preview(controls, controls->args);
}

static void
by_changed(GtkAdjustment *adj, MfmFieldControls *controls)
{
    controls->args->by = gtk_adjustment_get_value(adj);
    if (controls->args->update) preview(controls, controls->args);
}

static void
length_changed(GtkAdjustment *adj, MfmFieldControls *controls)
{
    controls->args->length = gtk_adjustment_get_value(adj);
    if (controls->args->update) preview(controls, controls->args);
}

static void
angle_changed(GtkAdjustment *adj, MfmFieldControls *controls)
{
    controls->args->angle = gtk_adjustment_get_value(adj);
    if (controls->args->update) preview(controls, controls->args);
}

static void
update_changed(GtkToggleButton *button, MfmFieldControls *controls)
{
    controls->args->update = gtk_toggle_button_get_active(button);
    if (controls->args->update) preview(controls, controls->args);
}

static void
walls_changed(GtkToggleButton *button, MfmFieldControls *controls)
{
    controls->args->walls = gtk_toggle_button_get_active(button);
    update_sensitivity(controls);
    if (controls->args->update) preview(controls, controls->args);
}

static void
probe_changed(GtkComboBox *combo,
                MfmFieldControls *controls)
{
    controls->args->probe = gwy_enum_combo_box_get_active(combo);
    update_sensitivity(controls);
    if (controls->args->update) preview(controls, controls->args);
}

static void
out_changed(GtkComboBox *combo,
                MfmFieldControls *controls)
{
    controls->args->out = gwy_enum_combo_box_get_active(combo);
    update_sensitivity(controls);
    if (controls->args->update) preview(controls, controls->args);
}

static void
update_dw(MfmFieldControls *controls)
{
    gchar buffer[100];
    g_snprintf(buffer, sizeof(buffer), "%g nm", G_PI*sqrt(controls->args->wall_a*1e-12/(controls->args->wall_kn*1e3))*1e9);

    gtk_label_set_text(GTK_LABEL(controls->widthvalue), buffer);
}
 
static void
update_sensitivity(MfmFieldControls *controls)
{
    gboolean is_walls = controls->args->walls;
    gboolean is_force = TRUE, is_bar = FALSE;

    if (controls->args->out == GWY_MFM_FIELD_OUTPUT_FIELD || controls->args->out == GWY_MFM_FIELD_OUTPUT_MEFF)
        is_force = FALSE;
    if (controls->args->probe == GWY_MFM_PROBE_BAR)
        is_bar = TRUE;
    if (!is_force)
        is_bar = FALSE;

    gtk_widget_set_sensitive(controls->probe, is_force);

    gwy_table_hscale_set_sensitive(controls->wall_a, is_walls);
    gwy_table_hscale_set_sensitive(controls->wall_kn, is_walls);
    gtk_widget_set_sensitive(controls->widthlabel, is_walls);
    gtk_widget_set_sensitive(controls->widthvalue, is_walls);


    gwy_table_hscale_set_sensitive(controls->mtip, is_bar);
    gwy_table_hscale_set_sensitive(controls->bx, is_bar);
    gwy_table_hscale_set_sensitive(controls->by, is_bar);
    gwy_table_hscale_set_sensitive(controls->length, is_bar);

}

static void
mfm_field_dialog_update(MfmFieldControls *controls,
                  MfmFieldArgs *args)
{
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->walls),
                                 args->walls);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->update),
                                 args->update);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->out),
                                  args->out);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->probe),
                                  args->probe);

    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->height), args->height);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->thickness), args->thickness);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->sigma), args->sigma);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->wall_a), args->wall_a);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->wall_kn), args->wall_kn);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->mtip), args->mtip);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->bx), args->bx);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->by), args->by);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->length), args->length);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->angle), args->angle);


}
static const gchar walls_key[]       = "/module/mfm_field/walls";
static const gchar update_key[]      = "/module/mfm_field/update";
static const gchar out_key[]         = "/module/mfm_field/out";
static const gchar probe_key[]       = "/module/mfm_field/probe";
static const gchar height_key[]      = "/module/mfm_field/height";
static const gchar thickness_key[]   = "/module/mfm_field/thickness";
static const gchar sigma_key[]       = "/module/mfm_field/sigma";
static const gchar wall_a_key[]      = "/module/mfm_field/wall_a";
static const gchar wall_kn_key[]     = "/module/mfm_field/wall_kn";
static const gchar mtip_key[]        = "/module/mfm_field/mtip";
static const gchar bx_key[]          = "/module/mfm_field/bx";
static const gchar by_key[]          = "/module/mfm_field/by";
static const gchar length_key[]      = "/module/mfm_field/length";
static const gchar angle_key[]       = "/module/mfm_field/angle";


static void
mfm_field_sanitize_args(MfmFieldArgs *args)
{
    args->walls = !!args->walls;
    args->update = !!args->update;
    args->out = CLAMP(args->out, 0, GWY_MFM_FIELD_OUTPUT_MEFF);
    args->probe = CLAMP(args->probe, 0, GWY_MFM_PROBE_BAR);
}

static void
mfm_field_load_args(GwyContainer *container,
              MfmFieldArgs *args)
{
    *args = mfm_field_defaults;

    gwy_container_gis_boolean_by_name(container, walls_key,
                                      &args->walls);
    gwy_container_gis_boolean_by_name(container, update_key,
                                      &args->update);
    gwy_container_gis_enum_by_name(container, probe_key, &args->probe);
    gwy_container_gis_enum_by_name(container, out_key, &args->out);
    gwy_container_gis_double_by_name(container, height_key, &args->height);
    gwy_container_gis_double_by_name(container, thickness_key, &args->thickness);
    gwy_container_gis_double_by_name(container, sigma_key, &args->sigma);
    gwy_container_gis_double_by_name(container, wall_a_key, &args->wall_a);
    gwy_container_gis_double_by_name(container, wall_kn_key, &args->wall_kn);
    gwy_container_gis_double_by_name(container, mtip_key, &args->mtip);
    gwy_container_gis_double_by_name(container, bx_key, &args->bx);
    gwy_container_gis_double_by_name(container, by_key, &args->by);
    gwy_container_gis_double_by_name(container, length_key, &args->length);
    gwy_container_gis_double_by_name(container, angle_key, &args->angle);

    mfm_field_sanitize_args(args);
}

static void
mfm_field_save_args(GwyContainer *container,
              MfmFieldArgs *args)
{
    gwy_container_set_boolean_by_name(container, walls_key,
                                      args->walls);
    gwy_container_set_boolean_by_name(container, update_key,
                                      args->update);
    gwy_container_set_enum_by_name(container, probe_key, args->probe);
    gwy_container_set_enum_by_name(container, out_key, args->out);
    gwy_container_set_double_by_name(container, height_key, args->height);
    gwy_container_set_double_by_name(container, thickness_key, args->thickness);
    gwy_container_set_double_by_name(container, sigma_key, args->sigma);
    gwy_container_set_double_by_name(container, wall_a_key, args->wall_a);
    gwy_container_set_double_by_name(container, wall_kn_key, args->wall_kn);
    gwy_container_set_double_by_name(container, mtip_key, args->mtip);
    gwy_container_set_double_by_name(container, bx_key, args->bx);
    gwy_container_set_double_by_name(container, by_key, args->by);
    gwy_container_set_double_by_name(container, length_key, args->length);
    gwy_container_set_double_by_name(container, angle_key, args->angle);


}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
