/*
 *  $Id: volume_swaxes.c 22242 2019-07-14 12:17:43Z yeti-dn $
 *  Copyright (C) 2017-2018 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/brick.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define SWAXES_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    AXIS_XPOS = 0,
    AXIS_XNEG = 1,
    AXIS_YPOS = 2,
    AXIS_YNEG = 3,
    AXIS_ZPOS = 4,
    AXIS_ZNEG = 5,
    NAXES
} AxisType;

typedef struct {
    AxisType x;
    AxisType y;
    AxisType z;
    gboolean new_channel;
} SwapAxesArgs;

typedef struct {
    SwapAxesArgs *args;
    GwyBrick *brick;
    gboolean has_zcal;
    gint last_changed;
    gint second_last_changed;
    GtkWidget *dialog;
    GtkWidget *x;
    GtkWidget *y;
    GtkWidget *z;
    GtkWidget *new_channel;
    GtkWidget *message;
} SwapAxesControls;

static gboolean module_register     (void);
static void     volume_swaxes       (GwyContainer *data,
                                     GwyRunType run);
static gboolean swaxes_dialog       (SwapAxesArgs *args,
                                     GwyBrick *brick);
static void     new_channel_changed (GtkToggleButton *toggle,
                                     SwapAxesControls *controls);
static void     xaxis_changed       (GtkComboBox *combo,
                                     SwapAxesControls *controls);
static void     yaxis_changed       (GtkComboBox *combo,
                                     SwapAxesControls *controls);
static void     zaxis_changed       (GtkComboBox *combo,
                                     SwapAxesControls *controls);
static void     update_sensitivity  (SwapAxesControls *controls);
static void     update_third_axis   (SwapAxesControls *controls,
                                     gint changed_axis);
static void     update_message      (SwapAxesControls *controls);
static gboolean axes_are_consistent (const SwapAxesArgs *args);
static void     swaxes_do           (GwyContainer *data,
                                     gint id,
                                     GwyBrick *brick,
                                     SwapAxesArgs *args);
static void     swaxes_sanitize_args(SwapAxesArgs *args);
static void     swaxes_load_args    (GwyContainer *container,
                                     SwapAxesArgs *args);
static void     swaxes_save_args    (GwyContainer *container,
                                     SwapAxesArgs *args);

static const SwapAxesArgs swaxes_defaults = {
    AXIS_XPOS, AXIS_YPOS, AXIS_ZPOS,
    FALSE,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Swaps axes of volume data."),
    "Yeti <yeti@gwyddion.net>",
    "1.1",
    "David Nečas (Yeti)",
    "2017",
};

GWY_MODULE_QUERY2(module_info, volume_swaxes)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_swaxes",
                             (GwyVolumeFunc)&volume_swaxes,
                             N_("/S_wap Axes..."),
                             GWY_STOCK_VOLUME_SWAP_AXES,
                             SWAXES_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Swap axes"));

    return TRUE;
}

static void
volume_swaxes(GwyContainer *data, GwyRunType run)
{
    SwapAxesArgs args;
    GwyBrick *brick = NULL;
    gboolean ok = TRUE;
    gint id;

    g_return_if_fail(run & SWAXES_RUN_MODES);

    swaxes_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));

    if (run == GWY_RUN_INTERACTIVE) {
        ok = swaxes_dialog(&args, brick);
        swaxes_save_args(gwy_app_settings_get(), &args);
    }

    if (ok)
        swaxes_do(data, id, brick, &args);
}

static gboolean
swaxes_dialog(SwapAxesArgs *args, GwyBrick *brick)
{
    static const GwyEnum axes[] = {
        { N_("X"),           AXIS_XPOS,  },
        { N_("X, reversed"), AXIS_XNEG,  },
        { N_("Y"),           AXIS_YPOS,  },
        { N_("Y, reversed"), AXIS_YNEG,  },
        { N_("Z"),           AXIS_ZPOS,  },
        { N_("Z, reversed"), AXIS_ZNEG,  },
    };

    GtkWidget *dialog, *table;
    GtkSizeGroup *sizegroup;
    SwapAxesControls controls;
    gint response, row;

    gwy_clear(&controls, 1);
    controls.args = args;
    controls.last_changed = 1;
    controls.second_last_changed = 0;
    controls.has_zcal = !!gwy_brick_get_zcalibration(brick);

    dialog = gtk_dialog_new_with_buttons(_("Swap Volume Axes"),
                                         NULL, 0,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_volume_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    table = gtk_table_new(5, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), table, TRUE, TRUE, 4);
    row = 0;

    sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

    controls.x = gwy_enum_combo_box_new(axes, G_N_ELEMENTS(axes),
                                        G_CALLBACK(xaxis_changed), &controls,
                                        args->x, TRUE);
    gwy_table_attach_adjbar(table, row, _("Current _X axis will become:"), NULL,
                            GTK_OBJECT(controls.x), GWY_HSCALE_WIDGET);
    gtk_size_group_add_widget(sizegroup, controls.x);
    row++;

    controls.y = gwy_enum_combo_box_new(axes, G_N_ELEMENTS(axes),
                                        G_CALLBACK(yaxis_changed), &controls,
                                        args->y, TRUE);
    gwy_table_attach_adjbar(table, row, _("Current _Y axis will become:"), NULL,
                            GTK_OBJECT(controls.y), GWY_HSCALE_WIDGET);
    gtk_size_group_add_widget(sizegroup, controls.y);
    row++;

    controls.z = gwy_enum_combo_box_new(axes, G_N_ELEMENTS(axes),
                                        G_CALLBACK(zaxis_changed), &controls,
                                        args->z, TRUE);
    gwy_table_attach_adjbar(table, row, _("Current _Z axis will become:"), NULL,
                            GTK_OBJECT(controls.z), GWY_HSCALE_WIDGET);
    gtk_size_group_add_widget(sizegroup, controls.z);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.new_channel
        = gtk_check_button_new_with_mnemonic(_("Create new volume data"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.new_channel),
                                 args->new_channel);
    gtk_table_attach(GTK_TABLE(table), controls.new_channel,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls.new_channel, "toggled",
                     G_CALLBACK(new_channel_changed), &controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.message = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.message), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.message,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    g_object_unref(sizegroup);
    update_message(&controls);
    update_sensitivity(&controls);

    gtk_widget_show_all(dialog);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return response == GTK_RESPONSE_OK;
}

static void
new_channel_changed(GtkToggleButton *toggle, SwapAxesControls *controls)
{
    controls->args->new_channel = gtk_toggle_button_get_active(toggle);
    update_sensitivity(controls);
}

static void
xaxis_changed(GtkComboBox *combo, SwapAxesControls *controls)
{
    controls->args->x = gwy_enum_combo_box_get_active(combo);
    update_third_axis(controls, 0);
}

static void
yaxis_changed(GtkComboBox *combo, SwapAxesControls *controls)
{
    controls->args->y = gwy_enum_combo_box_get_active(combo);
    update_third_axis(controls, 1);
}

static void
zaxis_changed(GtkComboBox *combo, SwapAxesControls *controls)
{
    controls->args->z = gwy_enum_combo_box_get_active(combo);
    update_third_axis(controls, 2);
    update_message(controls);
}

static void
update_message(SwapAxesControls *controls)
{
    if (!controls->has_zcal)
        return;

    if (controls->args->z == AXIS_ZPOS || controls->args->z == AXIS_ZNEG)
        gtk_label_set_text(GTK_LABEL(controls->message), NULL);
    else {
        gtk_label_set_text(GTK_LABEL(controls->message),
                           _("Z axis calibration will be lost."));
    }
}

static void
update_sensitivity(SwapAxesControls *controls)
{
    SwapAxesArgs *args = controls->args;
    gboolean is_noop;

    is_noop = (args->x == AXIS_XPOS
               && args->y == AXIS_YPOS
               && args->z == AXIS_ZPOS
               && !args->new_channel);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      GTK_RESPONSE_OK, !is_noop);
}

static void
update_third_axis(SwapAxesControls *controls, gint changed_axis)
{
    SwapAxesArgs *args = controls->args;
    AxisType xyz[3];
    gint third, axis_to_fix;

    if (changed_axis == controls->last_changed) {
        /* Dd nothing. */
    }
    else if (changed_axis == controls->second_last_changed)
        GWY_SWAP(gint, controls->last_changed, controls->second_last_changed);
    else {
        controls->second_last_changed = controls->last_changed;
        controls->last_changed = changed_axis;
    }

    if (axes_are_consistent(args)) {
        update_sensitivity(controls);
        return;
    }

    third = 3 - (controls->last_changed + controls->second_last_changed);
    xyz[0] = args->x;
    xyz[1] = args->y;
    xyz[2] = args->z;

    if (xyz[third]/2 == xyz[controls->last_changed]/2)
        axis_to_fix = third;
    else
        axis_to_fix = controls->second_last_changed;

    if (axis_to_fix == 0) {
        args->x = (2*(3 - args->y/2 - args->z/2)) | (args->x & 1);
        g_assert(axes_are_consistent(args));
        gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->x), args->x);
    }
    else if (axis_to_fix == 1) {
        args->y = (2*(3 - args->z/2 - args->x/2)) | (args->y & 1);
        g_assert(axes_are_consistent(args));
        gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->y), args->y);
    }
    else if (axis_to_fix == 2) {
        args->z = (2*(3 - args->x/2 - args->y/2)) | (args->z & 1);
        g_assert(axes_are_consistent(args));
        gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->z), args->z);
    }
    else {
        g_assert_not_reached();
    }
}

static gboolean
axes_are_consistent(const SwapAxesArgs *args)
{
    if (args->y/2 == args->x/2)
        return FALSE;
    if (args->z/2 == args->y/2)
        return FALSE;
    if (args->x/2 == args->z/2)
        return FALSE;

    return TRUE;
}

static void
swaxes_do(GwyContainer *data, gint id, GwyBrick *brick, SwapAxesArgs *args)
{
    AxisType ax = args->x, ay = args->y, az = args->z;
    AxisType bx = 2*(ax/2), by = 2*(ay/2);
    GwyBrickTransposeType transtype;
    gboolean xinv = ax & 1, yinv = ay & 1, zinv = az & 1;
    GwyBrick *result;
    GwyDataField *preview = NULL;
    gdouble xoff, yoff;
    gint xres, yres, newid;
    GQuark quarks[2];

    if (ax == AXIS_XPOS && ay == AXIS_YPOS)
        transtype = GWY_BRICK_TRANSPOSE_XYZ;
    else if (ax == AXIS_XPOS && ay == AXIS_ZPOS)
        transtype = GWY_BRICK_TRANSPOSE_XZY;
    else if (ax == AXIS_YPOS && ay == AXIS_XPOS)
        transtype = GWY_BRICK_TRANSPOSE_YXZ;
    else if (ax == AXIS_YPOS && ay == AXIS_ZPOS)
        transtype = GWY_BRICK_TRANSPOSE_YZX;
    else if (ax == AXIS_ZPOS && ay == AXIS_XPOS)
        transtype = GWY_BRICK_TRANSPOSE_ZXY;
    else if (ax == AXIS_ZPOS && ay == AXIS_YPOS)
        transtype = GWY_BRICK_TRANSPOSE_ZYX;
    else {
        g_return_if_reached();
    }

    result = gwy_brick_new(1, 1, 1, 1.0, 1.0, 1.0, FALSE);
    gwy_brick_transpose(brick, result, transtype, xinv, yinv, zinv);

    /* Reuse the old preview if the XY plane is preserved. */
    if (gwy_container_gis_object(data, gwy_app_get_brick_preview_key_for_id(id),
                                 &preview)
        && (bx == AXIS_XPOS || bx == AXIS_YPOS)
        && (by == AXIS_XPOS || by == AXIS_YPOS)) {
        GwyDataField *tmp;

        if (ax == AXIS_YPOS && ay == AXIS_XNEG)
            tmp = gwy_data_field_new_rotated_90(preview, TRUE);
        else if (ax == AXIS_YNEG && ay == AXIS_XPOS)
            tmp = gwy_data_field_new_rotated_90(preview, FALSE);
        else {
            tmp = gwy_data_field_duplicate(preview);
            if (ax == AXIS_XPOS && ay == AXIS_YPOS) {
                /* Do nothing. */
            }
            else if (ax == AXIS_XNEG && ay == AXIS_YNEG)
                gwy_data_field_invert(tmp, TRUE, TRUE, FALSE);
            else if (ax == AXIS_XNEG && ay == AXIS_YPOS)
                gwy_data_field_invert(tmp, FALSE, TRUE, FALSE);
            else if (ax == AXIS_XPOS && ay == AXIS_YNEG)
                gwy_data_field_invert(tmp, TRUE, FALSE, FALSE);
            else if (ax == AXIS_YPOS && ay == AXIS_XPOS)
                gwy_data_field_flip_xy(preview, tmp, FALSE);
            else if (ax == AXIS_YNEG && ay == AXIS_XNEG)
                gwy_data_field_flip_xy(preview, tmp, TRUE);
            else {
                g_assert_not_reached();
            }
        }
        GWY_SWAP(GwyDataField*, tmp, preview);

        xoff = gwy_data_field_get_xoffset(preview);
        yoff = gwy_data_field_get_yoffset(preview);
        gwy_data_field_set_xoffset(preview, yoff);
        gwy_data_field_set_yoffset(preview, xoff);
    }
    else {
        xres = gwy_brick_get_xres(result);
        yres = gwy_brick_get_yres(result);
        preview = gwy_data_field_new(xres, yres, xres, yres, FALSE);
        gwy_brick_mean_xy_plane(result, preview);
    }

    /* Create new channel or modify the current one. */
    if (args->new_channel) {
        newid = gwy_app_data_browser_add_brick(result, preview, data, TRUE);
        gwy_app_set_brick_title(data, id, _("Rotated Data"));
        gwy_app_volume_log_add_volume(data, id, newid);
        gwy_app_sync_volume_items(data, data, id, newid,
                                  GWY_DATA_ITEM_GRADIENT,
                                  0);
    }
    else {
        quarks[0] = gwy_app_get_brick_key_for_id(id);
        quarks[1] = gwy_app_get_brick_preview_key_for_id(id);
        gwy_app_undo_qcheckpointv(data, G_N_ELEMENTS(quarks), quarks);
        gwy_container_set_object(data, quarks[0], result);
        gwy_container_set_object(data, quarks[1], preview);
        gwy_app_volume_log_add_volume(data, id, id);
    }

    g_object_unref(result);
    g_object_unref(preview);
}

static const gchar x_key[]           = "/module/volume_swaxes/x";
static const gchar y_key[]           = "/module/volume_swaxes/y";
static const gchar z_key[]           = "/module/volume_swaxes/z";
static const gchar new_channel_key[] = "/module/volume_swaxes/new_channel";

static void
swaxes_sanitize_args(SwapAxesArgs *args)
{
    args->x = MIN(args->x, NAXES-1);
    args->y = MIN(args->y, NAXES-1);
    args->z = MIN(args->z, NAXES-1);
    args->new_channel = !!args->new_channel;

    /* Do not bother fixing invalid configurations, reset to no-op. */
    if (!axes_are_consistent(args)) {
        args->x = swaxes_defaults.x;
        args->y = swaxes_defaults.y;
        args->z = swaxes_defaults.z;
    }
}

static void
swaxes_load_args(GwyContainer *container, SwapAxesArgs *args)
{
    *args = swaxes_defaults;

    gwy_container_gis_enum_by_name(container, x_key, &args->x);
    gwy_container_gis_enum_by_name(container, y_key, &args->y);
    gwy_container_gis_enum_by_name(container, z_key, &args->z);
    gwy_container_gis_boolean_by_name(container, new_channel_key,
                                      &args->new_channel);
    swaxes_sanitize_args(args);
}

static void
swaxes_save_args(GwyContainer *container, SwapAxesArgs *args)
{
    gwy_container_set_enum_by_name(container, x_key, args->x);
    gwy_container_set_enum_by_name(container, y_key, args->y);
    gwy_container_set_enum_by_name(container, z_key, args->z);
    gwy_container_set_boolean_by_name(container, new_channel_key,
                                      args->new_channel);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
