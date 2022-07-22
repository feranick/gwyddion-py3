/*
 *  $Id: xyzops.c 21866 2019-01-29 17:27:51Z yeti-dn $
 *  Copyright (C) 2019 David Necas (Yeti).
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/surface.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwymodule/gwymodule-xyz.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define XYZMERGE_RUN_MODES (GWY_RUN_INTERACTIVE | GWY_RUN_IMMEDIATE)

typedef struct {
    GwyAppDataId op1;
    GwyAppDataId op2;
    gboolean do_average;
} XYZMergeArgs;

typedef struct {
    XYZMergeArgs *args;
    GtkWidget *dialogue;
    GtkWidget *op2;
    GtkWidget *do_average;
} XYZMergeControls;

static GwyAppDataId op2_id = GWY_APP_DATA_ID_NONE;

static gboolean module_register   (void);
static void     xyzmerge          (GwyContainer *data,
                                   GwyRunType run);
static gboolean xyzmerge_dialogue (XYZMergeArgs *arg);
static void     op2_changed       (GwyDataChooser *chooser,
                                   XYZMergeControls *controls);
static void     do_average_changed(GtkToggleButton *toggle,
                                   XYZMergeControls *controls);
static gboolean merge_data_filter (GwyContainer *data,
                                   gint id,
                                   gpointer user_data);
static void     xyzmerge_do       (GwySurface *surface,
                                   GwyContainer *data,
                                   const XYZMergeArgs *args);
static void     xyzmerge_load_args(GwyContainer *container,
                                   XYZMergeArgs *args);
static void     xyzmerge_save_args(GwyContainer *container,
                                   XYZMergeArgs *args);

static const XYZMergeArgs xyzmerge_defaults = {
    GWY_APP_DATA_ID_NONE, GWY_APP_DATA_ID_NONE,
    TRUE,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Elementary XYZ data operations."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, xyzops)

static gboolean
module_register(void)
{
    gwy_xyz_func_register("xyz_merge",
                          (GwyXYZFunc)&xyzmerge,
                          N_("/_Merge..."),
                          NULL,
                          XYZMERGE_RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Merge two XYZ point sets"));

    return TRUE;
}

static void
xyzmerge(GwyContainer *data, GwyRunType run)
{
    XYZMergeArgs args;
    GwyContainer *settings;
    GwySurface *surface = NULL;
    gboolean ok = TRUE;

    g_return_if_fail(run & XYZMERGE_RUN_MODES);

    settings = gwy_app_settings_get();
    xyzmerge_load_args(settings, &args);

    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &surface,
                                     GWY_APP_SURFACE_ID, &args.op1.id,
                                     GWY_APP_CONTAINER_ID, &args.op1.datano,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(surface));

    if (run == GWY_RUN_INTERACTIVE)
        ok = xyzmerge_dialogue(&args);

    xyzmerge_save_args(settings, &args);

    if (ok && args.op2.datano)
        xyzmerge_do(surface, data, &args);
}

static gboolean
xyzmerge_dialogue(XYZMergeArgs *args)
{
    XYZMergeControls controls;
    GtkWidget *dialogue, *check;
    GwyDataChooser *chooser;
    GtkTable *table;
    gint row, response;

    gwy_clear(&controls, 1);
    controls.args = args;

    dialogue = gtk_dialog_new_with_buttons(_("Merge XYZ Data"), NULL, 0,
                                           GTK_STOCK_CANCEL,
                                           GTK_RESPONSE_CANCEL,
                                           GTK_STOCK_OK,
                                           GTK_RESPONSE_OK,
                                           NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialogue), GTK_RESPONSE_OK);
    gwy_help_add_to_xyz_dialog(GTK_DIALOG(dialogue), GWY_HELP_DEFAULT);
    controls.dialogue = dialogue;

    table = GTK_TABLE(gtk_table_new(2, 3, FALSE));
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialogue)->vbox), GTK_WIDGET(table),
                       FALSE, FALSE, 0);
    row = 0;

    controls.op2 = gwy_data_chooser_new_xyzs();
    chooser = GWY_DATA_CHOOSER(controls.op2);
    gwy_data_chooser_set_active_id(chooser, &args->op2);
    g_signal_connect(chooser, "changed", G_CALLBACK(op2_changed), &controls);
    gwy_data_chooser_set_filter(chooser, merge_data_filter, &args->op1, NULL);
    gwy_table_attach_adjbar(GTK_WIDGET(table),
                            row, _("Second _XYZ data:"), NULL,
                            GTK_OBJECT(chooser), GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    check = gtk_check_button_new_with_mnemonic(_("_Average coincident points"));
    controls.do_average = check;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), args->do_average);
    g_signal_connect(check, "toggled",
                     G_CALLBACK(do_average_changed), &controls);
    gtk_table_attach(table, controls.do_average, 0, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    gtk_widget_show_all(dialogue);

    do {
        response = gtk_dialog_run(GTK_DIALOG(dialogue));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialogue);
            case GTK_RESPONSE_NONE:
            return FALSE;
            break;

            case GTK_RESPONSE_OK:
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialogue);

    return TRUE;
}

static gboolean
merge_data_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyAppDataId *object = (GwyAppDataId*)user_data;
    GwySurface *op1, *op2;
    GwySIUnit *unit1, *unit2;
    GQuark quark;

    quark = gwy_app_get_surface_key_for_id(id);
    op2 = GWY_SURFACE(gwy_container_get_object(data, quark));

    data = gwy_app_data_browser_get(object->datano);
    quark = gwy_app_get_surface_key_for_id(object->id);
    op1 = GWY_SURFACE(gwy_container_get_object(data, quark));

    if (op1 == op2)
        return FALSE;

    unit1 = gwy_surface_get_si_unit_xy(op1);
    unit2 = gwy_surface_get_si_unit_xy(op2);
    if (!gwy_si_unit_equal(unit1, unit2))
        return FALSE;

    unit1 = gwy_surface_get_si_unit_z(op1);
    unit2 = gwy_surface_get_si_unit_z(op2);
    if (!gwy_si_unit_equal(unit1, unit2))
        return FALSE;

    return TRUE;
}

static void
op2_changed(GwyDataChooser *chooser, XYZMergeControls *controls)
{
    gwy_data_chooser_get_active_id(chooser, &controls->args->op2);
}

static void
do_average_changed(GtkToggleButton *toggle, XYZMergeControls *controls)
{
    controls->args->do_average = gtk_toggle_button_get_active(toggle);
}

static int
compare_xy(gconstpointer a, gconstpointer b)
{
    return memcmp(a, b, sizeof(GwyXY));
}

static void
xyzmerge_do(GwySurface *surface, GwyContainer *data,
            const XYZMergeArgs *args)
{
    GwyContainer *data2;
    GwySurface *surface2, *out;
    GQuark quark;
    const GwyXYZ *xyz1, *xyz2;
    GwyXYZ *xyz;
    guint n, n1, n2, i, pos, bstart, len;
    gint newid;

    data2 = gwy_app_data_browser_get(args->op2.datano);
    quark = gwy_app_get_surface_key_for_id(args->op2.id);
    surface2 = gwy_container_get_object(data2, quark);

    out = gwy_surface_new_alike(surface);
    n1 = gwy_surface_get_npoints(surface);
    n2 = gwy_surface_get_npoints(surface2);
    xyz1 = gwy_surface_get_data_const(surface);
    xyz2 = gwy_surface_get_data_const(surface2);
    xyz = g_new(GwyXYZ, n1+n2);

    gwy_assign(xyz, xyz1, n1);
    gwy_assign(xyz + n1, xyz2, n2);
    n = n1 + n2;

    if (args->do_average) {
        /* Merge exact matches.  We do not promise anything cleverer. */
        qsort(xyz, n, sizeof(GwyXYZ), compare_xy);
        bstart = 0;
        pos = 0;
        for (i = 1; i < n; i++) {
            if (xyz[i].x != xyz[bstart].x || xyz[i].y != xyz[bstart].y) {
                len = i-bstart;
                xyz[pos] = xyz[bstart++];
                while (bstart < i)
                    xyz[pos].z += xyz[bstart++].z;
                xyz[pos++].z /= len;
            }
        }
        len = i-bstart;
        xyz[pos] = xyz[bstart++];
        while (bstart < i)
            xyz[pos].z += xyz[bstart++].z;
        xyz[pos++].z /= len;

        gwy_debug("merged %u points", n-pos);
        n = pos;
    }

    gwy_surface_set_data_full(out, xyz, n);
    g_free(xyz);

    newid = gwy_app_data_browser_add_surface(out, data, TRUE);
    gwy_app_set_surface_title(data, newid, _("Merged"));
    g_object_unref(out);
}

static const gchar do_average_key[] = "/module/xyz_merge/do_average";

static void
xyzmerge_sanitize_args(XYZMergeArgs *args)
{
    gwy_app_data_id_verify_xyz(&args->op2);
    args->do_average = !!args->do_average;
}

static void
xyzmerge_load_args(GwyContainer *container, XYZMergeArgs *args)
{
    *args = xyzmerge_defaults;

    gwy_container_gis_boolean_by_name(container, do_average_key,
                                      &args->do_average);
    args->op2 = op2_id;

    xyzmerge_sanitize_args(args);
}

static void
xyzmerge_save_args(GwyContainer *container, XYZMergeArgs *args)
{
    op2_id = args->op2;

    gwy_container_set_boolean_by_name(container, do_average_key,
                                      args->do_average);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
