/*
 *  $Id: nanoobserver.c 22001 2019-04-16 13:58:04Z yeti-dn $
 *  Copyright (C) 2012-2017 David Necas (Yeti).
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
 * <mime-type type="application/x-nanoobserver-spm">
 *   <comment>NanoObserver SPM data</comment>
 *   <glob pattern="*.nao"/>
 *   <glob pattern="*.NAO"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Nano-Solution/NanoObserver 1.23
 * # A ZIP archive, we have to look for Scan/Measure.xml as the first file.
 * 0 string PK\x03\x04
 * >30 string Scan/Measure.xml Nano-Solution SPM data version 1.23
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Nano-Solution/NanoObserver 1.33
 * # A ZIP archive, we have to look for NAO_v133.txt as the first file.
 * # This is a dummy file put there exactly for indentification purposes.
 * 0 string PK\x03\x04
 * >30 string NAO_v133.txt Nano-Solution SPM data version 1.33
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Nano-Solution/NanoObserver
 * .nao
 * Read SPS
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"
#include "gwyzip.h"

#define MAGIC "PK\x03\x04"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define MAGIC123_0 "Scan"
#define MAGIC123_1 "Scan/Streams.xml"
#define MAGIC123_2 "Scan/Measure.xml"
#define MAGIC123_3 "Scan/Data"

#define MAGIC133_0 "NAO_v133.txt"
#define MAGIC133_1 "Data/Imaging.xml"
#define MAGIC133_2 "Data/Spectro.xml"

#define BLOODY_UTF8_BOM "\xef\xbb\xbf"
#define EXTENSION ".nao"

typedef struct {
    gchar *name;
    gchar *units;
    gchar *dir;
    /* 1.33 only */
    gchar *filename;
} NAOStream;

typedef struct {
    gchar *dir;
    gchar *name;
    gchar *unit;
    /* What the header says */
    guint capacity;
    guint sizeused;
    /* Actual data */
    guint nvalues;
    gdouble *values;
} NAOSpectrumData;

typedef struct {
    /* Filled from imaging header, if any. */
    gdouble x;
    gdouble y;
    gchar *filename;
    /* Filled by parsing spectrum XML file. */
    GHashTable *hash;
    GArray *specdata;
    gdouble sweep_from;
    gdouble sweep_to;
    gchar *sweep_unit;
    /* Borrowed from main NAOFile. */
    GString *path;
    /* Workspace */
    gchar *current_name;
    gchar *current_unit;
    gchar *spectro_parameters;
    guint current_specdata_id;
} NAOSpectrum;

typedef struct {
    guint xres;
    guint yres;
    gdouble xreal;
    gdouble yreal;
    GArray *streams;
    GArray *spectra;
    GHashTable *hash;
    GwyContainer *meta;
    /* Workspace */
    GString *path;
    gchar *current_name;
    gchar *current_unit;
    gchar *imaging_parameters;
    /* For the log */
    const gchar *filename;
} NAOFile;

static gboolean      module_register             (void);
static gint          nao_detect                  (const GwyFileDetectInfo *fileinfo,
                                                  gboolean only_name);
static gint          nao123_detect               (const GwyFileDetectInfo *fileinfo);
static gint          nao133_detect               (const GwyFileDetectInfo *fileinfo);
static GwyContainer* nao_load                    (const gchar *filename,
                                                  GwyRunType mode,
                                                  GError **error);
static GwyContainer* nao123_load                 (GwyZipFile zipfile,
                                                  NAOFile *naofile,
                                                  GError **error);
static GwyContainer* nao133_imaging_load         (GwyZipFile zipfile,
                                                  NAOFile *naofile,
                                                  GError **error);
static GwyContainer* nao133_spectro_load         (GwyZipFile zipfile,
                                                  NAOFile *naofile,
                                                  GError **error);
static void          add_meta                    (gpointer hkey,
                                                  gpointer hvalue,
                                                  gpointer user_data);
static GwyDataField* nao_read_field              (GwyZipFile zipfile,
                                                  NAOFile *naofile,
                                                  guint id,
                                                  GError **error);
static gboolean      nao_parse_xml_header        (GwyZipFile zipfile,
                                                  NAOFile *naofile,
                                                  const gchar *filename,
                                                  GMarkupParser *parser,
                                                  GError **error);
static gboolean      nao133_parse_spectrum       (GwyZipFile zipfile,
                                                  NAOSpectrum *spectrum,
                                                  GError **error);
static void          create_channel              (NAOFile *naofile,
                                                  GwyDataField *dfield,
                                                  NAOStream *stream,
                                                  guint channelno,
                                                  GwyContainer *container);
static void          create_spectra              (NAOFile *naofile,
                                                  GwyContainer *container);
static GwyDataLine*  create_dataline_for_spectrum(NAOSpectrumData *specdata,
                                                  NAOSpectrum *spectrum);
static void          add_dline_to_spectra        (GPtrArray *sps,
                                                  GwyDataLine *dline,
                                                  const gchar *xtitle,
                                                  const gchar *name,
                                                  const gchar *dir,
                                                  gdouble x,
                                                  gdouble y);
static gboolean      find_size_and_resolution    (NAOFile *naofile,
                                                  GError **error);
static gboolean      find_spectrum_abscissa      (NAOSpectrum *spectrum,
                                                  GError **error);
static const gchar*  find_attribute              (const gchar **attribute_names,
                                                  const gchar **attribute_values,
                                                  const gchar *attrname);
static void          nao_file_free               (NAOFile *naofile);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Reads Nano-Solution/NanoObserver .nao files."),
    "Yeti <yeti@gwyddion.net>",
    "2.1",
    "David Nečas (Yeti)",
    "2012",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_file_func_register("nanoobserver",
                           N_("Nano-Solution/NanoObserver data (.nao)"),
                           (GwyFileDetectFunc)&nao_detect,
                           (GwyFileLoadFunc)&nao_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
nao_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    gint score;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 15 : 0;

    /* Generic ZIP file. */
    if (fileinfo->file_size < MAGIC_SIZE
        || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    /* Format versions. */
    if ((score = nao133_detect(fileinfo)) > 0)
        return score;
    if ((score = nao123_detect(fileinfo)) > 0)
        return score;

    return 0;
}

static gint
nao123_detect(const GwyFileDetectInfo *fileinfo)
{
    GwyZipFile zipfile;
    gint score = 0;

    /* It contains directory Scan so this should be somewehre near the begining
     * of the file. */
    if (gwy_memmem(fileinfo->head, fileinfo->buffer_len,
                   MAGIC123_0, sizeof(MAGIC123_0)-1)) {
        gwy_debug("found magic0 %s", MAGIC123_0);
    }
    else {
        gwy_debug("not found magic0 %s", MAGIC123_0);
        return 0;
    }

    if (gwy_memmem(fileinfo->head, fileinfo->buffer_len,
                   MAGIC123_1, sizeof(MAGIC123_1)-1)) {
        gwy_debug("found magic1 %s", MAGIC123_1);
    }
    else if (gwy_memmem(fileinfo->head, fileinfo->buffer_len,
                        MAGIC123_2, sizeof(MAGIC123_2)-1)) {
        gwy_debug("found magic2 %s", MAGIC123_2);
    }
    else if (gwy_memmem(fileinfo->head, fileinfo->buffer_len,
                        MAGIC123_3, sizeof(MAGIC123_3)-1)) {
        gwy_debug("found magic3 %s", MAGIC123_3);
    }
    else {
        gwy_debug("no magic filename fragment found.");
        return 0;
    }

    /* We have to realy look inside. */
    if (!(zipfile = gwyzip_open(fileinfo->name, NULL)))
        return 0;

    if (gwyzip_locate_file(zipfile, "Scan/Measure.xml", 1, NULL)) {
        gwy_debug("found Scan/Measure.xml in the archive");
        score = 100;
    }
    else {
        gwy_debug("cannot find Scan/Measure.xml in the archive");
    }
    gwyzip_close(zipfile);

    return score;
}

static gint
nao133_detect(const GwyFileDetectInfo *fileinfo)
{
    GwyZipFile zipfile;
    gint score = 0;

    /* It contains directory Scan so this should be somewehre near the begining
     * of the file. */
    if (gwy_memmem(fileinfo->head, fileinfo->buffer_len,
                   MAGIC133_0, sizeof(MAGIC133_0)-1)) {
        gwy_debug("found magic0 %s", MAGIC133_0);
    }
    else {
        gwy_debug("not found magic0 %s", MAGIC133_0);
        return 0;
    }

    if (gwy_memmem(fileinfo->head, fileinfo->buffer_len,
                   MAGIC133_1, sizeof(MAGIC133_1)-1)) {
        gwy_debug("found magic1 %s", MAGIC133_1);
    }
    else if (gwy_memmem(fileinfo->head, fileinfo->buffer_len,
                        MAGIC133_2, sizeof(MAGIC133_2)-1)) {
        gwy_debug("found magic2 %s", MAGIC133_2);
    }
    else {
        gwy_debug("no magic filename fragment found.");
        return 0;
    }

    /* We have to realy look inside. */
    if (!(zipfile = gwyzip_open(fileinfo->name, NULL)))
        return 0;

    if (gwyzip_locate_file(zipfile, "Data/Imaging.xml", 1, NULL)) {
        gwy_debug("found Data/Imaging.xml in the archive");
        score = 100;
    }
    else if (gwyzip_locate_file(zipfile, "Data/Spectro.xml", 1, NULL)) {
        gwy_debug("found Data/Spectro.xml in the archive");
        score = 100;
    }
    else {
        gwy_debug("cannot find neither Data/Imaging.xml nor Data/Spectro.xml "
                  "in the archive");
    }
    gwyzip_close(zipfile);

    return score;
}

static GwyContainer*
nao_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *container = NULL;
    NAOFile naofile;
    GwyZipFile zipfile;

    if (!(zipfile = gwyzip_open(filename, error)))
        return NULL;

    gwy_clear(&naofile, 1);
    naofile.filename = filename;
    if (gwyzip_locate_file(zipfile, "NAO_v133.txt", 1, NULL)) {
        if (gwyzip_locate_file(zipfile, "Data/Imaging.xml", 1, NULL))
            container = nao133_imaging_load(zipfile, &naofile, error);
        else if (gwyzip_locate_file(zipfile, "Data/Spectro.xml", 1, NULL))
            container = nao133_spectro_load(zipfile, &naofile, error);
        else
            err_FILE_TYPE(error, "Nano-Solution");
    }
    else if (gwyzip_locate_file(zipfile, "Scan/Measure.xml", 1, NULL)) {
        container = nao123_load(zipfile, &naofile, error);
    }
    else {
        err_FILE_TYPE(error, "Nano-Solution");
    }

    gwyzip_close(zipfile);
    nao_file_free(&naofile);

    return container;
}

static void
add_meta(gpointer hkey, gpointer hvalue, gpointer user_data)
{
    gwy_container_set_string_by_name(GWY_CONTAINER(user_data),
                                     (gchar*)hkey, g_strdup((gchar*)hvalue));
}

static GwyDataField*
nao_read_field(GwyZipFile zipfile, NAOFile *naofile, guint id, GError **error)
{
    gsize size, expected_size;
    guint width, G_GNUC_UNUSED height, nscanlines, i, j;
    guchar *buffer;
    const guchar *p;
    const gchar *units;
    GwyDataField *field;
    gdouble *data;

    if (!(buffer = gwyzip_get_file_content(zipfile, &size, error)))
        return NULL;

    if (size < 3*4 + 4 + 4) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Data block is truncated"));
        g_free(buffer);
        return NULL;
    }

    p = buffer;
    width = gwy_get_guint32_le(&p);
    height = gwy_get_guint32_le(&p);
    nscanlines = gwy_get_guint32_le(&p);
    gwy_debug("[%u] %u %u %u", id, width, height, nscanlines);

    expected_size = 3*4 + 4*nscanlines*(width + 1);
    if (err_SIZE_MISMATCH(error, expected_size, size, TRUE)) {
        g_free(buffer);
        return NULL;
    }

    field = gwy_data_field_new(width, nscanlines,
                               naofile->xreal,
                               naofile->yreal*nscanlines/naofile->yres,
                               TRUE);
    data = gwy_data_field_get_data(field);

    for (i = 0; i < nscanlines; i++) {
        guint lineno = gwy_get_guint32_le(&p);
        gdouble *d;

        lineno = MIN(lineno, nscanlines-1);
        d = data + (nscanlines-1 - lineno)*width;
        for (j = width; j; j--)
            *(d++) = gwy_get_gfloat_le(&p);
    }

    g_free(buffer);

    /* Older versions of the format had human-readable units there but we
     * disregard these. */
    units = g_array_index(naofile->streams, NAOStream, id).units;
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(field), units);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(field), "m");

    return field;
}

static void
nao123_start_element(G_GNUC_UNUSED GMarkupParseContext *context,
                     const gchar *element_name,
                     const gchar **attribute_names,
                     const gchar **attribute_values,
                     gpointer user_data,
                     G_GNUC_UNUSED GError **error)
{
    NAOFile *naofile = (NAOFile*)user_data;
    gchar *path;

    g_string_append_c(naofile->path, '/');
    g_string_append(naofile->path, element_name);
    path = naofile->path->str;

    if (gwy_strequal(path, "/Measure/Streams/Stream")) {
        const gchar *name, *unit;

        gwy_debug(element_name);
        if ((name = find_attribute(attribute_names, attribute_values, "Id"))
            && (unit = find_attribute(attribute_names, attribute_values,
                                      "Unit"))) {
            NAOStream stream;
            gwy_debug("Adding stream %s [%s]", name, unit);
            gwy_clear(&stream, 1);
            stream.name = g_strdup(name);
            stream.units = g_strdup(unit);
            g_array_append_val(naofile->streams, stream);
        }
    }
}

static void
nao123_end_element(G_GNUC_UNUSED GMarkupParseContext *context,
                   const gchar *element_name,
                   gpointer user_data,
                   G_GNUC_UNUSED GError **error)
{
    NAOFile *naofile = (NAOFile*)user_data;
    guint n = strlen(element_name), len = naofile->path->len;
    gchar *path = naofile->path->str;

    g_return_if_fail(g_str_has_suffix(path, element_name));
    g_return_if_fail(len > n);
    g_return_if_fail(path[len-1 - n] == '/');
    g_string_set_size(naofile->path, len-1 - n);
}

static void
nao123_text(G_GNUC_UNUSED GMarkupParseContext *context,
            const gchar *text,
            gsize text_len,
            gpointer user_data,
            G_GNUC_UNUSED GError **error)
{
    NAOFile *naofile = (NAOFile*)user_data;
    gchar *path = naofile->path->str;

    if (g_str_has_prefix(path, "/Measure/Parameters/")) {
        gchar *name = g_strdup(path + strlen("/Measure/Parameters/"));
        gchar *value = g_strndup(text, text_len);
        g_strdelimit(name, "/", ' ');
        g_strstrip(value);
        if (strlen(value)) {
            if (!naofile->hash) {
                naofile->hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                      g_free, g_free);
            }
            g_hash_table_replace(naofile->hash, name, value);
        }
        else
            g_free(value);
    }
}

static GwyContainer*
nao123_load(GwyZipFile zipfile, NAOFile *naofile, GError **error)
{
    GMarkupParser parser = {
        &nao123_start_element,
        &nao123_end_element,
        &nao123_text,
        NULL,
        NULL,
    };
    GwyContainer *container = NULL;
    gchar *filename_curr;
    guint len, id, channelno = 0;
    gboolean status;

    if (!nao_parse_xml_header(zipfile, naofile, "Scan/Measure.xml", &parser,
                              error))
        goto fail;

    status = gwyzip_first_file(zipfile, error);
    if (!status)
        goto fail;

    container = gwy_container_new();
    while (status && gwyzip_get_current_filename(zipfile, &filename_curr,
                                                 NULL)) {
        if (g_str_has_prefix(filename_curr, "Scan/Data/")) {
            const gchar *dataname = filename_curr + strlen("Scan/Data/");
            NAOStream *stream = NULL;

            gwy_debug("dataname <%s>", dataname);
            for (id = 0; id < naofile->streams->len; id++) {
                stream = &g_array_index(naofile->streams, NAOStream, id);
                len = strlen(stream->name);
                if (strncmp(dataname, stream->name, len) == 0) {
                    if (gwy_strequal(dataname + len, "_Left.dat"))
                        stream->dir = g_strdup("Left");
                    else if (gwy_strequal(dataname + len, "_Right.dat"))
                        stream->dir = g_strdup("Right");
                }
                if (stream->dir)
                    break;
            }
            if (stream->dir) {
                GwyDataField *dfield = nao_read_field(zipfile, naofile, id,
                                                      error);
                if (!dfield)
                    goto fail;
                create_channel(naofile, dfield, stream, channelno, container);
                channelno++;
            }
        }
        g_free(filename_curr);
        status = gwyzip_next_file(zipfile, NULL);
    }

fail:
    if (!container || !gwy_container_get_n_items(container)) {
        GWY_OBJECT_UNREF(container);
        if (error && !*error)
            err_NO_DATA(error);
    }

    return container;
}

static void
nao133_imaging_start_element(G_GNUC_UNUSED GMarkupParseContext *context,
                             const gchar *element_name,
                             const gchar **attribute_names,
                             const gchar **attribute_values,
                             gpointer user_data,
                             G_GNUC_UNUSED GError **error)
{
    NAOFile *naofile = (NAOFile*)user_data;
    gchar *path;
    guint n, i;

    g_string_append_c(naofile->path, '/');
    g_string_append(naofile->path, element_name);
    path = naofile->path->str;

    /* The channels are a mess in 1.33.  They are listed three times but units
     * are only given in the ‘view’ part, not as units of the data files
     * themselves.  Try to piece the information toegether... */
    if (gwy_strequal(path, "/Imaging/ChannelList/ChannelData")) {
        const gchar *s;
        gwy_debug(element_name);
        if ((s = find_attribute(attribute_names, attribute_values, "Name")))
            naofile->current_name = g_strdup(s);
        if ((s = find_attribute(attribute_names, attribute_values, "Unit"))) {
            gwy_debug("Found Unit on ChannelData");
            naofile->current_unit = g_strdup(s);
        }
    }
    else if (gwy_stramong(path,
                          "/Imaging/ChannelList/ChannelData/Left",
                          "/Imaging/ChannelList/ChannelData/Right",
                          NULL)) {
        const gchar *filename;
        gwy_debug(element_name);
        if (naofile->current_name &&
            (filename = find_attribute(attribute_names, attribute_values,
                                       "NaoSubFile"))) {
            NAOStream stream;
            gwy_clear(&stream, 1);
            stream.name = g_strdup(naofile->current_name);
            stream.filename = g_strdelimit(g_strdup(filename), "\\", '/');
            stream.dir = g_strdup(element_name);
            if (naofile->current_unit)
                stream.units = g_strdup(naofile->current_unit);
            g_array_append_val(naofile->streams, stream);
        }
    }
    else if (gwy_strequal(path, "/Imaging/ImagingView/ChannelView")) {
        const gchar *name, *unit;
        gwy_debug(element_name);
        if ((name = find_attribute(attribute_names, attribute_values, "Name"))
            && (unit = find_attribute(attribute_names, attribute_values,
                                      "Unit"))) {
            n = naofile->streams->len;
            for (i = 0; i < n; i++) {
                NAOStream *stream = &g_array_index(naofile->streams, NAOStream,
                                                   i);
                /* Use ChannelView unit only of there is no in ChannelData. */
                if (gwy_strequal(stream->name, name) && !stream->units) {
                    gwy_debug("Found Unit on ChannelView");
                    g_free(stream->units);
                    stream->units = g_strdup(unit);
                }
            }
        }
    }
    /* Spectra.  The GridMode attribute is only informational.  Each spectrum
     * always has X and Y, and that is what we need. */
    else if (gwy_strequal(path, "/Imaging/FlexGrid/Locus")) {
        const gchar *filename, *x, *y;
        gwy_debug(element_name);
        if ((filename = find_attribute(attribute_names, attribute_values,
                                       "NaoSubFile"))
            && (x = find_attribute(attribute_names, attribute_values, "X"))
            && (y = find_attribute(attribute_names, attribute_values, "Y"))) {
            NAOSpectrum spectrum;
            gwy_clear(&spectrum, 1);
            spectrum.filename = g_strdelimit(g_strdup(filename), "\\", '/');
            spectrum.x = atoi(x);
            spectrum.y = atoi(y);
            g_array_append_val(naofile->spectra, spectrum);
        }
    }
}

static void
nao133_imaging_end_element(G_GNUC_UNUSED GMarkupParseContext *context,
                           const gchar *element_name,
                           gpointer user_data,
                           G_GNUC_UNUSED GError **error)
{
    NAOFile *naofile = (NAOFile*)user_data;
    guint n = strlen(element_name), len = naofile->path->len;
    gchar *path = naofile->path->str;

    g_return_if_fail(g_str_has_suffix(path, element_name));
    g_return_if_fail(len > n);
    g_return_if_fail(path[len-1 - n] == '/');
    if (gwy_strequal(path, "/Imaging/ChannelList/ChannelData")) {
        GWY_FREE(naofile->current_name);
        GWY_FREE(naofile->current_unit);
    }
    g_string_set_size(naofile->path, len-1 - n);
}

static void
nao133_imaging_text(G_GNUC_UNUSED GMarkupParseContext *context,
                    const gchar *text,
                    gsize text_len,
                    gpointer user_data,
                    G_GNUC_UNUSED GError **error)
{
    NAOFile *naofile = (NAOFile*)user_data;
    gchar *path = naofile->path->str;

    /* The parameters are a mess in 1.33.  There is a nested plain text header
     * inside a XML tag.  Why on Earth someone did that whey they already had
     * everything represented in XML... */
    if (gwy_strequal(path, "/Imaging/ImagingParameters")) {
        GwyTextHeaderParser parser;
        GHashTable *hash;
        gchar *imaging_parameters = g_strndup(text, text_len);

        gwy_clear(&parser, 1);
        parser.key_value_separator = "=";
        hash = gwy_text_header_parse(imaging_parameters, &parser, NULL, NULL);
        if (naofile->hash && hash) {
            g_warning("Multiple ImagingParameter tags.  Using the last one.");
            g_free(naofile->imaging_parameters);
            g_hash_table_destroy(naofile->hash);
        }
        if (hash) {
            naofile->imaging_parameters = imaging_parameters;
            naofile->hash = hash;
        }
        else
            g_free(imaging_parameters);
    }
}

/* NB: This can be imaging + spectroscopy, just not standalone spectra cuves. */
static GwyContainer*
nao133_imaging_load(GwyZipFile zipfile, NAOFile *naofile, GError **error)
{
    GMarkupParser parser = {
        &nao133_imaging_start_element,
        &nao133_imaging_end_element,
        &nao133_imaging_text,
        NULL,
        NULL,
    };
    GwyContainer *container = NULL;
    GwyDataField *dfield;
    guint id;

    if (!nao_parse_xml_header(zipfile, naofile, "Data/Imaging.xml", &parser,
                              error))
        goto fail;

    container = gwy_container_new();
    for (id = 0; id < naofile->streams->len; id++) {
        NAOStream *stream = &g_array_index(naofile->streams, NAOStream, id);

        if (!gwyzip_locate_file(zipfile, stream->filename, 1, error))
            goto fail;
        if (!(dfield = nao_read_field(zipfile, naofile, id, error)))
            goto fail;

        create_channel(naofile, dfield, stream, id, container);
    }

    for (id = 0; id < naofile->spectra->len; id++) {
        NAOSpectrum *spectrum = &g_array_index(naofile->spectra, NAOSpectrum,
                                               id);
        /* Do not fail when a spectrum curve data are not found.  According to
         * Scientec-CSI a spectrum NaoSubFile only means the spectrum was
         * planned.  But it might not be actually measured. */
        if (!gwyzip_locate_file(zipfile, spectrum->filename, 1, NULL))
            continue;
        spectrum->path = naofile->path;
        if (!nao133_parse_spectrum(zipfile, spectrum, error))
            goto fail;
    }
    create_spectra(naofile, container);

    if (!gwy_container_get_n_items(container)) {
        err_NO_DATA(error);
        goto fail;
    }
    return container;

fail:
    GWY_OBJECT_UNREF(container);
    return NULL;
}

/* The pure spectra case.  We do not know how to recalculate pixel coordinates
 * to real coordinates so we import the spectra as graph curves. */
static GwyContainer*
nao133_spectro_load(GwyZipFile zipfile, NAOFile *naofile, GError **error)
{
    GwyContainer *container = NULL;
    const gchar *xtitle;
    NAOSpectrum spectrum;
    guint j;

    /* Do this for unified cleanup after we are done. */
    naofile->path = g_string_new(NULL);
    naofile->spectra = g_array_new(FALSE, FALSE, sizeof(NAOSpectrum));

    gwy_clear(&spectrum, 1);
    spectrum.path = naofile->path;
    g_array_append_val(naofile->spectra, spectrum);

    if (!nao133_parse_spectrum(zipfile, &spectrum, error))
        goto fail;

    container = gwy_container_new();
    xtitle = g_hash_table_lookup(spectrum.hash, "SweepSignal");

    for (j = 0; j < spectrum.specdata->len; j++) {
        NAOSpectrumData *specdata = &g_array_index(spectrum.specdata,
                                                   NAOSpectrumData, j);
        GwyDataLine *dline = create_dataline_for_spectrum(specdata, &spectrum);
        GwyGraphModel *gmodel = gwy_graph_model_new();
        GwyGraphCurveModel *gcmodel = gwy_graph_curve_model_new();
        gchar *fullname = g_strconcat(specdata->name, " ", specdata->dir, NULL);
        GQuark key;

        gwy_graph_curve_model_set_data_from_dataline(gcmodel, dline, 0, 0);
        gwy_graph_model_set_units_from_data_line(gmodel, dline);
        g_object_unref(dline);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description", fullname,
                     NULL);
        g_free(fullname);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
        g_object_set(gmodel,
                     "title", specdata->name,
                     "axis-label-left", specdata->name,
                     NULL);
        if (xtitle)
            g_object_set(gmodel, "axis-label-bottom", xtitle, NULL);

        key = gwy_app_get_graph_key_for_id(j);
        gwy_container_set_object(container, key, gmodel);
        g_object_unref(gmodel);
    }

fail:
    return container;
}

static gboolean
nao_parse_xml_header(GwyZipFile zipfile, NAOFile *naofile,
                     const gchar *filename, GMarkupParser *parser,
                     GError **error)
{
    GMarkupParseContext *context = NULL;
    guchar *content = NULL, *s;
    gboolean ok = FALSE;

    if (!gwyzip_locate_file(zipfile, filename, 1, error)
        || !(content = gwyzip_get_file_content(zipfile, NULL, error)))
        return FALSE;

    gwy_strkill(content, "\r");
    s = content;
    if (g_str_has_prefix(s, BLOODY_UTF8_BOM))
        s += strlen(BLOODY_UTF8_BOM);

    if (!naofile->path)
        naofile->path = g_string_new(NULL);
    naofile->streams = g_array_new(FALSE, FALSE, sizeof(NAOStream));
    naofile->spectra = g_array_new(FALSE, FALSE, sizeof(NAOSpectrum));

    context = g_markup_parse_context_new(parser, 0, naofile, NULL);
    if (!g_markup_parse_context_parse(context, s, -1, error))
        goto fail;
    if (!g_markup_parse_context_end_parse(context, error))
        goto fail;
    if (!find_size_and_resolution(naofile, error))
        goto fail;
    gwy_debug("nstreams %u", (guint)naofile->streams->len);
    if (!naofile->streams->len) {
        err_NO_DATA(error);
        goto fail;
    }
    ok = TRUE;

    if (g_hash_table_size(naofile->hash)) {
        naofile->meta = gwy_container_new();
        g_hash_table_foreach(naofile->hash, &add_meta, naofile->meta);
    }

fail:
    if (context)
        g_markup_parse_context_free(context);
    g_free(content);

    return ok;
}

static void
nao133_spectro_start_element(G_GNUC_UNUSED GMarkupParseContext *context,
                             const gchar *element_name,
                             const gchar **attribute_names,
                             const gchar **attribute_values,
                             gpointer user_data,
                             G_GNUC_UNUSED GError **error)
{
    NAOSpectrum *spectrum = (NAOSpectrum*)user_data;
    gchar *path;

    g_string_append_c(spectrum->path, '/');
    g_string_append(spectrum->path, element_name);
    path = spectrum->path->str;

    if (gwy_strequal(path, "/Spectroscopy/SpectroData/ChannelData")) {
        const gchar *name, *unit;
        gwy_debug(element_name);
        if ((name = find_attribute(attribute_names, attribute_values, "Name"))
            && (unit = find_attribute(attribute_names, attribute_values,
                                      "Unit"))) {
            spectrum->current_name = g_strdup(name);
            spectrum->current_unit = g_strdup(unit);
        }
    }
    else if (gwy_strequal(path,
                          "/Spectroscopy/SpectroData/ChannelData/PassData")) {
        const gchar *name, *capacity, *sizeused;
        gwy_debug(element_name);
        if ((name = find_attribute(attribute_names, attribute_values, "Name"))
            && (capacity = find_attribute(attribute_names, attribute_values,
                                          "Capacity"))
            && (sizeused = find_attribute(attribute_names, attribute_values,
                                          "SizeUsed"))) {
            NAOSpectrumData specdata;
            gwy_clear(&specdata, 1);
            specdata.capacity = atoi(capacity);
            specdata.sizeused = atoi(sizeused);
            specdata.dir = g_strdup(name);
            specdata.name = g_strdup(spectrum->current_name);
            specdata.unit = g_strdup(spectrum->current_unit);
            spectrum->current_specdata_id = spectrum->specdata->len;
            g_array_append_val(spectrum->specdata, specdata);
        }
    }
}

static void
nao133_spectro_end_element(G_GNUC_UNUSED GMarkupParseContext *context,
                           const gchar *element_name,
                           gpointer user_data,
                           G_GNUC_UNUSED GError **error)
{
    NAOSpectrum *spectrum = (NAOSpectrum*)user_data;
    guint n = strlen(element_name), len = spectrum->path->len;
    gchar *path = spectrum->path->str;

    g_return_if_fail(g_str_has_suffix(path, element_name));
    g_return_if_fail(len > n);
    g_return_if_fail(path[len-1 - n] == '/');
    if (gwy_strequal(path, "/Spectroscopy/SpectroData/ChannelData")) {
        GWY_FREE(spectrum->current_name);
        GWY_FREE(spectrum->current_unit);
    }
    else if (gwy_strequal(path,
                          "/Spectroscopy/SpectroData/ChannelData/PassData")) {
        spectrum->current_specdata_id = G_MAXUINT;
    }
    g_string_set_size(spectrum->path, len-1 - n);
}

static void
nao133_spectro_text(G_GNUC_UNUSED GMarkupParseContext *context,
                    const gchar *text,
                    gsize text_len,
                    gpointer user_data,
                    G_GNUC_UNUSED GError **error)
{
    NAOSpectrum *spectrum = (NAOSpectrum*)user_data;
    gchar *path = spectrum->path->str;

    /* The per-spectrum metadata.  Unfortunately, there is no place in GWY
     * files for them; but we use them to construct the GwySpectra. */
    if (gwy_strequal(path, "/Spectroscopy/SpectroParameters")) {
        GwyTextHeaderParser parser;
        GHashTable *hash;
        gchar *spectro_parameters = g_strndup(text, text_len);

        gwy_clear(&parser, 1);
        parser.key_value_separator = "=";
        hash = gwy_text_header_parse(spectro_parameters, &parser, NULL, NULL);
        if (spectrum->hash && hash) {
            g_warning("Multiple SpectroParameters tags.  Using the last one.");
            g_free(spectrum->spectro_parameters);
            g_hash_table_destroy(spectrum->hash);
        }
        if (hash) {
            spectrum->spectro_parameters = spectro_parameters;
            spectrum->hash = hash;
        }
        else
            g_free(spectro_parameters);
    }
    /* The actual data, represented as a list of plain text data values. */
    else if (gwy_strequal(path,
                          "/Spectroscopy/SpectroData/ChannelData/PassData")
             && spectrum->current_specdata_id != G_MAXUINT) {
        NAOSpectrumData *specdata = &g_array_index(spectrum->specdata,
                                                   NAOSpectrumData,
                                                   spectrum->current_specdata_id);
        gchar *end;
        GArray *values = g_array_new(FALSE, FALSE, sizeof(gdouble));

        while (TRUE) {
            gdouble v = g_ascii_strtod(text, &end);
            if (end == text)
                break;
            g_array_append_val(values, v);
            text = end;
        }
        /* Limit the number of point to SizeUsed. */
        values->len = MIN(values->len, specdata->sizeused);
        if (values->len) {
            specdata->nvalues = values->len;
            specdata->values = (gdouble*)g_array_free(values, FALSE);
        }
        else
            g_array_free(values, TRUE);
    }
}

static gboolean
nao133_parse_spectrum(GwyZipFile zipfile, NAOSpectrum *spectrum, GError **error)
{
    GMarkupParser parser = {
        &nao133_spectro_start_element,
        &nao133_spectro_end_element,
        &nao133_spectro_text,
        NULL,
        NULL,
    };
    GMarkupParseContext *context = NULL;
    guchar *content = NULL, *s;
    gboolean ok = FALSE;

    if (!(content = gwyzip_get_file_content(zipfile, NULL, error)))
        return FALSE;

    gwy_strkill(content, "\r");
    s = content;
    if (g_str_has_prefix(s, BLOODY_UTF8_BOM))
        s += strlen(BLOODY_UTF8_BOM);

    spectrum->specdata = g_array_new(FALSE, FALSE, sizeof(NAOSpectrumData));

    context = g_markup_parse_context_new(&parser, 0, spectrum, NULL);
    if (!g_markup_parse_context_parse(context, s, -1, error))
        goto fail;
    if (!g_markup_parse_context_end_parse(context, error))
        goto fail;
    gwy_debug("nspecdata %u", (guint)spectrum->specdata->len);
    if (!spectrum->specdata->len) {
        /* A bit misleading? */
        err_NO_DATA(error);
        goto fail;
    }
    if (!find_spectrum_abscissa(spectrum, error))
        goto fail;

    ok = TRUE;

fail:
    if (context)
        g_markup_parse_context_free(context);
    g_free(content);

    return ok;
}

static void
create_channel(NAOFile *naofile, GwyDataField *dfield,
               NAOStream *stream, guint channelno, GwyContainer *container)
{
    GwyContainer *meta;
    GQuark key;
    gchar *title;

    key = gwy_app_get_data_key_for_id(channelno);
    gwy_container_set_object(container, key, dfield);
    g_object_unref(dfield);

    key = gwy_app_get_data_title_key_for_id(channelno);
    title = g_strconcat(stream->name, " ", stream->dir, NULL);
    gwy_container_set_string(container, key, title);

    if (naofile->meta) {
        key = gwy_app_get_data_meta_key_for_id(channelno);
        meta = gwy_container_duplicate(naofile->meta);
        gwy_container_set_object(container, key, meta);
        g_object_unref(meta);
    }
    gwy_file_channel_import_log_add(container, channelno,
                                    NULL, naofile->filename);
}

/* We must organise spectra differently than in the file: split them by channel
 * and direction but group different positions to one GwySpectra object. */
static void
create_spectra(NAOFile *naofile, GwyContainer *container)
{
    GwyDataField *dfield = NULL;
    GPtrArray *sps;
    guint i, j, id;

    if (!naofile->spectra || !naofile->spectra->len)
        return;

    if (!gwy_container_gis_object(container, gwy_app_get_data_key_for_id(0),
                                  (GObject**)&dfield)) {
        g_warning("Cannot convert spectra pixel coordinates to real "
                  "because there is no image.");
    }

    sps = g_ptr_array_new();
    for (i = 0; i < naofile->spectra->len; i++) {
        NAOSpectrum *spectrum = &g_array_index(naofile->spectra, NAOSpectrum,
                                               i);
        const gchar *xtitle;

        /* Spectrum planned, but not measured. */
        if (!spectrum->specdata)
            continue;

        if (dfield) {
            spectrum->x = gwy_data_field_jtor(dfield, spectrum->x + 0.5);
            spectrum->y = gwy_data_field_itor(dfield,
                                              dfield->yres - 0.5 - spectrum->y);
        }

        xtitle = g_hash_table_lookup(spectrum->hash, "SweepSignal");
        for (j = 0; j < spectrum->specdata->len; j++) {
            NAOSpectrumData *specdata = &g_array_index(spectrum->specdata,
                                                       NAOSpectrumData, j);
            GwyDataLine *dline = create_dataline_for_spectrum(specdata,
                                                              spectrum);
            add_dline_to_spectra(sps, dline,
                                 xtitle, specdata->name, specdata->dir,
                                 spectrum->x, spectrum->y);
        }
    }

    for (id = 0; id < sps->len; id++) {
        GwySpectra *spectra = (GwySpectra*)g_ptr_array_index(sps, id);
        GQuark key = gwy_app_get_spectra_key_for_id(id);
        gwy_container_set_object(container, key, spectra);
        g_object_unref(spectra);
    }
    g_ptr_array_free(sps, TRUE);
}

static GwyDataLine*
create_dataline_for_spectrum(NAOSpectrumData *specdata, NAOSpectrum *spectrum)
{
    GwyDataLine *dline;
    gdouble real = spectrum->sweep_to - spectrum->sweep_from;

    if (!(fabs(real) > 0.0)) {
        g_warning("Spectrum sweep range is zero, fixing to 1.0");
        real = 1.0;
    }

    dline = gwy_data_line_new(specdata->nvalues, fabs(real), FALSE);
    gwy_assign(gwy_data_line_get_data(dline), specdata->values,
               specdata->nvalues);

    if (real > 0.0)
        gwy_data_line_set_offset(dline, spectrum->sweep_from);
    else {
        gwy_data_line_invert(dline, TRUE, FALSE);
        gwy_data_line_set_offset(dline, spectrum->sweep_to);
    }

    gwy_si_unit_set_from_string(gwy_data_line_get_si_unit_x(dline),
                                spectrum->sweep_unit);
    gwy_si_unit_set_from_string(gwy_data_line_get_si_unit_y(dline),
                                specdata->unit);

    return dline;
}

static void
add_dline_to_spectra(GPtrArray *sps, GwyDataLine *dline,
                     const gchar *xtitle, const gchar *name, const gchar *dir,
                     gdouble x, gdouble y)
{
    gchar *fullname = g_strconcat(name, " ", dir, NULL);
    const gchar *specxlabel;
    GwyDataLine *firstspec;
    GwySpectra *spectra = NULL;
    guint i;

    gwy_debug("looking for <%s><%s><%s>", fullname, name, xtitle);
    /* Find spectra set that matches all the attributes. */
    for (i = 0; i < sps->len; i++) {
        spectra = (GwySpectra*)g_ptr_array_index(sps, i);
        firstspec = gwy_spectra_get_spectrum(spectra, 0);
        specxlabel = gwy_spectra_get_spectrum_x_label(spectra);
        if (gwy_strequal(gwy_spectra_get_title(spectra), fullname)
            && gwy_strequal(gwy_spectra_get_spectrum_y_label(spectra), name)
            && ((!specxlabel && !xtitle)
                || (specxlabel && xtitle && gwy_strequal(specxlabel, xtitle)))
            && gwy_si_unit_equal(gwy_data_line_get_si_unit_x(dline),
                                 gwy_data_line_get_si_unit_x(firstspec))
            && gwy_si_unit_equal(gwy_data_line_get_si_unit_y(dline),
                                 gwy_data_line_get_si_unit_y(firstspec)))
            break;
        spectra = NULL;
    }

    if (!spectra) {
        spectra = gwy_spectra_new();
        gwy_spectra_set_title(spectra, fullname);
        gwy_spectra_set_spectrum_y_label(spectra, name);
        if (xtitle)
            gwy_spectra_set_spectrum_x_label(spectra, xtitle);
        gwy_si_unit_set_from_string(gwy_spectra_get_si_unit_xy(spectra), "m");
        g_ptr_array_add(sps, spectra);
    }

    gwy_spectra_add_spectrum(spectra, dline, x, y);
    g_object_unref(dline);
    g_free(fullname);
}

static gboolean
find_size_and_resolution(NAOFile *naofile, GError **error)
{
    GHashTable *hash = naofile->hash;
    const gchar *value;

    /* Ensure the parameter hash table exists.  It may not if there is no
     * parameters part or (in 1.23) is empty. */
    if (!hash) {
        err_MISSING_FIELD(error, "Resolution");
        return FALSE;
    }

    if ((value = g_hash_table_lookup(hash, "Resolution"))) {
        if (sscanf(value, "%u, %u", &naofile->xres, &naofile->yres) != 2) {
            err_INVALID(error, "Resolution");
            return FALSE;
        }
        if (err_DIMENSION(error, naofile->xres)
            || err_DIMENSION(error, naofile->yres))
            return FALSE;
    }
    else {
        err_MISSING_FIELD(error, "Resolution");
        return FALSE;
    }
    gwy_debug("xres %u, yres %u", naofile->xres, naofile->yres);

    if ((value = g_hash_table_lookup(hash, "Size"))) {
        gchar *end, *s = g_strdup(value);
        if ((naofile->xreal = g_ascii_strtod(s, &end)) > 0.0
            && *end == ','
            && (naofile->yreal = g_ascii_strtod(end+1, NULL)) > 0.0) {
            /* OK. */
        }
        else {
            g_free(s);
            err_INVALID(error, "Size");
            return FALSE;
        }
        g_free(s);
    }
    else {
        err_MISSING_FIELD(error, "Size");
        return FALSE;
    }
    gwy_debug("xreal %g, yreal %g", naofile->xreal, naofile->yreal);

    return TRUE;
}

static gboolean
find_spectrum_abscissa(NAOSpectrum *spectrum, GError **error)
{
    GHashTable *hash = spectrum->hash;
    const gchar *value;

    /* Ensure the parameter hash table exists. */
    if (!hash) {
        err_MISSING_FIELD(error, "SweepFromValue");
        return FALSE;
    }

    if ((value = g_hash_table_lookup(hash, "SweepFromValue"))) {
        spectrum->sweep_from = g_ascii_strtod(value, NULL);
    }
    else {
        err_MISSING_FIELD(error, "SweepFromValue");
        return FALSE;
    }

    if ((value = g_hash_table_lookup(hash, "SweepToValue"))) {
        spectrum->sweep_to = g_ascii_strtod(value, NULL);
    }
    else {
        err_MISSING_FIELD(error, "SweepToValue");
        return FALSE;
    }
    gwy_debug("sweep from %g to %g", spectrum->sweep_from, spectrum->sweep_to);

    if ((value = g_hash_table_lookup(hash, "SweepSignalUnitName"))
        || (value = g_hash_table_lookup(hash, "SweepSignalUnitSymbol"))) {
        spectrum->sweep_unit = g_strdup(value);
    }
    else {
        err_MISSING_FIELD(error, "SweepSignalUnitName");
        return FALSE;
    }

    return TRUE;
}

static const gchar*
find_attribute(const gchar **attribute_names, const gchar **attribute_values,
               const gchar *attrname)
{
    guint i;

    if (!attribute_names)
        return NULL;

    for (i = 0; attribute_names[i]; i++) {
        if (gwy_strequal(attribute_names[i], attrname))
            return attribute_values[i];
    }

    return NULL;
}

static void
nao_file_free(NAOFile *naofile)
{
    guint i, j;

    if (naofile->streams) {
        for (i = 0; i < naofile->streams->len; i++) {
            NAOStream *stream = &g_array_index(naofile->streams, NAOStream, i);
            g_free(stream->name);
            g_free(stream->units);
            g_free(stream->filename);
            g_free(stream->dir);
        }
        g_array_free(naofile->streams, TRUE);
    }
    if (naofile->spectra) {
        GArray *spectra = naofile->spectra;
        for (i = 0; i < spectra->len; i++) {
            NAOSpectrum *spectrum = &g_array_index(spectra, NAOSpectrum, i);
            if (spectrum->specdata) {
                GArray *specdatas = spectrum->specdata;
                for (j = 0; j < specdatas->len; j++) {
                    NAOSpectrumData *specdata = &g_array_index(specdatas,
                                                               NAOSpectrumData,
                                                               j);
                    g_free(specdata->dir);
                    g_free(specdata->name);
                    g_free(specdata->unit);
                    g_free(specdata->values);
                }
                g_array_free(specdatas, TRUE);
            }
            if (spectrum->hash)
                g_hash_table_destroy(spectrum->hash);
            g_free(spectrum->sweep_unit);
            g_free(spectrum->spectro_parameters);
            g_free(spectrum->current_name);
            g_free(spectrum->current_unit);
            g_free(spectrum->filename);
        }
        g_array_free(naofile->spectra, TRUE);
    }
    if (naofile->hash) {
        g_hash_table_destroy(naofile->hash);
        naofile->hash = NULL;
    }
    if (naofile->path) {
        g_string_free(naofile->path, TRUE);
        naofile->path = NULL;
    }
    g_free(naofile->current_name);
    g_free(naofile->imaging_parameters);
    GWY_OBJECT_UNREF(naofile->meta);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
