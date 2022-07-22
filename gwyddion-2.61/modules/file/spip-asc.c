/*
 *  $Id: spip-asc.c 24698 2022-03-21 12:12:25Z yeti-dn $
 *  Copyright (C) 2009-2019 David Necas (Yeti).
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

/* FIXME: Not sure where these come from, but the files tend to bear `created by SPIP'.  The field names resemble BCR,
 * but the format is not the same.  So let's call the format SPIP ASCII data... */
/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-spip-asc">
 *   <comment>SPIP ASCII data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="# File Format = ASCII\r\n"/>
 *   </magic>
 *   <glob pattern="*.asc"/>
 *   <glob pattern="*.ASC"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # SPIP ASCII data
 * 0 string #\ File\ Format\ =\ ASCII\r\n SPIP ASCII export SPM text data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * SPIP ASCII
 * .asc
 * Read Export
 **/

#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyversion.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"

#define MAGIC "# File Format = ASCII"
#define MAGIC_SIZE (sizeof(MAGIC)-1)
#define MAGIC2 "# Created by "
#define MAGIC2_SIZE (sizeof(MAGIC2)-1)
#define EXTENSION ".asc"

#define Nanometer (1e-9)

static gboolean      module_register  (void);
static gint          asc_detect       (const GwyFileDetectInfo *fileinfo,
                                       gboolean only_name);
static GwyContainer* asc_load         (const gchar *filename,
                                       GwyRunType mode,
                                       GError **error);
static GwyContainer* read_image_data  (GHashTable *hash,
                                       gchar *p,
                                       const gchar *filename,
                                       GError **error);
static GwyContainer* read_graph_data  (GHashTable *hash,
                                       gchar *buffer,
                                       gchar *p,
                                       const gchar *filename,
                                       GError **error);
static gboolean      asc_export       (GwyContainer *data,
                                       const gchar *filename,
                                       GwyRunType mode,
                                       GError **error);
static gchar*        asc_format_header(GwyContainer *data,
                                       GwyDataField *field,
                                       gboolean *zunit_is_nm);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports and exports SPIP ASC files."),
    "Yeti <yeti@gwyddion.net>",
    "0.7",
    "David Nečas (Yeti)",
    "2009",
};

GWY_MODULE_QUERY2(module_info, spip_asc)

static gboolean
module_register(void)
{
    gwy_file_func_register("spip-asc",
                           N_("SPIP ASCII files (.asc)"),
                           (GwyFileDetectFunc)&asc_detect,
                           (GwyFileLoadFunc)&asc_load,
                           NULL,
                           (GwyFileSaveFunc)&asc_export);

    return TRUE;
}

static gint
asc_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    gint seplen;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 10 : 0;

    if (fileinfo->file_size < MAGIC_SIZE + MAGIC2_SIZE + 4 || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    if (fileinfo->head[MAGIC_SIZE] == '\r')
        seplen = (fileinfo->head[MAGIC_SIZE+1] == '\n' ? 2 : 1);
    else if (fileinfo->head[MAGIC_SIZE] == '\n')
        seplen = 1;
    else
        return 0;

    /* Return nonzero score for files with matching first line, but a high score only for files with matching second
     * line. */
    return memcmp(fileinfo->head + MAGIC_SIZE + seplen, MAGIC2, MAGIC2_SIZE) ? 50 : 95;
}

static gboolean
header_error(G_GNUC_UNUSED const GwyTextHeaderContext *context,
             GError *error,
             G_GNUC_UNUSED gpointer user_data)
{
    return error->code == GWY_TEXT_HEADER_ERROR_TERMINATOR;
}

static void
header_end(G_GNUC_UNUSED const GwyTextHeaderContext *context,
           gsize length,
           gpointer user_data)
{
    gchar **pp = (gchar**)user_data;

    *pp += length;
}

static GwyContainer*
asc_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *container = NULL;
    GwyTextHeaderParser parser;
    gchar *p, *line, *buffer = NULL;
    GHashTable *hash = NULL;
    gsize size;
    GError *err = NULL;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        goto fail;
    }

    p = buffer;
    line = gwy_str_next_line(&p);
    if (!gwy_strequal(line, MAGIC)) {
        err_FILE_TYPE(error, "SPIP ASCII data");
        goto fail;
    }

    gwy_clear(&parser, 1);
    parser.line_prefix = "#";
    parser.key_value_separator = "=";
    parser.terminator = "# Start of Data:";
    parser.error = &header_error;
    parser.end = &header_end;
    if (!(hash = gwy_text_header_parse(p, &parser, &p, &err))) {
        g_propagate_error(error, err);
        goto fail;
    }
    if (require_keys(hash, NULL, "x-pixels", "y-pixels", "x-length", "y-length", NULL))
        container = read_image_data(hash, p, filename, error);
    else if (require_keys(hash, NULL, "points", "length", NULL))
        container = read_graph_data(hash, buffer, p, filename, error);
    else
        err_NO_DATA(error);

fail:
    g_free(buffer);
    if (hash)
        g_hash_table_destroy(hash);

    return container;
}

static GwyContainer*
read_image_data(GHashTable *hash, gchar *p, const gchar *filename,
                GError **error)
{
    GwyContainer *container = NULL;
    GwyDataField *field = NULL, *mfield = NULL;
    gchar *value;
    gdouble xreal, yreal, q;
    gint i, xres, yres;
    gdouble *data;

    xres = atoi(g_hash_table_lookup(hash, "x-pixels"));
    yres = atoi(g_hash_table_lookup(hash, "y-pixels"));
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        return NULL;

    xreal = Nanometer * g_ascii_strtod(g_hash_table_lookup(hash, "x-length"), NULL);
    yreal = Nanometer * g_ascii_strtod(g_hash_table_lookup(hash, "y-length"), NULL);
    /* Use negated positive conditions to catch NaNs */
    if (!((xreal = fabs(xreal)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        xreal = 1.0;
    }
    if (!((yreal = fabs(yreal)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        yreal = 1.0;
    }

    field = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(field), "m");

    if ((value = g_hash_table_lookup(hash, "z-unit"))) {
        gint power10;
        gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_z(field), value, &power10);
        q = pow10(power10);
    }
    else if ((value = g_hash_table_lookup(hash, "Bit2nm"))) {
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(field), "m");
        q = Nanometer * g_ascii_strtod(value, NULL);
    }
    else
        q = 1.0;

    data = gwy_data_field_get_data(field);
    value = p;
    for (i = 0; i < xres*yres; i++) {
        data[i] = q*g_ascii_strtod(value, &p);
        if (p == value && (!*p || g_ascii_isspace(*p))) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("End of file reached when reading sample #%d of %d"), i, xres*yres);
            g_object_unref(field);
            return NULL;
        }
        if (p == value) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Malformed data encountered when reading sample #%d of %d"), i, xres*yres);
            g_object_unref(field);
            return NULL;
        }
        value = p;
    }

    if ((value = g_hash_table_lookup(hash, "voidpixels")) && atoi(value)) {
        mfield = gwy_data_field_new_alike(field, FALSE);
        data = gwy_data_field_get_data(mfield);
        value = p;
        for (i = 0; i < xres*yres; i++) {
            data[i] = 1.0 - g_ascii_strtod(value, &p);
            value = p;
        }
        if (!gwy_app_channel_remove_bad_data(field, mfield))
            GWY_OBJECT_UNREF(mfield);
    }

    container = gwy_container_new();
    gwy_container_set_object(container, gwy_app_get_data_key_for_id(0), field);
    g_object_unref(field);
    if (mfield) {
        gwy_container_set_object(container, gwy_app_get_mask_key_for_id(0), mfield);
        g_object_unref(mfield);
    }
    gwy_app_channel_title_fall_back(container, 0);
    gwy_file_channel_import_log_add(container, 0, NULL, filename);

    return container;
}

static GwyContainer*
read_graph_data(GHashTable *hash, gchar *buffer, gchar *p,
                const gchar *filename,
                GError **error)
{
    GwyContainer *container = NULL;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GRegex *regex;
    GMatchInfo *minfo = NULL;
    GError *err = NULL;
    guchar *buf2;
    gchar *line, *header, *s;
    GwySIUnit *xunit, *yunit;
    gdouble qx, qy;
    gsize size2;
    guint npoints, i;
    GwyXY *xydata;
    gboolean ok;

    npoints = atoi(g_hash_table_lookup(hash, "points"));
    if (err_DIMENSION(error, npoints))
        return NULL;

    /* Unfortunately, the axes are given in the header in some random format, different from other header lines.  So
     * the text header parser discards them and we have to extract them separately. */
    if (!gwy_file_get_contents(filename, &buf2, &size2, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (size2 < p - buffer) {
        err_TRUNCATED_HEADER(error);
        gwy_file_abandon_contents(buf2, size2, NULL);
        return NULL;
    }

    header = g_memdup(buf2, p-buffer + 1);
    header[p-buffer] = '\0';
    gwy_file_abandon_contents(buf2, size2, NULL);

    regex = g_regex_new("^#\\s*X-Axis:\\s*(?P<xunit>[^;]*);\\s*Y-Axis:\\s*(?P<yunit>.*?)\\s*$",
                        G_REGEX_MULTILINE | G_REGEX_NO_AUTO_CAPTURE, 0, NULL);
    g_assert(regex);

    ok = g_regex_match(regex, header, 0, &minfo);
    if (ok) {
        gchar *unitstr;
        gint power10;

        unitstr = g_match_info_fetch_named(minfo, "xunit");
        xunit = gwy_si_unit_new_parse(unitstr, &power10);
        g_free(unitstr);
        qx = pow10(power10);

        unitstr = g_match_info_fetch_named(minfo, "yunit");
        yunit = gwy_si_unit_new_parse(unitstr, &power10);
        g_free(unitstr);
        qy = pow10(power10);
    }
    g_match_info_free(minfo);
    g_regex_unref(regex);
    g_free(header);
    if (!ok) {
        err_MISSING_FIELD(error, "X-Axis");
        return NULL;
    }

    xydata = g_new(GwyXY, npoints);
    i = 0;
    while ((line = gwy_str_next_line(&p)) && i < npoints) {
        gdouble x, y;
        gchar *end;

        g_strstrip(line);
        if (!line[0] || line[0] == '#')
            continue;

        x = g_ascii_strtod(line, &end);
        line = end;
        y = g_ascii_strtod(line, &end);
        if (end == line) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Malformed data encountered when reading sample #%u"), i);
            g_free(xydata);
            g_object_unref(xunit);
            g_object_unref(yunit);
            return NULL;
        }
        xydata[i].x = qx*x;
        xydata[i].y = qy*y;
        i++;
    }
    if (i < npoints) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("End of file reached when reading sample #%u of %u"), i, npoints);
        g_free(xydata);
        g_object_unref(xunit);
        g_object_unref(yunit);
        return NULL;
    }

    gmodel = gwy_graph_model_new();
    g_object_set(gmodel, "si-unit-x", xunit, "si-unit-y", yunit, NULL);
    g_object_unref(xunit);
    g_object_unref(yunit);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
    if ((s = g_hash_table_lookup(hash, "description"))) {
        g_object_set(gmodel, "title", s, NULL);
        g_object_set(gcmodel, "description", s, NULL);
    }
    gwy_graph_curve_model_set_data_interleaved(gcmodel, (gdouble*)xydata, npoints);
    g_free(xydata);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);

    container = gwy_container_new();
    gwy_container_set_object(container, gwy_app_get_graph_key_for_id(1), gmodel);
    g_object_unref(gmodel);

    return container;
}

static gboolean
asc_export(GwyContainer *data,
           const gchar *filename,
           G_GNUC_UNUSED GwyRunType mode,
           GError **error)
{
    GwyDataField *field;
    guint xres, i, n;
    gchar *header;
    const gdouble *d;
    gboolean zunit_is_nm;
    FILE *fh;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field, 0);

    if (!field) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    if (!(fh = gwy_fopen(filename, "w"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    header = asc_format_header(data, field, &zunit_is_nm);
    if (fputs(header, fh) == EOF)
        goto fail;

    d = gwy_data_field_get_data_const(field);
    xres = gwy_data_field_get_xres(field);
    n = xres*gwy_data_field_get_yres(field);
    for (i = 0; i < n; i++) {
        gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
        gchar c;

        if (zunit_is_nm)
            g_ascii_dtostr(buf, G_ASCII_DTOSTR_BUF_SIZE, d[i]/Nanometer);
        else
            g_ascii_dtostr(buf, G_ASCII_DTOSTR_BUF_SIZE, d[i]);

        if (fputs(buf, fh) == EOF)
            goto fail;

        c = (i % xres == xres-1) ? '\n' : '\t';
        if (fputc(c, fh) == EOF)
            goto fail;
    }

    fclose(fh);
    g_free(header);

    return TRUE;

fail:
    err_WRITE(error);
    fclose(fh);
    g_free(header);
    g_unlink(filename);

    return FALSE;
}

static gchar*
asc_format_header(GwyContainer *data, GwyDataField *field,
                  gboolean *zunit_is_nm)
{
    static const gchar asc_header_template[] =
        "# File Format = ASCII\n"
        "# Created by Gwyddion %s\n"
        "# Original file: %s\n"
        "# x-pixels = %u\n"
        "# y-pixels = %u\n"
        "# x-length = %s\n"
        "# y-length = %s\n"
        "# x-offset = %s\n"
        "# y-offset = %s\n"
        "# Bit2nm = 1.0\n"
        "%s"
        "# Start of Data:\n";

    GwySIUnit *zunit;
    gchar *header, *zunit_str, *zunit_line;
    gchar xreal_str[G_ASCII_DTOSTR_BUF_SIZE], yreal_str[G_ASCII_DTOSTR_BUF_SIZE],
          xoff_str[G_ASCII_DTOSTR_BUF_SIZE], yoff_str[G_ASCII_DTOSTR_BUF_SIZE];
    const guchar *filename = "NONE";
    gdouble xreal, yreal, xoff, yoff;

    /* XXX: Gwyddion can have lateral dimensions as whatever we want.  But who knows about the SPIP ASC format... */
    xreal = gwy_data_field_get_xreal(field)/Nanometer;
    yreal = gwy_data_field_get_yreal(field)/Nanometer;
    xoff = gwy_data_field_get_xoffset(field)/Nanometer;
    yoff = gwy_data_field_get_yoffset(field)/Nanometer;
    zunit = gwy_data_field_get_si_unit_z(field);

    g_ascii_dtostr(xreal_str, G_ASCII_DTOSTR_BUF_SIZE, xreal);
    g_ascii_dtostr(yreal_str, G_ASCII_DTOSTR_BUF_SIZE, yreal);
    g_ascii_dtostr(xoff_str, G_ASCII_DTOSTR_BUF_SIZE, xoff);
    g_ascii_dtostr(yoff_str, G_ASCII_DTOSTR_BUF_SIZE, yoff);
    zunit_str = gwy_si_unit_get_string(zunit, GWY_SI_UNIT_FORMAT_PLAIN);
    if ((*zunit_is_nm = gwy_strequal(zunit_str, "m")))
        zunit_line = g_strdup("");
    else
        zunit_line = g_strdup_printf("# z-unit = %s\n", zunit_str);

    gwy_container_gis_string_by_name(data, "/filename", &filename);

    header = g_strdup_printf(asc_header_template,
                             gwy_version_string(), filename,
                             gwy_data_field_get_xres(field), gwy_data_field_get_yres(field),
                             xreal_str, yreal_str, xoff_str, yoff_str, zunit_line);

    g_free(zunit_str);
    g_free(zunit_line);

    return header;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
