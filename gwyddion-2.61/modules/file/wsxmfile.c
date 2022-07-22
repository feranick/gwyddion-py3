/*
 *  $Id: wsxmfile.c 24544 2021-11-25 14:39:33Z yeti-dn $
 *  Copyright (C) 2005-2021 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
 * <mime-type type="application/x-wsxm-spm">
 *   <comment>WSxM SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="WSxM file copyright ">
 *       <match type="string" offset="22:60" value="SxM Image file"/>
 *     </match>
 *   </magic>
 *   <glob pattern="*.tom"/>
 *   <glob pattern="*.TOM"/>
 *   <glob pattern="*.top"/>
 *   <glob pattern="*.TOP"/>
 *   <glob pattern="*.cur"/>
 *   <glob pattern="*.CUR"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # WsXM
 * 0 string WSxM\ file\ copyright\ Nanotec\ Electronica\x0d\x0aSxM\ Image\ file
 * 0 string WSxM\ file\ copyright\ WSxM\ solutions\x0d\x0aSxM\ Image\ file
 * 0 string WSxM\ file\ copyright\ UAM\x0d\x0aSxM\ Image\ file
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Nanotec WSxM
 * .tom, .top, .stp
 * Read Export
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Nanotec WSxM curves
 * .cur
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
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

#define MAGIC1 "WSxM file copyright "
#define MAGIC1_SIZE (sizeof(MAGIC1) - 1)
#define MAGIC_SIZE (MAGIC1_SIZE + 20)

#define SIZE_HEADER "Image header size:"
#define HEADER_END "[Header end]\r\n"

static gboolean       module_register       (void);
static gint           wsxmfile_detect       (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static gint           wsxmcurve_detect      (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static GwyContainer*  wsxmfile_load         (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static GwyContainer*  wsxmcurve_load        (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static gboolean       wsxmfile_export_double(GwyContainer *data,
                                             const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static GwyDataField*  read_data_field       (const guchar *buffer,
                                             gint xres,
                                             gint yres,
                                             GwyRawDataType type);
static void           process_metadata      (GHashTable *wsxmmeta,
                                             GwyContainer *container);
static GwyGraphModel* read_graph_model      (const guchar *p,
                                             gint ncurves,
                                             gint npts,
                                             const gchar *xlabel,
                                             const gchar *ylabel,
                                             const gchar *xunitstr,
                                             const gchar *yunitstr,
                                             const gchar *which,
                                             GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Nanotec WSxM data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.21",
    "David Nečas (Yeti) & Petr Klapetek",
    "2005",
};

GWY_MODULE_QUERY2(module_info, wsxmfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("wsxmfile",
                           N_("WSxM files (.tom, .top, .stp)"),
                           (GwyFileDetectFunc)&wsxmfile_detect,
                           (GwyFileLoadFunc)&wsxmfile_load,
                           NULL,
                           (GwyFileSaveFunc)&wsxmfile_export_double);
    gwy_file_func_register("wsxmcurve",
                           N_("WSxM curve files (.cur)"),
                           (GwyFileDetectFunc)&wsxmcurve_detect,
                           (GwyFileLoadFunc)&wsxmcurve_load,
                           NULL,
                           NULL);

    return TRUE;
}

/* Return pointer to character after newline */
static const char*
skip_to_next_line(const char *str)
{
    while (*str && *str != '\n' && *str != '\r')
        str++;

    if (*str == '\n')
        return str+1;
    else if (*str == '\r' && *(str+1) == '\n')
        return str+2;

    return NULL;
}

/* Return pointer to character after magic */
static const char*
wsxmfile_check_magic(const char *head)
{
    static const gchar imgstring[] = "SxM Image file";
    const char *rest = NULL;
    guint slen;

    if (memcmp(head, MAGIC1, MAGIC1_SIZE) != 0)
        return NULL;

    if (!(rest = skip_to_next_line(head + MAGIC1_SIZE)))
        return NULL;

    slen = strlen(imgstring);
    if (strncmp(rest, imgstring, slen) == 0 && (rest = skip_to_next_line(rest + slen)))
        return rest;

    return NULL;
}

static gint
wsxmfile_detect(const GwyFileDetectInfo *fileinfo,
                gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return (g_str_has_suffix(fileinfo->name_lowercase, ".tom")
                || g_str_has_suffix(fileinfo->name_lowercase, ".stp")
                || g_str_has_suffix(fileinfo->name_lowercase, ".top")
                ? 20 : 0);

    if (fileinfo->buffer_len > MAGIC_SIZE && wsxmfile_check_magic(fileinfo->head))
        score = 100;

    return score;
}

static const char*
wsxmcurve_check_magic(const char *head, const gchar **which)
{
    /* Not sure if they can all actually appear in file headers. */
    static const gchar *curvenames[] = {
        "IV", "FZ", "IZ", "ZV", "Histogram", "Profile", "Generic",
    };
    static const gchar cfstring[] = " curve file";
    const char *rest = NULL;
    guint len, cflen, i;

    if (memcmp(head, MAGIC1, MAGIC1_SIZE) != 0)
        return NULL;

    if (!(rest = skip_to_next_line(head + MAGIC1_SIZE)))
        return NULL;

    for (i = 0; i < G_N_ELEMENTS(curvenames); i++) {
        len = strlen(curvenames[i]);
        if (strncmp(rest, curvenames[i], len) == 0)
            break;
    }
    if (i == G_N_ELEMENTS(curvenames))
        return NULL;
    cflen = strlen(cfstring);
    if (strncmp(rest + len, cfstring, cflen) == 0 && (rest = skip_to_next_line(rest + len + cflen))) {
        if (which)
            *which = curvenames[i];
        return rest;
    }

    return NULL;
}

static gint
wsxmcurve_detect(const GwyFileDetectInfo *fileinfo,
                 gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return (g_str_has_suffix(fileinfo->name_lowercase, ".cur") ? 20 : 0);

    if (fileinfo->buffer_len > MAGIC_SIZE && wsxmcurve_check_magic(fileinfo->head, NULL))
        score = 100;

    return score;
}

/* Read an integer, unless we have already encountered an error (as indicated
 * by ok – error can be NULL). */
static gint
read_positive_integer(GHashTable *meta, const gchar *key,
                      gboolean *ok, GError **error)
{
    const gchar *p;
    gint retval = 0;

    if (!*ok)
        return retval;
    if (!(p = g_hash_table_lookup(meta, key)) || (retval = atol(p)) <= 0) {
        err_INVALID(error, key);
        *ok = FALSE;
    }
    return retval;
}

static gboolean
convert_to_utf8(G_GNUC_UNUSED const GwyTextHeaderContext *context,
                GHashTable *hash,
                gchar *key,
                gchar *value,
                G_GNUC_UNUSED gpointer user_data,
                G_GNUC_UNUSED GError **error)
{
    g_hash_table_replace(hash, key, g_convert(value, strlen(value), "UTF-8", "ISO-8859-1", NULL, NULL, NULL));
    return TRUE;
}

static GHashTable*
parse_header_common(guchar *buffer, gsize size,
                    const guchar *rest,
                    const gchar *filetype_name,
                    gchar **header,
                    guint *header_size,
                    GError **error)
{
    GwyTextHeaderParser parser;
    gchar *p;

    if (!memcmp(rest, SIZE_HEADER, sizeof(SIZE_HEADER))
        || (*header_size = strtol(rest + sizeof(SIZE_HEADER), &p, 10)) < 1) {
        err_FILE_TYPE(error, filetype_name);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }
    if (size < *header_size) {
        err_TOO_SHORT(error);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    /* Unfortunately, some programs miscalculate the header size so we cannot
     * use it even for a sanity check.  Must look for [Header end]. */
    p = gwy_memmem(buffer, size, HEADER_END, sizeof(HEADER_END)-1);
    if (!p) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Expected header end marker ‘%s’ was not found."), HEADER_END);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }
    *header_size = ((guchar*)p - buffer) + sizeof(HEADER_END)-1;

    *header = g_strndup(buffer, *header_size);
    p = strchr(*header, '[');
    if (!p) {
        err_FILE_TYPE(error, filetype_name);
        gwy_file_abandon_contents(buffer, size, NULL);
        g_free(*header);
        return NULL;
    }
    gwy_clear(&parser, 1);
    parser.key_value_separator = ":";
    parser.section_template = "[\x1a]";
    parser.section_accessor = "::";
    parser.destroy_value = g_free;
    parser.item = convert_to_utf8;

    return gwy_text_header_parse(p, &parser, NULL, NULL);
}

static GwyContainer*
wsxmfile_load(const gchar *filename,
              G_GNUC_UNUSED GwyRunType mode,
              GError **error)
{
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    const gchar *rest = NULL;
    gsize size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    GHashTable *meta = NULL;
    GwyRawDataType type = GWY_RAW_DATA_SINT16;
    guint header_size;
    gchar *p, *header;
    gboolean ok = TRUE;
    gint xres = 0, yres = 0;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (!(rest = wsxmfile_check_magic(buffer))) {
        err_FILE_TYPE(error, "WSxM");
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }
    if (!(meta = parse_header_common(buffer, size, rest, "WSxM", &header, &header_size, error)))
        return NULL;

    xres = read_positive_integer(meta, "General Info::Number of columns", &ok, error);
    yres = read_positive_integer(meta, "General Info::Number of rows", &ok, error);

    if (ok
        && (p = g_hash_table_lookup(meta, "General Info::Image Data Type"))) {
        if (gwy_strequal(p, "double"))
            type = GWY_RAW_DATA_DOUBLE;
        else if (gwy_strequal(p, "float"))
            type = GWY_RAW_DATA_FLOAT;
        else if (gwy_stramong(p, "integer", "short", NULL))
            type = GWY_RAW_DATA_SINT16;
        else {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Unknown data type `%s'."), p);
            ok = FALSE;
        }
    }

    if (ok)
        ok = !err_SIZE_MISMATCH(error, 2*xres*yres, (guint)size - header_size, FALSE);
    if (ok)
        dfield = read_data_field(buffer + header_size, xres, yres, type);
    gwy_file_abandon_contents(buffer, size, NULL);

    if (dfield) {
        container = gwy_container_new();
        gwy_container_set_object(container, gwy_app_get_data_key_for_id(0), dfield);
        g_object_unref(dfield);
        process_metadata(meta, container);

        gwy_file_channel_import_log_add(container, 0, NULL, filename);
    }
    else {
        g_assert(!ok);
    }
    g_hash_table_destroy(meta);
    g_free(header);

    return container;
}

static GwyContainer*
wsxmcurve_load(const gchar *filename,
               G_GNUC_UNUSED GwyRunType mode,
               GError **error)
{
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    const gchar *xlabel = NULL, *ylabel = NULL, *xunitstr = NULL, *yunitstr = NULL;
    const gchar *which, *rest = NULL;
    gsize size = 0;
    GError *err = NULL;
    GHashTable *meta = NULL;
    GwyGraphModel *gmodel = NULL;
    guint header_size;
    gchar *header;
    gboolean ok = TRUE;
    guint ncurves, npts;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    /* NB: The files apparently say ‘Image header size’ even for curves. */
    if (!(rest = wsxmcurve_check_magic(buffer, &which))) {
        err_FILE_TYPE(error, "WSxM curve");
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }
    if (!(meta = parse_header_common(buffer, size, rest, "WSxM curve", &header, &header_size, error)))
        return NULL;

    /*
     * Number of lines: 2
     * Number of points: 512
     * X axis text: Bias [#x]
     * X axis unit: V
     * Y axis text: Amplitude [#y]
     * Y axis unit: V
     */
    ncurves = read_positive_integer(meta, "General Info::Number of lines", &ok, error);
    npts = read_positive_integer(meta, "General Info::Number of points", &ok, error);

    if (ok) {
        xlabel = g_hash_table_lookup(meta, "General Info::X axis text");
        ylabel = g_hash_table_lookup(meta, "General Info::Y axis text");
        xunitstr = g_hash_table_lookup(meta, "General Info::X axis unit");
        yunitstr = g_hash_table_lookup(meta, "General Info::Y axis unit");
    }

    if (ok) {
        gmodel = read_graph_model(buffer + header_size, ncurves, npts, xlabel, ylabel, xunitstr, yunitstr, which,
                                  error);
    }
    gwy_file_abandon_contents(buffer, size, NULL);

    if (gmodel) {
        container = gwy_container_new();
        gwy_container_set_object(container, gwy_app_get_graph_key_for_id(1), gmodel);
        g_object_unref(gmodel);
    }
    g_hash_table_destroy(meta);
    g_free(header);

    return container;
}

static void
store_meta(gpointer key,
           gpointer value,
           gpointer user_data)
{
    GwyContainer *meta = (GwyContainer*)user_data;

    gwy_container_set_string_by_name(meta, key, g_strdup(value));
}

static gboolean
get_double_value(GHashTable *hash, const gchar *name, gdouble *value, gchar **pend)
{
    const gchar *s;
    gchar *end;

    if (!(s = g_hash_table_lookup(hash, name))) {
        g_warning("Missing %s", name);
        return FALSE;
    }

    *value = g_ascii_strtod(s, &end);
    if (end == s) {
        g_warning("Invalid value of %s", name);
        return FALSE;
    }
    if (*value <= 0.0) {
        g_warning("Invalid value of %s", name);
        return FALSE;
    }
    if (pend)
        *pend = end;

    return TRUE;
}

static void
process_metadata(GHashTable *wsxmmeta,
                 GwyContainer *container)
{
    const gchar *nometa[] = {
        "General Info::Z Amplitude", "Control::X Amplitude", "Control::Y Amplitude",
        "General Info::Number of rows", "General Info::Number of columns",
    };
    GwyDataField *dfield;
    GwySIUnit *siunit;
    GwyContainer *meta;
    gdouble r;
    gchar *end;
    gint power10;
    guint i;
    gdouble min, max;
    GQuark quark;

    quark = gwy_app_get_data_key_for_id(0);
    dfield = GWY_DATA_FIELD(gwy_container_get_object(container, quark));

    /* Fix value scale */
    if (get_double_value(wsxmmeta, "General Info::Z Amplitude", &r, &end)) {
        /* import `arbitrary units' as unit-less */
        while (g_ascii_isspace(*end))
            end++;
        if (gwy_strequal(end, "a.u."))
            siunit = gwy_si_unit_new(NULL);
        else if (gwy_strequal(end, "Pi")) {
            siunit = gwy_si_unit_new(NULL);
            r *= G_PI;
        }
        else {
            siunit = gwy_si_unit_new_parse(end, &power10);
            r *= pow10(power10);
        }
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield), siunit);
        g_object_unref(siunit);

        gwy_data_field_get_min_max(dfield, &min, &max);
        gwy_data_field_multiply(dfield, r/(max - min));

        gwy_app_channel_title_fall_back(container, 0);
    }
    else
        gwy_data_field_multiply(dfield, 1e-9);

    /* Fix lateral scale */
    if (get_double_value(wsxmmeta, "Control::X Amplitude", &r, NULL)) {
        siunit = gwy_si_unit_new_parse(end, &power10);
        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(dfield), siunit);
        g_object_unref(siunit);
        gwy_data_field_set_xreal(dfield, r*pow10(power10));
    }

    if (get_double_value(wsxmmeta, "Control::Y Amplitude", &r, NULL)) {
        siunit = gwy_si_unit_new_parse(end, &power10);
        g_object_unref(siunit);
        gwy_data_field_set_yreal(dfield, r*pow10(power10));
    }
    else
        gwy_data_field_set_yreal(dfield, gwy_data_field_get_xreal(dfield));

    /* And store everything else as metadata */
    for (i = 0; i < G_N_ELEMENTS(nometa); i++)
        g_hash_table_remove(wsxmmeta, nometa[i]);

    meta = gwy_container_new();
    g_hash_table_foreach(wsxmmeta, store_meta, meta);
    if (gwy_container_get_n_items(meta)) {
        quark = gwy_app_get_data_meta_key_for_id(0);
        gwy_container_set_object(container, quark, meta);
    }
    g_object_unref(meta);
}

static GwyDataField*
read_data_field(const guchar *buffer,
                gint xres, gint yres,
                GwyRawDataType type)
{
    GwyDataField *dfield;

    dfield = gwy_data_field_new(xres, yres, 1e-6, 1e-6, FALSE);
    /* The conversion is probably wrong for the SINT16 type. */
    gwy_convert_raw_data(buffer, xres*yres, 1, type, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield), 1.0, 0.0);
    gwy_data_field_invert(dfield, TRUE, TRUE, FALSE);

    return dfield;
}

static GwyGraphModel*
read_graph_model(const guchar *p,
                 gint ncurves, gint npts,
                 const gchar *xlabel, const gchar *ylabel,
                 const gchar *xunitstr, const gchar *yunitstr,
                 const gchar *which,
                 GError **error)
{
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    gdouble *data;
    gchar *end, *s;
    guint i, j, k, n = ncurves*npts;
    gint pwr10;
    GwySIUnit *xunit, *yunit;
    GString *str;
    gdouble qx = 1.0, qy = 1.0;

    xunit = gwy_si_unit_new_parse(xunitstr, &pwr10);
    qx = pow10(pwr10);
    yunit = gwy_si_unit_new_parse(yunitstr, &pwr10);
    qy = pow10(pwr10);

    data = g_new(gdouble, 2*n);
    for (i = 0; i < npts; i++) {
        for (j = 0; j < ncurves; j++) {
            k = 2*(j*npts + i);
            /* Abscissa */
            data[k] = qx*g_ascii_strtod(p, &end);
            if (p == (const guchar*)end)
                goto fail;
            p = end;
            /* Ordinate */
            data[k+1] = qy*g_ascii_strtod(p, &end);
            if (p == (const guchar*)end)
                goto fail;
            p = end;
        }
    }

    gmodel = gwy_graph_model_new();

    str = g_string_new(NULL);
    if (xlabel) {
        g_string_assign(str, xlabel);
        if ((s = strstr(str->str, " ["))) {
            *s = '\0';
            g_object_set(gmodel, "axis-label-bottom", str->str, NULL);
        }
    }
    if (ylabel) {
        g_string_assign(str, ylabel);
        if ((s = strstr(str->str, " ["))) {
            *s = '\0';
            g_object_set(gmodel, "axis-label-left", str->str, NULL);
        }
    }
    g_string_assign(str, which);
    g_string_append(str, " curve");

    g_object_set(gmodel,
                 "si-unit-x", xunit,
                 "si-unit-y", yunit,
                 "title", str->str,
                 NULL);

    for (j = 0; j < ncurves; j++) {
        gcmodel = gwy_graph_curve_model_new();
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(j),
                     "description", str->str,
                     NULL);
        gwy_graph_curve_model_set_data_interleaved(gcmodel, data + j*2*npts, npts);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

    g_string_free(str, TRUE);
    g_object_unref(xunit);
    g_object_unref(yunit);
    g_free(data);

    return gmodel;

fail:
    g_object_unref(xunit);
    g_object_unref(yunit);
    g_free(data);
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("File is truncated."));

    return NULL;
}

static gboolean
wsxmfile_export_double(GwyContainer *data,
                       const gchar *filename,
                       G_GNUC_UNUSED GwyRunType mode,
                       GError **error)
{
    static const gchar header_template[] =
        "WSxM file copyright Nanotec Electronica\r\n"
        "SxM Image file\r\n"
        "Image header size: 99999\r\n"
        "\r\n"
        "[Control]\r\n"
        "\r\n"
        "    X Amplitude: %g %s\r\n"
        "    Y Amplitude: %g %s\r\n"
        "\r\n"
        "[General Info]\r\n"
        "\r\n"
        "    Image Data Type: double\r\n"
        "    Acquisition channel: %s\r\n"
        "    Number of columns: %u\r\n"
        "    Number of rows: %u\r\n"
        "    Z Amplitude: %g %s\r\n"
        "\r\n"
        "[Miscellaneous]\r\n"
        "\r\n"
        "    Comments: Exported from Gwyddion %s\r\n"
        "    Version: 1.0 (December 2003)\r\n"
        "\r\n"
        "[Header end]\r\n";

    GwyDataField *dfield;
    const gdouble *d, *r;
    guint8 *bytes = NULL;
    gdouble *dbuf = NULL, *drow;
    gchar *xyunit, *zunit, *title, *header;
    guint xres, yres, i, j, hlen;
    gdouble min, max;
    size_t written;
    gchar buf[6];
    gint id;
    gboolean ok = FALSE;
    FILE *fh;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    if (!dfield) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    if (!(fh = gwy_fopen(filename, "w"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    d = gwy_data_field_get_data_const(dfield);
    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    gwy_data_field_get_min_max(dfield, &min, &max);

    xyunit = gwy_si_unit_get_string(gwy_data_field_get_si_unit_xy(dfield), GWY_SI_UNIT_FORMAT_PLAIN);
    zunit = gwy_si_unit_get_string(gwy_data_field_get_si_unit_z(dfield), GWY_SI_UNIT_FORMAT_PLAIN);
    title = gwy_app_get_data_field_title(data, id);
    header = g_strdup_printf(header_template,
                             gwy_data_field_get_xreal(dfield), xyunit,
                             gwy_data_field_get_yreal(dfield), xyunit,
                             title, xres, yres,
                             max-min, zunit,
                             gwy_version_string());
    g_free(title);
    g_free(zunit);
    g_free(xyunit);

    hlen = strlen(header);
    g_snprintf(buf, sizeof(buf), "%5u", hlen);
    memcpy(strstr(header, "99999"), buf, 5);

    if (fwrite(header, 1, hlen, fh) != hlen) {
        err_WRITE(error);
        goto fail;
    }

    dbuf = g_new(gdouble, xres);
    if (G_BYTE_ORDER == G_BIG_ENDIAN)
        bytes = g_new(guint8, xres*sizeof(gdouble));

    for (i = 0; i < yres; i++) {
        r = d + xres*(yres-1 - i);
        drow = dbuf + xres-1;
        for (j = xres; j; j--, r++, drow--)
            *drow = *r;

        if (G_BYTE_ORDER == G_BIG_ENDIAN) {
            gwy_memcpy_byte_swap((const guint8*)dbuf, bytes, sizeof(gdouble), xres, sizeof(gdouble)-1);
            written = fwrite(bytes, 8, xres, fh);
        }
        else
            written = fwrite(dbuf, 8, xres, fh);

        if (written != xres) {
            err_WRITE(error);
            goto fail;
        }
    }
    ok = TRUE;

fail:
    fclose(fh);
    g_free(bytes);
    g_free(dbuf);
    g_free(header);
    if (!ok)
        g_unlink(filename);

    return ok;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
