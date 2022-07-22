/*
 *  $Id: wrustfile.c 24756 2022-04-05 11:16:24Z yeti-dn $
 *  Copyright (C) 2021-2022 David Necas (Yeti).
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-wrust-spm">
 *   <comment>WRUST Department of Nanometrology AFM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="[Nazwa Systemu]"/>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # WRUST Department of Nanometrology AFM data
 * 0 string \x5bNazwa\ Systemu\x5d WRUST Department of Nanometrology AFM data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Department of Nanometrology, WRUST
 * .dat
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"

#define MAGIC "[Nazwa Systemu]"
#define MAGIC_SIZE (sizeof(MAGIC)-1)
#define EXTENSION ".dat"

static gboolean      module_register(void);
static gint          dat_detect     (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name);
static GwyContainer* dat_load       (const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports AFM files from Department of Nanometrology, WRUST."),
    "Yeti <yeti@gwyddion.net>",
    "1.2",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, wrustfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("wrustfile",
                           N_("WRUST Department of Nanometrology AFM data (.dat)"),
                           (GwyFileDetectFunc)&dat_detect,
                           (GwyFileLoadFunc)&dat_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
dat_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 10 : 0;

    if (fileinfo->file_size > MAGIC_SIZE && memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) == 0)
        return 90;

    return 0;
}

static GwySIUnit*
parse_record_with_units(GwySIUnit *unit, gdouble *value,
                        const gchar *unitstr, const gchar *valuestr)
{
    gint power10;

    if (unit)
        gwy_si_unit_set_from_string_parse(unit, unitstr, &power10);
    else
        unit = gwy_si_unit_new_parse(unitstr, &power10);

    *value = g_ascii_strtod(valuestr, NULL) * pow10(power10);
    return unit;
}

static void
store_meta(gpointer key, gpointer value, gpointer user_data)
{
    if (strlen(value))
        gwy_container_set_const_string_by_name((GwyContainer*)user_data, key, value);
}

static GwyContainer*
dat_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *container = NULL, *meta = NULL;
    GwyDataField *dfield = NULL;
    GwySIUnit *xunit = NULL, *yunit = NULL, *actzunit = NULL, *amplzunit = NULL, *voltunit = NULL;
    gchar *line, *p, *s, *value, *key, *sens, *title, *buffer = NULL;
    GHashTable *hash = NULL;
    GRegex *regex;
    GMatchInfo *info = NULL;
    gsize size;
    GError *err = NULL;
    gdouble xreal, yreal, q, actzsens = 1.0, amplz = 1.0, xscale = 1.0, yscale = 1.0;
    gint i, xres, yres, len;
    gdouble *data;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        goto fail;
    }

    voltunit = gwy_si_unit_new("V");
    hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    regex = g_regex_new("^(?P<name>.+) (?P<sens>[a-zA-Z]+/[0-9]*V)$", G_REGEX_NO_AUTO_CAPTURE, 0, NULL);
    g_return_val_if_fail(regex, NULL);

    p = buffer;
    while (TRUE) {
        if (!(line = gwy_str_next_line(&p))) {
            err_TRUNCATED_HEADER(error);
            goto fail;
        }
        g_strstrip(line);
        len = strlen(line);
        /* Skip empty lines.  Apparently they can occur in some files. */
        if (!len)
            continue;

        if (line[0] != '[' || line[len-1] != ']') {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Invalid file header."));
            goto fail;
        }
        line[len-1] = '\0';
        key = line + 1;
        if (gwy_strequal(key, "Dane"))
            break;

        if (!(line = gwy_str_next_line(&p))) {
            err_TRUNCATED_HEADER(error);
            goto fail;
        }
        if (g_regex_match(regex, key, 0, &info)) {
            key = g_match_info_fetch_named(info, "name");
            sens = g_match_info_fetch_named(info, "sens");
            if (gwy_strequal(key, "Czulosc Piezoaktuatora Z"))
                actzunit = parse_record_with_units(actzunit, &actzsens, sens, line);
            else if (gwy_strequal(key, "WzmocnienieHVZ"))
                amplzunit = parse_record_with_units(amplzunit, &amplz, sens, line);
            else if (gwy_strequal(key, "RozdzielczoscX"))
                xunit = parse_record_with_units(xunit, &xscale, sens, line);
            else if (gwy_strequal(key, "RozdzielczoscY"))
                yunit = parse_record_with_units(yunit, &yscale, sens, line);
            g_match_info_free(info);
            info = NULL;
            value = g_strconcat(line, " ", sens, NULL);
            g_free(sens);
        }
        else {
            key = g_strdup(key);
            value = g_strdup(line);
        }
        g_hash_table_replace(hash, key, value);
    }

    if (!require_keys(hash, error,
                      "Liczba Linii", "RozdzielczoscX", "RozdzielczoscY", "RasterX", "RasterY",
                      "Czulosc Piezoaktuatora Z", "WzmocnienieHVZ",
                      NULL))
        goto fail;

    /* Older files can have just Liczba Linii. */
    xres = yres = atoi(g_hash_table_lookup(hash, "Liczba Linii"));
    if ((s = g_hash_table_lookup(hash, "Liczba Kolumn")))
        xres = atoi(s);
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        goto fail;

    /* There is an extra factor 1/10 due to some electronics.  We also need to ignore the 100 in RozdzielczoscX
     * which is given like 1234 um/100V, which we do by multiplying by 100 back.  Together they give ×100/10 = ×10 */
    xreal = xscale * 10.0 * g_ascii_strtod(g_hash_table_lookup(hash, "RasterX"), NULL);
    yreal = yscale * 10.0 * g_ascii_strtod(g_hash_table_lookup(hash, "RasterY"), NULL);
    /* Use negated positive conditions to catch NaNs */
    if (!((xreal = fabs(xreal)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        xreal = 1.0;
    }
    if (!((yreal = fabs(yreal)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        yreal = 1.0;
    }

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);

    if (!gwy_si_unit_equal(yunit, xunit))
        g_warning("X and Y units differ, using X");
    gwy_si_unit_multiply(xunit, voltunit, gwy_data_field_get_si_unit_xy(dfield));

    q = amplz * actzsens;
    gwy_si_unit_multiply(gwy_si_unit_multiply(amplzunit, voltunit, amplzunit), actzunit,
                         gwy_data_field_get_si_unit_z(dfield));

    data = gwy_data_field_get_data(dfield);
    value = p;
    for (i = 0; i < xres*yres; i++) {
        data[i] = q*g_ascii_strtod(value, &p);
        if (p == value && (!*p || g_ascii_isspace(*p))) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("End of file reached when reading sample #%d of %d"), i, xres*yres);
            goto fail;
        }
        if (p == value) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Malformed data encountered when reading sample #%d of %d"), i, xres*yres);
            goto fail;
        }
        for (value = p; *value == ';' || g_ascii_isspace(*value); value++)
            ;
    }

    container = gwy_container_new();

    gwy_container_set_object(container, gwy_app_get_data_key_for_id(0), dfield);

    if ((title = g_hash_table_lookup(hash, "Rodzaj Obrazka")))
        gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(0), title);
    else
        gwy_app_channel_title_fall_back(container, 0);

    meta = gwy_container_new();
    g_hash_table_foreach(hash, store_meta, meta);
    gwy_container_set_object(container, gwy_app_get_data_meta_key_for_id(0), meta);
    g_object_unref(meta);

    gwy_app_channel_check_nonsquare(container, 0);
    gwy_file_channel_import_log_add(container, 0, NULL, filename);

fail:
    g_free(buffer);
    GWY_OBJECT_UNREF(dfield);
    GWY_OBJECT_UNREF(xunit);
    GWY_OBJECT_UNREF(yunit);
    GWY_OBJECT_UNREF(actzunit);
    GWY_OBJECT_UNREF(amplzunit);
    GWY_OBJECT_UNREF(voltunit);
    if (hash)
        g_hash_table_destroy(hash);

    return container;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
