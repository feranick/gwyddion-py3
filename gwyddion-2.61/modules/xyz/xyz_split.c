/*
 *  $Id: xyz_split.c 21865 2019-01-29 16:24:47Z yeti-dn $
 *  Copyright (C) 2016 David Necas (Yeti).
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
#include <libprocess/surface.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwymodule/gwymodule-xyz.h>
#include <app/gwyapp.h>

#define XYZSPLIT_RUN_MODES (GWY_RUN_INTERACTIVE | GWY_RUN_IMMEDIATE)

typedef enum {
    XYZ_SPLIT_XDIR = 0,
    XYZ_SPLIT_YDIR = 1,
} XYZSplitType;

typedef struct {
    XYZSplitType method;
} XYZSplitArgs;

typedef struct {
    XYZSplitArgs *args;
    GtkWidget *dialogue;
    GSList *method;
} XYZSplitControls;

static gboolean module_register   (void);
static void     xyzsplit          (GwyContainer *data,
                                   GwyRunType run);
static gboolean xyzsplit_dialogue (XYZSplitArgs *arg,
                                   GwySurface *surface);
static void     method_changed    (GtkToggleButton *toggle,
                                   XYZSplitControls *controls);
static void     xyzsplit_do       (GwySurface *surface,
                                   GwyContainer *data,
                                   gint id,
                                   const XYZSplitArgs *args);
static void     xyzsplit_load_args(GwyContainer *container,
                                   XYZSplitArgs *args);
static void     xyzsplit_save_args(GwyContainer *container,
                                   XYZSplitArgs *args);

static const XYZSplitArgs xyzsplit_defaults = {
    XYZ_SPLIT_XDIR,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("XYZ data split based on direction."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.1",
    "Petr Klapetek",
    "2019",
};

GWY_MODULE_QUERY2(module_info, xyz_split)

static gboolean
module_register(void)
{
    gwy_xyz_func_register("xyz_split",
                          (GwyXYZFunc)&xyzsplit,
                          N_("/Split..."),
                          NULL,
                          XYZSPLIT_RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Split XYZ data based on direction"));

    return TRUE;
}


static void
xyzsplit(GwyContainer *data, GwyRunType run)
{
    XYZSplitArgs args;

    GwyContainer *settings;
    GwySurface *surface = NULL;
    gboolean ok = TRUE;
    gint id;

    g_return_if_fail(run & XYZSPLIT_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &surface,
                                     GWY_APP_SURFACE_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(surface));

    settings = gwy_app_settings_get();
    xyzsplit_load_args(settings, &args);

    if (run == GWY_RUN_INTERACTIVE)
        ok = xyzsplit_dialogue(&args, surface);

    xyzsplit_save_args(settings, &args);

    if (ok)
        xyzsplit_do(surface, data, id, &args);
}

static gboolean
xyzsplit_dialogue(XYZSplitArgs *args, GwySurface *surface)
{
    static const GwyEnum methods[] = {
        { N_("X direction"), XYZ_SPLIT_XDIR, },
        { N_("Y direction"), XYZ_SPLIT_YDIR, },
    };

    GtkWidget *dialogue, *label;
    GtkTable *table;
    XYZSplitControls controls;
    gint row, response;

    gwy_clear(&controls, 1);
    controls.args = args;

    dialogue = gtk_dialog_new_with_buttons(_("Split XYZ Data"), NULL, 0,
                                           GTK_STOCK_CANCEL,
                                           GTK_RESPONSE_CANCEL,
                                           GTK_STOCK_OK,
                                           GTK_RESPONSE_OK,
                                           NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialogue), GTK_RESPONSE_OK);
    gwy_help_add_to_xyz_dialog(GTK_DIALOG(dialogue), GWY_HELP_DEFAULT);
    controls.dialogue = dialogue;

    table = GTK_TABLE(gtk_table_new(4, 4, FALSE));
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialogue)->vbox), GTK_WIDGET(table),
                       FALSE, FALSE, 0);
    row = 0;

    label = gtk_label_new_with_mnemonic(_("Fast axis to split:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    controls.method = gwy_radio_buttons_create(methods, G_N_ELEMENTS(methods),
                                               G_CALLBACK(method_changed),
                                               &controls,
                                               args->method);
    row = gwy_radio_buttons_attach_to_table(controls.method, table, 3, row);
    if (!gwy_si_unit_equal(gwy_surface_get_si_unit_xy(surface),
                           gwy_surface_get_si_unit_z(surface))) {
        gtk_widget_set_sensitive(gwy_radio_buttons_find(controls.method,
                                                        XYZ_SPLIT_YDIR),
                                 FALSE);
    }


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

static void
method_changed(GtkToggleButton *toggle,
               XYZSplitControls *controls)
{
    XYZSplitArgs *args = controls->args;

    if (toggle && !gtk_toggle_button_get_active(toggle))
        return;

    args->method = gwy_radio_buttons_get_current(controls->method);
}


static void
xyzsplit_do(GwySurface *surface,
            GwyContainer *data,
            G_GNUC_UNUSED gint id,
            const XYZSplitArgs *args)
{
    GwySurface *surf1, *surf2;
    GwyXYZ *xyz;
    GwyXYZ *xyz1, *xyz2;
    guint k, n, newid;
    guint nfw, nrev;


    xyz = gwy_surface_get_data(surface);

    n = gwy_surface_get_npoints(surface);

    nfw = nrev = 0;
    if (args->method == XYZ_SPLIT_XDIR) {
       for (k = 0; k < (n-1); k++) {
          if (xyz[k+1].x >= xyz[k].x) nfw++;
          if (xyz[k+1].x <= xyz[k].x) nrev++;
       }
    }
    else {
       for (k = 0; k < (n-1); k++) {
          if (xyz[k+1].y >= xyz[k].y) nfw++;
          if (xyz[k+1].y <= xyz[k].y) nrev++;
       }
    }

    //printf("forward %d reverse %d\n", nfw, nrev);

    xyz1 = g_new(GwyXYZ, nfw);
    xyz2 = g_new(GwyXYZ, nrev);

    nfw = nrev = 0;
    if (args->method == XYZ_SPLIT_XDIR) {
       for (k = 0; k < (n-1); k++) {
          if (xyz[k+1].x >= xyz[k].x) {
              xyz1[nfw] = xyz[k];
              nfw++;
          }
          if (xyz[k+1].x <= xyz[k].x) {
              xyz2[nrev] = xyz[k];
              nrev++;
          }
       }
    }
    else {
       for (k = 0; k < (n-1); k++) {
          if (xyz[k+1].y >= xyz[k].y) {
              xyz1[nfw] = xyz[k];
              nfw++;
          }
          if (xyz[k+1].y <= xyz[k].y) {
              xyz2[nrev] = xyz[k];
              nrev++;
          }
       }
    }

    surf1 = gwy_surface_new_from_data(xyz1, nfw);
    surf2 = gwy_surface_new_from_data(xyz2, nrev);

    gwy_surface_set_si_unit_xy(surf1, gwy_surface_get_si_unit_xy(surface));
    gwy_surface_set_si_unit_z(surf1, gwy_surface_get_si_unit_z(surface));

    gwy_surface_set_si_unit_xy(surf2, gwy_surface_get_si_unit_xy(surface));
    gwy_surface_set_si_unit_z(surf2, gwy_surface_get_si_unit_z(surface));

    newid = gwy_app_data_browser_add_surface(surf1, data, TRUE);
    gwy_app_set_surface_title(data, newid, _("Split forward"));
    g_object_unref(surf1);

    newid = gwy_app_data_browser_add_surface(surf2, data, TRUE);
    gwy_app_set_surface_title(data, newid, _("Split reverse"));
    g_object_unref(surf2);
}

static const gchar method_key[]     = "/module/xyz_split/method";

static void
xyzsplit_sanitize_args(XYZSplitArgs *args)
{
    args->method = CLAMP(args->method, XYZ_SPLIT_XDIR, XYZ_SPLIT_YDIR);
}

static void
xyzsplit_load_args(GwyContainer *container,
                   XYZSplitArgs *args)
{
    *args = xyzsplit_defaults;

    gwy_container_gis_enum_by_name(container, method_key, &args->method);
    xyzsplit_sanitize_args(args);
}

static void
xyzsplit_save_args(GwyContainer *container,
                   XYZSplitArgs *args)
{
    gwy_container_set_enum_by_name(container, method_key, args->method);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
