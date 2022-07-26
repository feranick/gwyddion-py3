/*
 *  $Id: opdfile.c 20676 2017-12-18 18:19:09Z yeti-dn $
 *  Copyright (C) 2008-2017 David Necas (Yeti).
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
 * <mime-type type="application/x-wyko-opd">
 *   <comment>Wyko OPD data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="\x01\x00Directory"/>
 *   </magic>
 *   <glob pattern="*.opd"/>
 *   <glob pattern="*.OPD"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-wyko-asc">
 *   <comment>Wyko ASCII data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="Wyko ASCII Data File Format 0\t0\t1"/>
 *   </magic>
 *   <glob pattern="*.asc"/>
 *   <glob pattern="*.ASC"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Wyko/Veeco OPD
 * # It has binary and text variants.
 * 0 string \x01\x00Directory Vision surface profilometry OPD binary data
 * 0 string Wyko\ ASCII\ Data\ File\ Format\ 0\x090\x091 Vision surface profilometry OPD text data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Wyko OPD
 * .opd
 * Read
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Wyko ASCII
 * .asc
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"

/* Not a real magic header, but should catch the stuff */
#define MAGIC "\x01\x00" "Directory"
#define MAGIC_SIZE (sizeof(MAGIC)-1)
#define EXTENSION ".opd"

#define MAGIC_ASC "Wyko ASCII Data File Format "
#define MAGIC_ASC_SIZE (sizeof(MAGIC_ASC)-1)
#define EXTENSION_ASC ".asc"

#define Nanometer (1e-9)
#define Milimeter (1e-3)
#define OPD_BAD_FLOAT 1e38
#define OPD_BAD_INT16 32766
#define OPD_BAD_ASC "Bad"

enum {
    BLOCK_SIZE = 24,
    BLOCK_NAME_SIZE = 16,
};

enum {
    OPD_DIRECTORY = 1,
    OPD_ARRAY = 3,
    OPD_TEXT = 5,
    OPD_SHORT = 6,
    OPD_FLOAT = 7,
    OPD_DOUBLE = 8,
    OPD_LONG = 12,
    /* Serialised structs; some look like bits of OPDx, some do not.  If this
     * appears positions in the file are off.  Unfortunately not just of the
     * binary stuff but apparently also of other things. */
    OPD_BINARY_STUFF = 15,
} OPDDataType;

typedef enum {
    OPD_ARRAY_FLOAT = 4,
    OPD_ARRAY_INT16 = 2,
    OPD_ARRAY_BYTE = 1,
} OPDArrayType;

typedef enum {
    OPD_STANDARD_IMAGE = 0,
    OPD_XYZ_PIXEL = 1,
    OPD_XYZ_REAL = 2,
} OPDArrayFormat;

/* The header consists of a sequence of these creatures. */
typedef struct {
    /* This is in the file */
    char name[BLOCK_NAME_SIZE + 1];
    guint type;
    guint size;
    guint flags;   /* XXX: I don't know what is this good for. */
    /* Derived info */
    guint pos;
    const guchar *data;
} OPDBlock;

static gboolean      module_register   (void);
static gint          opd_detect        (const GwyFileDetectInfo *fileinfo,
                                        gboolean only_name);
static gint          opd_asc_detect    (const GwyFileDetectInfo *fileinfo,
                                        gboolean only_name);
static GwyContainer* opd_load          (const gchar *filename,
                                        GwyRunType mode,
                                        GError **error);
static GwyContainer* opd_asc_load      (const gchar *filename,
                                        GwyRunType mode,
                                        GError **error);
static void          get_block         (OPDBlock *block,
                                        const guchar **p);
static gboolean      get_float         (const OPDBlock *header,
                                        guint nblocks,
                                        const gchar *name,
                                        gdouble *value,
                                        GError **error);
static gboolean      get_int16         (const OPDBlock *header,
                                        guint nblocks,
                                        const gchar *name,
                                        gint *value,
                                        GError **error);
static GwyDataField* get_data_field    (const OPDBlock *datablock,
                                        gdouble pixel_size,
                                        gdouble aspect,
                                        gdouble wavelength,
                                        const gchar *zunits,
                                        GwyDataField **maskfield,
                                        GError **error);
static void          store_asc_meta    (gpointer key,
                                        gpointer value,
                                        gpointer user_data);
static GwyDataField* get_asc_data_field(gchar **p,
                                        OPDArrayFormat format,
                                        guint xres,
                                        guint yres,
                                        gdouble pixel_size,
                                        gdouble aspect,
                                        gdouble wavelength,
                                        const gchar *zunits,
                                        GwyDataField **maskfield);
static GwyContainer* get_meta          (const OPDBlock *header,
                                        guint nblocks);
static gboolean      check_sizes       (const OPDBlock *header,
                                        guint nblocks,
                                        GError **error);
static guint         find_block        (const OPDBlock *header,
                                        guint nblocks,
                                        const gchar *name);
static const guchar* get_array_params  (const guchar *p,
                                        guint *xres,
                                        guint *yres,
                                        OPDArrayType *type);
static void          clone_meta        (GwyContainer *container,
                                        GwyContainer *meta,
                                        guint nchannels);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Wyko OPD and ASC files."),
    "Yeti <yeti@gwyddion.net>",
    "0.11",
    "David Nečas (Yeti)",
    "2008",
};

GWY_MODULE_QUERY2(module_info, opdfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("opdfile",
                           N_("Wyko OPD files (.opd)"),
                           (GwyFileDetectFunc)&opd_detect,
                           (GwyFileLoadFunc)&opd_load,
                           NULL,
                           NULL);

    gwy_file_func_register("opdfile-asc",
                           N_("Wyko ASCII export files (.asc)"),
                           (GwyFileDetectFunc)&opd_asc_detect,
                           (GwyFileLoadFunc)&opd_asc_load,
                           NULL,
                           NULL);

    return TRUE;
}

/***** Native binary OPD file *********************************************/

static gint
opd_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 10 : 0;

    if (fileinfo->file_size < BLOCK_SIZE + 2
        || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    return 100;
}

static GwyContainer*
opd_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    OPDBlock directory_block, *header = NULL;
    gsize size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL, *mfield = NULL;
    const guchar *p;
    guint nblocks, offset, i, j, k;
    gdouble pixel_size, wavelength, aspect = 1.0;
    gint mult = 1;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (size < BLOCK_SIZE + 2) {
        err_TOO_SHORT(error);
        goto fail;
    }

    p = buffer + 2;
    get_block(&directory_block, &p);
    directory_block.pos = 2;
    directory_block.data = buffer + 2;
    gwy_debug("<%s> size=0x%08x, pos=0x%08x, type=%u, flags=0x%04x",
              directory_block.name, directory_block.size, directory_block.pos,
              directory_block.type, directory_block.flags);
    /* This check may need to be relieved a bit */
    if (!gwy_strequal(directory_block.name, "Directory")
        || directory_block.type != OPD_DIRECTORY
        || directory_block.flags != 0xffff) {
        err_FILE_TYPE(error, "Wyko OPD data");
        goto fail;
    }

    nblocks = directory_block.size/BLOCK_SIZE;
    if (size < BLOCK_SIZE*nblocks + 2) {
        err_TRUNCATED_HEADER(error);
        goto fail;
    }

    /* Read info block.  We've already read the directory, do not count it */
    nblocks--;
    header = g_new(OPDBlock, nblocks);
    offset = directory_block.pos + directory_block.size;
    for (i = j = 0; i < nblocks; i++) {
        get_block(header + j, &p);
        header[j].pos = offset;
        header[j].data = buffer + offset;
        offset += header[j].size;
        /* Skip void header blocks */
        if (header[j].size) {
            gwy_debug("<%s> size=0x%08x, pos=0x%08x, type=%u, flags=0x%04x",
                      header[j].name, header[j].size, header[j].pos,
                      header[j].type, header[j].flags);
            j++;
        }
        if (offset > size) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Item `%s' is beyond the end of the file."),
                        header[i].name);
            goto fail;
        }
    }
    nblocks = j;

    /* XXX: There can be a block called "\xcaxtendedKe\xfds" at the end that
     * provides mapping from the names truncated to 16 bytes to full names.
     * Unfortunately, it is of type OPD_BINARY_STUFF and therefore in a
     * different position in the file than @pos says... */

    if (!check_sizes(header, nblocks, error))
        goto fail;

    /* Physical scales */
    if (!get_float(header, nblocks, "Pixel_size", &pixel_size, error)
        || !get_float(header, nblocks, "Wavelength", &wavelength, error))
        goto fail;

    wavelength *= Nanometer;
    pixel_size *= Milimeter;
    get_int16(header, nblocks, "Mult", &mult, NULL);
    get_float(header, nblocks, "Aspect", &aspect, NULL);
    wavelength /= mult;

    container = gwy_container_new();

    /* Read the data */
    for (i = j = 0; i < nblocks; i++) {
        gboolean intensity;
        gchar *s;

        if (!gwy_stramong(header[i].name,
                          "OPD", "SAMPLE_DATA", "RAW_DATA", "RAW DATA",
                          "Image", "Intensity", "SecArr_0", "Raw",
                          NULL))
            continue;

        if (header[i].type != OPD_ARRAY) {
            g_warning("Block %s is not of array type", header[i].name);
            continue;
        }

        dfield = mfield = NULL;
        intensity = gwy_stramong(header[i].name,
                                 "Image", "Intensity", "SecArr_0", NULL);
        if (intensity) {
            if (!(dfield = get_data_field(header + i,
                                          pixel_size, aspect, 1.0, NULL,
                                          NULL, error)))
                goto fail;
        }
        else {
            if (!(dfield = get_data_field(header + i,
                                          pixel_size, aspect, wavelength, "m",
                                          &mfield, error)))
                goto fail;
        }

        gwy_container_set_object(container, gwy_app_get_data_key_for_id(j),
                                 dfield);
        if (mfield)
            gwy_container_set_object(container, gwy_app_get_mask_key_for_id(j),
                                     mfield);

        s = g_strdup_printf("%s/title",
                            g_quark_to_string(gwy_app_get_data_key_for_id(j)));
        if (intensity) {
            gwy_container_set_string_by_name(container, s,
                                             g_strdup(header[i].name));
        }
        else if ((k = find_block(header, nblocks, "Title")) != nblocks) {
            gwy_container_set_string_by_name(container, s,
                                             g_strndup(header[k].data,
                                                       header[k].size));
        }
        else
            gwy_app_channel_title_fall_back(container, j);
        g_free(s);

        s = g_strdup_printf("%s/realsquare",
                            g_quark_to_string(gwy_app_get_data_key_for_id(j)));
        gwy_container_set_boolean_by_name(container, s, TRUE);
        g_free(s);

        gwy_file_channel_import_log_add(container, j, NULL, filename);

        j++;
    }

    if (j) {
        GwyContainer *meta = get_meta(header, nblocks);

        clone_meta(container, meta, j);
        g_object_unref(meta);
    }
    else {
        GWY_OBJECT_UNREF(container);
        err_NO_DATA(error);
    }

fail:
    g_free(header);
    GWY_OBJECT_UNREF(dfield);
    GWY_OBJECT_UNREF(mfield);
    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

static void
get_block(OPDBlock *block, const guchar **p)
{
    memset(block->name, 0, BLOCK_NAME_SIZE + 1);
    strncpy(block->name, *p, BLOCK_NAME_SIZE);
    *p += BLOCK_NAME_SIZE;
    g_strstrip(block->name);
    block->type = gwy_get_guint16_le(p);
    block->size = gwy_get_guint32_le(p);
    block->flags = gwy_get_guint16_le(p);

    if (strncmp(block->name, "AdjustVSI_", sizeof("AdjustVSI_")-1) == 0
        && block->type == 7
        && block->size == 2) {
        gwy_info("Changing the type of field %s from float to short.",
                 block->name);
        block->type = 6;
    }
    if (gwy_strequal(block->name, "ImageModificat~0")
        && block->type == 7
        && block->size == 40) {
        gwy_info("Changing the size of field %s from 40 to 4.", block->name);
        block->size = 4;
    }
}

static gboolean
get_float(const OPDBlock *header,
          guint nblocks,
          const gchar *name,
          gdouble *value,
          GError **error)
{
    const guchar *p;
    guint i;

    if ((i = find_block(header, nblocks, name)) == nblocks) {
        err_MISSING_FIELD(error, name);
        return FALSE;
    }
    if (header[i].type != OPD_FLOAT) {
        err_INVALID(error, name);
        return FALSE;
    }
    p = header[i].data;
    *value = gwy_get_gfloat_le(&p);
    gwy_debug("%s = %g", name, *value);
    return TRUE;
}

static gboolean
get_int16(const OPDBlock *header,
          guint nblocks,
          const gchar *name,
          gint *value,
          GError **error)
{
    const guchar *p;
    guint i;

    if ((i = find_block(header, nblocks, name)) == nblocks) {
        err_MISSING_FIELD(error, name);
        return FALSE;
    }
    if (header[i].type != OPD_SHORT) {
        err_INVALID(error, name);
        return FALSE;
    }
    p = header[i].data;
    *value = gwy_get_gint16_le(&p);
    gwy_debug("%s = %d", name, *value);
    return TRUE;
}

static GwyDataField*
get_data_field(const OPDBlock *datablock,
               gdouble pixel_size, gdouble aspect, gdouble wavelength,
               const gchar *zunits,
               GwyDataField **maskfield,
               GError **error)
{
    OPDArrayType datatype;
    GwyDataField *dfield, *mfield;
    GwySIUnit *siunit;
    gdouble *data, *mdata;
    guint xres, yres, mcount;
    const guchar *p;
    guint i, j;

    if (maskfield)
        *maskfield = NULL;

    p = get_array_params(datablock->data, &xres, &yres, &datatype);
    dfield = gwy_data_field_new(xres, yres,
                                xres*pixel_size, aspect*yres*pixel_size, FALSE);

    siunit = gwy_si_unit_new("m");
    gwy_data_field_set_si_unit_xy(dfield, siunit);
    g_object_unref(siunit);

    siunit = gwy_si_unit_new(zunits);
    gwy_data_field_set_si_unit_z(dfield, siunit);
    g_object_unref(siunit);

    mfield = gwy_data_field_new_alike(dfield, FALSE);
    gwy_data_field_fill(mfield, 1.0);

    data = gwy_data_field_get_data(dfield);
    mdata = gwy_data_field_get_data(mfield);
    for (i = 0; i < xres; i++) {
        if (datatype == OPD_ARRAY_FLOAT) {
            for (j = yres; j; j--) {
                gdouble v = gwy_get_gfloat_le(&p);
                if (v < OPD_BAD_FLOAT)
                    data[(j - 1)*xres + i] = wavelength*v;
                else
                    mdata[(j - 1)*xres + i] = 0.0;
            }
        }
        else if (datatype == OPD_ARRAY_INT16) {
            for (j = yres; j; j--) {
                gint v = gwy_get_gint16_le(&p);
                if (v < OPD_BAD_INT16)
                    data[(j - 1)*xres + i] = wavelength*v;
                else
                    mdata[(j - 1)*xres + i] = 0.0;
            }
        }
        else if (datatype == OPD_ARRAY_BYTE) {
            /* FIXME: Bad data? */
            for (j = yres; j; j--) {
                data[(j - 1)*xres + i] = wavelength*(*(p++));
            }
        }
        else {
            err_DATA_TYPE(error, datatype);
            g_object_unref(dfield);
            g_object_unref(mfield);
            return NULL;
        }
    }
    mcount = gwy_app_channel_remove_bad_data(dfield, mfield);

    if (maskfield && mcount)
        *maskfield = mfield;
    else
        g_object_unref(mfield);

    return dfield;
}

static GwyContainer*
get_meta(const OPDBlock *header,
         guint nblocks)
{
    GwyContainer *meta = gwy_container_new();
    guint i, type;
    const guchar *p;
    gchar *s;

    for (i = 0; i < nblocks; i++) {
        type = header[i].type;
        p = header[i].data;
        s = NULL;
        if (type == OPD_TEXT) {
            s = g_convert(header[i].data, header[i].size, "UTF-8", "ISO-8859-1",
                          NULL, NULL, NULL);
            if (s && !*s) {
                g_free(s);
                s = NULL;
            }
        }
        else if (type == OPD_SHORT || type == OPD_LONG) {
            gint32 value = (type == OPD_SHORT)
                           ? gwy_get_gint16_le(&p)
                           : gwy_get_gint32_le(&p);
            s = g_strdup_printf("%d", value);
        }
        else if (type == OPD_FLOAT || type == OPD_DOUBLE) {
            gdouble value = (type == OPD_FLOAT)
                            ? gwy_get_gfloat_le(&p)
                            : gwy_get_gdouble_le(&p);
            s = g_strdup_printf("%g", value);
        }
        /* Ignore all other types */
        if (s)
            gwy_container_set_string_by_name(meta, header[i].name, s);
    }

    return meta;
}

/* TODO: Improve error messages */
static gboolean
check_sizes(const OPDBlock *header,
            guint nblocks,
            GError **error)
{
    /*                             0  1  2  3  4  5  6  7  8  9 10 11 12 */
    static const guint sizes[] = { 0, 0, 0, 0, 0, 0, 2, 4, 8, 0, 0, 0, 4 };

    guint i, type;

    for (i = 0; i < nblocks; i++) {
        type = header[i].type;
        if (type < G_N_ELEMENTS(sizes)) {
            if (sizes[type]) {
                if (header[i].size != sizes[type]) {
                    err_INVALID(error, header[i].name);
                    return FALSE;
                }
            }
            else if (type == OPD_DIRECTORY) {
                g_set_error(error, GWY_MODULE_FILE_ERROR,
                            GWY_MODULE_FILE_ERROR_DATA,
                            _("Nested directories found"));
                return FALSE;
            }
            else if (type == OPD_ARRAY) {
                guint xres, yres;

                /* array params */
                if (header[i].size < 3*2) {
                    err_INVALID(error, header[i].name);
                    return FALSE;
                }
                /* array contents */
                get_array_params(header[i].data, &xres, &yres, &type);
                gwy_debug("%s xres=%u yres=%u type=%u size=%u",
                          header[i].name, xres, yres, type, header[i].size);
                if (header[i].size < 3*2 + xres*yres*type) {
                    err_INVALID(error, header[i].name);
                    return FALSE;
                }
            }
            else if (type == OPD_TEXT) {
                /* Nothing to do here, text can fill the field completely */
            }
            else {
                g_warning("Unknown item type %u", type);
            }
        }
    }

    return TRUE;
}

static const guchar*
get_array_params(const guchar *p, guint *xres, guint *yres, OPDArrayType *type)
{
    *xres = gwy_get_guint16_le(&p);
    *yres = gwy_get_guint16_le(&p);
    *type = gwy_get_guint16_le(&p);
    switch (*type) {
        case OPD_ARRAY_FLOAT:
        case OPD_ARRAY_INT16:
        case OPD_ARRAY_BYTE:
        break;

        default:
        g_warning("Unknown array type %u", *type);
        break;
    }

    return p;
}

static guint
find_block(const OPDBlock *header,
           guint nblocks,
           const gchar *name)
{
    unsigned int i;

    for (i = 0; i < nblocks; i++) {
        if (gwy_strequal(header[i].name, name))
            return i;
    }
    return nblocks;
}

/***** ASCII data *********************************************************/

static gint
opd_asc_detect(const GwyFileDetectInfo *fileinfo,
               gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase,
                                EXTENSION_ASC) ? 10 : 0;

    if (fileinfo->file_size < MAGIC_ASC_SIZE + 2
        || memcmp(fileinfo->head, MAGIC_ASC, MAGIC_ASC_SIZE) != 0)
        return 0;

    return 100;
}

/* FIXME: This is woefuly confusing spaghetti. */
static GwyContainer*
opd_asc_load(const gchar *filename,
             G_GNUC_UNUSED GwyRunType mode,
             GError **error)
{
    GwyContainer *container = NULL;
    GwyDataField *dfield = NULL, *mfield = NULL;
    gchar *line, *p, *s, *buffer = NULL;
    GHashTable *hash = NULL;
    guint j, xres = 0, yres = 0;
    OPDArrayFormat format;
    gint tmp;
    gboolean real_units, is_float;
    gsize size;
    GError *err = NULL;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        goto fail;
    }

    p = buffer;
    line = gwy_str_next_line(&p);
    if (!g_str_has_prefix(line, MAGIC_ASC)) {
        err_FILE_TYPE(error, "Wyko ASC data");
        goto fail;
    }
    if (sscanf(line + MAGIC_ASC_SIZE, "%d %d %d",
               &tmp, &real_units, &is_float) != 3) {
        err_INVALID(error, "Data File Format");
        goto fail;
    }
    format = tmp;
    gwy_debug("array format %d, real units %d, is float %d",
              format, real_units, is_float);
    if (format < 0 || format > 2) {
        err_UNSUPPORTED(error, "Array Format");
        goto fail;
    }

    container = gwy_container_new();
    hash = g_hash_table_new(g_str_hash, g_str_equal);
    j = 0;
    while ((s = line = gwy_str_next_line(&p))) {
        /* XXX: make noise */
        if (!(s = strchr(s, '\t')))
            continue;

        *s = '\0';
        s++;

        if (gwy_strequal(line, "X Size")) {
            xres = atoi(s);
            gwy_debug("xres=%u", xres);
            continue;
        }
        if (gwy_strequal(line, "Y Size")) {
            yres = atoi(s);
            gwy_debug("yres=%u", yres);
            continue;
        }

        /* Skip type and length, they seem useless in the ASCII file */
        /* XXX: make noise */
        if (!(s = strchr(s, '\t')))
            continue;
        s++;
        if (!(s = strchr(s, '\t')))
            continue;
        s++;

        if (gwy_stramong(line, "OPD", "SAMPLE_DATA", "RAW_DATA", "RAW DATA",
                         "Image", "Intensity", "SecArr_0", "Raw",
                         NULL)) {
            gdouble pixel_size, wavelength, mult = 1.0, aspect = 1.0, zcalib;
            gboolean intensity;
            gchar *k;

            if (!xres) {
                err_MISSING_FIELD(error, "Y Size");
                goto fail;
            }
            if (!yres) {
                err_MISSING_FIELD(error, "X Size");
                goto fail;
            }

            if (!(s = g_hash_table_lookup(hash, "Pixel_size"))
                || !(pixel_size = fabs(g_ascii_strtod(s, NULL)))) {
                err_MISSING_FIELD(error, "Pixel_size");
                goto fail;
            }
            gwy_debug("pixel_size = %g", pixel_size);
            if (!(s = g_hash_table_lookup(hash, "Wavelength"))
                || !(wavelength = fabs(g_ascii_strtod(s, NULL)))) {
                err_MISSING_FIELD(error, "Wavelength");
                goto fail;
            }
            gwy_debug("wavelength = %g", wavelength);

            if ((s = g_hash_table_lookup(hash, "Aspect")))
                aspect = g_ascii_strtod(s, NULL);
            /* Should only occur in integer-data files.  Have not seen any... */
            if ((s = g_hash_table_lookup(hash, "Mult")))
                mult = g_ascii_strtod(s, NULL);

            intensity = gwy_stramong(line,
                                     "Image", "Intensity", "SecArr_0", NULL);

            pixel_size *= Milimeter;
            zcalib = Nanometer*mult;
            if (!intensity && !real_units)
                zcalib *= wavelength;

            dfield = mfield = NULL;
            dfield = get_asc_data_field(&p, format, xres, yres,
                                        pixel_size, aspect, zcalib,
                                        intensity ? "" : "m",
                                        intensity ? NULL : &mfield);
            if (!dfield) {
                err_TRUNCATED_PART(error, line);
                GWY_OBJECT_UNREF(container);
                goto fail;
            }

            gwy_container_set_object(container, gwy_app_get_data_key_for_id(j),
                                     dfield);
            if (mfield)
                gwy_container_set_object(container,
                                         gwy_app_get_mask_key_for_id(j),
                                         mfield);

            s = g_strdup_printf("%s/title",
                                g_quark_to_string(gwy_app_get_data_key_for_id(j)));
            if (intensity)
                gwy_container_set_string_by_name(container, s, g_strdup(line));
            else if ((k = g_hash_table_lookup(hash, "Title")))
                gwy_container_set_string_by_name(container, s, g_strdup(k));
            else
                gwy_app_channel_title_fall_back(container, j);
            g_free(s);

            s = g_strdup_printf("%s/realsquare",
                                g_quark_to_string(gwy_app_get_data_key_for_id(j)));
            gwy_container_set_boolean_by_name(container, s, TRUE);
            g_free(s);

            gwy_file_channel_import_log_add(container, j, NULL,
                                            filename);

            j++;
            continue;
        }

        if (gwy_strequal(line, "Block Name"))
            continue;

        gwy_debug("<%s> = <%s>", line, s);

        g_hash_table_insert(hash, line, s);
    }

    if (j) {
        GwyContainer *meta = gwy_container_new();

        g_hash_table_foreach(hash, store_asc_meta, meta);
        clone_meta(container, meta, j);
        g_object_unref(meta);
    }
    else {
        GWY_OBJECT_UNREF(container);
        err_NO_DATA(error);
    }

fail:
    GWY_OBJECT_UNREF(dfield);
    GWY_OBJECT_UNREF(mfield);
    g_free(buffer);
    if (hash)
        g_hash_table_destroy(hash);

    return container;
}

static void
store_asc_meta(gpointer key,
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

static GwyDataField*
get_asc_data_field(gchar **p,
                   OPDArrayFormat format, guint xres, guint yres,
                   gdouble pixel_size, gdouble aspect, gdouble zcalib,
                   const gchar *zunits,
                   GwyDataField **maskfield)
{
    GwyDataField *dfield, *mfield;
    GwySIUnit *siunit;
    gdouble *data, *mdata;
    const gchar *sprev;
    guint mcount, i, j;
    gchar *s;

    if (maskfield)
        *maskfield = NULL;

    dfield = gwy_data_field_new(xres, yres,
                                xres*pixel_size, aspect*yres*pixel_size, FALSE);

    siunit = gwy_si_unit_new("m");
    gwy_data_field_set_si_unit_xy(dfield, siunit);
    g_object_unref(siunit);

    siunit = gwy_si_unit_new(zunits);
    gwy_data_field_set_si_unit_z(dfield, siunit);
    g_object_unref(siunit);

    mfield = gwy_data_field_new_alike(dfield, FALSE);
    gwy_data_field_fill(mfield, 1.0);

    data = gwy_data_field_get_data(dfield);
    mdata = gwy_data_field_get_data(mfield);

    if (format == OPD_XYZ_PIXEL || format == OPD_XYZ_REAL) {
        gwy_debug("assuming XYZ format");
        for (j = 0; j < xres; j++) {
            for (i = yres; i; i--) {
                if (!(s = gwy_str_next_line(p)))
                    goto fail;

                /* Y and X, we just ignore them */
                if (!(s = strchr(s, '\t')))
                    goto fail;
                s++;
                if (!(s = strchr(s, '\t')))
                    goto fail;
                s++;

                sprev = s;
                if (strncmp(s, "Bad", 3) == 0) {
                    mdata[(i - 1)*xres + j] = 0.0;
                    s += 3;
                }
                else
                    data[(i - 1)*xres + j] = g_ascii_strtod(s, &s)*zcalib;

                if (s == sprev)
                    goto fail;
            }
        }
    }
    else if (format == OPD_STANDARD_IMAGE) {
        gwy_debug("assuming data matrix format");
        for (j = 0; j < xres; j++) {
            if (!(s = gwy_str_next_line(p)))
                goto fail;

            for (i = yres; i; i--) {
                sprev = s;
                if (strncmp(s, "Bad", 3) == 0) {
                    mdata[(i - 1)*xres + j] = 0.0;
                    s += 3;
                }
                else
                    data[(i - 1)*xres + j] = g_ascii_strtod(s, &s)*zcalib;

                while (g_ascii_isspace(*s))
                    s++;

                if (s == sprev)
                    goto fail;
            }
        }
    }
    else {
        g_assert_not_reached();
    }

    mcount = gwy_app_channel_remove_bad_data(dfield, mfield);

    if (maskfield && mcount)
        *maskfield = mfield;
    else
        g_object_unref(mfield);

    return dfield;

fail:
    g_object_unref(dfield);
    g_object_unref(mfield);
    return NULL;
}

/***** Common *************************************************************/

static void
clone_meta(GwyContainer *container, GwyContainer *meta, guint nchannels)
{
    guint i;
    gchar s[32];

    if (!gwy_container_get_n_items(meta))
        return;

    /* Simply store identical metadata for each channel */
    for (i = 0; i < nchannels; i++) {
        GwyContainer *m = gwy_container_duplicate(meta);
        g_snprintf(s, sizeof(s), "/%u/meta", i);
        gwy_container_set_object_by_name(container, s, m);
        g_object_unref(m);
    }
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
