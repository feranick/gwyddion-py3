/*
 *  $Id: nrrdfile.c 22562 2019-10-10 14:45:41Z yeti-dn $
 *  Copyright (C) 2011-2019 David Necas (Yeti).
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
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-nearly-raw-raster-data">
 *   <comment>Nearly raw raster data (NRRD)</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="NRRD000"/>
 *   </magic>
 *   <glob pattern="*.nrrd"/>
 *   <glob pattern="*.NRRD"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Nearly raw raster data
 * # Metaformat, see http://teem.sourceforge.net/nrrd/format.html
 * 0 string NRRD000 Nearly Raw Raster Data
 * >&0 regex [0-9]+ version %s
 * >&0 search/200 data\ file:\x20 text header for
 * >>&0 regex [^\x0d\x0a]+ %s
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Nearly raw raster data (NRRD)
 * .nrrd
 * Read[1] Export[2] Volume
 * [1] Not all variants are implemented.
 * [2] Data are exported in a fixed attached native-endian float point format.
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#ifdef HAVE_BZIP2
#include <bzlib.h>
#endif

#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/datafield.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "get.h"

#define MAGIC "NRRD000"
#define MAGIC_SIZE (sizeof(MAGIC) - 1)

#define EXTENSION ".nrrd"

typedef enum {
    NRRD_TYPE_UNKNOWN = -1,
    /* Importable types start from 0 */
    NRRD_TYPE_SINT8,
    NRRD_TYPE_UINT8,
    NRRD_TYPE_SINT16,
    NRRD_TYPE_UINT16,
    NRRD_TYPE_SINT32,
    NRRD_TYPE_UINT32,
    NRRD_TYPE_SINT64,
    NRRD_TYPE_UINT64,
    NRRD_TYPE_FLOAT,
    NRRD_TYPE_DOUBLE,
    /* Non-importable types */
    NRRD_TYPE_BLOCK,
} NRRDDataType;

typedef enum {
    NRRD_ENCODING_UNKNOWN = -1,
    NRRD_ENCODING_RAW,
    NRRD_ENCODING_TEXT,
    NRRD_ENCODING_HEX,
    NRRD_ENCODING_GZIP,
    NRRD_ENCODING_BZIP2,
} NRRDEncoding;

static gboolean      module_register     (void);
static gint          nrrdfile_detect     (const GwyFileDetectInfo *fileinfo,
                                          gboolean only_name);
static GwyContainer* nrrdfile_load       (const gchar *filename,
                                          GwyRunType mode,
                                          GError **error);
static gboolean      nrrdfile_export     (GwyContainer *data,
                                          const gchar *filename,
                                          GwyRunType mode,
                                          GError **error);
static void          normalise_field_name(gchar *name);
static void          unescape_field_value(gchar *value);
static NRRDDataType  parse_data_type     (const gchar *value);
static NRRDEncoding  parse_encoding      (const gchar *value);
static gboolean      find_gwy_data_type  (NRRDDataType datatype,
                                          NRRDEncoding encoding,
                                          const gchar *endian,
                                          GwyRawDataType *rawdatatype,
                                          GwyByteOrder *byteorder,
                                          GError **error);
static guchar*       load_detached_file  (const gchar *datafile,
                                          gsize *size,
                                          gboolean gzcompressed,
                                          gboolean bz2compressed,
                                          GError **error);
static guint         pick_channel_axis   (guint dimension,
                                          const guint *sizes,
                                          gchar **kinds);
static gconstpointer get_raw_data_pointer(gconstpointer base,
                                          gsize *size,
                                          gsize nitems,
                                          GwyRawDataType datatype,
                                          GwyByteOrder *byteorder,
                                          NRRDEncoding encoding,
                                          gssize lineskip,
                                          gssize byteskip,
                                          GSList **buffers_to_free,
                                          GError **error);
static GwyDataField* read_raw_data_field (guint xres,
                                          guint yres,
                                          gint stride,
                                          guint rowstride,
                                          GwyRawDataType rawdatatype,
                                          GwyByteOrder byteorder,
                                          GHashTable *fields,
                                          const guchar *data);
static GwyBrick*     read_raw_brick      (guint xres,
                                          guint yres,
                                          guint zres,
                                          GwyRawDataType rawdatatype,
                                          GwyByteOrder byteorder,
                                          GHashTable *fields,
                                          const guchar *data);
static GwyContainer* nrrd_make_meta      (GHashTable *keyvalue);
static gboolean      parse_uint_vector   (const gchar *value,
                                          guint n,
                                          ...);
static gboolean      parse_float_vector  (const gchar *value,
                                          guint n,
                                          ...);
static gboolean      parse_string_vector (const gchar *value,
                                          guint n,
                                          ...);
static gchar**       split_per_axis_field(const gchar *value,
                                          const gchar *name,
                                          guint nitems,
                                          gboolean quoted,
                                          GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports and exports nearly raw raster data (NRRD) files."),
    "Yeti <yeti@gwyddion.net>",
    "0.8",
    "David Nečas (Yeti)",
    "2011",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_file_func_register("nrrdfile",
                           N_("Nearly raw raster data (NRRD) files (.nrrd)"),
                           (GwyFileDetectFunc)&nrrdfile_detect,
                           (GwyFileLoadFunc)&nrrdfile_load,
                           NULL,
                           (GwyFileSaveFunc)&nrrdfile_export);

    return TRUE;
}

static gint
nrrdfile_detect(const GwyFileDetectInfo *fileinfo,
                gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;

    // Weed out files that do not pretend to be Windows bitmaps quickly.
    if (fileinfo->buffer_len >= MAGIC_SIZE + 3
        && memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) == 0
        && g_ascii_isdigit(fileinfo->head[MAGIC_SIZE])
        && (fileinfo->head[MAGIC_SIZE+1] == '\n'
            || (fileinfo->head[MAGIC_SIZE+1] == '\r'
                && fileinfo->head[MAGIC_SIZE+2] == '\n')))
        return 100;

    return 0;
}

static inline gboolean
nrrd_encoding_is_compressed(NRRDEncoding encoding)
{
    return encoding == NRRD_ENCODING_GZIP || encoding == NRRD_ENCODING_BZIP2;
}

static GwyContainer*
nrrdfile_load(const gchar *filename,
              G_GNUC_UNUSED GwyRunType mode,
              GError **error)
{
    GwyContainer *meta = NULL, *container = NULL;
    GHashTable *hash, *fields = NULL, *keyvalue = NULL;
    GwyDataField *dfield = NULL;
    GSList *l, *buffers_to_free = NULL;
    guchar *buffer = NULL, *data_buffer = NULL, *header_end;
    const guchar *data_start;
    gsize data_size, size = 0;
    GError *err = NULL;
    guint sizes[3] = { 1, 1, 1 };
    G_GNUC_UNUSED guint version;
    guint dimension, xres = 0, yres = 0, zres = 0, header_size, i,
          stride = 0, rowstride = 0, fieldstride = 0, chanaxis, xaxis, yaxis,
          nchannels = 0;
    gchar *value, *vkeyvalue, *vfield, *p, *line, *datafile = NULL;
    gchar **kinds = NULL;
    gboolean unix_eol, detached_header = FALSE;
    guchar first_byte = 0;
    gssize byteskip = 0, lineskip = 0;
    NRRDDataType data_type;
    NRRDEncoding encoding;
    GwyRawDataType rawdatatype;
    GwyByteOrder byteorder;
    GQuark quark;

    if (!g_file_get_contents(filename, (gchar**)&buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    buffers_to_free = g_slist_append(buffers_to_free, buffer);

    if (size < MAGIC_SIZE + 3) {
        err_TOO_SHORT(error);
        goto fail;
    }
    if (memcmp(buffer, MAGIC, MAGIC_SIZE) != 0
        || !g_ascii_isdigit(buffer[MAGIC_SIZE])
        || (buffer[MAGIC_SIZE+1] != '\n'
            && (buffer[MAGIC_SIZE+1] != '\r'
                || buffer[MAGIC_SIZE+2] != '\n'))) {
        err_FILE_TYPE(error, "NRRD");
        goto fail;
    }

    version = buffer[MAGIC_SIZE] - '0';
    unix_eol = (buffer[MAGIC_SIZE+1] == '\n');
    header_end = gwy_memmem(buffer, size,
                            unix_eol ? "\n\n" : "\r\n\r\n",
                            unix_eol ? 2 : 4);
    if (header_end) {
        header_size = header_end - buffer + (unix_eol ? 2 : 4);
        first_byte = buffer[header_size];
        buffer[header_size] = '\0';
    }
    else {
        header_size = size;
        detached_header = TRUE;
    }

    fields = g_hash_table_new(g_str_hash, g_str_equal);
    keyvalue = g_hash_table_new(g_str_hash, g_str_equal);
    p = buffer + (MAGIC_SIZE + 2 + !unix_eol);
    for (line = gwy_str_next_line(&p); line; line = gwy_str_next_line(&p)) {
        g_strstrip(line);
        if (!line[0])
            break;
        if (line[0] == '#')
            continue;

        /* Fields and key-values are almost the same for us.  But do not put
         * them to one hash table as we do not want key-values to override
         * fields. */
        vfield = strstr(line, ": ");
        vkeyvalue = strstr(line, ":=");
        if (vfield) {
            hash = (vkeyvalue && vkeyvalue < vfield) ? keyvalue : fields;
            value = (vkeyvalue && vkeyvalue < vfield) ? vkeyvalue : vfield;
        }
        else {
            hash = vkeyvalue ? keyvalue : NULL;
            value = vkeyvalue ? vkeyvalue : NULL;
        }
        if (!hash) {
            g_warning("Neither field nor key-value separator found on line: %s",
                      line);
            continue;
        }

        *value = '\0';
        value += 2;
        g_strchomp(line);
        g_strchug(value);
        if (hash == fields)
            normalise_field_name(line);
        unescape_field_value(value);
        gwy_debug("<%s> = <%s> (%s)", line, value, hash == fields ? "F" : "KV");
        g_hash_table_insert(hash, line, value);
    }
    if (!detached_header)
        buffer[header_size] = first_byte;

    datafile = g_hash_table_lookup(fields, "datafile");
    if (detached_header && !datafile) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Detached header does not refer to any data file."));
        goto fail;
    }

    if (!require_keys(fields, error,
                      "dimension", "encoding", "sizes", "type", NULL))
        goto fail;

    if ((data_type = parse_data_type(g_hash_table_lookup(fields, "type")))
        == (NRRDDataType)-1) {
        err_UNSUPPORTED(error, "type");
        goto fail;
    }
    if ((encoding = parse_encoding(g_hash_table_lookup(fields, "encoding")))
        == (NRRDEncoding)-1) {
        err_UNSUPPORTED(error, "encoding");
        goto fail;
    }
    gwy_debug("data_type: %u, encoding: %u", data_type, encoding);

    /* TODO: Read 1D data as a graph. */
    if (sscanf(g_hash_table_lookup(fields, "dimension"), "%u",
               &dimension) != 1) {
        err_INVALID(error, "dimension");
        goto fail;
    }
    if (dimension != 2 && dimension != 3) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Only two- and three-dimensional data are supported."));
        goto fail;
    }
    if (!parse_uint_vector(g_hash_table_lookup(fields, "sizes"), dimension,
                           &sizes[0], &sizes[1], &sizes[2])) {
        err_INVALID(error, "sizes");
        goto fail;
    }
    gwy_debug("sizes: %u, %u, %u", sizes[0], sizes[1], sizes[2]);
    if (err_DIMENSION(error, sizes[0]) || err_DIMENSION(error, sizes[1])
        || (dimension == 3 && err_DIMENSION(error, sizes[2])))
        goto fail;

    if ((value = g_hash_table_lookup(fields, "lineskip")))
        lineskip = atol(value);
    if ((value = g_hash_table_lookup(fields, "byteskip")))
        byteskip = atol(value);

    if (lineskip && nrrd_encoding_is_compressed(encoding)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Non-zero lineskip is supported only for uncompressed "
                      "files."));
        goto fail;
    }

    if (!find_gwy_data_type(data_type, encoding,
                            g_hash_table_lookup(fields, "endian"),
                            &rawdatatype, &byteorder, error))
        goto fail;

    if ((value = g_hash_table_lookup(fields, "kinds")))
        kinds = split_per_axis_field(value, "kinds", dimension, TRUE, NULL);

    if (datafile) {
        gchar *datafilepath;

        if (g_path_is_absolute(datafile))
            datafilepath = g_strdup(datafile);
        else {
            gchar *dirname = g_path_get_dirname(filename);
            datafilepath = g_build_filename(dirname, datafile, NULL);
            g_free(dirname);
        }

        data_buffer = load_detached_file(datafilepath, &data_size,
                                         encoding == NRRD_ENCODING_GZIP,
                                         encoding == NRRD_ENCODING_BZIP2,
                                         error);
        g_free(datafilepath);
        if (!data_buffer)
            goto fail;

        buffers_to_free = g_slist_append(buffers_to_free, data_buffer);
        if (nrrd_encoding_is_compressed(encoding))
            encoding = NRRD_ENCODING_RAW;
    }
    else {
        if (nrrd_encoding_is_compressed(encoding)) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Compression is supported only for detached files."));
            goto fail;
        }
        data_buffer = buffer + header_size;
        data_size = size - header_size;
    }

    chanaxis = pick_channel_axis(dimension, sizes, kinds);
    if (chanaxis == G_MAXUINT) {
        gwy_debug("after much deliberation, we decided it's volume data");
        xres = sizes[0];
        yres = sizes[1];
        zres = sizes[2];
    }
    else {
        gwy_debug("we picked %u as the channel axis", chanaxis);
        nchannels = sizes[chanaxis];
        if (chanaxis == 0) {
            xaxis = 1;
            yaxis = 2;
            stride = nchannels;
            rowstride = nchannels*sizes[xaxis];
            fieldstride = 1;
        }
        else if (chanaxis == 1) {
            xaxis = 0;
            yaxis = 2;
            stride = 1;
            rowstride = nchannels*sizes[xaxis];
            fieldstride = sizes[xaxis];
        }
        else if (chanaxis == 2) {
            xaxis = 0;
            yaxis = 1;
            stride = 1;
            rowstride = sizes[xaxis];
            fieldstride = sizes[xaxis]*sizes[yaxis];
        }
        else {
            g_assert_not_reached();
        }
        xres = sizes[xaxis];
        yres = sizes[yaxis];
        gwy_debug("xres: %u, yres: %u, nchannels: %u", xres, yres, nchannels);
        gwy_debug("stride: %u, rowstride: %u, fieldstride: %u",
                  stride, rowstride, fieldstride);
    }

    if (!(data_start = get_raw_data_pointer(data_buffer, &data_size,
                                            sizes[0]*sizes[1]*sizes[2],
                                            rawdatatype, &byteorder, encoding,
                                            lineskip, byteskip,
                                            &buffers_to_free,
                                            error)))
        goto fail;

    container = gwy_container_new();
    meta = nrrd_make_meta(keyvalue);

    if (chanaxis == G_MAXUINT) {
        GwyBrick *brick = read_raw_brick(xres, yres, zres,
                                         rawdatatype, byteorder,
                                         fields, data_start);
        quark = gwy_app_get_brick_key_for_id(0);
        gwy_container_set_object(container, quark, brick);
        g_object_unref(brick);

        if ((value = g_hash_table_lookup(fields, "content"))) {
            gwy_container_set_const_string_by_name(container, "/brick/0/title",
                                                   value);
        }

        if (meta)
            gwy_container_set_object_by_name(container, "/brick/0/meta", meta);
    }
    else {
        for (i = 0; i < nchannels; i++) {
            const guchar *chandata;
            chandata = (data_start
                        + i*fieldstride*gwy_raw_data_size(rawdatatype));
            dfield = read_raw_data_field(xres, yres,
                                         stride, rowstride,
                                         rawdatatype, byteorder,
                                         fields, chandata);
            quark = gwy_app_get_data_key_for_id(i);
            gwy_container_set_object(container, quark, dfield);
            g_object_unref(dfield);

            if ((value = g_hash_table_lookup(fields, "content"))) {
                gchar *key = g_strconcat(g_quark_to_string(quark), "/title",
                                         NULL);
                gwy_container_set_string_by_name(container, key,
                                                 g_strdup(value));
                g_free(key);
            }
            if (meta) {
                gchar *key = g_strdup(g_quark_to_string(quark));
                GwyContainer *chanmeta = gwy_container_duplicate(meta);

                g_assert(g_str_has_suffix(key, "/data"));
                strcpy(key + strlen(key)-4, "meta");
                gwy_container_set_object_by_name(container, key, chanmeta);
                g_object_unref(chanmeta);
                g_free(key);
            }
            gwy_file_channel_import_log_add(container, i, NULL, filename);
        }
    }

fail:
    GWY_OBJECT_UNREF(meta);
    for (l = buffers_to_free; l; l = g_slist_next(l))
        g_free(l->data);
    g_slist_free(buffers_to_free);
    g_strfreev(kinds);
    if (fields)
        g_hash_table_destroy(fields);
    if (keyvalue)
        g_hash_table_destroy(keyvalue);

    return container;
}

static void
normalise_field_name(gchar *name)
{
    guint i, j;

    /* Get rid of non-alnum characters, e.g. "sample units" → "sampleunits"
     * and convert alphabetic characters to lowercase. */
    for (i = j = 0; name[i]; i++) {
        if (g_ascii_isalnum(name[i])) {
            name[j++] = g_ascii_tolower(name[i]);
        }
    }
    name[j] = '\0';

    if (gwy_strequal(name, "centerings"))
        strcpy(name, "centers");
}

static void
unescape_field_value(gchar *value)
{
    guint i, j;

    if (!strchr(value, '\\'))
        return;

    for (i = j = 0; value[i]; i++, j++) {
        if (value[i] == '\\') {
            if (value[i+1] == '\\') {
                value[j] = '\\';
                i++;
            }
            else if (value[i+1] == 'n') {
                value[j] = '\n';
                i++;
            }
            else {
                g_warning("Undefined escape sequence \\%c found.", value[i+1]);
            }
        }
        value[j] = value[i];
    }
    value[j] = '\0';
}

static NRRDDataType
parse_data_type(const gchar *value)
{
    GwyEnum data_types[] = {
        { "signed char",            NRRD_TYPE_SINT8,  },
        { "int8",                   NRRD_TYPE_SINT8,  },
        { "int8_t",                 NRRD_TYPE_SINT8,  },
        { "unsigned char",          NRRD_TYPE_UINT8,  },
        { "uchar",                  NRRD_TYPE_UINT8,  },
        { "uint8",                  NRRD_TYPE_UINT8,  },
        { "uint8_t",                NRRD_TYPE_UINT8,  },
        { "short",                  NRRD_TYPE_SINT16, },
        { "short int",              NRRD_TYPE_SINT16, },
        { "signed short",           NRRD_TYPE_SINT16, },
        { "signed short int",       NRRD_TYPE_SINT16, },
        { "int16",                  NRRD_TYPE_SINT16, },
        { "int16_t",                NRRD_TYPE_SINT16, },
        { "ushort",                 NRRD_TYPE_UINT16, },
        { "unsigned short",         NRRD_TYPE_UINT16, },
        { "unsigned short int",     NRRD_TYPE_UINT16, },
        { "uint16",                 NRRD_TYPE_UINT16, },
        { "uint16_t",               NRRD_TYPE_UINT16, },
        { "int",                    NRRD_TYPE_SINT32, },
        { "signed int",             NRRD_TYPE_SINT32, },
        { "int32",                  NRRD_TYPE_SINT32, },
        { "int32_t",                NRRD_TYPE_SINT32, },
        { "uint",                   NRRD_TYPE_UINT32, },
        { "unsigned int",           NRRD_TYPE_UINT32, },
        { "uint32",                 NRRD_TYPE_UINT32, },
        { "uint32_t",               NRRD_TYPE_UINT32, },
        { "longlong",               NRRD_TYPE_SINT64, },
        { "long long",              NRRD_TYPE_SINT64, },
        { "long long int",          NRRD_TYPE_SINT64, },
        { "signed long long",       NRRD_TYPE_SINT64, },
        { "signed long long int",   NRRD_TYPE_SINT64, },
        { "int64",                  NRRD_TYPE_SINT64, },
        { "int64_t",                NRRD_TYPE_SINT64, },
        { "ulonglong",              NRRD_TYPE_UINT64, },
        { "unsigned long long",     NRRD_TYPE_UINT64, },
        { "unsigned long long int", NRRD_TYPE_UINT64, },
        { "uint64",                 NRRD_TYPE_UINT64, },
        { "uint64_t",               NRRD_TYPE_UINT64, },
        { "float",                  NRRD_TYPE_FLOAT,  },
        { "double",                 NRRD_TYPE_DOUBLE, },
        { "block",                  NRRD_TYPE_BLOCK,  },
    };

    gchar *s;
    NRRDDataType data_type;

    if (!value)
        return NRRD_TYPE_UNKNOWN;

    s = g_ascii_strdown(value, -1);
    data_type = gwy_string_to_enum(s, data_types, G_N_ELEMENTS(data_types));
    g_free(s);

    return data_type;
}

static NRRDEncoding
parse_encoding(const gchar *value)
{
    GwyEnum encodings[] = {
        { "raw",   NRRD_ENCODING_RAW,   },
        { "text",  NRRD_ENCODING_TEXT,  },
        { "txt",   NRRD_ENCODING_TEXT,  },
        { "ascii", NRRD_ENCODING_TEXT,  },
        { "hex",   NRRD_ENCODING_HEX,   },
        { "gzip",  NRRD_ENCODING_GZIP,  },
        { "gz",    NRRD_ENCODING_GZIP,  },
        { "bzip2", NRRD_ENCODING_BZIP2, },
        { "bz2",   NRRD_ENCODING_BZIP2, },
    };

    NRRDEncoding encoding;
    gchar *s;

    if (!value)
        return NRRD_ENCODING_UNKNOWN;

    s = g_ascii_strdown(value, -1);
    encoding = gwy_string_to_enum(s, encodings, G_N_ELEMENTS(encodings));
    g_free(s);

    return encoding;
}

static gboolean
find_gwy_data_type(NRRDDataType datatype, NRRDEncoding encoding,
                   const gchar *endian,
                   GwyRawDataType *rawdatatype, GwyByteOrder *byteorder,
                   GError **error)
{
    static const GwyRawDataType types[] = {
        GWY_RAW_DATA_SINT8,
        GWY_RAW_DATA_UINT8,
        GWY_RAW_DATA_SINT16,
        GWY_RAW_DATA_UINT16,
        GWY_RAW_DATA_SINT32,
        GWY_RAW_DATA_UINT32,
        GWY_RAW_DATA_SINT64,
        GWY_RAW_DATA_UINT64,
        GWY_RAW_DATA_FLOAT,
        GWY_RAW_DATA_DOUBLE,
    };

    if (datatype > NRRD_TYPE_DOUBLE) {
        err_UNSUPPORTED(error, "type");
        return FALSE;
    }

    *rawdatatype = types[datatype];
    *byteorder = GWY_BYTE_ORDER_NATIVE;
    if (encoding != NRRD_ENCODING_TEXT && gwy_raw_data_size(*rawdatatype) > 1) {
        if (!endian) {
            err_MISSING_FIELD(error, "endian");
            return FALSE;
        }

        if (g_ascii_strcasecmp(endian, "little") == 0)
            *byteorder = GWY_BYTE_ORDER_LITTLE_ENDIAN;
        else if (g_ascii_strcasecmp(endian, "big") == 0)
            *byteorder = GWY_BYTE_ORDER_BIG_ENDIAN;
        else {
            err_INVALID(error, "endian");
            return FALSE;
        }
    }

    return TRUE;
}

/* TODO: Someday we may read split files here. */
/* XXX: Numbered split files (with a printf-like format) are a risk. */
static guchar*
load_detached_file(const gchar *datafile,
                   gsize *size,
                   gboolean gzcompressed,
                   gboolean bz2compressed,
                   GError **error)
{
    GError *err = NULL;
    gchar *buffer = NULL;

    gwy_debug("Loading detached <%s>", datafile);
    if (datafile) {
        if (strchr(datafile, ' ') || gwy_strequal(datafile, "LIST")) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Split detached data files are not supported."));
            return NULL;
        }
    }

    if (gzcompressed) {
#ifdef HAVE_ZLIB
        gzFile cfile = gzopen(datafile, "rb");
        gsize prevsize, toread;
        gssize readbytes;

        if (!cfile) {
            err_OPEN_READ(error);
            return NULL;
        }

        *size = 0;
        do {
            prevsize = *size;
            *size = MAX(2*prevsize, 4096);
            buffer = g_realloc(buffer, *size);
            toread = *size - prevsize;
        } while ((readbytes = gzread(cfile, buffer + prevsize, toread))
                 == toread);

        if (readbytes < 0) {
            gint errcode;
            const gchar *message = gzerror(cfile, &errcode);

            if (errcode == Z_ERRNO)
                err_READ(error);
            else
                g_set_error(error, GWY_MODULE_FILE_ERROR,
                            GWY_MODULE_FILE_ERROR_DATA,
                            _("Cannot read from file: %s."), message);
            gzclose(cfile);
            g_free(buffer);
            return NULL;
        }

        gzclose(cfile);
        *size = prevsize + readbytes;
        gwy_debug("decompressed size: %lu", (gulong)*size);
#else
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("Cannot decompress gzip-encoded data.  "
                      "Zlib support was not built in."));
#endif
    }
    else if (bz2compressed) {
#ifdef HAVE_BZIP2
        BZFILE *cfile = BZ2_bzopen(datafile, "rb");
        gsize prevsize, toread;
        gssize readbytes;

        if (!cfile) {
            err_OPEN_READ(error);
            return NULL;
        }

        *size = 0;
        do {
            prevsize = *size;
            *size = MAX(2*prevsize, 4096);
            buffer = g_realloc(buffer, *size);
            toread = *size - prevsize;
        } while ((readbytes = BZ2_bzread(cfile, buffer + prevsize, toread))
                 == toread);

        if (readbytes < 0) {
            gint errcode;
            const gchar *message = BZ2_bzerror(cfile, &errcode);

            if (errcode == BZ_IO_ERROR)  /* XXX: Is this right? */
                err_READ(error);
            else
                g_set_error(error, GWY_MODULE_FILE_ERROR,
                            GWY_MODULE_FILE_ERROR_DATA,
                            _("Cannot read from file: %s."), message);
            BZ2_bzclose(cfile);
            g_free(buffer);
            return NULL;
        }

        BZ2_bzclose(cfile);
        *size = prevsize + readbytes;
        gwy_debug("decompressed size: %lu", (gulong)*size);
#else
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("Cannot decompress bzip2-encoded data.  "
                      "Bzip2 support was not built in."));
#endif
    }
    else {
        if (!g_file_get_contents(datafile, &buffer, size, &err)) {
            err_GET_FILE_CONTENTS(error, &err);
            return NULL;
        }
        gwy_debug("file size: %lu", (gulong)*size);
    }

    return buffer;
}

static guint
pick_channel_axis(guint dimension,
                  const guint *sizes,
                  gchar **kinds)
{
    guint xdomain = TRUE, ydomain = TRUE, zdomain = TRUE;
    guint m;

    if (dimension == 2)
        return 2;

    g_return_val_if_fail(dimension == 3, 0);

    if (kinds) {
        normalise_field_name(kinds[0]);
        xdomain = gwy_stramong(kinds[0], "domain", "space", "time", NULL);

        normalise_field_name(kinds[1]);
        ydomain = gwy_stramong(kinds[1], "domain", "space", "time", NULL);

        normalise_field_name(kinds[2]);
        zdomain = gwy_stramong(kinds[2], "domain", "space", "time", NULL);
    }

    /* Pick the axis in which the size 1, preferrably of non-domain kind and
     * first or last. */
    if (sizes[2] == 1 && !zdomain)
        return 2;
    if (sizes[0] == 1 && !xdomain)
        return 0;
    if (sizes[1] == 1 && !ydomain)
        return 0;
    if (xdomain && ydomain && !zdomain)
        return 2;
    if (!xdomain && ydomain && zdomain)
        return 0;
    if (xdomain && !ydomain && zdomain)
        return 1;
    if (sizes[2] == 1)
        return 2;
    if (sizes[0] == 1)
        return 0;
    if (sizes[1] == 1)
        return 0;

    /* If that fails try the axis much smaller than the other two. */
    if (sizes[2] <= (m = MIN(sizes[0], sizes[1]))) {
        if (sizes[2] < 5 || sizes[2]*sizes[2] < m)
            return 2;
    }
    if (sizes[0] <= (m = MIN(sizes[1], sizes[2]))) {
        if (sizes[0] < 5 || sizes[0]*sizes[0] < m)
            return 0;
    }
    if (sizes[1] <= (m = MIN(sizes[0], sizes[2]))) {
        if (sizes[1] < 5 || sizes[1]*sizes[1] < m)
            return 1;
    }

    /* Import as volume data */
    return G_MAXUINT;
}

static guchar*
decode_hex(const guchar *encoded,
           gsize nitems,
           GwyRawDataType rawdatatype,
           gsize *decsize,
           GError **error)
{
    static const gint16 hex[] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    gsize dsize = gwy_raw_data_size(rawdatatype)*nitems;
    guchar *decoded = g_new(guchar, dsize);
    const guchar *p = encoded;
    gsize i;

    for (i = nitems; i; i--, decoded++) {
        gint hi, lo;

        do {
            p++;
        } while (*p && (hi = hex[*p]) == -1);
        if (!*p)
            goto fail;

        do {
            p++;
        } while (*p && (lo = hex[*p]) == -1);
        if (!*p)
            goto fail;

        *decoded = hi*16 + lo;
    }

    *decsize = dsize;
    return decoded;

fail:
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Hex data contain fewer values (%u) "
                  "than corresponds to the sizes (%u)."),
                (guint)(nitems-i), (guint)nitems);

    g_free(decoded);
    return NULL;
}

/* Handle also ASCII encoding as another representation of raw encoding to
 * unify all encodings. */
static guchar*
decode_text(const gchar *encoded,
            gsize nitems,
            GwyRawDataType rawdatatype,
            gsize *decsize,
            GError **error)
{
    gsize dsize = gwy_raw_data_size(rawdatatype)*nitems;
    guchar *decoded = g_new(guchar, dsize);
    const gchar *p = encoded;
    gchar *end;
    gsize i;

    if (rawdatatype == GWY_RAW_DATA_SINT8) {
        gint8 *s8 = (gint8*)decoded;
        for (i = nitems; i; i--, s8++) {
            *s8 = (gint8)g_ascii_strtoll(p, &end, 10);
            if (end == p)
                goto fail;
            p = end;
        }
    }
    else if (rawdatatype == GWY_RAW_DATA_UINT8) {
        guint8 *u8 = (guint8*)decoded;
        for (i = nitems; i; i--, u8++) {
            *u8 = (guint8)g_ascii_strtoull(p, &end, 10);
            if (end == p)
                goto fail;
            p = end;
        }
    }
    else if (rawdatatype == GWY_RAW_DATA_SINT16) {
        gint16 *s16 = (gint16*)decoded;
        for (i = nitems; i; i--, s16++) {
            *s16 = (gint16)g_ascii_strtoll(p, &end, 10);
            if (end == p)
                goto fail;
            p = end;
        }
    }
    else if (rawdatatype == GWY_RAW_DATA_UINT16) {
        guint16 *u16 = (guint16*)decoded;
        for (i = nitems; i; i--, u16++) {
            *u16 = (guint16)g_ascii_strtoull(p, &end, 10);
            if (end == p)
                goto fail;
            p = end;
        }
    }
    else if (rawdatatype == GWY_RAW_DATA_SINT32) {
        gint32 *s32 = (gint32*)decoded;
        for (i = nitems; i; i--, s32++) {
            *s32 = (gint32)g_ascii_strtoll(p, &end, 10);
            if (end == p)
                goto fail;
            p = end;
        }
    }
    else if (rawdatatype == GWY_RAW_DATA_UINT32) {
        guint32 *u32 = (guint32*)decoded;
        for (i = nitems; i; i--, u32++) {
            *u32 = (guint32)g_ascii_strtoull(p, &end, 10);
            if (end == p)
                goto fail;
            p = end;
        }
    }
    else if (rawdatatype == GWY_RAW_DATA_SINT64) {
        gint64 *s64 = (gint64*)decoded;
        for (i = nitems; i; i--, s64++) {
            *s64 = g_ascii_strtoll(p, &end, 10);
            if (end == p)
                goto fail;
            p = end;
        }
    }
    else if (rawdatatype == GWY_RAW_DATA_UINT64) {
        guint64 *u64 = (guint64*)decoded;
        for (i = nitems; i; i--, u64++) {
            *u64 = g_ascii_strtoull(p, &end, 10);
            if (end == p)
                goto fail;
            p = end;
        }
    }
    else if (rawdatatype == GWY_RAW_DATA_FLOAT) {
        gfloat *f32 = (gfloat*)decoded;
        for (i = nitems; i; i--, f32++) {
            *f32 = (gfloat)g_ascii_strtod(p, &end);
            if (end == p)
                goto fail;
            p = end;
        }
    }
    else if (rawdatatype == GWY_RAW_DATA_DOUBLE) {
        gdouble *d64 = (gdouble*)decoded;
        for (i = nitems; i; i--, d64++) {
            *d64 = g_ascii_strtod(p, &end);
            if (end == p)
                goto fail;
            p = end;
        }
    }
    else {
        g_return_val_if_reached(NULL);
    }

    *decsize = dsize;
    return decoded;

fail:
    if (!*p)
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Text data contain fewer values (%u) "
                      "than corresponds to the sizes (%u)."),
                    (guint)(nitems-i), (guint)nitems);
    else
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Garbage after data sample #%u."), (guint)(nitems-i));

    g_free(decoded);
    return NULL;
}

/*
 * The sequence of actions must be:
 * 1. line skipping
 * 2. decompression (decoding)
 * 3. byte skipping
 *
 * However, we treat text and hex as decoding steps.  On the other hand, the
 * format uses actual gzip-compressed data, not just zlib-deflated and to use
 * gzopen() we need the compressed data as a detached file.  Fortunately, this
 * seems to be the common case.
 */
static gconstpointer
get_raw_data_pointer(gconstpointer base,
                     gsize *size,
                     gsize nitems,
                     GwyRawDataType rawdatatype,
                     GwyByteOrder *byteorder,
                     NRRDEncoding encoding,
                     gssize lineskip,
                     gssize byteskip,
                     GSList **buffers_to_free,
                     GError **error)
{
    gsize expected_size;
    gboolean binary = TRUE;
    gssize i;

    if (byteskip < -1) {
        err_INVALID(error, "byteskip");
        return NULL;
    }
    /* Do not bother skipping lines at the begining if we look from the end. */
    if (byteskip == -1)
        lineskip = 0;

    if (lineskip < 0) {
        err_INVALID(error, "lineskip");
        return NULL;
    }

    if (byteskip == -1 && (encoding == NRRD_ENCODING_TEXT
                           || encoding == NRRD_ENCODING_HEX)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Field byteskip cannot be -1 for text encodings."));
        return NULL;
    }

    /* Line skipping */
    for (i = 0; i < lineskip; i++) {
        const guchar *p = memchr(base, '\n', *size);

        if (!p) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Field lineskip specifies more lines than there "
                          "are in the file."));
            return NULL;
        }
        p++;
        *size -= p - (const guchar*)base;
        base = p;
    }

    /* Decompression */
    if (encoding == NRRD_ENCODING_RAW) {
        /* Nothing to do. */
    }
    else if (encoding == NRRD_ENCODING_TEXT || encoding == NRRD_ENCODING_HEX) {
        /* Decoded later. */
        binary = FALSE;
    }
    else {
        err_UNSUPPORTED(error, "encoding");
        return NULL;
    }

    /* Byte skipping and final size validation for binary formats. */
    if (byteskip <= 0)
        expected_size = gwy_raw_data_size(rawdatatype)*nitems;
    else
        expected_size = gwy_raw_data_size(rawdatatype)*nitems + byteskip;

    if (binary) {
        if (err_SIZE_MISMATCH(error, expected_size, *size, FALSE))
            return NULL;

        if (byteskip == -1)
            return (gconstpointer)((const gchar*)base + *size - expected_size);
        else
            return (gconstpointer)((const gchar*)base + byteskip);
    }

    /* Decoding for text formats. */
    g_assert(byteskip >= 0);   /* This must hold if we got here. */
    if (byteskip > *size) {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_DATA,
                    _("Field byteskip specifies more bytes than there "
                      "are in the file."));
        return NULL;
    }
    base = (gconstpointer)((const gchar*)base + byteskip);
    *size -= byteskip;

    /* Buffers are always nul-terminated, passing the encoed size is not
     * necessary. */
    if (encoding == NRRD_ENCODING_HEX)
        base = decode_hex(base, nitems, rawdatatype, size, error);
    else if (encoding == NRRD_ENCODING_TEXT) {
        /* Text data are always created (‘decoded’) in the native byte order. */
        base = decode_text(base, nitems, rawdatatype, size, error);
        *byteorder = GWY_BYTE_ORDER_NATIVE;
    }
    else {
        g_assert_not_reached();
    }

    if (base)
        *buffers_to_free = g_slist_append(*buffers_to_free, (gpointer)base);

    return base;
}

static GwyDataField*
read_raw_data_field(guint xres, guint yres,
                    gint stride, guint rowstride,
                    GwyRawDataType rawdatatype, GwyByteOrder byteorder,
                    GHashTable *fields,
                    const guchar *data)
{
    GwyDataField *dfield;
    GwySIUnit *siunitz = NULL, *siunitxy = NULL;
    gdouble dx = 1.0, dy = 1.0, q = 1.0, z0 = 0.0, xoff = 0.0, yoff = 0.0;
    gdouble *d;
    gint power10;
    gchar *value;
    guint i;

    if ((value = g_hash_table_lookup(fields, "oldmin")))
        z0 = g_ascii_strtod(value, NULL);

    if ((value = g_hash_table_lookup(fields, "oldmax")))
        q = g_ascii_strtod(value, NULL) - z0;

    /* FIXME: This is probably wrong if dimensions == 3 && chanaxis != 2. */
    if ((value = g_hash_table_lookup(fields, "spacings"))
        && parse_float_vector(value, 2, &dx, &dy)) {
        if (!((dx = fabs(dx)) > 0)) {
            g_warning("Real x step is 0.0, fixing to 1.0");
            dx = 1.0;
        }
        if (!((dy = fabs(dy)) > 0)) {
            g_warning("Real y step is 0.0, fixing to 1.0");
            dy = 1.0;
        }
    }

    /* FIXME: This is probably wrong if dimensions == 3 && chanaxis != 2. */
    if ((value = g_hash_table_lookup(fields, "axismins")))
        parse_float_vector(value, 2, &xoff, &yoff);

    /* Prefer axismaxs if both spacings and axismaxs are given. */
    /* FIXME: This is probably wrong if dimensions == 3 && chanaxis != 2. */
    if ((value = g_hash_table_lookup(fields, "axismaxs"))
        && parse_float_vector(value, 2, &dx, &dy)) {
        dx = (dx - xoff)/xres;
        dy = (dy - xoff)/xres;
        if (!((dx = fabs(dx)) > 0)) {
            g_warning("Real x step is 0.0, fixing to 1.0");
            dx = 1.0;
        }
        if (!((dy = fabs(dy)) > 0)) {
            g_warning("Real y step is 0.0, fixing to 1.0");
            dy = 1.0;
        }
    }

    if ((value = g_hash_table_lookup(fields, "sampleunits"))) {
        siunitz = gwy_si_unit_new_parse(value, &power10);
        q *= pow10(power10);
        z0 *= pow10(power10);
    }

    if ((value = g_hash_table_lookup(fields, "units"))) {
        gchar *unitx, *unity;
        if (parse_string_vector(value, 2, &unitx, &unity)) {
            if (!gwy_strequal(unitx, unity))
                g_warning("X and Y units differ, using X");
            siunitxy = gwy_si_unit_new_parse(unitx, &power10);
            dx *= pow10(power10);
            dy *= pow10(power10);
            g_free(unitx);
            g_free(unity);
        }
    }

    rowstride *= gwy_raw_data_size(rawdatatype);
    dfield = gwy_data_field_new(xres, yres, xres*dx, yres*dy, FALSE);
    gwy_data_field_set_xoffset(dfield, xoff);
    gwy_data_field_set_yoffset(dfield, yoff);
    d = gwy_data_field_get_data(dfield);
    for (i = 0; i < yres; i++)
        gwy_convert_raw_data(data + i*rowstride, xres, stride,
                             rawdatatype, byteorder,
                             d + i*xres, q, z0);

    if (siunitxy) {
        gwy_data_field_set_si_unit_xy(dfield, siunitxy);
        g_object_unref(siunitxy);
    }
    if (siunitz) {
        gwy_data_field_set_si_unit_z(dfield, siunitz);
        g_object_unref(siunitz);
    }

    return dfield;
}

static GwyBrick*
read_raw_brick(guint xres, guint yres, guint zres,
               GwyRawDataType rawdatatype, GwyByteOrder byteorder,
               GHashTable *fields,
               const guchar *data)
{
    GwyBrick *brick;
    GwySIUnit *siunitz = NULL, *siunitx = NULL, *siunity = NULL,
              *siunitw = NULL;
    gdouble dx = 1.0, dy = 1.0, dz = 1.0, q = 1.0, w0 = 0.0,
            xoff = 0.0, yoff = 0.0, zoff = 0.0;
    gdouble *d;
    gint power10;
    gchar *value;

    if ((value = g_hash_table_lookup(fields, "oldmin")))
        w0 = g_ascii_strtod(value, NULL);

    if ((value = g_hash_table_lookup(fields, "oldmax")))
        q = g_ascii_strtod(value, NULL) - w0;

    if ((value = g_hash_table_lookup(fields, "spacings"))
        && parse_float_vector(value, 3, &dx, &dy, &dz)) {
        if (!((dx = fabs(dx)) > 0)) {
            g_warning("Real x step is 0.0, fixing to 1.0");
            dx = 1.0;
        }
        if (!((dy = fabs(dy)) > 0)) {
            g_warning("Real y step is 0.0, fixing to 1.0");
            dy = 1.0;
        }
        if (!((dz = fabs(dz)) > 0)) {
            g_warning("Real z step is 0.0, fixing to 1.0");
            dz = 1.0;
        }
    }

    if ((value = g_hash_table_lookup(fields, "axismins")))
        parse_float_vector(value, 3, &xoff, &yoff, &zoff);

    /* Prefer axismaxs if both spacings and axismaxs are given. */
    if ((value = g_hash_table_lookup(fields, "axismaxs"))
        && parse_float_vector(value, 3, &dx, &dy, &dz)) {
        dx = (dx - xoff)/xres;
        dy = (dy - xoff)/xres;
        dz = (dz - zoff)/zres;
        if (!((dx = fabs(dx)) > 0)) {
            g_warning("Real x step is 0.0, fixing to 1.0");
            dx = 1.0;
        }
        if (!((dy = fabs(dy)) > 0)) {
            g_warning("Real y step is 0.0, fixing to 1.0");
            dy = 1.0;
        }
        if (!((dz = fabs(dz)) > 0)) {
            g_warning("Real z step is 0.0, fixing to 1.0");
            dz = 1.0;
        }
    }

    if ((value = g_hash_table_lookup(fields, "sampleunits"))) {
        siunitw = gwy_si_unit_new_parse(value, &power10);
        q *= pow10(power10);
        w0 *= pow10(power10);
    }

    if ((value = g_hash_table_lookup(fields, "units"))) {
        gchar *unitx, *unity, *unitz;
        if (parse_string_vector(value, 3, &unitx, &unity, &unitz)) {
            siunitx = gwy_si_unit_new_parse(unitx, &power10);
            dx *= pow10(power10);
            siunity = gwy_si_unit_new_parse(unity, &power10);
            dy *= pow10(power10);
            siunitz = gwy_si_unit_new_parse(unitz, &power10);
            dz *= pow10(power10);
            g_free(unitx);
            g_free(unity);
            g_free(unitz);
        }
    }

    brick = gwy_brick_new(xres, yres, zres, xres*dx, yres*dy, zres*dz, FALSE);
    gwy_brick_set_xoffset(brick, xoff);
    gwy_brick_set_yoffset(brick, yoff);
    gwy_brick_set_zoffset(brick, zoff);
    d = gwy_brick_get_data(brick);
    gwy_convert_raw_data(data, xres*yres*zres, 1, rawdatatype, byteorder, d,
                         q, w0);

    if (siunitx) {
        gwy_brick_set_si_unit_x(brick, siunitx);
        g_object_unref(siunitx);
    }
    if (siunity) {
        gwy_brick_set_si_unit_y(brick, siunity);
        g_object_unref(siunity);
    }
    if (siunitz) {
        gwy_brick_set_si_unit_z(brick, siunitz);
        g_object_unref(siunitz);
    }
    if (siunitw) {
        gwy_brick_set_si_unit_w(brick, siunitw);
        g_object_unref(siunitw);
    }

    return brick;
}

static void
add_meta(gpointer key, gpointer value, gpointer user_data)
{
    const gchar *strkey = (const gchar*)key;
    const gchar *strvalue = (const gchar*)value;
    GwyContainer *meta = (GwyContainer*)user_data;

    gwy_container_set_string_by_name(meta, strkey, g_strdup(strvalue));
}

static GwyContainer*
nrrd_make_meta(GHashTable *keyvalue)
{
    GwyContainer *meta = gwy_container_new();

    g_hash_table_foreach(keyvalue, add_meta, meta);
    if (gwy_container_get_n_items(meta))
        return meta;
    g_object_unref(meta);

    return NULL;
}

static gboolean
parse_uint_vector(const gchar *value,
                  guint n,
                  ...)
{
    guint *values;
    va_list ap;
    gchar *end;
    guint i;

    values = g_new(guint, n);

    for (i = 0; i < n; i++) {
        values[i] = g_ascii_strtoull(value, &end, 10);
        if (end == value) {
            g_free(values);
            return FALSE;
        }
        value = end;
    }

    /* Guarantee that we don't touch args pointers if we don't return TRUE. */
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        guint *u = va_arg(ap, guint*);
        *u = values[i];
    }
    va_end(ap);

    g_free(values);

    return TRUE;
}

static gboolean
parse_float_vector(const gchar *value,
                   guint n,
                   ...)
{
    gdouble *values;
    va_list ap;
    gchar *end;
    guint i;

    values = g_new(gdouble, n);

    for (i = 0; i < n; i++) {
        values[i] = g_ascii_strtod(value, &end);
        if (end == value || gwy_isnan(values[i]) || gwy_isinf(values[i])) {
            return FALSE;
        }
        value = end;
    }

    /* Guarantee that we don't touch args pointers if we don't return TRUE. */
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        gdouble *d = va_arg(ap, gdouble*);
        *d = values[i];
    }
    va_end(ap);

    g_free(values);

    return TRUE;
}

static gboolean
parse_string_vector(const gchar *value,
                    guint n,
                    ...)
{
    gchar **values;
    va_list ap;
    guint i;

    values = g_new0(gchar*, n+1);

    for (i = 0; i < n; i++) {
        const gchar *begin;
        gboolean backslashed = FALSE, backslashedany = FALSE;

        while (g_ascii_isspace(*value))
            value++;

        if (*value != '"') {
            g_strfreev(values);
            return FALSE;
        }
        value++;
        begin = value;

        while (*value && (backslashed || *value != '"')) {
            if (backslashed)
                backslashed = FALSE;
            else if (*value == '\\')
                backslashed = backslashedany = TRUE;
            value++;
        }

        if (!*value) {
            g_strfreev(values);
            return FALSE;
        }

        if (backslashedany) {
            gchar *compressed = g_strndup(begin, value - begin);
            values[i] = g_strcompress(compressed);
            g_free(compressed);
        }
        else
            values[i] = g_strndup(begin, value - begin);

        /* Technically this means we do not require whitespace between an
         * ending " and another starting ". */
        value++;
    }

    /* Guarantee that we don't touch args pointers if we don't return TRUE. */
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        gchar **u = va_arg(ap, gchar**);
        *u = values[i];
    }
    va_end(ap);

    g_free(values);

    return TRUE;
}

static gchar**
split_per_axis_field(const gchar *value,
                     const gchar *name,
                     guint nitems,
                     gboolean quoted,
                     GError **error)
{
    gchar **raw_fields, **fields, *f;
    guint n = 0, i, len;

    raw_fields = g_strsplit_set(value, " \t\v\r\n", -1);
    fields = g_new0(gchar*, nitems+1);
    for (i = 0; (f = raw_fields[i]); i++) {
        len = strlen(f);
        if (!len)
            continue;

        if (quoted) {
            if (len < 2 || f[0] != '"' || f[len-1] != '"') {
                g_set_error(error, GWY_MODULE_FILE_ERROR,
                            GWY_MODULE_FILE_ERROR_DATA,
                            _("Items of per-axis header field %s are not "
                              "quoted."),
                            name);
                goto fail;
            }
            f[len-1] = '\0';
            f++;
        }
        if (n == nitems) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Per-axis header field %s contains too many "
                          "items."),
                        name);
            goto fail;
        }
        fields[n++] = g_strdup(f);
    }
    if (n < nitems) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Per-axis header field %s contains too few items."),
                    name);
        goto fail;
    }

    g_strfreev(raw_fields);
    return fields;

fail:
    g_strfreev(raw_fields);
    g_strfreev(fields);
    return NULL;
}

static gboolean
nrrdfile_export(G_GNUC_UNUSED GwyContainer *data,
                const gchar *filename,
                G_GNUC_UNUSED GwyRunType mode,
                GError **error)
{
    /* We specify lateral units so at least version 4 is necessary. */
    static const gchar header_format[] =
        "NRRD0004\n"
        "type: float\n"
        "encoding: raw\n"
        "endian: %s\n"
        "dimension: 2\n"
        "sizes: %u %u\n"
        "axismins: %s %s\n"
        "axismaxs: %s %s\n"
        "units: \"%s\" \"%s\"\n"
        "sampleunits: \"%s\"\n"
        "\n";

    GwyDataField *dfield;
    const gdouble *d;
    gdouble xreal, yreal, xoff, yoff;
    guint xres, yres, i;
    gchar *unitxy, *unitz;
    gfloat *dfl;
    gchar buf[4][32];
    gboolean ok = TRUE;
    FILE *fh;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield, 0);
    if (!dfield) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    /* The specification says both kind of EOLs are fine so write Unix EOLs
     * everywhere. */
    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    d = gwy_data_field_get_data_const(dfield);
    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    xreal = gwy_data_field_get_xreal(dfield);
    yreal = gwy_data_field_get_yreal(dfield);
    xoff = gwy_data_field_get_xoffset(dfield);
    yoff = gwy_data_field_get_yoffset(dfield);
    unitxy = gwy_si_unit_get_string(gwy_data_field_get_si_unit_xy(dfield),
                                    GWY_SI_UNIT_FORMAT_PLAIN);
    unitz = gwy_si_unit_get_string(gwy_data_field_get_si_unit_z(dfield),
                                   GWY_SI_UNIT_FORMAT_PLAIN);

    g_ascii_formatd(buf[0], sizeof(buf[0]), "%.8g", xoff);
    g_ascii_formatd(buf[1], sizeof(buf[1]), "%.8g", yoff);
    g_ascii_formatd(buf[2], sizeof(buf[2]), "%.8g", xreal + xoff);
    g_ascii_formatd(buf[3], sizeof(buf[3]), "%.8g", yreal + yoff);

    /* Write in native endian. */
    gwy_fprintf(fh, header_format,
            G_BYTE_ORDER == G_LITTLE_ENDIAN ? "little" : "big",
            xres, yres,
            buf[0], buf[1], buf[2], buf[3],
            unitxy, unitxy, unitz);
    g_free(unitz);
    g_free(unitxy);

    dfl = g_new(gfloat, xres*yres);
    for (i = 0; i < xres*yres; i++)
        dfl[i] = d[i];

    if (fwrite(dfl, sizeof(gfloat), xres*yres, fh) != xres*yres) {
        /* uses errno, must do this before fclose(). */
        err_WRITE(error);
        ok = FALSE;
    }
    g_free(dfl);
    fclose(fh);

    return ok;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
