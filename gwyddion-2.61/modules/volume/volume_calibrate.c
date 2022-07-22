/*
 *  $Id: volume_calibrate.c 22502 2019-09-19 14:54:59Z yeti-dn $
 *  Copyright (C) 2013-2018 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <gtk/gtk.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>

#define VOLCAL_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef struct {
    gdouble xratio;
    gdouble yratio;
    gdouble zratio;
    gdouble wratio;
    gint xexponent;
    gint yexponent;
    gint zexponent;
    gint wexponent;
    gdouble xreal;
    gdouble yreal;
    gdouble zreal;
    gdouble wreal;
    gdouble x0;
    gdouble y0;
    gdouble z0;
    gdouble wshift;
    gdouble xorig;
    gdouble yorig;
    gdouble zorig;
    gdouble worig;
    gdouble x0orig;
    gdouble y0orig;
    gdouble z0orig;
    gint xorigexp;
    gint yorigexp;
    gint zorigexp;
    gint worigexp;
    gint xres;
    gint yres;
    gint zres;
    gchar *xunit;
    gchar *xunitorig;
    gchar *yunit;
    gchar *yunitorig;
    gchar *zunit;
    gchar *zunitorig;
    gchar *wunit;
    gchar *wunitorig;
} VolcalArgs;

typedef struct {
    VolcalArgs *args;
    GtkObject *xratio;
    GtkObject *yratio;
    GtkObject *zratio;
    GtkObject *wratio;
    GtkWidget *xexponent;
    GtkWidget *yexponent;
    GtkWidget *zexponent;
    GtkWidget *wexponent;
    GtkWidget *xpower10;
    GtkWidget *ypower10;
    GtkWidget *zpower10;
    GtkWidget *wpower10;
    GtkWidget *square;
    GtkObject *xreal;
    GtkObject *yreal;
    GtkObject *zreal;
    GtkObject *wreal;
    GtkObject *x0;
    GtkObject *y0;
    GtkObject *z0;
    GtkObject *zshift;
    GtkObject *wshift;
    gboolean in_update;
    GtkWidget *xunits;
    GtkWidget *yunits;
    GtkWidget *zunits;
    GtkWidget *wunits;
    GtkWidget *ok;
} VolcalControls;

static gboolean    module_register           (void);
static void        volcal                 (GwyContainer *data,
                                              GwyRunType run);
static gboolean    volcal_dialog          (VolcalArgs *args,
                                              GwyBrick *brick);
static void        dialog_reset              (VolcalControls *controls,
                                              VolcalArgs *args);
static void        xratio_changed_cb         (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        yratio_changed_cb         (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        zratio_changed_cb         (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        wratio_changed_cb         (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        xreal_changed_cb          (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        yreal_changed_cb          (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        x0_changed_cb             (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        y0_changed_cb             (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        z0_changed_cb            (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        wshift_changed_cb         (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        zreal_changed_cb          (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        wreal_changed_cb          (GtkAdjustment *adj,
                                              VolcalControls *controls);
static void        xexponent_changed_cb     (GtkWidget *combo,
                                              VolcalControls *controls);
static void        yexponent_changed_cb     (GtkWidget *combo,
                                              VolcalControls *controls);
static void        zexponent_changed_cb      (GtkWidget *combo,
                                              VolcalControls *controls);
static void        wexponent_changed_cb      (GtkWidget *combo,
                                              VolcalControls *controls);
static void        volcal_dialog_update   (VolcalControls *controls,
                                              VolcalArgs *args);
static void        volcal_sanitize_args   (VolcalArgs *args);
static void        volcal_load_args       (GwyContainer *container,
                                              VolcalArgs *args);
static void        volcal_save_args       (GwyContainer *container,
                                              VolcalArgs *args);
static void        units_change_cb           (GtkWidget *button,
                                              VolcalControls *controls);
static void        set_combo_from_unit       (GtkWidget *combo,
                                              const gchar *str,
                                              gint basepower);

static const VolcalArgs volcal_defaults = {
    1.0, 1.0, 1.0, 1.0, -6, -6, -6, -6,
    0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0,
    0, 0, 0, 0, 0, 0, 0,
    "m", "m", "m", "m", "m", "m", "m", "m"
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Recalibrate volume data dimensions or value range."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.6",
    "David Nečas (Yeti) & Petr Klapetek",
    "2013",
};

GWY_MODULE_QUERY2(module_info, volume_calibrate)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volcal",
                             (GwyVolumeFunc)&volcal,
                             N_("/_Dimensions and Units..."),
                             GWY_STOCK_VOLUME_DIMENSIONS,
                             VOLCAL_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Change physical dimensions, units "
                                "or value scale"));

    return TRUE;
}

static void
volcal(GwyContainer *data, GwyRunType run)
{
    GwyBrick *brick;
    gint id, newid;
    VolcalArgs args;
    gboolean ok;
    GwySIUnit *siunitx, *siunity, *siunitz, *siunitw;

    g_return_if_fail(run & VOLCAL_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));

    volcal_load_args(gwy_app_settings_get(), &args);

    args.xorig = gwy_brick_get_xreal(brick);
    args.yorig = gwy_brick_get_yreal(brick);
    args.zorig = gwy_brick_get_zreal(brick);
    args.worig = gwy_brick_get_max(brick) - gwy_brick_get_min(brick);

    args.xres = gwy_brick_get_xres(brick);
    args.yres = gwy_brick_get_yres(brick);
    args.zres = gwy_brick_get_zres(brick);

    args.x0orig = gwy_brick_get_xoffset(brick);
    args.y0orig = gwy_brick_get_yoffset(brick);
    args.z0orig = gwy_brick_get_zoffset(brick);

    args.xorigexp = 3*floor(log10(args.xorig)/3);
    args.yorigexp = 3*floor(log10(args.yorig)/3);
    args.zorigexp = 3*floor(log10(args.zorig)/3);
    args.worigexp = (fabs(args.worig) > 0.0
                     ? 3*floor(log10(fabs(args.worig))/3)
                     : 0);

    args.xreal = args.xratio * args.xorig;
    args.yreal = args.yratio * args.yorig;
    args.zreal = args.zratio * args.zorig;
    args.wreal = args.wratio * args.worig;

    args.xexponent = 3*floor(log10(args.xreal)/3);
    args.yexponent = 3*floor(log10(args.yreal)/3);
    args.zexponent = 3*floor(log10(args.zreal)/3);
    args.wexponent = (fabs(args.wreal) > 0.0
                      ? 3*floor(log10(fabs(args.wreal))/3)
                      : 0);

    args.x0 = args.x0orig;
    args.y0 = args.y0orig;
    args.z0 = args.z0orig;

    siunitx = gwy_brick_get_si_unit_x(brick);
    args.xunitorig = gwy_si_unit_get_string(siunitx,
                                            GWY_SI_UNIT_FORMAT_VFMARKUP);

    siunity = gwy_brick_get_si_unit_y(brick);
    args.yunitorig = gwy_si_unit_get_string(siunity,
                                            GWY_SI_UNIT_FORMAT_VFMARKUP);

    siunitz = gwy_brick_get_si_unit_z(brick);
    args.zunitorig = gwy_si_unit_get_string(siunitz,
                                            GWY_SI_UNIT_FORMAT_VFMARKUP);

    siunitw = gwy_brick_get_si_unit_w(brick);
    args.wunitorig = gwy_si_unit_get_string(siunitw,
                                            GWY_SI_UNIT_FORMAT_VFMARKUP);

    if (run == GWY_RUN_INTERACTIVE) {
        ok = volcal_dialog(&args, brick);
        volcal_save_args(gwy_app_settings_get(), &args);
        if (!ok)
            return;
    }

    brick = gwy_brick_duplicate(brick);

    gwy_brick_set_xreal(brick, args.xreal);
    gwy_brick_set_yreal(brick, args.yreal);
    gwy_brick_set_zreal(brick, args.zreal);

    if (args.wratio != 1.0)
        gwy_brick_multiply(brick, args.wratio);
    if (args.wshift != 0.0)
        gwy_brick_add(brick, args.wshift);

    gwy_brick_set_xoffset(brick, args.x0);
    gwy_brick_set_yoffset(brick, args.y0);
    gwy_brick_set_zoffset(brick, args.z0);

    if (!gwy_strequal(args.xunit, args.xunitorig)) {
        siunitx = gwy_brick_get_si_unit_x(brick);
        gwy_si_unit_set_from_string(siunitx, args.xunit);
    }
    if (!gwy_strequal(args.yunit, args.yunitorig)) {
        siunity = gwy_brick_get_si_unit_y(brick);
        gwy_si_unit_set_from_string(siunity, args.yunit);
    }
    if (!gwy_strequal(args.zunit, args.zunitorig)) {
        siunitz = gwy_brick_get_si_unit_z(brick);
        gwy_si_unit_set_from_string(siunitz, args.zunit);
    }
    if (!gwy_strequal(args.wunit, args.wunitorig)) {
        siunitw = gwy_brick_get_si_unit_w(brick);
        gwy_si_unit_set_from_string(siunitw, args.wunit);
    }

    newid = gwy_app_data_browser_add_brick(brick, NULL, data, TRUE);
    g_object_unref(brick);

    gwy_app_set_brick_title(data, newid, _("Recalibrated Data"));
    gwy_app_volume_log_add_volume(data, id, newid);
}

static gboolean
volcal_dialog(VolcalArgs *args,
              GwyBrick *brick)
{
    enum { RESPONSE_RESET = 1 };
    GtkWidget *dialog, *spin, *table, *label;
    GwySIUnit *unit;
    VolcalControls controls;
    gint row, response;
    GwySIValueFormat *vfx, *vfy, *vfz, *vfw;
    gchar *buf;

    dialog = gtk_dialog_new_with_buttons(_("Volume Dimensions and Units"),
                                         NULL, 0,
                                         _("_Reset"), RESPONSE_RESET,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         NULL);

    controls.ok = gtk_dialog_add_button(GTK_DIALOG(dialog),
                                        GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_volume_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);

    controls.args = args;
    controls.in_update = TRUE;

    table = gtk_table_new(17, 4, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);
    row = 0;

    /***** Current Dimensions *****/

    gtk_table_attach(GTK_TABLE(table),
                     gwy_label_new_header(_("Current Dimensions")),
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    vfx = gwy_brick_get_value_format_x(brick, GWY_SI_UNIT_FORMAT_VFMARKUP,
                                       NULL);
    vfy = gwy_brick_get_value_format_y(brick, GWY_SI_UNIT_FORMAT_VFMARKUP,
                                       NULL);
    vfz = gwy_brick_get_value_format_z(brick, GWY_SI_UNIT_FORMAT_VFMARKUP,
                                       NULL);
    vfw = gwy_brick_get_value_format_w(brick, GWY_SI_UNIT_FORMAT_VFMARKUP,
                                       NULL);

    label = gtk_label_new(_("Dimensions:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    buf = g_strdup_printf("%.*f%s%s × %.*f%s%s × %.*f%s%s",
                          vfx->precision, args->xorig/vfx->magnitude,
                          *vfx->units ? " " : "", vfx->units,
                          vfy->precision, args->yorig/vfy->magnitude,
                          *vfy->units ? " " : "", vfy->units,
                          vfz->precision, args->zorig/vfz->magnitude,
                          *vfz->units ? " " : "", vfz->units);
    label = gtk_label_new(buf);
    g_free(buf);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     1, 4, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new(_("Offsets:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    buf = g_strdup_printf("(%.*f%s%s, %.*f%s%s, %.*f%s%s)",
                          vfx->precision, args->x0orig/vfx->magnitude,
                          *vfx->units ? " " : "", vfx->units,
                          vfy->precision, args->y0orig/vfy->magnitude,
                          *vfy->units ? " " : "", vfy->units,
                          vfz->precision, args->z0orig/vfz->magnitude,
                          *vfz->units ? " " : "", vfz->units);
    label = gtk_label_new(buf);
    g_free(buf);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     1, 4, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new(_("Value range:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    buf = g_strdup_printf("%.*f%s%s",
                          vfw->precision, args->worig/vfw->magnitude,
                          *vfw->units ? " " : "", vfw->units);
    label = gtk_label_new(buf);
    g_free(buf);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     1, 4, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    gwy_si_unit_value_format_free(vfw);
    gwy_si_unit_value_format_free(vfz);
    gwy_si_unit_value_format_free(vfy);
    gwy_si_unit_value_format_free(vfx);

    /***** New Real Dimensions *****/

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    gtk_table_attach(GTK_TABLE(table),
                     gwy_label_new_header(_("New Real Dimensions")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.xreal = gtk_adjustment_new(args->xreal/pow10(args->xexponent),
                                        0.01, 10000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_X range:"), NULL,
                                   controls.xreal, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);

    unit = gwy_si_unit_new(args->xunit);
    controls.xexponent
        = gwy_combo_box_metric_unit_new(G_CALLBACK(xexponent_changed_cb),
                                        &controls, -15, 6, unit,
                                        args->xexponent);
    g_object_unref(unit);
    gtk_table_attach(GTK_TABLE(table), controls.xexponent, 2, 3, row, row+1,
                     GTK_FILL | GTK_SHRINK, 0, 0, 0);

    controls.xunits = gtk_button_new_with_label(gwy_sgettext("verb|Change"));
    g_object_set_data(G_OBJECT(controls.xunits), "id", (gpointer)"x");
    gtk_table_attach(GTK_TABLE(table), controls.xunits,
                     3, 4, row, row+1, GTK_FILL | GTK_SHRINK, 0, 0, 0);
    row++;

    controls.yreal = gtk_adjustment_new(args->yreal/pow10(args->yexponent),
                                        0.01, 10000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Y range:"), NULL,
                                   controls.yreal, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);

    unit = gwy_si_unit_new(args->yunit);
    controls.yexponent
        = gwy_combo_box_metric_unit_new(G_CALLBACK(yexponent_changed_cb),
                                        &controls, -15, 6, unit,
                                        args->yexponent);
    g_object_unref(unit);
    gtk_table_attach(GTK_TABLE(table), controls.yexponent,
                     2, 3, row, row+1, GTK_FILL | GTK_SHRINK, 0, 0, 0);

    controls.yunits = gtk_button_new_with_label(gwy_sgettext("verb|Change"));
    g_object_set_data(G_OBJECT(controls.yunits), "id", (gpointer)"y");
    gtk_table_attach(GTK_TABLE(table), controls.yunits,
                     3, 4, row, row+1, GTK_FILL | GTK_SHRINK, 0, 0, 0);
    row++;

    controls.zreal = gtk_adjustment_new(args->zreal/pow10(args->zexponent),
                                        0.01, 10000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Z range:"), NULL,
                                   controls.zreal, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);

    unit = gwy_si_unit_new(args->zunit);
    controls.zexponent
        = gwy_combo_box_metric_unit_new(G_CALLBACK(zexponent_changed_cb),
                                        &controls, -15, 6, unit,
                                        args->zexponent);
    g_object_unref(unit);
    gtk_table_attach(GTK_TABLE(table), controls.zexponent,
                     2, 3, row, row+1, GTK_FILL | GTK_SHRINK, 0, 0, 0);

    controls.zunits = gtk_button_new_with_label(gwy_sgettext("verb|Change"));
    g_object_set_data(G_OBJECT(controls.zunits), "id", (gpointer)"z");
    gtk_table_attach(GTK_TABLE(table), controls.zunits,
                     3, 4, row, row+1, GTK_FILL | GTK_SHRINK, 0, 0, 0);
    row++;


    /***** Lateral Offsets *****/

    controls.x0 = gtk_adjustment_new(args->x0/pow10(args->xexponent),
                                     -10000, 10000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_X offset:"), NULL,
                                   controls.x0, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 3);
    row++;

    controls.y0 = gtk_adjustment_new(args->y0/pow10(args->xexponent),
                                     -10000, 10000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Y offset:"), NULL,
                                   controls.y0, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 3);
    row++;

    controls.z0 = gtk_adjustment_new(args->z0/pow10(args->xexponent),
                                     -10000, 10000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Z offset:"), NULL,
                                   controls.z0, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 3);
    row++;

    /***** New Value Range *****/

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    gtk_table_attach(GTK_TABLE(table),
                     gwy_label_new_header(_("New Value Range")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.wreal = gtk_adjustment_new(args->wreal/pow10(args->wexponent),
                                        -10000, 10000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Value range:"), NULL,
                                   controls.wreal, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 3);

    unit = gwy_si_unit_new(args->wunit);
    controls.wexponent
        = gwy_combo_box_metric_unit_new(G_CALLBACK(wexponent_changed_cb),
                                        &controls, -15, 6, unit,
                                        args->wexponent);
    g_object_unref(unit);
    gtk_table_attach(GTK_TABLE(table), controls.wexponent,
                     2, 3, row, row+1, GTK_FILL | GTK_SHRINK, 0, 0, 0);

    controls.wunits = gtk_button_new_with_label(gwy_sgettext("verb|Change"));
    g_object_set_data(G_OBJECT(controls.wunits), "id", (gpointer)"w");
    gtk_table_attach(GTK_TABLE(table), controls.wunits,
                     3, 4, row, row+1, GTK_FILL | GTK_SHRINK, 0, 0, 0);
    row++;

    /***** Value Shift *****/

    controls.wshift = gtk_adjustment_new(args->wshift/pow10(args->wexponent),
                                         -10000, 10000, 1, 10, 0);
    spin = gwy_table_attach_adjbar(table, row, _("Value shi_ft:"), NULL,
                                   controls.wshift, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 3);
    row++;

    /***** Calibration Coefficients *****/

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    gtk_table_attach(GTK_TABLE(table),
                     gwy_label_new_header(_("Calibration Coefficients")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.xratio = gtk_adjustment_new(args->xratio, 0.001, 1000, 0.1, 1, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_X calibration factor:"), " ",
                                   controls.xratio, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);
    controls.xpower10 = gwy_table_hscale_get_units(controls.xratio);
    row++;

    controls.yratio = gtk_adjustment_new(args->yratio, 0.001, 1000, 0.1, 1, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Y calibration factor:"), " ",
                                   controls.yratio, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);
    controls.ypower10 = gwy_table_hscale_get_units(controls.xratio);
    row++;

    controls.zratio = gtk_adjustment_new(args->zratio, 0.001, 1000, 0.1, 1, 0);
    spin = gwy_table_attach_adjbar(table, row, _("_Z calibration factor:"), " ",
                                   controls.zratio, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);
    controls.zpower10 = gwy_table_hscale_get_units(controls.zratio);
    row++;

    controls.wratio = gtk_adjustment_new(args->wratio, -1000, 1000, 0.1, 1, 0);
    spin = gwy_table_attach_adjbar(table, row,
                                   _("_Value calibration factor:"), " ",
                                   controls.wratio, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);
    controls.wpower10 = gwy_table_hscale_get_units(controls.zratio);
    row++;

    g_signal_connect(controls.xreal, "value-changed",
                     G_CALLBACK(xreal_changed_cb), &controls);
    g_signal_connect(controls.yreal, "value-changed",
                     G_CALLBACK(yreal_changed_cb), &controls);
    g_signal_connect(controls.zreal, "value-changed",
                     G_CALLBACK(zreal_changed_cb), &controls);
    g_signal_connect(controls.xunits, "clicked",
                     G_CALLBACK(units_change_cb), &controls);
    g_signal_connect(controls.yunits, "clicked",
                     G_CALLBACK(units_change_cb), &controls);
    g_signal_connect(controls.zunits, "clicked",
                     G_CALLBACK(units_change_cb), &controls);
    g_signal_connect(controls.wunits, "clicked",
                     G_CALLBACK(units_change_cb), &controls);
    g_signal_connect(controls.x0, "value-changed",
                     G_CALLBACK(x0_changed_cb), &controls);
    g_signal_connect(controls.y0, "value-changed",
                     G_CALLBACK(y0_changed_cb), &controls);
    g_signal_connect(controls.z0, "value-changed",
                     G_CALLBACK(z0_changed_cb), &controls);
    g_signal_connect(controls.wshift, "value-changed",
                     G_CALLBACK(wshift_changed_cb), &controls);
    g_signal_connect(controls.wreal, "value-changed",
                     G_CALLBACK(wreal_changed_cb), &controls);
    g_signal_connect(controls.xratio, "value-changed",
                     G_CALLBACK(xratio_changed_cb), &controls);
    g_signal_connect(controls.yratio, "value-changed",
                     G_CALLBACK(yratio_changed_cb), &controls);
    g_signal_connect(controls.zratio, "value-changed",
                     G_CALLBACK(zratio_changed_cb), &controls);
    g_signal_connect(controls.wratio, "value-changed",
                     G_CALLBACK(wratio_changed_cb), &controls);

    controls.in_update = FALSE;
    /* sync all fields */
    volcal_dialog_update(&controls, args);

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
            dialog_reset(&controls, args);
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
dialog_reset(VolcalControls *controls,
             VolcalArgs *args)
{
    set_combo_from_unit(controls->xexponent, args->xunitorig, args->xorigexp);
    set_combo_from_unit(controls->yexponent, args->xunitorig, args->yorigexp);
    set_combo_from_unit(controls->zexponent, args->xunitorig, args->zorigexp);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->xexponent),
                                  args->xorigexp);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->yexponent),
                                  args->yorigexp);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->zexponent),
                                  args->zorigexp);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->xreal),
                             args->xorig/pow10(args->xorigexp));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->yreal),
                             args->yorig/pow10(args->yorigexp));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->zreal),
                             args->zorig/pow10(args->zorigexp));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->xratio),
                             volcal_defaults.xratio);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->yratio),
                             volcal_defaults.yratio);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->zratio),
                             volcal_defaults.zratio);

    set_combo_from_unit(controls->wexponent, args->wunitorig, args->worigexp);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->wexponent),
                                  args->worigexp);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->wreal),
                             args->worig/pow10(args->worigexp));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->wratio),
                             volcal_defaults.wratio);

    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->x0),
                             args->x0orig/pow10(args->xexponent));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->y0),
                             args->y0orig/pow10(args->yexponent));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->z0),
                             args->z0orig/pow10(args->zexponent));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->wshift),
                             volcal_defaults.wshift);

    volcal_dialog_update(controls, args);

    args->xreal = args->xorig;
    args->yreal = args->yorig;
    args->zreal = args->zorig;
    args->wreal = args->worig;
    args->x0 = args->xorig;
    args->y0 = args->yorig;
    args->z0 = args->zorig;

    g_free(args->xunit);
    args->xunit = g_strdup(args->xunitorig);
    g_free(args->yunit);
    args->yunit = g_strdup(args->yunitorig);
    g_free(args->zunit);
    args->zunit = g_strdup(args->zunitorig);
    g_free(args->wunit);
    args->wunit = g_strdup(args->wunitorig);
}

static void
xratio_changed_cb(GtkAdjustment *adj,
                  VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->xratio = gtk_adjustment_get_value(adj)
                   * pow10(args->xexponent - args->xorigexp);
    args->xreal = args->xratio * args->xorig;
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}
static void
yratio_changed_cb(GtkAdjustment *adj,
                  VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->yratio = gtk_adjustment_get_value(adj)
                   * pow10(args->yexponent - args->yorigexp);
    args->yreal = args->yratio * args->yorig;
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}
static void
zratio_changed_cb(GtkAdjustment *adj,
                  VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->zratio = gtk_adjustment_get_value(adj)
                   * pow10(args->zexponent - args->zorigexp);
    args->zreal = args->zratio * args->zorig;
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}


static void
wratio_changed_cb(GtkAdjustment *adj,
                  VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->wratio = gtk_adjustment_get_value(adj)
                   * pow10(args->wexponent - args->worigexp);

    args->wreal = args->wratio * args->worig;

    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
xreal_changed_cb(GtkAdjustment *adj,
                 VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->xreal = gtk_adjustment_get_value(adj) * pow10(args->xexponent);
    args->xratio = args->xreal/args->xorig;
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
yreal_changed_cb(GtkAdjustment *adj,
                 VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->yreal = gtk_adjustment_get_value(adj) * pow10(args->yexponent);
    args->yratio = args->yreal/args->yorig;
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
zreal_changed_cb(GtkAdjustment *adj,
                 VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->zreal = gtk_adjustment_get_value(adj) * pow10(args->zexponent);
    args->zratio = args->zreal/args->zorig;
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}


static void
x0_changed_cb(GtkAdjustment *adj,
              VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->x0 = gtk_adjustment_get_value(adj) * pow10(args->xexponent);
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;

}

static void
y0_changed_cb(GtkAdjustment *adj,
              VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;

    args->y0 = gtk_adjustment_get_value(adj) * pow10(args->yexponent);
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
z0_changed_cb(GtkAdjustment *adj,
              VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;

    args->z0 = gtk_adjustment_get_value(adj) * pow10(args->zexponent);
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
wshift_changed_cb(GtkAdjustment *adj,
                  VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;

    args->wshift = gtk_adjustment_get_value(adj) * pow10(args->wexponent);
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
wreal_changed_cb(GtkAdjustment *adj,
                 VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;

    args->wreal = gtk_adjustment_get_value(adj) * pow10(args->wexponent);
    args->wratio = (args->worig != 0.0 ? args->wreal/args->worig : 1.0);
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
xexponent_changed_cb(GtkWidget *combo,
                      VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->xexponent = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    args->xreal = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->xreal))
                  * pow10(args->xexponent);
    args->x0 = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->x0))
               * pow10(args->xexponent);
    args->xratio = args->xreal/args->xorig;
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
yexponent_changed_cb(GtkWidget *combo,
                      VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->yexponent = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    args->yreal = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->yreal))
                  * pow10(args->yexponent);
    args->y0 = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->y0))
               * pow10(args->yexponent);
    args->yratio = args->yreal/args->yorig;
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
zexponent_changed_cb(GtkWidget *combo,
                      VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    args->zexponent = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    args->zreal = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->zreal))
                  * pow10(args->zexponent);
    args->z0 = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->z0))
               * pow10(args->zexponent);
    args->zratio = args->zreal/args->zorig;
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
wexponent_changed_cb(GtkWidget *combo,
                     VolcalControls *controls)
{
    VolcalArgs *args = controls->args;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;

    args->wexponent = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(combo));
    args->wreal = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->wreal))
                  * pow10(args->wexponent);
    args->wratio = (args->worig != 0.0 ? args->wreal/args->worig : 1.0);
    volcal_dialog_update(controls, args);
    controls->in_update = FALSE;
}

static void
units_change_cb(GtkWidget *button,
                VolcalControls *controls)
{
    GtkWidget *dialog, *hbox, *label, *entry;
    const gchar *id, *unit;
    gint response;
    VolcalArgs *args = controls->args;

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
        return;
    }

    unit = gtk_entry_get_text(GTK_ENTRY(entry));
    if (gwy_strequal(id, "x")) {
        set_combo_from_unit(controls->xexponent, unit, 0);
        controls->args->xunit = g_strdup(unit);
    }
    else if (gwy_strequal(id, "y")) {
        set_combo_from_unit(controls->yexponent, unit, 0);
        controls->args->yunit = g_strdup(unit);
    }
    else if (gwy_strequal(id, "z")) {
        set_combo_from_unit(controls->zexponent, unit, 0);
        controls->args->zunit = g_strdup(unit);
    }
    else if (gwy_strequal(id, "w")) {
        set_combo_from_unit(controls->wexponent, unit, 0);
        controls->args->wunit = g_strdup(unit);
    }


    gtk_widget_destroy(dialog);

    volcal_dialog_update(controls, args);
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

static void
volcal_dialog_update(VolcalControls *controls,
                        VolcalArgs *args)
{
    gchar buffer[32];
    gint e;

    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->xreal),
                             args->xreal/pow10(args->xexponent));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->yreal),
                             args->yreal/pow10(args->yexponent));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->zreal),
                             args->zreal/pow10(args->zexponent));

    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->x0),
                             args->x0/pow10(args->xexponent));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->y0),
                             args->y0/pow10(args->yexponent));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->z0),
                             args->z0/pow10(args->zexponent));

    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->wreal),
                             args->wreal/pow10(args->wexponent));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->wshift),
                             args->wshift/pow10(args->wexponent));

    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->xratio),
                             args->xratio/pow10(args->xexponent
                                                - args->xorigexp));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->yratio),
                             args->yratio/pow10(args->yexponent
                                                - args->yorigexp));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->zratio),
                             args->zratio/pow10(args->zexponent
                                                - args->zorigexp));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->wratio),
                             args->wratio/pow10(args->wexponent
                                                - args->worigexp));

    e = args->xexponent - args->xorigexp;
    if (!e)
        buffer[0] = '\0';
    else
        g_snprintf(buffer, sizeof(buffer), "× 10<sup>%d</sup>", e);
    gtk_label_set_markup(GTK_LABEL(controls->xpower10), buffer);

    e = args->yexponent - args->yorigexp;
    if (!e)
        buffer[0] = '\0';
    else
        g_snprintf(buffer, sizeof(buffer), "× 10<sup>%d</sup>", e);
    gtk_label_set_markup(GTK_LABEL(controls->ypower10), buffer);

    e = args->zexponent - args->zorigexp;
    if (!e)
        buffer[0] = '\0';
    else
        g_snprintf(buffer, sizeof(buffer), "× 10<sup>%d</sup>", e);
    gtk_label_set_markup(GTK_LABEL(controls->zpower10), buffer);

    e = args->wexponent - args->worigexp;
    if (!e)
        buffer[0] = '\0';
    else
        g_snprintf(buffer, sizeof(buffer), "× 10<sup>%d</sup>", e);
    gtk_label_set_markup(GTK_LABEL(controls->wpower10), buffer);
}

static const gchar wratio_key[] = "/module/volcal/wratio";
static const gchar wshift_key[] = "/module/volcal/wshift";
static const gchar wunit_key[]  = "/module/volcal/wunit";
static const gchar xratio_key[] = "/module/volcal/xratio";
static const gchar xunit_key[]  = "/module/volcal/xunit";
static const gchar yratio_key[] = "/module/volcal/yratio";
static const gchar yunit_key[]  = "/module/volcal/yunit";
static const gchar zratio_key[] = "/module/volcal/zratio";
static const gchar zunit_key[]  = "/module/volcal/zunit";

/* NB: We do not free the unit because it's meant to be taken from settings
 * or defaults, i.e. const. */
static void
sanitize_unit(gchar **unitstr)
{
    GwySIUnit *unit;

    unit = gwy_si_unit_new(*unitstr);
    *unitstr = gwy_si_unit_get_string(unit, GWY_SI_UNIT_FORMAT_PLAIN);
    g_object_unref(unit);
}

static void
volcal_sanitize_args(VolcalArgs *args)
{
    args->xratio = CLAMP(args->xratio, 1e-30, 1e15);
    args->yratio = CLAMP(args->yratio, 1e-30, 1e15);
    args->zratio = CLAMP(args->zratio, 1e-30, 1e15);
    args->wratio = CLAMP(args->wratio, -1e30, 1e15);
    args->wshift = CLAMP(args->wshift, -1e-9, 1e9);
    sanitize_unit(&args->xunit);
    sanitize_unit(&args->yunit);
    sanitize_unit(&args->zunit);
    sanitize_unit(&args->wunit);
}

static void
volcal_load_args(GwyContainer *container,
                 VolcalArgs *args)
{
    *args = volcal_defaults;

    gwy_container_gis_double_by_name(container, xratio_key, &args->xratio);
    gwy_container_gis_double_by_name(container, yratio_key, &args->yratio);
    gwy_container_gis_double_by_name(container, zratio_key, &args->zratio);
    gwy_container_gis_double_by_name(container, wratio_key, &args->wratio);
    gwy_container_gis_double_by_name(container, wshift_key, &args->wshift);
    gwy_container_gis_string_by_name(container, xunit_key,
                                     (const guchar**)&args->xunit);
    gwy_container_gis_string_by_name(container, yunit_key,
                                     (const guchar**)&args->yunit);
    gwy_container_gis_string_by_name(container, zunit_key,
                                     (const guchar**)&args->zunit);
    gwy_container_gis_string_by_name(container, wunit_key,
                                     (const guchar**)&args->wunit);
    volcal_sanitize_args(args);
}

static void
volcal_save_args(GwyContainer *container,
                 VolcalArgs *args)
{
    gwy_container_set_double_by_name(container, xratio_key, args->xratio);
    gwy_container_set_double_by_name(container, yratio_key, args->yratio);
    gwy_container_set_double_by_name(container, zratio_key, args->zratio);
    gwy_container_set_double_by_name(container, wratio_key, args->wratio);
    gwy_container_set_double_by_name(container, wshift_key, args->wshift);
    gwy_container_set_const_string_by_name(container, xunit_key, args->xunit);
    gwy_container_set_const_string_by_name(container, yunit_key, args->yunit);
    gwy_container_set_const_string_by_name(container, zunit_key, args->zunit);
    gwy_container_set_const_string_by_name(container, wunit_key, args->zunit);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
