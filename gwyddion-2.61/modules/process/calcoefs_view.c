/*
 *  $Id: calcoefs_view.c 23556 2021-04-21 08:00:51Z yeti-dn $
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwynlfit.h>
#include <libgwydgets/gwydgets.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/gwycalibration.h>
#include <libprocess/gwycaldata.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define CC_VIEW_RUN_MODES GWY_RUN_INTERACTIVE

#define MAX_PARAMS 4

typedef enum {
    GWY_CC_VIEW_DISPLAY_X_CORR   = 0,
    GWY_CC_VIEW_DISPLAY_Y_CORR = 1,
    GWY_CC_VIEW_DISPLAY_Z_CORR = 2,
    GWY_CC_VIEW_DISPLAY_X_UNC = 3,
    GWY_CC_VIEW_DISPLAY_Y_UNC = 4,
    GWY_CC_VIEW_DISPLAY_Z_UNC = 5,
} GwyCCViewDisplayType;

typedef enum {
    GWY_CC_VIEW_PLANE_X   = 0,
    GWY_CC_VIEW_PLANE_Y = 1,
    GWY_CC_VIEW_PLANE_Z = 2
} GwyCCViewPlaneType;

typedef enum {
    GWY_CC_VIEW_INTERPOLATION_3D   = 0,
    GWY_CC_VIEW_INTERPOLATION_PLANE = 1,
    GWY_CC_VIEW_INTERPOLATION_NATURAL   = 2
} GwyCCViewInterpolationType;


typedef struct {
    gdouble xrange;
    gdouble yrange;
    gdouble zrange;
    gint ndata;
    gdouble **calval;  // set of calibration values: x, y, z, x_cor, y_cor, z_cor, x_unc, y_unc, z_unc
} GwyCalibrationData;


typedef struct {
    GwyCCViewDisplayType display_type;
    GwyCCViewPlaneType plane_type;
    GwyCCViewInterpolationType interpolation_type;
    gdouble xplane;
    gdouble yplane;
    gdouble zplane;
    gboolean crop;
    gboolean update;
    gint calibration;
    gboolean computed;
    gint id;
    gdouble xoffset;
    gdouble yoffset;
    gdouble zoffset;
    gint xyexponent;
    gint zexponent;

} CCViewArgs;

static const CCViewArgs ccview_defaults = {
    GWY_CC_VIEW_DISPLAY_X_CORR,
    GWY_CC_VIEW_PLANE_X,
    GWY_CC_VIEW_INTERPOLATION_3D,
    0,
    0,
    0,
    FALSE,
    FALSE,
    0,
    FALSE,
    0,
    0,
    0,
    0,
    0,
    0,
};


typedef struct {
    GtkWidget *dialog;
    GtkWidget *view;
    GtkWidget *type;
    GwyContainer *mydata;
    GtkWidget *menu_display;
    GtkWidget *menu_plane;
    GtkWidget *menu_interpolation;
    GtkWidget *crop;
    GtkWidget *update;
    GtkWidget *calibration;
    GwyContainer *data;
    gint original_id;
    GwyDataField *view_field;
    GwyDataField *actual_field;
    GwyDataField *xerr;
    GwyDataField *yerr;
    GwyDataField *zerr;
    GwyDataField *xunc;
    GwyDataField *yunc;
    GwyDataField *zunc;
    GtkObject *xplane;
    GtkObject *yplane;
    GtkObject *zplane;
    CCViewArgs *args;
    GtkObject *xoffset;
    GtkObject *yoffset;
    GtkObject *zoffset;
    GtkWidget *xyunits;
    GtkWidget *zunits;
    GtkWidget *xyexponent;
    GtkWidget *zexponent;
    GtkWidget *button_ok;
    GtkWidget *message1; //TODO rename this to something reasonable when fixed
    GtkWidget *message2;
    GtkWidget *message3;
    GtkWidget *message4;
    GtkWidget *message5;
    GtkWidget *resmes;
    gboolean in_update;

} CCViewControls;

static gboolean     module_register        (void);
static void         cc_view                 (GwyContainer *data,
                                            GwyRunType run);
static void         cc_view_dialog          (CCViewArgs *args,
                                            GwyContainer *data,
                                            GwyDataField *dfield,
                                            gint id);
static void         cc_view_load_args       (GwyContainer *container,
                                            CCViewArgs *args);
static void         cc_view_save_args       (GwyContainer *container,
                                            CCViewArgs *args);
static void         cc_view_sanitize_args   (CCViewArgs *args);
static void         cc_view_do              (CCViewControls *controls);
static void         cc_view_dialog_abandon  (CCViewControls *controls);
static GtkWidget*   menu_display           (GCallback callback,
                                            gpointer cbdata,
                                            GwyCCViewDisplayType current);
static GtkWidget*   menu_plane             (GCallback callback,
                                            gpointer cbdata,
                                            GwyCCViewPlaneType current);
static GtkWidget*   menu_interpolation     (GCallback callback,
                                            gpointer cbdata,
                                            GwyCCViewInterpolationType current);
static void         display_changed        (GtkComboBox *combo,
                                            CCViewControls *controls);
static void         calculation_changed        (GtkComboBox *combo,
                                            CCViewControls *controls);


static void         update_view            (CCViewControls *controls,
                                            CCViewArgs *args);
static void         settings_changed       (CCViewControls *controls);
static void         crop_change_cb         (CCViewControls *controls);
static void         calibration_changed_cb (CCViewControls *controls);
static void         update_change_cb       (CCViewControls *controls);
static void         brutal_search          (GwyCalData *caldata,
                                            gdouble x,
                                            gdouble y,
                                            gdouble z,
                                            gdouble radius,
                                            gint *pos,
                                            gdouble *dist,
                                            gint *ndata,
                                            GwyCCViewInterpolationType snap_type);
static void         get_value              (GwyCalData *caldata,
                                            gdouble x,
                                            gdouble y,
                                            gdouble z,
                                            gdouble *xerr,
                                            gdouble *yerr,
                                            gdouble *zerr,
                                            gdouble *xunc,
                                            gdouble *yunc,
                                            gdouble *zunc,
                                            GwyCCViewInterpolationType snap_type);
static void        xyexponent_changed_cb       (GtkWidget *combo,
                                                CCViewControls *controls);
static void        zexponent_changed_cb       (GtkWidget *combo,
                                               CCViewControls *controls);
static void        units_change_cb             (GtkWidget *button,
                                                CCViewControls *controls);
static void        set_combo_from_unit       (GtkWidget *combo,
                                              const gchar *str,
                                              gint basepower);
static void        xoffset_changed_cb          (GtkAdjustment *adj,
                                                CCViewControls *controls);
static void        yoffset_changed_cb          (GtkAdjustment *adj,
                                                CCViewControls *controls);
static void        zoffset_changed_cb          (GtkAdjustment *adj,
                                                CCViewControls *controls);



static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("3D calibration/uncertainty"),
    "Petr Klapetek <petr@klapetek.cz>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2010",
};

GWY_MODULE_QUERY2(module_info, calcoefs_view)

static gboolean
module_register(void)
{
    gwy_process_func_register("cc_view",
                              (GwyProcessFunc)&cc_view,
                              N_("/Cali_bration/_Apply to Data..."),
                              NULL,
                              CC_VIEW_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("3D calibration and uncertainty"));

    return TRUE;
}

static void
cc_view(GwyContainer *data, GwyRunType run)
{
    CCViewArgs args;
    GwyDataField *dfield;
    g_return_if_fail(run & CC_VIEW_RUN_MODES);

    cc_view_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &(args.id),
                                     0);
    g_return_if_fail(dfield);

    cc_view_dialog(&args, data, dfield, args.id);
    cc_view_save_args(gwy_app_settings_get(), &args);
}

static void
cc_view_dialog(CCViewArgs *args,
              GwyContainer *data,
              GwyDataField *dfield,
              gint id)
{
    GtkWidget *dialog, *table, *hbox, *vbox, *alignment, *spin;
    CCViewControls controls;
    GwyInventory *inventory;
    GwyInventoryStore *store;
    /* GwyCalibration *calibration; */
    GtkCellRenderer *renderer;
    gboolean cropval;
    gint response;
    guint row = 0;
    GtkWidget *label;
    GwySIUnit *unit;
    GwySIValueFormat *vf;


    controls.args = args;
    args->calibration = 0;
    args->computed = 0;

    /*FIXME: load more from dfield*/
    args->xoffset = gwy_data_field_get_xoffset(dfield);
    args->yoffset = gwy_data_field_get_yoffset(dfield);
    args->zoffset = 0;

    vf = gwy_data_field_get_value_format_xy(dfield,
                                       GWY_SI_UNIT_FORMAT_MARKUP,
                                       NULL);
    args->xyexponent = log10(vf->magnitude);

    vf = gwy_data_field_get_value_format_z(dfield,
                                       GWY_SI_UNIT_FORMAT_MARKUP,
                                       vf);
    args->zexponent = log10(vf->magnitude);

    gwy_si_unit_value_format_free(vf);


    dialog = gtk_dialog_new_with_buttons(_("3D Calibration"), NULL, 0, NULL);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                                 gwy_stock_like_button_new(_("_Update"),
                                                           GTK_STOCK_EXECUTE),
                                 RESPONSE_PREVIEW);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    controls.button_ok = gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_OK, GTK_RESPONSE_OK);

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    hbox = gtk_hbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    controls.actual_field = dfield;
    controls.view_field = gwy_data_field_new(200, 200,
                                             gwy_data_field_get_xreal(dfield),
                                             gwy_data_field_get_yreal(dfield),
                                             TRUE);
    gwy_data_field_set_si_unit_xy(controls.view_field, gwy_data_field_get_si_unit_xy(dfield));

    controls.xerr = gwy_data_field_new_alike(controls.view_field, TRUE);
    gwy_data_field_set_si_unit_z(controls.xerr, gwy_data_field_get_si_unit_xy(dfield));

    controls.yerr = gwy_data_field_new_alike(controls.view_field, TRUE);
    gwy_data_field_set_si_unit_z(controls.yerr, gwy_data_field_get_si_unit_xy(dfield));

    controls.zerr = gwy_data_field_new_alike(controls.view_field, TRUE);
    gwy_data_field_set_si_unit_z(controls.zerr, gwy_data_field_get_si_unit_z(dfield));

    controls.xunc = gwy_data_field_new_alike(controls.view_field, TRUE);
    gwy_data_field_set_si_unit_z(controls.xunc, gwy_data_field_get_si_unit_xy(dfield));

    controls.yunc = gwy_data_field_new_alike(controls.view_field, TRUE);
    gwy_data_field_set_si_unit_z(controls.yunc, gwy_data_field_get_si_unit_xy(dfield));

    controls.zunc = gwy_data_field_new_alike(controls.view_field, TRUE);
    gwy_data_field_set_si_unit_z(controls.zunc, gwy_data_field_get_si_unit_z(dfield));


    controls.data = data;
    controls.mydata = gwy_container_new();
    gwy_container_set_object_by_name(controls.mydata,
                                     "/0/data", controls.view_field);
    gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            0);
    controls.view = gwy_create_preview(controls.mydata, 0, PREVIEW_SMALL_SIZE,
                                   FALSE);
    alignment = GTK_WIDGET(gtk_alignment_new(0.5, 0, 0, 0));
    gtk_container_add(GTK_CONTAINER(alignment), controls.view);

    vbox = gtk_vbox_new(FALSE, 3);
    gtk_box_pack_start(GTK_BOX(vbox), alignment, FALSE, FALSE, 4);

    controls.message1 = gtk_label_new(_("No data used."));
    gtk_box_pack_start(GTK_BOX(vbox), controls.message1, FALSE, FALSE, 4);
    controls.message2 = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(vbox), controls.message2, FALSE, FALSE, 4);
    controls.message3 = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(vbox), controls.message3, FALSE, FALSE, 4);
    controls.message4 = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(vbox), controls.message4, FALSE, FALSE, 4);
    controls.message5 = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(vbox), controls.message5, FALSE, FALSE, 4);
    controls.resmes = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(vbox), controls.resmes, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 4);


    /*set up controls*/
    vbox = gtk_vbox_new(FALSE, 3);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 4);

    label = gwy_label_new_header(_("Used calibration data:"));
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 4);

    inventory = gwy_calibrations();
    store = gwy_inventory_store_new(inventory);
    controls.calibration = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    renderer = gtk_cell_renderer_text_new();
    g_signal_connect_swapped(controls.calibration, "changed",
                             G_CALLBACK(calibration_changed_cb),
                             &controls);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(controls.calibration), renderer, FALSE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(controls.calibration), renderer,
                                  "text", 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(controls.calibration), args->calibration);

    gtk_box_pack_start(GTK_BOX(vbox), controls.calibration, FALSE, FALSE, 4);

    label = gwy_label_new_header(_("Shown planes:"));
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 4);

    table = gtk_table_new(8, 4, FALSE);


    label = gtk_label_new_with_mnemonic(_("View:"));

    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);

    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
                     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 2, 2);

    controls.menu_display = menu_display(G_CALLBACK(display_changed),
                                         &controls,
                                         args->display_type);
    //row++;

    gtk_table_attach(GTK_TABLE(table), controls.menu_display, 1, 2, row, row+1,
                     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 2, 2);

    label = gtk_label_new_with_mnemonic(_("Plane:"));

    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);

    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
                     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 2, 2);

    row++;
    controls.menu_plane = menu_plane(G_CALLBACK(calculation_changed),
                                     &controls,
                                     args->plane_type);

    gtk_table_attach(GTK_TABLE(table), controls.menu_plane, 1, 2, row, row+1,
                     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 2, 2);

    row++;
    args->xplane = args->yplane = args->zplane = 0;
    controls.xplane = gtk_adjustment_new(args->xplane,
                                         0.0, 100.0, 1, 100, 0);
    gwy_table_attach_hscale(table, row++, _("X position:"), "%",
                                       controls.xplane, 0);
    g_signal_connect_swapped(controls.xplane, "value-changed",
                             G_CALLBACK(settings_changed), &controls);

    controls.yplane = gtk_adjustment_new(args->yplane,
                                       0.0, 100.0, 1, 100, 0);
    gwy_table_attach_hscale(table, row++, _("Y position:"), "%",
                            controls.yplane, 0);
    g_signal_connect_swapped(controls.yplane, "value-changed",
                                       G_CALLBACK(settings_changed), &controls);

    controls.zplane = gtk_adjustment_new(args->zplane,
                                       0.0, 100.0, 1, 100, 0);
    gwy_table_attach_hscale(table, row++, _("Z position:"), "%",
                                       controls.zplane, 0);
    g_signal_connect_swapped(controls.zplane, "value-changed",
                                       G_CALLBACK(settings_changed), &controls);

    label = gtk_label_new_with_mnemonic(_("_Interpolation type:"));

    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);

    gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row+1,
                     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 2, 2);


    controls.menu_interpolation = menu_interpolation(G_CALLBACK(calculation_changed),
                                         &controls,
                                         args->interpolation_type);

    gtk_table_attach(GTK_TABLE(table), controls.menu_interpolation, 1, 2, row, row+1,
                     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 2, 2);

    row++;


    label = gtk_label_new_with_mnemonic(_("_X offset:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls.xoffset = gtk_adjustment_new(args->xoffset/pow10(args->xyexponent),
                                        -10000, 10000, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.xoffset), 1, 2);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_table_attach(GTK_TABLE(table), spin,
                     1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    unit = gwy_data_field_get_si_unit_xy(dfield);
    controls.xyexponent
        = gwy_combo_box_metric_unit_new(G_CALLBACK(xyexponent_changed_cb),
                                        &controls, -15, 6, unit,
                                        args->xyexponent);
    gtk_table_attach(GTK_TABLE(table), controls.xyexponent, 2, 3, row, row+2,
                     GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0, 0, 0);

    controls.xyunits = gtk_button_new_with_label(gwy_sgettext("verb|Change"));
    g_object_set_data(G_OBJECT(controls.xyunits), "id", (gpointer)"xy");
    gtk_table_attach(GTK_TABLE(table), controls.xyunits,
                     3, 4, row, row+2,
                     GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0, 0, 0);
    row++;
    label = gtk_label_new_with_mnemonic(_("_Y offset:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls.yoffset = gtk_adjustment_new(args->yoffset/pow10(args->xyexponent),
                                        -10000, 10000, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.yoffset), 1, 2);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_table_attach(GTK_TABLE(table), spin,
                     1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;
    label = gtk_label_new_with_mnemonic(_("_Z offset:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls.zoffset = gtk_adjustment_new(args->zoffset/pow10(args->zexponent),
                                        -10000, 10000, 1, 10, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.zoffset), 1, 2);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_table_attach(GTK_TABLE(table), spin,
                     1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    unit = gwy_data_field_get_si_unit_z(dfield);
    controls.zexponent
        = gwy_combo_box_metric_unit_new(G_CALLBACK(zexponent_changed_cb),
                                        &controls, -15, 6, unit,
                                        args->zexponent);
    gtk_table_attach(GTK_TABLE(table), controls.zexponent, 2, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0, 0, 0);

    controls.zunits = gtk_button_new_with_label(gwy_sgettext("verb|Change"));
    g_object_set_data(G_OBJECT(controls.zunits), "id", (gpointer)"z");
    gtk_table_attach(GTK_TABLE(table), controls.zunits,
                     3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;


    controls.crop = gtk_check_button_new_with_mnemonic(_("Crop to actual data"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.crop),
                                                    args->crop);
    gtk_table_attach(GTK_TABLE(table), controls.crop,
                         0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.crop, "toggled",
                        G_CALLBACK(crop_change_cb), &controls);
    row++;


    controls.update = gtk_check_button_new_with_mnemonic(_("I_nstant updates"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.update),
                                                    args->update);
    gtk_table_attach(GTK_TABLE(table), controls.update,
                         0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.update, "toggled",
                         G_CALLBACK(update_change_cb), &controls);
    row++;


    gtk_container_add(GTK_CONTAINER(vbox), table);
    calculation_changed(NULL, &controls);

    g_signal_connect(controls.xoffset, "value-changed",
                     G_CALLBACK(xoffset_changed_cb), &controls);
    g_signal_connect(controls.yoffset, "value-changed",
                     G_CALLBACK(yoffset_changed_cb), &controls);
    g_signal_connect(controls.zoffset, "value-changed",
                     G_CALLBACK(zoffset_changed_cb), &controls);
    g_signal_connect(controls.xyunits, "clicked",
                     G_CALLBACK(units_change_cb), &controls);

    g_signal_connect(controls.zunits, "clicked",
                     G_CALLBACK(units_change_cb), &controls);

    controls.in_update = FALSE;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),
                                      RESPONSE_PREVIEW,
                                      !args->update);
    //update_view(&controls, args);


    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            return;
            break;

            case GTK_RESPONSE_OK:
            if (!args->computed || !args->crop)
            {
                cropval = args->crop;
                args->crop = TRUE;
                args->computed = FALSE;
                update_view(&controls, args);
                args->crop = cropval;
            }
            cc_view_do(&controls);
            break;

            case RESPONSE_PREVIEW:
            update_view(&controls, args);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    args->calibration = gtk_combo_box_get_active(GTK_COMBO_BOX(controls.calibration));

    /*
    if (args->calibration < gwy_inventory_get_n_items(inventory))
        calibration = gwy_inventory_get_nth_item(inventory, args->calibration);
    else
        calibration = NULL;
    */

    gtk_widget_destroy(dialog);
    cc_view_dialog_abandon(&controls);
}

static gboolean
field_inside(GwyCalData *caldata, GwyDataField *dfield,
             gdouble xoffset, gdouble yoffset, gdouble zoffset)
{
    gdouble xmin, xmax, ymin, ymax, zmin, zmax;

    gwy_caldata_get_range(caldata, &xmin, &xmax, &ymin, &ymax, &zmin, &zmax);
    if (gwy_caldata_inside(caldata, xoffset + gwy_data_field_get_xoffset(dfield),
                                    yoffset + gwy_data_field_get_yoffset(dfield),
                                    zoffset + gwy_data_field_get_min(dfield))
        &&
        gwy_caldata_inside(caldata, xoffset + gwy_data_field_get_xoffset(dfield)
                                            + gwy_data_field_get_xreal(dfield),
                                    yoffset + gwy_data_field_get_yoffset(dfield)
                                            + gwy_data_field_get_yreal(dfield),
                                    zoffset + gwy_data_field_get_max(dfield)))
        return TRUE;
    return FALSE;

}

static void
cc_view_dialog_abandon(CCViewControls *controls)
{
    GWY_OBJECT_UNREF(controls->view_field);
    GWY_OBJECT_UNREF(controls->mydata);
}

static void show_info(CCViewControls *controls, GwyDataField *dfield)
{
    GwySIUnit *siunit;
    GwySIValueFormat *maxf;
    gdouble max, min;
    gchar msg[50];

    if (gwy_data_field_get_sum(dfield) == 0.0)
        g_snprintf(msg, sizeof(msg), _("Shown part has zero range."));
    else {
        siunit = gwy_data_field_get_si_unit_z(dfield);
        min = gwy_data_field_get_min(dfield);
        max = gwy_data_field_get_max(dfield);
        maxf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, max,
                                      NULL);

        g_snprintf(msg, sizeof(msg), _("Shown range (%.*f - %.*f) %s"),
                   maxf->precision, min/maxf->magnitude,
                   maxf->precision, max/maxf->magnitude,
                   maxf->units);
        gwy_si_unit_value_format_free(maxf);
    }
    gtk_label_set_markup(GTK_LABEL(controls->message5), msg);
}


/*update preview depending on user's wishes*/
static void
update_view(CCViewControls *controls, CCViewArgs *args)
{
    gint col, row, xres, yres, zres;
    gdouble x, y, z, xerr, yerr, zerr, xunc, yunc, zunc;
    gdouble x_from, x_to, y_from, y_to, z_from, z_to;
    GwyDataField *viewfield;
    gchar msg[50];
    GwyCalibration *calibration = NULL;
    GwyCalData *caldata = NULL;
    GwySIUnit *six, *siy, *siz;
    GwySIValueFormat *maxf;
    gboolean run = TRUE;
    gboolean posok = TRUE;

    viewfield = GWY_DATA_FIELD(gwy_container_get_object_by_name(controls->mydata,
                                                                  "/0/data"));

    args->calibration = gtk_combo_box_get_active(GTK_COMBO_BOX(controls->calibration));
    if (args->calibration < gwy_inventory_get_n_items(gwy_calibrations()))
        calibration = gwy_inventory_get_nth_item(gwy_calibrations(), args->calibration);

    if (calibration==NULL) {
        gtk_widget_set_sensitive(controls->button_ok, FALSE);
    } else {
        gtk_widget_set_sensitive(controls->button_ok, TRUE);

        gwy_resource_use(GWY_RESOURCE(calibration));
        caldata = gwy_calibration_get_data(calibration);

        six = gwy_caldata_get_si_unit_x(caldata);
        siy = gwy_caldata_get_si_unit_y(caldata);
        siz = gwy_caldata_get_si_unit_z(caldata);
        gwy_caldata_get_range(caldata, &x_from, &x_to, &y_from, &y_to, &z_from, &z_to);

       // printf("Loaded %g %g %g %g\n", x_from, x_to, y_from, y_to);

        g_snprintf(msg, sizeof(msg), _("%d calibration data"), gwy_caldata_get_ndata(caldata));
        gtk_label_set_text(GTK_LABEL(controls->message1), msg);

        maxf = gwy_si_unit_get_format(six, GWY_SI_UNIT_FORMAT_VFMARKUP, x_to, NULL);
        g_snprintf(msg, sizeof(msg), _("X span: (%.*f - %.*f) %s"),
                    maxf->precision, x_from/maxf->magnitude, maxf->precision, x_to/maxf->magnitude,
                    maxf->units);
        gtk_label_set_markup(GTK_LABEL(controls->message2), msg);

        gwy_si_unit_get_format(siy, GWY_SI_UNIT_FORMAT_VFMARKUP, y_to, maxf);
        g_snprintf(msg, sizeof(msg), _("Y span: (%.*f - %.*f) %s"),
                    maxf->precision, y_from/maxf->magnitude, maxf->precision, y_to/maxf->magnitude,
                    maxf->units);
        gtk_label_set_markup(GTK_LABEL(controls->message3), msg);

        gwy_si_unit_get_format(siz, GWY_SI_UNIT_FORMAT_VFMARKUP, z_to, maxf);
        g_snprintf(msg, sizeof(msg), _("Z span: (%.*f - %.*f) %s"),
                    maxf->precision, z_from/maxf->magnitude, maxf->precision, z_to/maxf->magnitude,
                    maxf->units);
        gtk_label_set_markup(GTK_LABEL(controls->message4), msg);

        gwy_si_unit_value_format_free(maxf);
    }


    /*FIXME determine maximum necessary size of field*/
    xres = 200;
    yres = 200;
    zres = 200;

    if (!caldata) {
        gwy_data_field_fill(viewfield, 0);
        gwy_data_field_data_changed(viewfield);
        return;
    }

    //gwy_caldata_debug(caldata, "Using: ");
    if (controls->args->crop && !field_inside(caldata, controls->actual_field,
                              args->xoffset, args->yoffset, args->zoffset))
    {
        g_snprintf(msg, sizeof(msg), _("Error: out of range."));
        gtk_label_set_markup(GTK_LABEL(controls->resmes), msg);
        posok = FALSE;
        gtk_widget_set_sensitive(controls->button_ok, FALSE);
    }
    else gtk_label_set_markup(GTK_LABEL(controls->resmes), "");


    if (posok && !args->computed) {
        gwy_app_wait_start(GTK_WINDOW(controls->dialog), _("Building mesh..."));

        if (args->interpolation_type == GWY_CC_VIEW_INTERPOLATION_NATURAL)
        {
            gwy_caldata_setup_interpolation(caldata);
        }
        run = gwy_app_wait_set_message(_("Triangulating..."));
        run = gwy_app_wait_set_fraction(0);

        if (run && controls->args->crop) {
            for (row=0; row<yres; row++)
            {
                y = controls->args->yoffset + gwy_data_field_get_yoffset(controls->actual_field) +
                    row*gwy_data_field_get_yreal(controls->actual_field)/yres;
                for (col=0; col<xres; col++) {
                    x = controls->args->xoffset + gwy_data_field_get_xoffset(controls->actual_field) +
                        col*gwy_data_field_get_yreal(controls->actual_field)/xres;
                    z = controls->args->zoffset + gwy_data_field_get_dval(controls->actual_field,
                                                col*gwy_data_field_get_yreal(controls->actual_field)/xres,
                                                row*gwy_data_field_get_yreal(controls->actual_field)/yres,
                                                GWY_INTERPOLATION_BILINEAR);

                    get_value(caldata, x, y, z, &xerr, &yerr, &zerr, &xunc, &yunc, &zunc, args->interpolation_type);
                    controls->xerr->data[col + xres*row] = xerr;
                    controls->yerr->data[col + xres*row] = yerr;
                    controls->zerr->data[col + xres*row] = zerr;
                    controls->xunc->data[col + xres*row] = xunc;
                    controls->yunc->data[col + xres*row] = yunc;
                    controls->zunc->data[col + xres*row] = zunc;

                }
                if (!(run = gwy_app_wait_set_fraction((gdouble)row/(gdouble)yres))) break;
            }
        } else if (run) {
            if (controls->args->plane_type == GWY_CC_VIEW_PLANE_X)
            {
                gwy_data_field_resample(viewfield, yres, zres, GWY_INTERPOLATION_NONE);
                x = x_from + (x_to-x_from)*(gdouble)args->xplane/100.0;
                for (col=0; col<yres; col++)
                {
                    y = y_from + (y_to-y_from)*(gdouble)col/(double)yres;
                    for (row=0; row<zres; row++) {
                        z = z_from + (z_to-z_from)*(gdouble)row/(double)zres;

                        get_value(caldata, x, y, z, &xerr, &yerr, &zerr, &xunc, &yunc, &zunc, args->interpolation_type);
                        controls->xerr->data[col + yres*row] = xerr;
                        controls->yerr->data[col + yres*row] = yerr;
                        controls->zerr->data[col + yres*row] = zerr;
                        controls->xunc->data[col + yres*row] = xunc;
                        controls->yunc->data[col + yres*row] = yunc;
                        controls->zunc->data[col + yres*row] = zunc;
                    }
                    if (!(run = gwy_app_wait_set_fraction((gdouble)col/(gdouble)yres))) break;

                }
            }
            if (controls->args->plane_type == GWY_CC_VIEW_PLANE_Y)
            {
                gwy_data_field_resample(viewfield, xres, zres, GWY_INTERPOLATION_NONE);
                y = y_from + (y_to-y_from)*(gdouble)args->yplane/100.0;
                for (col=0; col<xres; col++)
                {
                    x = x_from + (x_to-x_from)*(gdouble)col/(double)xres;
                    for (row=0; row<zres; row++) {
                        z = z_from + (z_to-z_from)*(gdouble)row/(double)zres;
                        get_value(caldata, x, y, z, &xerr, &yerr, &zerr, &xunc, &yunc, &zunc, args->interpolation_type);
                        controls->xerr->data[col + xres*row] = xerr;
                        controls->yerr->data[col + xres*row] = yerr;
                        controls->zerr->data[col + xres*row] = zerr;
                        controls->xunc->data[col + xres*row] = xunc;
                        controls->yunc->data[col + xres*row] = yunc;
                        controls->zunc->data[col + xres*row] = zunc;
                    }
                    if (!(run = gwy_app_wait_set_fraction((gdouble)col/(gdouble)xres))) break;

                }
            }
            if (controls->args->plane_type == GWY_CC_VIEW_PLANE_Z)
            {
                gwy_data_field_resample(viewfield, xres, yres, GWY_INTERPOLATION_NONE);
                gwy_data_field_set_xreal(viewfield, x_to - x_from);
                gwy_data_field_set_yreal(viewfield, y_to - y_from);

                z = z_from + (z_to-z_from)*(gdouble)args->zplane/100.0;
                for (col=0; col<xres; col++)
                {
                    x = gwy_data_field_get_yoffset(viewfield) +
                        col*gwy_data_field_get_xreal(viewfield)/xres;
                    for (row=0; row<yres; row++) {
                        y = gwy_data_field_get_yoffset(viewfield) +
                            row*gwy_data_field_get_yreal(viewfield)/yres;

                        get_value(caldata, x, y, z, &xerr, &yerr, &zerr, &xunc, &yunc, &zunc, args->interpolation_type);
                        controls->xerr->data[col + xres*row] = xerr;
                        controls->yerr->data[col + xres*row] = yerr;
                        controls->zerr->data[col + xres*row] = zerr;
                        controls->xunc->data[col + xres*row] = xunc;
                        controls->yunc->data[col + xres*row] = yunc;
                        controls->zunc->data[col + xres*row] = zunc;
                    }
                    if (!(run = gwy_app_wait_set_fraction((gdouble)col/(gdouble)xres))) break;
                }
            }

        }
        gwy_data_field_invalidate(controls->xerr);
        gwy_data_field_invalidate(controls->yerr);
        gwy_data_field_invalidate(controls->zerr);
        gwy_data_field_invalidate(controls->xunc);
        gwy_data_field_invalidate(controls->yunc);
        gwy_data_field_invalidate(controls->zunc);
        if (run)
            args->computed = TRUE;

        gwy_app_wait_finish();

    }

    if (controls->args->display_type == GWY_CC_VIEW_DISPLAY_X_CORR) {
        show_info(controls, controls->xerr);
        if (run) gwy_data_field_copy(controls->xerr, viewfield, FALSE);
    }
    else if (controls->args->display_type == GWY_CC_VIEW_DISPLAY_Y_CORR) {
        show_info(controls, controls->yerr);
        if (run) gwy_data_field_copy(controls->yerr, viewfield, FALSE);
    }
    else if (controls->args->display_type == GWY_CC_VIEW_DISPLAY_Z_CORR) {
        show_info(controls, controls->zerr);
        if (run) gwy_data_field_copy(controls->zerr, viewfield, FALSE);
    }
    else if (controls->args->display_type == GWY_CC_VIEW_DISPLAY_X_UNC) {
        show_info(controls, controls->xunc);
        if (run) gwy_data_field_copy(controls->xunc, viewfield, FALSE);
    }
    else if (controls->args->display_type == GWY_CC_VIEW_DISPLAY_Y_UNC) {
        show_info(controls, controls->yunc);
        if (run) gwy_data_field_copy(controls->yunc, viewfield, FALSE);
    }
    else if (controls->args->display_type == GWY_CC_VIEW_DISPLAY_Z_UNC) {
        show_info(controls, controls->zunc);
        if (run) gwy_data_field_copy(controls->zunc, viewfield, FALSE);
    }

    gwy_data_field_invalidate(controls->view_field);
    gwy_data_field_data_changed(controls->view_field);
}


static void
add_calibration(GwyDataField *dfield,
                GwyContainer *data,
                gint id,
                GwyCCViewDisplayType type)
{
    gchar key[24];


    if (type == GWY_CC_VIEW_DISPLAY_X_CORR)
       g_snprintf(key, sizeof(key), "/%d/data/cal_xerr", id);
    else if (type == GWY_CC_VIEW_DISPLAY_Y_CORR)
       g_snprintf(key, sizeof(key), "/%d/data/cal_yerr", id);
    else if (type == GWY_CC_VIEW_DISPLAY_Z_CORR)
       g_snprintf(key, sizeof(key), "/%d/data/cal_zerr", id);
    else if (type == GWY_CC_VIEW_DISPLAY_X_UNC)
       g_snprintf(key, sizeof(key), "/%d/data/cal_xunc", id);
    else if (type == GWY_CC_VIEW_DISPLAY_Y_UNC)
       g_snprintf(key, sizeof(key), "/%d/data/cal_yunc", id);
    else if (type == GWY_CC_VIEW_DISPLAY_Z_UNC)
       g_snprintf(key, sizeof(key), "/%d/data/cal_zunc", id);
    else {
        g_critical("No such calibration key.");
        return;
    }
    gwy_container_set_object_by_name(data, key, dfield);

}


/*dialog finished, everything should be computed*/
static void
cc_view_do(CCViewControls *controls)
{

    add_calibration(controls->xerr, controls->data, controls->args->id, GWY_CC_VIEW_DISPLAY_X_CORR);
    add_calibration(controls->yerr, controls->data, controls->args->id, GWY_CC_VIEW_DISPLAY_Y_CORR);
    add_calibration(controls->zerr, controls->data, controls->args->id, GWY_CC_VIEW_DISPLAY_Z_CORR);
    add_calibration(controls->xunc, controls->data, controls->args->id, GWY_CC_VIEW_DISPLAY_X_UNC);
    add_calibration(controls->yunc, controls->data, controls->args->id, GWY_CC_VIEW_DISPLAY_Y_UNC);
    add_calibration(controls->zunc, controls->data, controls->args->id, GWY_CC_VIEW_DISPLAY_Z_UNC);
    /*now the data should be present in container and user functions can use them*/

    /*modules won't see it immediately if you don't emit anything*/
    gwy_data_field_data_changed(controls->actual_field);

}
static void
brutal_search(GwyCalData *caldata, gdouble x, gdouble y, gdouble z, gdouble radius,
                   gint *pos, gdouble *dist, gint *ndata, GwyCCViewInterpolationType snap_type)
{
    gint i, j, smallest = 0, largest = 0;
    gdouble val, minval, maxval, *xd, *yd, *zd;
    gint maxdata = *ndata;
    gboolean snap = 0;
    gdouble splane = 0;
    *ndata = 0;

    if (!caldata) return;

    xd = gwy_caldata_get_x(caldata);
    yd = gwy_caldata_get_y(caldata);
    zd = gwy_caldata_get_z(caldata);

    /*find closest plane, if requested*/
    if (snap_type == GWY_CC_VIEW_INTERPOLATION_PLANE)
    {
        minval = G_MAXDOUBLE;
        for (i=0; i<gwy_caldata_get_ndata(caldata); i++)
        {
            if (fabs(z - zd[i]) < minval) {
                minval = fabs(z - zd[i]);
                smallest = i;
            }
        }
        splane = zd[smallest];
        snap = 1;
    }

    for (i=0; i<gwy_caldata_get_ndata(caldata); i++)
    {
        if (snap && (fabs(zd[i]-splane))>1e-6) continue;

        if ((val=((xd[i] - x)*(xd[i] - x) +
             (yd[i] - y)*(yd[i] - y) +
             (zd[i] - z)*(zd[i] - z))) < (radius*radius))
        {
            if ((*ndata) == maxdata)
            {
                maxval = -G_MAXDOUBLE;
                for (j=0; j<(*ndata); j++)
                {
                    if (dist[j] > maxval) {
                        maxval = dist[j];
                        largest = j;
                    }
                }
                if ((dist[largest]*dist[largest]) > val)
                {
                    pos[largest] = i;
                    dist[largest] = sqrt(val);
                }

            }
            else {
                pos[(*ndata)] = i;
                dist[(*ndata)++] = sqrt(val);
            }
        }
    }
}


static void
get_value(GwyCalData *caldata, gdouble x, gdouble y, gdouble z,
          gdouble *xerr, gdouble *yerr, gdouble *zerr,
          gdouble *xunc, gdouble *yunc, gdouble *zunc,
          GwyCCViewInterpolationType snap_type)
{

    gint i;
    gint pos[500];
    gint ndata=9;
    gdouble sumxerr, sumyerr, sumzerr, sumxunc, sumyunc, sumzunc, sumw;
    gdouble *xerrd, *yerrd, *zerrd, *xuncd, *yuncd, *zuncd;
    gdouble w, dist[500];

    if (!caldata) {/*printf("no caldata!\n");*/ return;}


    if (snap_type == GWY_CC_VIEW_INTERPOLATION_NATURAL)
    gwy_caldata_interpolate(caldata, x, y, z, xerr, yerr, zerr, xunc, yunc, zunc);
    else
    {
        xerrd = gwy_caldata_get_xerr(caldata);
        yerrd = gwy_caldata_get_yerr(caldata);
        zerrd = gwy_caldata_get_zerr(caldata);
        xuncd = gwy_caldata_get_xunc(caldata);
        yuncd = gwy_caldata_get_yunc(caldata);
        zuncd = gwy_caldata_get_zunc(caldata);

        brutal_search(caldata, x, y, z, 1e-1, pos, dist, &ndata, snap_type);
        sumxerr = sumyerr = sumzerr = sumxunc = sumyunc = sumzunc = sumw = 0;
        for (i=0; i<ndata; i++)
        {
            if (dist[i]<1e-9) {
                sumw = 1;
                sumxerr = xerrd[pos[i]];
                sumyerr = yerrd[pos[i]];
                sumzerr = zerrd[pos[i]];
                sumxunc = xuncd[pos[i]];
                sumyunc = yuncd[pos[i]];
                sumzunc = zuncd[pos[i]];
                break;
            }
            else {
                w = 1.0/dist[i];
                w = w*w;
                sumw += w;
                sumxerr += w*xerrd[pos[i]];
                sumyerr += w*yerrd[pos[i]];
                sumzerr += w*zerrd[pos[i]];
                sumxunc += w*xuncd[pos[i]];
                sumyunc += w*yuncd[pos[i]];
                sumzunc += w*zuncd[pos[i]];
            }
        }
        *xerr = sumxerr/sumw;
        *yerr = sumyerr/sumw;
        *zerr = sumzerr/sumw;
        *xunc = sumxunc/sumw;
        *yunc = sumyunc/sumw;
        *zunc = sumzunc/sumw;
    }

}



/*display mode menu*/
static GtkWidget*
menu_display(GCallback callback, gpointer cbdata,
             GwyCCViewDisplayType current)
{
    static const GwyEnum entries[] = {
        { N_("X correction"),        GWY_CC_VIEW_DISPLAY_X_CORR,   },
        { N_("Y correction"),        GWY_CC_VIEW_DISPLAY_Y_CORR,   },
        { N_("Z correction"),        GWY_CC_VIEW_DISPLAY_Z_CORR,   },
        { N_("X uncertainty"),  GWY_CC_VIEW_DISPLAY_X_UNC, },
        { N_("Y uncertainty"),  GWY_CC_VIEW_DISPLAY_Y_UNC, },
        { N_("Z uncertainty"),  GWY_CC_VIEW_DISPLAY_Z_UNC, },

    };
    return gwy_enum_combo_box_new(entries, G_N_ELEMENTS(entries),
                                  callback, cbdata, current, TRUE);
}

static GtkWidget*
menu_plane(GCallback callback, gpointer cbdata,
           GwyCCViewPlaneType current)
{
    static const GwyEnum entries[] = {
        { N_("Constant X"),        GWY_CC_VIEW_PLANE_X,   },
        { N_("Constant Y"),        GWY_CC_VIEW_PLANE_Y,   },
        { N_("Constant Z"),        GWY_CC_VIEW_PLANE_Z,   },
    };
    return gwy_enum_combo_box_new(entries, G_N_ELEMENTS(entries),
                                  callback, cbdata, current, TRUE);
}

static GtkWidget*
menu_interpolation(GCallback callback, gpointer cbdata,
             GwyCCViewInterpolationType current)
{
    static const GwyEnum entries[] = {
        { N_("NNA 3D"),         GWY_CC_VIEW_INTERPOLATION_3D,   },
        { N_("Snap to planes"), GWY_CC_VIEW_INTERPOLATION_PLANE,   },
        { N_("Delaunay"), GWY_CC_VIEW_INTERPOLATION_NATURAL,   },
    };
    return gwy_enum_combo_box_new(entries, G_N_ELEMENTS(entries),
                                  callback, cbdata, current, TRUE);
}


static void
display_changed(G_GNUC_UNUSED GtkComboBox *combo, CCViewControls *controls)
{
    controls->args->display_type = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(controls->menu_display));

    if (controls->args->crop) {
        gwy_table_hscale_set_sensitive(controls->xplane, FALSE);
        gwy_table_hscale_set_sensitive(controls->yplane, FALSE);
        gwy_table_hscale_set_sensitive(controls->zplane, FALSE);
    }
    else {
       if (controls->args->plane_type == GWY_CC_VIEW_PLANE_X) {
          gwy_table_hscale_set_sensitive(controls->xplane, TRUE);
          gwy_table_hscale_set_sensitive(controls->yplane, FALSE);
          gwy_table_hscale_set_sensitive(controls->zplane, FALSE);
       }
       else if (controls->args->plane_type == GWY_CC_VIEW_PLANE_Y) {
          gwy_table_hscale_set_sensitive(controls->xplane, FALSE);
          gwy_table_hscale_set_sensitive(controls->yplane, TRUE);
          gwy_table_hscale_set_sensitive(controls->zplane, FALSE);
       }
       else if (controls->args->plane_type == GWY_CC_VIEW_PLANE_Z) {
          gwy_table_hscale_set_sensitive(controls->xplane, FALSE);
          gwy_table_hscale_set_sensitive(controls->yplane, FALSE);
          gwy_table_hscale_set_sensitive(controls->zplane, TRUE);
       }
    }
    if (controls->args->update)
                          update_view(controls, controls->args);
}


static void
calculation_changed(G_GNUC_UNUSED GtkComboBox *combo, CCViewControls *controls)
{
    controls->args->display_type = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(controls->menu_display));
    controls->args->plane_type = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(controls->menu_plane));
    controls->args->interpolation_type = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(controls->menu_interpolation));

    if (controls->args->crop) {
        gwy_table_hscale_set_sensitive(controls->xplane, FALSE);
        gwy_table_hscale_set_sensitive(controls->yplane, FALSE);
        gwy_table_hscale_set_sensitive(controls->zplane, FALSE);
    }
    else {
       if (controls->args->plane_type == GWY_CC_VIEW_PLANE_X) {
          gwy_table_hscale_set_sensitive(controls->xplane, TRUE);
          gwy_table_hscale_set_sensitive(controls->yplane, FALSE);
          gwy_table_hscale_set_sensitive(controls->zplane, FALSE);
       }
       else if (controls->args->plane_type == GWY_CC_VIEW_PLANE_Y) {
          gwy_table_hscale_set_sensitive(controls->xplane, FALSE);
          gwy_table_hscale_set_sensitive(controls->yplane, TRUE);
          gwy_table_hscale_set_sensitive(controls->zplane, FALSE);
       }
       else if (controls->args->plane_type == GWY_CC_VIEW_PLANE_Z) {
          gwy_table_hscale_set_sensitive(controls->xplane, FALSE);
          gwy_table_hscale_set_sensitive(controls->yplane, FALSE);
          gwy_table_hscale_set_sensitive(controls->zplane, TRUE);
       }
    }
    controls->args->computed = FALSE;
    if (controls->args->update)
                          update_view(controls, controls->args);
}


static void
crop_change_cb(CCViewControls *controls)
{
    controls->args->crop
              = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->crop));

    controls->args->computed = FALSE;
    display_changed(NULL, controls);
}

static void
update_change_cb(CCViewControls *controls)
{
    controls->args->update
              = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->update));

   gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                            RESPONSE_PREVIEW,
                                           !controls->args->update);
   controls->args->computed = FALSE;
   if (controls->args->update)
               update_view(controls, controls->args);
}

static void
settings_changed(CCViewControls *controls)
{
   controls->args->xplane = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->xplane));
   controls->args->yplane = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->yplane));
   controls->args->zplane = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->zplane));
   controls->args->computed = FALSE;
   if (controls->args->update)
       update_view(controls, controls->args);
}

static void
calibration_changed_cb(CCViewControls *controls)
{

    controls->args->computed = FALSE;

    if (controls->args->update)
        update_view(controls, controls->args);
}

static void
xoffset_changed_cb(GtkAdjustment *adj,
                 CCViewControls *controls)
{
    CCViewArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->xoffset = gtk_adjustment_get_value(adj) * pow10(args->xyexponent);
    controls->in_update = FALSE;
    if (controls->args->update)
    {
        controls->args->computed = FALSE;
        update_view(controls, controls->args);
    }
}

static void
yoffset_changed_cb(GtkAdjustment *adj,
                 CCViewControls *controls)
{
    CCViewArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->yoffset = gtk_adjustment_get_value(adj) * pow10(args->xyexponent);
    controls->in_update = FALSE;
    if (controls->args->update)
    {
        controls->args->computed = FALSE;
        update_view(controls, controls->args);
    }

}
static void
zoffset_changed_cb(GtkAdjustment *adj,
                 CCViewControls *controls)
{
    CCViewArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->zoffset = gtk_adjustment_get_value(adj) * pow10(args->xyexponent);
    controls->in_update = FALSE;
    if (controls->args->update)
    {
        controls->args->computed = FALSE;
        update_view(controls, controls->args);
    }

}
static void
xyexponent_changed_cb(GtkWidget *combo,
                      CCViewControls *controls)
{
    CCViewArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->xyexponent = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    args->xoffset = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->xoffset))
                  * pow10(args->xyexponent);
    args->yoffset = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->yoffset))
                  * pow10(args->xyexponent);

    //simple_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
zexponent_changed_cb(GtkWidget *combo,
                      CCViewControls *controls)
{
    CCViewArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->zexponent = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    args->zoffset = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->zoffset))
                  * pow10(args->zexponent);


    //simple_dialog_update(controls, args);
    controls->in_update = FALSE;
}
static void
units_change_cb(GtkWidget *button,
                CCViewControls *controls)
{
    GtkWidget *dialog, *hbox, *label, *entry;
    const gchar *id, *unit;
    gint response;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;

    id = g_object_get_data(G_OBJECT(button), "id");
    dialog = gtk_dialog_new_with_buttons(_("Change Units"),
                                         NULL,
                                         GTK_DIALOG_MODAL
                                         | GTK_DIALOG_NO_SEPARATOR,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    hbox = gtk_hbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic(_("New _units:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

    entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response != GTK_RESPONSE_OK) {
        gtk_widget_destroy(dialog);
        controls->in_update = FALSE;
        return;
    }

    unit = gtk_entry_get_text(GTK_ENTRY(entry));

    if (gwy_strequal(id, "xy"))
        set_combo_from_unit(controls->xyexponent, unit, 0);
    else if (gwy_strequal(id, "z"))
        set_combo_from_unit(controls->zexponent, unit, 0);

    gtk_widget_destroy(dialog);

    //simple_dialog_update(controls, args);
    controls->in_update = FALSE;
}
static void
set_combo_from_unit(GtkWidget *combo,
                    const gchar *str,
                    gint basepower)
{
    GwySIUnit *unit;
    gint power10;

    unit = gwy_si_unit_new_parse(str, &power10);
    power10 += basepower;
    gwy_combo_box_metric_unit_set_unit(GTK_COMBO_BOX(combo),
                                       power10 - 6, power10 + 6, unit);
    g_object_unref(unit);
}




static const gchar display_key[]  = "/module/cc_view/display";
static const gchar plane_key[]  = "/module/cc_view/plane";
static const gchar interpolation_key[]  = "/module/cc_view/interpolation";
static const gchar crop_key[]  = "/module/cc_view/crop";
static const gchar update_key[]  = "/module/cc_view/update";

static void
cc_view_sanitize_args(CCViewArgs *args)
{
    args->display_type = MIN(args->display_type, GWY_CC_VIEW_DISPLAY_Z_UNC);
    args->plane_type = MIN(args->plane_type, GWY_CC_VIEW_PLANE_Z);
    args->interpolation_type = MIN(args->interpolation_type, GWY_CC_VIEW_INTERPOLATION_PLANE);
    args->crop = !!args->crop;
    args->update = !!args->update;
}

static void
cc_view_load_args(GwyContainer *container,
                    CCViewArgs *args)
{
    *args = ccview_defaults;

    gwy_container_gis_enum_by_name(container, display_key, &args->display_type);
    gwy_container_gis_enum_by_name(container, plane_key, &args->plane_type);
    gwy_container_gis_enum_by_name(container, interpolation_key, &args->interpolation_type);
    gwy_container_gis_boolean_by_name(container, crop_key, &args->crop);
    gwy_container_gis_boolean_by_name(container, update_key, &args->update);

    cc_view_sanitize_args(args);
}

static void
cc_view_save_args(GwyContainer *container,
                    CCViewArgs *args)
{
    gwy_container_set_enum_by_name(container, display_key, args->display_type);
    gwy_container_set_enum_by_name(container, plane_key, args->plane_type);
    gwy_container_set_enum_by_name(container, interpolation_key, args->interpolation_type);
    gwy_container_set_boolean_by_name(container, crop_key, args->crop);
    gwy_container_set_boolean_by_name(container, update_key, args->update);
}


/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
