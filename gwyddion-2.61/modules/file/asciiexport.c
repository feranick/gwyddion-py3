/*
 *  $Id: asciiexport.c 22626 2019-10-30 09:14:30Z yeti-dn $
 *  Copyright (C) 2003-2017 David Necas (Yeti).
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

/**
 * [FILE-MAGIC-USERGUIDE]
 * Text matrix of data values
 * .txt
 * Export
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Export only.
 **/

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
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/datafield.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>

#include "err.h"

#define EXTENSION ".txt"

typedef struct {
    gboolean add_comment;
    gboolean decimal_dot;
    gboolean concat_all;
    guint precision;
} ASCIIExportArgs;

typedef struct {
    gboolean needs_decimal_dot;
    guint decimal_dot_len;
    gchar *decimal_dot;
} DecimalDotInfo;

static gboolean module_register          (void);
static gint     asciiexport_detect       (const GwyFileDetectInfo *fileinfo,
                                          gboolean only_name);
static gboolean asciiexport_export       (GwyContainer *data,
                                          const gchar *filename,
                                          GwyRunType mode,
                                          GError **error);
static gboolean asciiexport_export_dialog(ASCIIExportArgs *args,
                                          const DecimalDotInfo *decinfo);
static gboolean export_one_channel       (GwyContainer *data,
                                          gint id,
                                          const ASCIIExportArgs *args,
                                          const DecimalDotInfo *decinfo,
                                          FILE *fh);
static void     fill_decimal_dot_info    (DecimalDotInfo *info);
static void     asciiexport_load_args    (GwyContainer *settings,
                                          ASCIIExportArgs *args);
static void     asciiexport_save_args    (GwyContainer *settings,
                                          ASCIIExportArgs *args);

static const ASCIIExportArgs asciiexport_defaults = {
    FALSE, TRUE, FALSE, 5
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Exports data as simple ASCII matrix."),
    "Yeti <yeti@gwyddion.net>",
    "1.5",
    "David Nečas (Yeti)",
    "2004",
};

GWY_MODULE_QUERY2(module_info, asciiexport)

static gboolean
module_register(void)
{
    gwy_file_func_register("asciiexport",
                           N_("ASCII data matrix (.txt)"),
                           (GwyFileDetectFunc)&asciiexport_detect,
                           NULL,
                           NULL,
                           (GwyFileSaveFunc)&asciiexport_export);

    return TRUE;
}

static gint
asciiexport_detect(const GwyFileDetectInfo *fileinfo,
                   G_GNUC_UNUSED gboolean only_name)
{
    return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;
}

static inline gint
print_with_decimal_dot(FILE *fh,
                       gchar *formatted_number,
                       const gchar *decimal_dot,
                       guint decimal_dot_len)
{
    gchar *pos = strstr(formatted_number, decimal_dot);

    if (!pos)
        return fputs(formatted_number, fh);
    else {
        pos[0] = '.';
        if (decimal_dot_len == 1)
            return fputs(formatted_number, fh);
        else {
            gint l1, l2;

            pos[1] = '\0';
            if ((l1 = fputs(formatted_number, fh)) == EOF)
                return EOF;
            if ((l2 = fputs(pos + decimal_dot_len, fh)) == EOF)
                return EOF;
            return l1 + l2;
        }
    }
}

static gboolean
asciiexport_export(GwyContainer *data,
                   const gchar *filename,
                   GwyRunType mode,
                   GError **error)
{
    ASCIIExportArgs args;
    DecimalDotInfo decinfo;
    gint id, *ids;
    guint i;
    FILE *fh = NULL;
    gboolean ok = FALSE;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_ID, &id, 0);
    if (id < 0) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    asciiexport_load_args(gwy_app_settings_get(), &args);
    fill_decimal_dot_info(&decinfo);

    if (mode == GWY_RUN_INTERACTIVE) {
        if (!asciiexport_export_dialog(&args, &decinfo)) {
            err_CANCELLED(error);
            goto fail;
        }
    }

    if (!(fh = gwy_fopen(filename, "w"))) {
        err_OPEN_WRITE(error);
        goto fail;
    }

    if (args.concat_all) {
        ids = gwy_app_data_browser_get_data_ids(data);
        for (i = 0; ids[i] >= 0; i++) {
            if (!export_one_channel(data, ids[i], &args, &decinfo, fh)) {
                err_WRITE(error);
                goto fail;
            }
            if (gwy_fprintf(fh, "\n") < 0) {
                return FALSE;
            }
        }
    }
    else {
        if (!export_one_channel(data, id, &args, &decinfo, fh)) {
            err_WRITE(error);
            goto fail;
        }
    }

    ok = TRUE;

fail:
    if (fh)
        fclose(fh);
    if (!ok)
        g_unlink(filename);
    g_free(decinfo.decimal_dot);

    return ok;
}

static gboolean
asciiexport_export_dialog(ASCIIExportArgs *args,
                          const DecimalDotInfo *decinfo)
{
    GtkWidget *dialog, *vbox, *hbox, *label, *decimal_dot, *add_comment,
              *concat_all, *precision;
    gint response;

    dialog = gtk_dialog_new_with_buttons(_("Export Text"), NULL, 0,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_file_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);

    vbox = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), vbox, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

    gtk_box_pack_start(GTK_BOX(vbox), gwy_label_new_header(_("Options")),
                       FALSE, FALSE, 0);

    decimal_dot = gtk_check_button_new_with_mnemonic(_("Use _dot "
                                                       "as decimal separator"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(decimal_dot),
                                 args->decimal_dot
                                 || !decinfo->needs_decimal_dot);
    gtk_widget_set_sensitive(decimal_dot, decinfo->needs_decimal_dot);
    gtk_box_pack_start(GTK_BOX(vbox), decimal_dot, FALSE, FALSE, 0);

    add_comment = gtk_check_button_new_with_mnemonic(_("Add _informational "
                                                       "comment header"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(add_comment),
                                 args->add_comment);
    gtk_box_pack_start(GTK_BOX(vbox), add_comment, FALSE, FALSE, 0);

    concat_all = gtk_check_button_new_with_mnemonic(_("Conca_tenate exports "
                                                       "of all images"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(concat_all),
                                 args->concat_all);
    gtk_box_pack_start(GTK_BOX(vbox), concat_all, FALSE, FALSE, 0);

    hbox = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic(_("_Precision:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    precision = gtk_spin_button_new_with_range(0, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(precision), args->precision);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), precision);
    gtk_box_pack_start(GTK_BOX(hbox), precision, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response != GTK_RESPONSE_NONE) {
        if (decinfo->needs_decimal_dot)
            args->decimal_dot
                = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(decimal_dot));
        args->add_comment
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(add_comment));
        args->concat_all
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(concat_all));
        args->precision
            = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(precision));
        asciiexport_save_args(gwy_app_settings_get(), args);
        gtk_widget_destroy(dialog);
    }

    return response == GTK_RESPONSE_OK;
}

static gboolean
export_one_channel(GwyContainer *data, gint id,
                   const ASCIIExportArgs *args, const DecimalDotInfo *decinfo,
                   FILE *fh)
{
    GwyDataField *dfield;
    GQuark quark;
    gint xres, yres, i;
    const gdouble *d;
    gchar buf[40];

    quark = gwy_app_get_data_key_for_id(id);
    dfield = gwy_container_get_object(data, quark);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(dfield), FALSE);

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    d = gwy_data_field_get_data_const(dfield);
    if (args->add_comment) {
        GwySIUnit *units;
        GwySIValueFormat *vf = NULL;
        gchar *s;

        s = gwy_app_get_data_field_title(data, id);
        gwy_fprintf(fh, "# %s %s\n", _("Channel:"), s);
        g_free(s);

        vf = gwy_data_field_get_value_format_xy(dfield,
                                                GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                vf);
        if (decinfo->needs_decimal_dot && args->decimal_dot) {
            gwy_fprintf(fh, "# %s ", _("Width:"));
            g_snprintf(buf, sizeof(buf), "%.*f",
                       vf->precision,
                       gwy_data_field_get_xreal(dfield)/vf->magnitude);
            print_with_decimal_dot(fh, buf,
                                   decinfo->decimal_dot,
                                   decinfo->decimal_dot_len);
            gwy_fprintf(fh, " %s\n", vf->units);

            gwy_fprintf(fh, "# %s ", _("Height:"));
            g_snprintf(buf, sizeof(buf), "%.*f",
                       vf->precision,
                       gwy_data_field_get_yreal(dfield)/vf->magnitude);
            print_with_decimal_dot(fh, buf,
                                   decinfo->decimal_dot,
                                   decinfo->decimal_dot_len);
            gwy_fprintf(fh, " %s\n", vf->units);
        }
        else {
            gwy_fprintf(fh, "# %s %.*f %s\n", _("Width:"),
                        vf->precision,
                        gwy_data_field_get_xreal(dfield)/vf->magnitude,
                        vf->units);
            gwy_fprintf(fh, "# %s %.*f %s\n", _("Height:"),
                        vf->precision,
                        gwy_data_field_get_yreal(dfield)/vf->magnitude,
                        vf->units);
        }

        units = gwy_data_field_get_si_unit_z(dfield);
        s = gwy_si_unit_get_string(units, GWY_SI_UNIT_FORMAT_VFMARKUP);
        gwy_fprintf(fh, "# %s %s\n", _("Value units:"), s);
        g_free(s);

        gwy_si_unit_value_format_free(vf);
    }

    if (decinfo->needs_decimal_dot && args->decimal_dot) {
        for (i = 0; i < xres*yres; i++) {
            g_snprintf(buf, sizeof(buf), "%.*g%c",
                       args->precision, d[i], (i + 1) % xres ? '\t' : '\n');
            if (print_with_decimal_dot(fh, buf,
                                       decinfo->decimal_dot,
                                       decinfo->decimal_dot_len) == EOF)
                return FALSE;
        }
    }
    else {
        for (i = 0; i < xres*yres; i++) {
            if (gwy_fprintf(fh, "%.*g%c", args->precision, d[i],
                            (i + 1) % xres ? '\t' : '\n') < 2) {
                return FALSE;
            }
        }
    }

    return TRUE;
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

static const gchar add_comment_key[] = "/module/asciiexport/add-comment";
static const gchar concat_all_key[]  = "/module/asciiexport/concat-all";
static const gchar decimal_dot_key[] = "/module/asciiexport/decimal-dot";
static const gchar precision_key[]   = "/module/asciiexport/precision";

static void
asciiexport_load_args(GwyContainer *settings,
                      ASCIIExportArgs *args)
{
    *args = asciiexport_defaults;

    gwy_container_gis_boolean_by_name(settings, concat_all_key,
                                      &args->concat_all);
    gwy_container_gis_boolean_by_name(settings, decimal_dot_key,
                                      &args->decimal_dot);
    gwy_container_gis_boolean_by_name(settings, add_comment_key,
                                      &args->add_comment);
    gwy_container_gis_int32_by_name(settings, precision_key, &args->precision);

    args->precision = MIN(args->precision, 16);
}

static void
asciiexport_save_args(GwyContainer *settings,
                      ASCIIExportArgs *args)
{
    gwy_container_set_boolean_by_name(settings, concat_all_key,
                                      args->concat_all);
    gwy_container_set_boolean_by_name(settings, decimal_dot_key,
                                      args->decimal_dot);
    gwy_container_set_boolean_by_name(settings, add_comment_key,
                                      args->add_comment);
    gwy_container_set_int32_by_name(settings, precision_key, args->precision);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
