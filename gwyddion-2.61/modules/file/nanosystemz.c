/*
 *  $Id: nanosystemz.c 21622 2018-11-13 10:44:18Z yeti-dn $
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
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "get.h"

/* FIXME: This may be actually some version number and not fixed...
 * It is equal to 200, which gives a strong version-like vibe.
 * Furthermore, the zero second byte may be the length of some seldom seen
 * string, not a part of a two-byte item. */
#define MAGIC "\xc8\x00"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

/**
 * [FILE-MAGIC-USERGUIDE]
 * NanoSystem profilometry
 * .spm
 * Read
 **/

enum {
    FIXED_HEADER_SIZE = 38,
};

typedef enum {
    XRES_IS_MISSING = 0,
    XRES_IS_PRESENT = 13,
} NanosystemzXresFlag;

typedef enum {
    MEASUREMENT_PSI      = 1,
    MEASUREMENT_WSI_WSIE = 4,
} NanosystemzMeasurement;

typedef struct {
    guchar magic[2];
    gchar *comment;
    gchar *datetime;
    gchar *setup;
    gchar *string1;
    NanosystemzMeasurement meas_type;
    NanosystemzXresFlag xres_flag;
    /* The fixed part -- probably */
    guint xres;         /* XXX: Not present sometimes??? */
    guint yres;
    guint another_one;  /* Seems equal to 1 */
    gdouble dx;
    gdouble dy;
    guchar zeros[8];    /* Completely zeros -- more strings? */
    gdouble scale;      /* No idea, but it's another reasonable float */
    guchar some_more_zeros[4];
} NanosystemzHeader;

static gboolean      module_register           (void);
static gint          nanosystemz_detect        (const GwyFileDetectInfo *fileinfo,
                                                gboolean only_name);
static GwyContainer* nanosystemz_load          (const gchar *filename,
                                                GwyRunType mode,
                                                GError **error);
static gsize         nanosystemz_read_header   (NanosystemzHeader *header,
                                                const guchar *buffer,
                                                gsize size,
                                                GError **error);
static void          nanosystemz_header_free   (NanosystemzHeader *header);
static gboolean      nanosystemz_check_datetime(const NanosystemzHeader *header);

static GwyContainer* create_meta(const NanosystemzHeader *header);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports NanoSystem profilometry data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.1",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, nanosystemz)

static gboolean
module_register(void)
{
    gwy_file_func_register("nanosystemz",
                           N_("NanoSystem profilometry files (.spm)"),
                           (GwyFileDetectFunc)&nanosystemz_detect,
                           (GwyFileLoadFunc)&nanosystemz_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
nanosystemz_detect(const GwyFileDetectInfo *fileinfo,
                   gboolean only_name)
{
    NanosystemzHeader header;
    guint hlen, n;
    gint score = 0;

    if (only_name)
        return 0;

    if (fileinfo->buffer_len <= FIXED_HEADER_SIZE + 2 + 5
        || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    gwy_clear(&header, 1);
    hlen = nanosystemz_read_header(&header,
                                   fileinfo->head, fileinfo->buffer_len, NULL);
    if (!hlen)
        goto fail;

    n = header.xres*header.yres;
    if (hlen + n*(sizeof(guint32) + sizeof(guchar)) != fileinfo->file_size)
        goto fail;

    if (!nanosystemz_check_datetime(&header))
        goto fail;

    score = 95;

fail:
    nanosystemz_header_free(&header);

    return score;
}

static GwyContainer*
nanosystemz_load(const gchar *filename,
                 G_GNUC_UNUSED GwyRunType mode,
                 GError **error)
{
    GwyContainer *container = NULL, *meta;
    guchar *buffer = NULL;
    gsize imagesize, masksize, n, hlen, size = 0;
    GError *err = NULL;
    NanosystemzHeader header;
    GwyDataField *dfield, *mask;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    gwy_clear(&header, 1);
    if (!(hlen = nanosystemz_read_header(&header, buffer, size, error)))
        goto fail;
    if (memcmp(header.magic, MAGIC, MAGIC_SIZE) != 0) {
        err_FILE_TYPE(error, "Nanosystemz");
        goto fail;
    }

    /* Use negated positive conditions to catch NaNs */
    if (!((header.dx = fabs(header.dx)) > 0)) {
        g_warning("Real x pixel size is 0.0, fixing to 1.0");
        header.dx = 1.0;
    }
    header.dx *= 1e-3;
    if (!((header.dy = fabs(header.dy)) > 0)) {
        g_warning("Real y pixel size is 0.0, fixing to 1.0");
        header.dy = 1.0;
    }
    header.dy *= 1e-3;

    n = header.xres*header.yres;
    imagesize = n*sizeof(guint32);
    masksize = n;
    if (err_SIZE_MISMATCH(error, hlen + imagesize + masksize, size, TRUE))
        goto fail;

    container = gwy_container_new();
    dfield = gwy_data_field_new(header.xres, header.yres,
                                header.xres*header.dx, header.yres*header.dy,
                                FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
    mask = gwy_data_field_new_alike(dfield, FALSE);

    gwy_convert_raw_data(buffer + hlen, n, 1,
                         GWY_RAW_DATA_FLOAT, G_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield), 1e-6, 0.0);
    gwy_data_field_invert(dfield, TRUE, FALSE, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "m");
    gwy_container_set_object(container,
                             gwy_app_get_data_key_for_id(0), dfield);
    g_object_unref(dfield);

    /* FIXME: I see values
     * - 1 for good data
     * - 2 for some kind of bad data (WSIE)
     * - 8 for some kind of bad data (PSI and WSIE)
     * Or something like
     * that.  The probably correspond to some bits set -- masking anything
     * that is not equal to 1 (presumably good data) seems reasonable. */
    gwy_convert_raw_data(buffer + hlen + imagesize, n, 1,
                         GWY_RAW_DATA_UINT8, G_LITTLE_ENDIAN,
                         gwy_data_field_get_data(mask), 1.0, -1.0);
    gwy_data_field_invert(mask, TRUE, FALSE, FALSE);
    gwy_container_set_object(container,
                             gwy_app_get_mask_key_for_id(0), mask);
    g_object_unref(mask);

    meta = create_meta(&header);
    gwy_container_set_object(container,
                             gwy_app_get_data_meta_key_for_id(0), meta);
    g_object_unref(meta);

    gwy_app_channel_title_fall_back(container, 0);
    gwy_file_channel_import_log_add(container, 0, NULL, filename);

fail:
    nanosystemz_header_free(&header);
    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

static gboolean
read_pascal_string(const guchar *buffer, const guchar **p, gsize size,
                   gchar **s,
                   GError **error)
{
    guint len;

    if (size - (*p - buffer) < 1) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("File is truncated."));
        return FALSE;
    }
    len = *((*p)++);

    /* Represent empty strings as NULL; they are common. */
    if (!len) {
        gwy_debug("NULL string");
        *s = NULL;
        return TRUE;
    }

    if (size - (*p - buffer) < len) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("File is truncated."));
        return FALSE;
    }
    *s = g_new(gchar, len+1);
    gwy_assign(*s, *p, len);
    (*s)[len] = '\0';
    *p += len;
    gwy_debug("string of length %u <%s>", len, *s);

    return TRUE;
}

static gsize
nanosystemz_read_header(NanosystemzHeader *header,
                        const guchar *buffer, gsize size,
                        GError **error)
{
    const guchar *p = buffer;

    get_CHARARRAY(header->magic, &p);
    if (!read_pascal_string(buffer, &p, size, &header->comment, error)
        || !read_pascal_string(buffer, &p, size, &header->datetime, error)
        || !read_pascal_string(buffer, &p, size, &header->setup, error)
        || !read_pascal_string(buffer, &p, size, &header->string1, error))
        return 0;

    if (size - (p - buffer) < FIXED_HEADER_SIZE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("File is truncated."));
        return 0;
    }
    header->meas_type = *(p++);
    gwy_debug("meas_type %u", header->meas_type);
    header->xres_flag = *(p++);
    gwy_debug("xres_flag %u", header->xres_flag);
    if (header->xres_flag == XRES_IS_MISSING) {
        header->yres = gwy_get_guint32_le(&p);
        header->xres = 640;   /* FIXME FIXME FIXME */
    }
    else {
        header->xres = gwy_get_guint32_le(&p);
        header->yres = gwy_get_guint32_le(&p);
    }
    gwy_debug("res %dx%d", header->xres, header->yres);
    header->another_one = gwy_get_guint32_le(&p);
    gwy_debug("another_one %u", header->another_one);
    header->dx = gwy_get_gfloat_le(&p);
    header->dy = gwy_get_gfloat_le(&p);
    gwy_debug("real pixel %gx%g", header->dx, header->dy);
    get_CHARARRAY(header->zeros, &p);
    gwy_debug("zeros %02x %02x %02x %02x %02x %02x %02x %02x",
              header->zeros[0], header->zeros[1],
              header->zeros[2], header->zeros[3],
              header->zeros[4], header->zeros[5],
              header->zeros[6], header->zeros[7]);
    header->scale = gwy_get_gfloat_le(&p);
    gwy_debug("scale %g", header->scale);
    get_CHARARRAY(header->some_more_zeros, &p);
    gwy_debug("some_more_zeros %02x %02x %02x %02x",
              header->some_more_zeros[0], header->some_more_zeros[1],
              header->some_more_zeros[2], header->some_more_zeros[3]);

    return (gsize)(p - buffer);
}

static void
nanosystemz_header_free(NanosystemzHeader *header)
{
    g_free(header->comment);
    g_free(header->datetime);
    g_free(header->setup);
    g_free(header->string1);
}

static gboolean
nanosystemz_check_datetime(const NanosystemzHeader *header)
{
    const gchar *dt = header->datetime;

    if (!dt || strlen(dt) != 19)
        return FALSE;

    if (!g_ascii_isdigit(dt[0]) || !g_ascii_isdigit(dt[1])
        || !g_ascii_isdigit(dt[2]) || !g_ascii_isdigit(dt[3])
        || dt[4] != '-'
        || !g_ascii_isdigit(dt[5]) || !g_ascii_isdigit(dt[6])
        || dt[7] != '-'
        || !g_ascii_isdigit(dt[8]) || !g_ascii_isdigit(dt[9])
        || dt[10] != ' '
        || !g_ascii_isdigit(dt[11]) || !g_ascii_isdigit(dt[12])
        || dt[13] != ':'
        || !g_ascii_isdigit(dt[14]) || !g_ascii_isdigit(dt[15])
        || dt[16] != ':'
        || !g_ascii_isdigit(dt[17]) || !g_ascii_isdigit(dt[18]))
        return FALSE;

    return TRUE;
}

static GwyContainer*
create_meta(const NanosystemzHeader *header)
{
    GwyContainer *meta = gwy_container_new();
    const gchar *s;
    gchar *v;

    if ((s = header->comment))
        gwy_container_set_const_string_by_name(meta, "Comment", s);
    if ((s = header->datetime))
        gwy_container_set_const_string_by_name(meta, "Date and Time", s);
    if ((s = header->setup))
        gwy_container_set_const_string_by_name(meta, "Setup", s);
    if ((s = header->string1))
        gwy_container_set_const_string_by_name(meta, "String1", s);

    v = g_strdup_printf("%g", header->scale);
    gwy_container_set_string_by_name(meta, "Value1", v);

    v = g_strdup_printf("%.1f nm", header->dx/1e-9);
    gwy_container_set_string_by_name(meta, "Pixel size X", v);

    v = g_strdup_printf("%.1f nm", header->dy/1e-9);
    gwy_container_set_string_by_name(meta, "Pixel size Y", v);

    return meta;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
