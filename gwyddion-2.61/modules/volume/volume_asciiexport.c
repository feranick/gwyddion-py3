/*
 *  $Id: volume_asciiexport.c 20813 2018-03-08 17:04:35Z yeti-dn $
 *  Copyright (C) 2018 David Necas (Yeti).
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
#include <locale.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <libprocess/brick.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define VOLASCEXP_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    VOLUME_EXPORT_VTK      = 0,
    VOLUME_EXPORT_ZLINES   = 1,
    VOLUME_EXPORT_LAYERS   = 2,
    VOLUME_EXPORT_MATRICES = 3,
    VOLUME_EXPORT_NTYPES,
} VolumeExportStyle;

typedef struct {
    VolumeExportStyle style;
    gboolean decimal_dot;
    guint precision;
} VolumeASCIIExportArgs;

typedef struct {
    gboolean needs_decimal_dot;
    guint decimal_dot_len;
    gchar *decimal_dot;
} DecimalDotInfo;

typedef struct {
    VolumeASCIIExportArgs *args;
    DecimalDotInfo *decinfo;
    GwyBrick *brick;
    gchar *title;
} VolumeASCIIExportData;

static gboolean module_register        (void);
static void     volume_ascii_export    (GwyContainer *data,
                                        GwyRunType run);
static gboolean volascexp_export_dialog(VolumeASCIIExportArgs *args,
                                        const DecimalDotInfo *decinfo);
static void     decimal_dot_changed    (GtkToggleButton *toggle,
                                        VolumeASCIIExportArgs *args);
static void     precision_changed      (GtkAdjustment *adj,
                                        VolumeASCIIExportArgs *args);
static gchar*   export_brick           (gpointer user_data,
                                        gssize *data_len);
static void     destroy_brick          (gchar *data,
                                        gpointer user_data);
static void     fill_decimal_dot_info  (DecimalDotInfo *info);
static void     volascexp_load_args    (GwyContainer *settings,
                                        VolumeASCIIExportArgs *args);
static void     volascexp_save_args    (GwyContainer *settings,
                                        VolumeASCIIExportArgs *args);

static const VolumeASCIIExportArgs volascexp_defaults = {
    VOLUME_EXPORT_MATRICES, TRUE, 5,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Exports volume data in simple ASCII formats."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, volume_asciiexport)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_asciiexport",
                             (GwyVolumeFunc)&volume_ascii_export,
                             N_("/Export _Text..."),
                             NULL,
                             VOLASCEXP_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Export volume data to a text file"));

    return TRUE;
}

static void
volume_ascii_export(GwyContainer *data, GwyRunType run)
{
    VolumeASCIIExportData expdata;
    VolumeASCIIExportArgs args;
    DecimalDotInfo decinfo;
    GwyBrick *brick;
    GQuark quark;
    const guchar *title;
    gint id;

    g_return_if_fail(run & VOLASCEXP_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));

    volascexp_load_args(gwy_app_settings_get(), &args);
    fill_decimal_dot_info(&decinfo);

    if (run == GWY_RUN_INTERACTIVE) {
        if (!volascexp_export_dialog(&args, &decinfo))
            goto finish;
    }

    expdata.args = &args;
    expdata.decinfo = &decinfo;
    expdata.brick = brick;

    quark = gwy_app_get_brick_title_key_for_id(id);
    if (gwy_container_gis_string(data, quark, &title))
        expdata.title = g_strdup(title);
    else
        expdata.title = g_strdup("Volume data");

    gwy_save_auxiliary_with_callback(_("Export to Text File"), NULL,
                                     export_brick, destroy_brick, &expdata);
    g_free(expdata.title);

finish:
    g_free(decinfo.decimal_dot);
}

static gboolean
volascexp_export_dialog(VolumeASCIIExportArgs *args,
                        const DecimalDotInfo *decinfo)
{
    static const GwyEnum style_types[] = {
        { N_("VTK structured grid"),           VOLUME_EXPORT_VTK,      },
        { N_("One Z-profile per line"),        VOLUME_EXPORT_ZLINES,   },
        { N_("One XY-layer per line"),         VOLUME_EXPORT_LAYERS,   },
        { N_("Blank-line separated matrices"), VOLUME_EXPORT_MATRICES, },
    };

    GtkWidget *dialog, *vbox, *hbox, *label, *decimal_dot, *combo,
              *precision;
    gint response;

    dialog = gtk_dialog_new_with_buttons(_("Export Text"), NULL, 0,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_volume_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);

    vbox = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), vbox, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

    gtk_box_pack_start(GTK_BOX(vbox), gwy_label_new_header(_("Options")),
                       FALSE, FALSE, 0);

    combo = gwy_enum_combo_box_new(style_types, G_N_ELEMENTS(style_types),
                                   G_CALLBACK(gwy_enum_combo_box_update_int),
                                   &args->style, args->style, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), combo, FALSE, FALSE, 0);

    decimal_dot = gtk_check_button_new_with_mnemonic(_("Use _dot "
                                                       "as decimal separator"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(decimal_dot),
                                 args->decimal_dot
                                 || !decinfo->needs_decimal_dot);
    gtk_widget_set_sensitive(decimal_dot, decinfo->needs_decimal_dot);
    gtk_box_pack_start(GTK_BOX(vbox), decimal_dot, FALSE, FALSE, 0);
    g_signal_connect(decimal_dot, "toggled",
                     G_CALLBACK(decimal_dot_changed), args);

    hbox = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic(_("_Precision:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    precision = gtk_spin_button_new_with_range(0, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(precision), args->precision);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), precision);
    gtk_box_pack_start(GTK_BOX(hbox), precision, FALSE, FALSE, 0);
    g_signal_connect(gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(precision)),
                     "value-changed", G_CALLBACK(precision_changed), args);

    gtk_widget_show_all(dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response != GTK_RESPONSE_NONE) {
        if (decinfo->needs_decimal_dot)
            args->decimal_dot
                = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(decimal_dot));
        args->precision
            = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(precision));
        volascexp_save_args(gwy_app_settings_get(), args);
        gtk_widget_destroy(dialog);
    }

    return response == GTK_RESPONSE_OK;
}

static void
decimal_dot_changed(GtkToggleButton *toggle, VolumeASCIIExportArgs *args)
{
    args->decimal_dot = gtk_toggle_button_get_active(toggle);
}

static void
precision_changed(GtkAdjustment *adj, VolumeASCIIExportArgs *args)
{
    args->precision = gwy_adjustment_get_int(adj);
}

static inline void
append_formatted_number(GString *str,
                        gdouble v,
                        gint precision,
                        gboolean must_fix_decimal_dot,
                        const gchar *decimal_dot,
                        guint decimal_dot_len)
{
    gchar *pos;
    gchar buf[40];

    g_snprintf(buf, sizeof(buf), "%.*g", precision, v);
    if (!must_fix_decimal_dot) {
        g_string_append(str, buf);
        return;
    }

    pos = strstr(buf, decimal_dot);
    if (!pos) {
        g_string_append(str, buf);
        return;
    }

    pos[0] = '.';
    if (decimal_dot_len == 1) {
        g_string_append(str, buf);
        return;
    }

    pos[1] = '\0';
    g_string_append(str, buf);
    g_string_append(str, pos + decimal_dot_len);
}

static gchar*
export_brick(gpointer user_data, gssize *data_len)
{
    static const gchar vtk_header[] =
        "# vtk DataFile Version 2.0\n"
        "%s\n"
        "ASCII\n"
        "DATASET STRUCTURED_POINTS\n"
        "DIMENSIONS %u %u %u\n"
        "ASPECT_RATIO 1 1 1\n"
        "ORIGIN 0 0 0\n"
        "POINT_DATA %u\n"
        "SCALARS volume_scalars double 1\n"
        "LOOKUP_TABLE default\n"
        ;

    VolumeASCIIExportData *expdata = (VolumeASCIIExportData*)user_data;
    GwyBrick *brick = expdata->brick;
    guint xres, yres, zres, i, j, k, n;
    const gchar *decimal_dot;
    guint decimal_dot_len;
    GString *str;
    const gdouble *d, *dd;
    gboolean must_fix_decimal_dot;
    gint precision;

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);
    d = gwy_brick_get_data_const(brick);
    must_fix_decimal_dot = (expdata->decinfo->needs_decimal_dot
                            && expdata->args->decimal_dot);
    decimal_dot = expdata->decinfo->decimal_dot;
    decimal_dot_len = expdata->decinfo->decimal_dot_len;
    precision = expdata->args->precision;

    str = g_string_new(NULL);
    if (expdata->args->style == VOLUME_EXPORT_VTK) {
        n = xres*yres*zres;
        g_string_append_printf(str, vtk_header,
                               expdata->title, xres, yres, zres, n);
        for (i = 0; i < n; i++) {
            append_formatted_number(str, d[i], precision, must_fix_decimal_dot,
                                    decimal_dot, decimal_dot_len);
            g_string_append_c(str, '\n');
        }
    }
    else if (expdata->args->style == VOLUME_EXPORT_ZLINES) {
        n = xres*yres;
        for (i = 0; i < n; i++) {
            dd = d + i;
            for (j = 0; j < zres; j++) {
                append_formatted_number(str, *dd, precision,
                                        must_fix_decimal_dot,
                                        decimal_dot, decimal_dot_len);
                g_string_append_c(str, j == zres-1 ? '\n' : '\t');
                dd += n;
            }
        }
    }
    else if (expdata->args->style == VOLUME_EXPORT_LAYERS) {
        n = xres*yres;
        for (i = 0; i < zres; i++) {
            dd = d + i*n;
            for (j = 0; j < n; j++) {
                append_formatted_number(str, dd[j], precision,
                                        must_fix_decimal_dot,
                                        decimal_dot, decimal_dot_len);
                g_string_append_c(str, j == n-1 ? '\n' : '\t');
            }
        }
    }
    else if (expdata->args->style == VOLUME_EXPORT_MATRICES) {
        n = xres*yres;
        for (i = 0; i < zres; i++) {
            for (j = 0; j < yres; j++) {
                dd = d + i*n + j*xres;
                for (k = 0; k < xres; k++) {
                    append_formatted_number(str, dd[k], precision,
                                            must_fix_decimal_dot,
                                            decimal_dot, decimal_dot_len);
                    g_string_append_c(str, k == xres-1 ? '\n' : '\t');
                }
            }
            g_string_append_c(str, '\n');
        }
    }
    else {
        g_assert_not_reached();
    }

    *data_len = str->len;
    return g_string_free(str, FALSE);
}

static void
destroy_brick(gchar *data, G_GNUC_UNUSED gpointer user_data)
{
    g_free(data);
}

static void
fill_decimal_dot_info(DecimalDotInfo *info)
{
    struct lconv *locale_data;
    guint len;

    locale_data = localeconv();
    info->decimal_dot = g_strdup(locale_data->decimal_point);
    if (!info->decimal_dot || !(len = strlen(info->decimal_dot))) {
        g_warning("Cannot get decimal dot information from localeconv().");
        g_free(info->decimal_dot);
        info->decimal_dot = g_strdup(".");
        len = 1;
    }
    info->decimal_dot_len = len;
    info->needs_decimal_dot = !gwy_strequal(info->decimal_dot, ".");
}

static const gchar decimal_dot_key[] = "/module/volume_asciiexport/decimal-dot";
static const gchar precision_key[]   = "/module/volume_asciiexport/precision";
static const gchar style_key[]       = "/module/volume_asciiexport/style";

static void
volascexp_load_args(GwyContainer *settings,
                    VolumeASCIIExportArgs *args)
{
    *args = volascexp_defaults;

    gwy_container_gis_boolean_by_name(settings, decimal_dot_key,
                                      &args->decimal_dot);
    gwy_container_gis_int32_by_name(settings, precision_key, &args->precision);
    gwy_container_gis_enum_by_name(settings, style_key, &args->style);

    args->precision = MIN(args->precision, 16);
}

static void
volascexp_save_args(GwyContainer *settings,
                    VolumeASCIIExportArgs *args)
{
    gwy_container_set_boolean_by_name(settings, decimal_dot_key,
                                      args->decimal_dot);
    gwy_container_set_int32_by_name(settings, precision_key, args->precision);
    gwy_container_set_enum_by_name(settings, style_key, args->style);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
