/*
 *  $Id: sdfile.c 22599 2019-10-21 13:16:44Z yeti-dn $
 *  Copyright (C) 2005-2019 David Necas (Yeti), Petr Klapetek.
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

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-sdf-spm">
 *   <comment>Surfstand SDF data</comment>
 *   <glob pattern="*.sdf"/>
 *   <glob pattern="*.SDF"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-micromap-spm">
 *   <comment>Micromap SDF data</comment>
 *   <glob pattern="*.sdfa"/>
 *   <glob pattern="*.SDFA"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Surfstand Surface Data File
 * .sdf
 * Read Export
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Micromap SDFA
 * .sdfa
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/grains.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/gwyapp.h>

#include "err.h"
#include "get.h"

#define EXTENSION ".sdf"
#define MICROMAP_EXTENSION ".sdfa"

#define Micrometer (1e-6)

enum {
    SDF_HEADER_SIZE_BIN = 8 + 10 + 2*12 + 2*2 + 4*8 + 3*1
};

enum {
    SDF_MIN_TEXT_SIZE = 160
};

typedef enum {
    SDF_UINT8  = 0,
    SDF_UINT16 = 1,
    SDF_UINT32 = 2,
    SDF_FLOAT  = 3,
    SDF_SINT8  = 4,
    SDF_SINT16 = 5,
    SDF_SINT32 = 6,
    SDF_DOUBLE = 7,
    SDF_NTYPES
} SDFDataType;

typedef struct {
    gchar version[8];
    gchar manufacturer[10];
    gchar creation[12];
    gchar modification[12];
    gint xres;
    gint yres;
    gdouble xscale;
    gdouble yscale;
    gdouble zscale;
    gdouble zres;
    gint compression;
    SDFDataType data_type;
    gint check_type;
    gint iso_extra1;
    gint iso_extra2;
    GHashTable *extras;
    gchar *data;

    gint expected_size;
} SDFile;

static gboolean      module_register        (void);
static gint          sdfile_detect_bin      (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static gint          sdfile_detect_text     (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static gint          micromap_detect        (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static GwyContainer* sdfile_load_bin        (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static GwyContainer* sdfile_load_text       (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static gboolean      sdfile_export_text     (GwyContainer *data,
                                             const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static GwyContainer* micromap_load          (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static gboolean      check_params           (const SDFile *sdfile,
                                             guint len,
                                             GError **error);
static gboolean      sdfile_read_header_bin (const guchar **p,
                                             gsize *len,
                                             SDFile *sdfile,
                                             GError **error);
static gboolean      sdfile_read_header_text(gchar **buffer,
                                             gsize *len,
                                             SDFile *sdfile,
                                             GError **error);
static gchar*        sdfile_next_line       (gchar **buffer,
                                             const gchar *key,
                                             GError **error);
static GwyDataField* sdfile_read_data_bin   (SDFile *sdfile,
                                             GwyDataField **mfield);
static GwyDataField* sdfile_read_data_text  (SDFile *sdfile,
                                             GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Surfstand group SDF (Surface Data File) files."),
    "Yeti <yeti@gwyddion.net>",
    "0.14",
    "David Nečas (Yeti) & Petr Klapetek",
    "2005",
};

static const guint type_sizes[] = { 1, 2, 4, 4, 1, 2, 4, 8 };

GWY_MODULE_QUERY2(module_info, sdfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("sdfile-bin",
                           N_("Surfstand SDF files, binary (.sdf)"),
                           (GwyFileDetectFunc)&sdfile_detect_bin,
                           (GwyFileLoadFunc)&sdfile_load_bin,
                           NULL,
                           NULL);
    gwy_file_func_register("sdfile-txt",
                           N_("Surfstand SDF files, text (.sdf)"),
                           (GwyFileDetectFunc)&sdfile_detect_text,
                           (GwyFileLoadFunc)&sdfile_load_text,
                           NULL,
                           (GwyFileSaveFunc)&sdfile_export_text);
    gwy_file_func_register("micromap",
                           N_("Micromap SDF files (.sdfa)"),
                           (GwyFileDetectFunc)&micromap_detect,
                           (GwyFileLoadFunc)&micromap_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
sdfile_detect_bin(const GwyFileDetectInfo *fileinfo,
                  gboolean only_name)
{
    SDFile sdfile;
    const gchar *p;
    gsize len;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 15 : 0;

    p = fileinfo->head;
    len = fileinfo->buffer_len;
    if (len <= SDF_HEADER_SIZE_BIN || fileinfo->head[0] != 'b')
        return 0;
    if (sdfile_read_header_bin((const guchar**)&p, &len, &sdfile, NULL)
        && SDF_HEADER_SIZE_BIN + sdfile.expected_size <= fileinfo->file_size
        && !sdfile.compression
        && !sdfile.check_type)
        return 90;

    return 0;
}

static gint
sdfile_detect_text(const GwyFileDetectInfo *fileinfo,
                   gboolean only_name)
{
    SDFile sdfile;
    gchar *buffer, *p;
    gsize len;
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 15 : 0;

    len = fileinfo->buffer_len;
    if (len <= SDF_MIN_TEXT_SIZE || fileinfo->head[0] != 'a')
        return 0;

    buffer = p = g_memdup(fileinfo->head, len);
    if (sdfile_read_header_text(&p, &len, &sdfile, NULL)
        && sdfile.expected_size <= fileinfo->file_size
        && !sdfile.compression
        && !sdfile.check_type)
        score = 90;

    g_free(buffer);

    return score;
}

/* Perform generic SDF detection, then check for Micromap specific stuff */
static gint
micromap_detect(const GwyFileDetectInfo *fileinfo,
                gboolean only_name)
{
    SDFile sdfile;
    gchar *buffer, *p;
    gsize len;
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase,
                                MICROMAP_EXTENSION) ? 18 : 0;

    len = fileinfo->buffer_len;
    if (len <= SDF_MIN_TEXT_SIZE || fileinfo->head[0] != 'a')
        return 0;

    buffer = p = g_memdup(fileinfo->head, len);
    if (sdfile_read_header_text(&p, &len, &sdfile, NULL)
        && sdfile.expected_size <= fileinfo->file_size
        && !sdfile.compression
        && !sdfile.check_type
        && strncmp(sdfile.manufacturer, "Micromap", sizeof("Micromap")-1) == 0
        && strstr(fileinfo->tail, "OBJECTIVEMAG")
        && strstr(fileinfo->tail, "TUBEMAG")
        && strstr(fileinfo->tail, "CAMERAXPIXEL")
        && strstr(fileinfo->tail, "CAMERAYPIXEL"))
        score = 100;

    g_free(buffer);

    return score;
}

static void
sdfile_set_units(SDFile *sdfile,
                 GwyDataField *dfield)
{
    GwySIUnit *siunit;

    gwy_data_field_multiply(dfield, sdfile->zscale);

    siunit = gwy_si_unit_new("m");
    gwy_data_field_set_si_unit_xy(dfield, siunit);
    g_object_unref(siunit);

    siunit = gwy_si_unit_new("m");
    gwy_data_field_set_si_unit_z(dfield, siunit);
    g_object_unref(siunit);
}

static GwyContainer*
sdfile_load_bin(const gchar *filename,
                G_GNUC_UNUSED GwyRunType mode,
                GError **error)
{
    SDFile sdfile;
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    const guchar *p;
    gsize len, size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL, *mfield = NULL;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    len = size;
    p = buffer;
    if (sdfile_read_header_bin(&p, &len, &sdfile, error)) {
        if (check_params(&sdfile, len, error))
            dfield = sdfile_read_data_bin(&sdfile, &mfield);
    }

    gwy_file_abandon_contents(buffer, size, NULL);
    if (!dfield)
        return NULL;

    sdfile_set_units(&sdfile, dfield);

    container = gwy_container_new();
    gwy_container_set_object_by_name(container, "/0/data", dfield);
    g_object_unref(dfield);
    if (mfield) {
        gwy_container_set_object_by_name(container, "/0/mask", mfield);
        g_object_unref(mfield);
    }
    gwy_container_set_string_by_name(container, "/0/data/title",
                                     g_strdup("Topography"));
    gwy_file_channel_import_log_add(container, 0, NULL, filename);

    return container;
}

static void
store_meta(gpointer key,
           gpointer value,
           gpointer user_data)
{
    GwyContainer *meta = (GwyContainer*)user_data;
    gchar *cval;

    if (!(cval = g_convert(value, strlen(value), "UTF-8", "ISO-8859-1",
                           NULL, NULL, NULL)))
        return;
    gwy_container_set_string_by_name(meta, key, cval);
}

static GwyContainer*
sdfile_load_text(const gchar *filename,
                 G_GNUC_UNUSED GwyRunType mode,
                 GError **error)
{
    SDFile sdfile;
    GwyContainer *container = NULL;
    gchar *p, *buffer = NULL;
    gsize len, size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    len = size;
    p = buffer;
    if (sdfile_read_header_text(&p, &len, &sdfile, error)) {
        if (check_params(&sdfile, len, error))
            dfield = sdfile_read_data_text(&sdfile, error);
    }

    if (!dfield) {
        g_free(buffer);
        return NULL;
    }

    sdfile_set_units(&sdfile, dfield);

    container = gwy_container_new();
    gwy_container_set_object_by_name(container, "/0/data", dfield);
    g_object_unref(dfield);
    gwy_container_set_string_by_name(container, "/0/data/title",
                                     g_strdup("Topography"));

    if (sdfile.extras) {
        GwyContainer *meta = gwy_container_new();
        g_hash_table_foreach(sdfile.extras, store_meta, meta);
        gwy_container_set_object_by_name(container, "/0/meta", meta);
        g_object_unref(meta);
        g_hash_table_destroy(sdfile.extras);
    }
    g_free(buffer);

    gwy_file_channel_import_log_add(container, 0, NULL, filename);

    return container;
}

static gboolean
sdfile_export_text(G_GNUC_UNUSED GwyContainer *data,
                   const gchar *filename,
                   G_GNUC_UNUSED GwyRunType mode,
                   GError **error)
{
    enum { SCALE = 65535 };
    static const gchar header_format[] =
        "aBCR-0.0\n"
        "ManufacID   = Gwyddion\n"
        "CreateDate  = %02u%02u%04u%02u%02u\n"
        "ModDate     = %02u%02u%04u%02u%02u\n"
        "NumPoints   = %d\n"
        "NumProfiles = %d\n"
        "Xscale      = %e\n"
        "Yscale      = %e\n"
        "Zscale      = %e\n"
        "Zresolution = -1\n"  /* FIXME: Dunno */
        "Compression = 0\n"
        "DataType    = %d\n"
        "CheckType   = 0\n"
        "NumDataSet  = 1\n"
        "NanPresent  = 0\n"
        "*\n";

    GwyDataField *dfield;
    const gdouble *d;
    gint xres, yres, i;
    const struct tm *ttm;
    gchar buf[24];
    time_t t;
    FILE *fh;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield, 0);
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

    /* We do not keep date information, just use the current time */
    time(&t);
    ttm = localtime(&t);
    gwy_fprintf(fh, header_format,
            ttm->tm_mday, ttm->tm_mon, ttm->tm_year, ttm->tm_hour, ttm->tm_min,
            ttm->tm_mday, ttm->tm_mon, ttm->tm_year, ttm->tm_hour, ttm->tm_min,
            xres, yres,
            gwy_data_field_get_dx(dfield),
            gwy_data_field_get_dy(dfield),
            1.0,
            SDF_FLOAT);
    for (i = 0; i < xres*yres; i++) {
        g_ascii_formatd(buf, sizeof(buf), "%g", d[i]);
        fputs(buf, fh);
        fputc('\n', fh);
    }
    fclose(fh);

    return TRUE;
}

static GwyContainer*
micromap_load(const gchar *filename,
              G_GNUC_UNUSED GwyRunType mode,
              GError **error)
{
    SDFile sdfile;
    GwyContainer *container = NULL;
    gchar *p, *buffer = NULL;
    gsize len, size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    gdouble objectivemag, tubemag, cameraxpixel, cameraypixel;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    len = size;
    p = buffer;
    if (sdfile_read_header_text(&p, &len, &sdfile, error)) {
        if (check_params(&sdfile, len, error))
            dfield = sdfile_read_data_text(&sdfile, error);
    }
    if (!dfield) {
        g_free(buffer);
        return NULL;
    }

    if (!sdfile.extras) {
        err_MISSING_FIELD(error, "OBJECTIVEMAG");
        goto fail;
    }

    if (!require_keys(sdfile.extras, error,
                      "OBJECTIVEMAG", "TUBEMAG", "CAMERAXPIXEL", "CAMERAYPIXEL",
                      NULL))
        goto fail;

    objectivemag = g_ascii_strtod(g_hash_table_lookup(sdfile.extras,
                                                      "OBJECTIVEMAG"), NULL);
    tubemag = g_ascii_strtod(g_hash_table_lookup(sdfile.extras,
                                                 "TUBEMAG"), NULL);
    cameraxpixel = g_ascii_strtod(g_hash_table_lookup(sdfile.extras,
                                                      "CAMERAXPIXEL"), NULL);
    cameraypixel = g_ascii_strtod(g_hash_table_lookup(sdfile.extras,
                                                      "CAMERAYPIXEL"), NULL);

    sdfile_set_units(&sdfile, dfield);
    gwy_data_field_set_xreal(dfield,
                             Micrometer * sdfile.xres * objectivemag
                             * tubemag * cameraxpixel);
    gwy_data_field_set_yreal(dfield,
                             Micrometer * sdfile.yres * objectivemag
                             * tubemag * cameraypixel);

    container = gwy_container_new();
    gwy_container_set_object_by_name(container, "/0/data", dfield);
    gwy_container_set_string_by_name(container, "/0/data/title",
                                     g_strdup("Topography"));
    gwy_file_channel_import_log_add(container, 0, NULL, filename);

fail:
    g_object_unref(dfield);
    g_free(buffer);
    if (sdfile.extras)
        g_hash_table_destroy(sdfile.extras);

    return container;
}

static gboolean
check_params(const SDFile *sdfile,
             guint len,
             GError **error)
{
    if (sdfile->data_type >= SDF_NTYPES) {
        err_DATA_TYPE(error, sdfile->data_type);
        return FALSE;
    }
    if (err_DIMENSION(error, sdfile->xres)
        || err_DIMENSION(error, sdfile->yres))
        return FALSE;
    if (err_SIZE_MISMATCH(error, sdfile->expected_size, len, FALSE))
        return FALSE;
    if (sdfile->compression) {
        err_UNSUPPORTED(error, "Compression");
        return FALSE;
    }
    if (sdfile->check_type) {
        err_UNSUPPORTED(error, "CheckType");
        return FALSE;
    }

    return TRUE;
}

static gboolean
sdfile_read_header_bin(const guchar **p,
                       gsize *len,
                       SDFile *sdfile,
                       GError **error)
{
    const guchar *buf = *p;

    if (*len < SDF_HEADER_SIZE_BIN) {
        err_TOO_SHORT(error);
        return FALSE;
    }

    gwy_clear(sdfile, 1);
    get_CHARARRAY(sdfile->version, p);
    get_CHARARRAY(sdfile->manufacturer, p);
    get_CHARARRAY(sdfile->creation, p);
    get_CHARARRAY(sdfile->modification, p);
    gwy_debug("version [%.*s]",
              (gint)sizeof(sdfile->version), sdfile->version);
    gwy_debug("manufacturer [%.*s]",
              (gint)sizeof(sdfile->manufacturer), sdfile->manufacturer);
    gwy_debug("creation [%.*s]",
              (gint)sizeof(sdfile->creation), sdfile->creation);
    gwy_debug("modification [%.*s]",
              (gint)sizeof(sdfile->modification), sdfile->modification);
    sdfile->xres = gwy_get_guint16_le(p);
    sdfile->yres = gwy_get_guint16_le(p);
    gwy_debug("xres %d, yres %d", sdfile->xres, sdfile->yres);
    sdfile->xscale = gwy_get_gdouble_le(p);
    sdfile->yscale = gwy_get_gdouble_le(p);
    gwy_debug("xscale %g, yscale %g", sdfile->xscale, sdfile->yscale);
    sdfile->zscale = gwy_get_gdouble_le(p);
    sdfile->zres = gwy_get_gdouble_le(p);
    gwy_debug("zscale %g, zres %g", sdfile->zscale, sdfile->zres);
    sdfile->compression = **p;
    (*p)++;
    sdfile->data_type = **p;
    (*p)++;
    sdfile->check_type = **p;
    (*p)++;
    gwy_debug("compression %d, data_type %d, check_type %d",
              sdfile->compression, sdfile->data_type, sdfile->check_type);

    if (sdfile->data_type < SDF_NTYPES)
        sdfile->expected_size = type_sizes[sdfile->data_type]
                                * sdfile->xres * sdfile->yres;
    else
        sdfile->expected_size = -1;
    gwy_debug("expected size %d", sdfile->expected_size);

    /* Olympus software exports `ISO-1.0' files with 8 bytes longer header.
     * It is really the header, not trailer because if assume it's the trailer
     * the image is shifted.  The values are fixed, 5 and 1.  Try to detect
     * that we are dealing with something like this. */
    if (memcmp(sdfile->version, "bISO-1.0", 8) == 0
        && sdfile->expected_size > 0
        && *len == SDF_HEADER_SIZE_BIN + sdfile->expected_size + 8) {
        gwy_debug("file is 8 bytes too long; assuming Olympus header with "
                  "extra fields");
        sdfile->iso_extra1 = gwy_get_guint32_le(p);
        sdfile->iso_extra2 = gwy_get_guint32_le(p);
        gwy_debug("extra1 %d, extra2 %d",
                  sdfile->iso_extra1, sdfile->iso_extra2);
    }
    sdfile->data = (gchar*)*p;

    *len -= (*p - buf);
    return TRUE;
}

#define NEXT(line, key, val, error) \
    if (!(val = sdfile_next_line(&line, key, error))) { \
        err_MISSING_FIELD(error, key); \
        return FALSE; \
    }

#define READ_STRING(line, key, val, field, error) \
    NEXT(line, key, val, error) \
    strncpy(field, val, sizeof(field));

#define READ_INT(line, key, val, field, check, error) \
    NEXT(line, key, val, error) \
    field = atoi(val); \
    if (check && field <= 0) { \
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, \
                    _("Invalid `%s' value: %d."), key, field); \
        return FALSE; \
    }

#define READ_FLOAT(line, key, val, field, check, error) \
    NEXT(line, key, val, error) \
    field = g_ascii_strtod(val, NULL); \
    if (check && field <= 0.0) { \
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, \
                    _("Invalid `%s' value: %g."), key, field); \
        return FALSE; \
    }

/* NB: Buffer must be writable and nul-terminated, its initial part is
 * overwritten */
static gboolean
sdfile_read_header_text(gchar **buffer,
                        gsize *len,
                        SDFile *sdfile,
                        GError **error)
{
    gchar *val, *p;

    /* We do not need exact lenght of the minimum file */
    if (*len < SDF_MIN_TEXT_SIZE) {
        err_TOO_SHORT(error);
        return FALSE;
    }

    gwy_clear(sdfile, 1);
    p = *buffer;

    val = g_strstrip(gwy_str_next_line(&p));
    strncpy(sdfile->version, val, sizeof(sdfile->version));

    READ_STRING(p, "ManufacID", val, sdfile->manufacturer, error)
    READ_STRING(p, "CreateDate", val, sdfile->creation, error)
    READ_STRING(p, "ModDate", val, sdfile->modification, error)
    READ_INT(p, "NumPoints", val, sdfile->xres, TRUE, error)
    READ_INT(p, "NumProfiles", val, sdfile->yres, TRUE, error)
    READ_FLOAT(p, "Xscale", val, sdfile->xscale, TRUE, error)
    READ_FLOAT(p, "Yscale", val, sdfile->yscale, TRUE, error)
    READ_FLOAT(p, "Zscale", val, sdfile->zscale, TRUE, error)
    READ_FLOAT(p, "Zresolution", val, sdfile->zres, FALSE, error)
    READ_INT(p, "Compression", val, sdfile->compression, FALSE, error)
    READ_INT(p, "DataType", val, sdfile->data_type, FALSE, error)
    READ_INT(p, "CheckType", val, sdfile->check_type, FALSE, error)

    /* at least */
    if (sdfile->data_type < SDF_NTYPES)
        sdfile->expected_size = 2*sdfile->xres * sdfile->yres;
    else
        sdfile->expected_size = -1;

    /* Skip possible extra header lines */
    do {
        val = gwy_str_next_line(&p);
        if (!val)
            break;
        val = g_strstrip(val);
        if (g_ascii_isalpha(val[0])) {
            gwy_debug("Extra header line: <%s>\n", val);
        }
    } while (val[0] == ';' || g_ascii_isalpha(val[0]));

    if (!val || *val != '*') {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Missing data start marker (*)."));
        return FALSE;
    }

    *buffer = p;
    *len -= p - *buffer;
    sdfile->data = (gchar*)*buffer;
    return TRUE;
}

static gchar*
sdfile_next_line(gchar **buffer,
                 const gchar *key,
                 GError **error)
{
    guint klen;
    gchar *value, *line;

    do {
        line = gwy_str_next_line(buffer);
    } while (line && line[0] == ';');

    if (!line) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("End of file reached when looking for `%s' field."), key);
        return NULL;
    }

    klen = strlen(key);
    if (g_ascii_strncasecmp(line, key, klen) != 0
        || !g_ascii_isspace(line[klen])) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Invalid line found when looking for `%s' field."), key);
        return NULL;
    }

    value = line + klen;
    g_strstrip(value);
    if (value[0] == '=') {
        value++;
        g_strstrip(value);
    }

    return value;
}

static GwyDataField*
sdfile_read_data_bin(SDFile *sdfile, GwyDataField **mfield)
{
    static const struct {
        SDFDataType sdftype;
        GwyRawDataType gwytype;
        gdouble bad_data;
    }
    data_types[] = {
        { SDF_UINT8,  GWY_RAW_DATA_SINT8,  NAN,         },
        { SDF_UINT16, GWY_RAW_DATA_UINT16, G_MAXUINT16, },
        { SDF_UINT32, GWY_RAW_DATA_UINT32, G_MAXUINT32, },
        { SDF_FLOAT,  GWY_RAW_DATA_FLOAT,  NAN,         },
        { SDF_SINT8,  GWY_RAW_DATA_SINT8,  NAN,         },
        { SDF_SINT16, GWY_RAW_DATA_SINT16, G_MININT16,  },
        { SDF_SINT32, GWY_RAW_DATA_SINT32, G_MININT32,  },
        { SDF_DOUBLE, GWY_RAW_DATA_DOUBLE, NAN,         },
    };

    GwyDataField *dfield, *mask = NULL;
    gdouble *data, *m;
    guint i, n, idt;

    dfield = gwy_data_field_new(sdfile->xres, sdfile->yres,
                                sdfile->xres * sdfile->xscale,
                                sdfile->yres * sdfile->yscale,
                                FALSE);
    for (idt = 0; idt < G_N_ELEMENTS(data_types); idt++) {
        if (sdfile->data_type == data_types[idt].sdftype)
            break;
    }
    g_return_val_if_fail(idt < G_N_ELEMENTS(data_types), dfield);

    data = gwy_data_field_get_data(dfield);
    n = sdfile->xres * sdfile->yres;
    /* We assume Intel byteorder, although the format does not specify
     * any order.  But it was developed in PC context... */
    gwy_convert_raw_data(sdfile->data, n, 1, data_types[idt].gwytype,
                         GWY_BYTE_ORDER_LITTLE_ENDIAN, data, 1.0, 0.0);
    if (!gwy_isnan(data_types[idt].bad_data)) {
        for (i = 0; i < n; i++) {
            if (data[i] == data_types[idt].bad_data) {
                if (!mask) {
                    mask = gwy_data_field_new_alike(dfield, TRUE);
                    m = gwy_data_field_get_data(mask);
                }
                m[i] = 1.0;
            }
        }
        if (mask) {
            gwy_data_field_grains_invert(mask);
            gwy_app_channel_remove_bad_data(dfield, mask);
        }
    }
    else
        mask = gwy_app_channel_mask_of_nans(dfield, TRUE);

    *mfield = mask;

    return dfield;
}

static GwyDataField*
sdfile_read_data_text(SDFile *sdfile,
                      GError **error)
{
    gint i, n;
    GwyDataField *dfield;
    gdouble *data;
    gchar *p, *end, *line;

    dfield = gwy_data_field_new(sdfile->xres, sdfile->yres,
                                sdfile->xres * sdfile->xscale,
                                sdfile->yres * sdfile->yscale,
                                FALSE);
    data = gwy_data_field_get_data(dfield);
    n = sdfile->xres * sdfile->yres;
    switch (sdfile->data_type) {
        case SDF_UINT8:
        case SDF_SINT8:
        case SDF_UINT16:
        case SDF_SINT16:
        case SDF_UINT32:
        case SDF_SINT32:
        p = sdfile->data;
        for (i = 0; i < n; i++) {
            data[i] = strtol(p, (gchar**)&end, 10);
            if (p == end) {
                g_object_unref(dfield);
                g_set_error(error, GWY_MODULE_FILE_ERROR,
                            GWY_MODULE_FILE_ERROR_DATA,
                            _("End of file reached when reading sample #%d "
                              "of %d"), i, n);
                return NULL;
            }
            p = end;
        }
        break;

        case SDF_FLOAT:
        case SDF_DOUBLE:
        p = sdfile->data;
        for (i = 0; i < n; i++) {
            data[i] = g_ascii_strtod(p, (gchar**)&end);
            if (p == end) {
                g_object_unref(dfield);
                g_set_error(error, GWY_MODULE_FILE_ERROR,
                            GWY_MODULE_FILE_ERROR_DATA,
                            _("End of file reached when reading sample #%d "
                              "of %d"), i, n);
                return NULL;
            }
            p = end;
        }
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }

    /* Find out if there is anything beyond the end-of-data-marker */
    while (*end && *end != '*')
        end++;
    if (!*end) {
        gwy_debug("Missing end-of-data marker `*' was ignored");
        return dfield;
    }

    do {
        end++;
    } while (g_ascii_isspace(*end));
    if (!*end)
        return dfield;

    /* Read the extra stuff */
    end--;
    sdfile->extras = g_hash_table_new(g_str_hash, g_str_equal);
    while ((line = gwy_str_next_line(&end))) {
        g_strstrip(line);
        if (!*line || *line == ';')
            continue;
        for (p = line; g_ascii_isalnum(*p); p++)
            ;
        if (!*p || (*p != '=' && !g_ascii_isspace(*p)))
            continue;
        *p = '\0';
        p++;
        while (*p == '=' || g_ascii_isspace(*p))
            p++;
        if (!*p)
            continue;
        g_strstrip(p);
        gwy_debug("extra: <%s> = <%s>", line, p);
        g_hash_table_insert(sdfile->extras, line, p);
    }

    return dfield;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
