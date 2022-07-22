/*
 *  $Id: pnifile.c 20866 2018-03-20 10:15:35Z yeti-dn $
 *  Copyright (C) 2006-2018 David Necas (Yeti), Petr Klapetek.
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
 * <mime-type type="application/x-pni-spm">
 *   <comment>PNI SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="\0\0\0\0001.0"/>
 *     <match type="string" offset="0" value="\315\315\315\3151.0"/>
 *     <match type="string" offset="0" value="\0\0\0\0002.0"/>
 *   </magic>
 *   <glob pattern="*.pni"/>
 *   <glob pattern="*.PNI"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Nano-R PNI
 * # Have at least two variants, the first might be prone to false positives.
 * 0 string \0\0\0\0001.0 Pacific Nanotechlology Nano-R SPM data
 * 0 string \xcd\xcd\xcd\xcd1.0 Pacific Nanotechlology Nano-R SPM data
 * 0 string \0\0\0\0002.0 Pacific Nanotechlology Nano-R SPM data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Pacific Nanotechnology PNI
 * .pni
 * Read
 **/

#include "config.h"
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

#define EXTENSION ".pni"

#define MAGIC1 "1.0"
#define MAGIC2 "2.0"
#define MAGIC_SIZE (sizeof(MAGIC1)-1)

#define Nanometer (1e-9)
#define Micrometer (1e-6)
#define Milivolt (1e-3)

enum {
    /* Absolute in file */
    HEADER_START       = 0x0090,
    /* Palette is 3x256 8bit r,g,b components. */
    PALETTE_START      = 0x00ca,
    /* Thumbnail is 64x64, 8 bits per sample */
    THUMB_START        = 0x03ca,
    DATA_HEADER_START  = 0x13ca,
    /* Data is 16 bits per sample */
    DATA_START         = 0x1c90
};

typedef enum {
    DIRECTION_FORWARD = 0,
    DIRECTION_REVERSE = 1
} PNIDirection;

typedef enum {
    DATA_TYPE1_HGT = 1,
    DATA_TYPE1_L_R = 2,
    DATA_TYPE1_SEN = 3,
    DATA_TYPE1_DEM = 6,
    DATA_TYPE1_ERR = 8,

    DATA_TYPE2_ZACTUATOR = 0,
    DATA_TYPE2_ERROR     = 2,
    DATA_TYPE2_PHASE     = 3,
} PNIDataType;

typedef enum {
    VALUE_TYPE_NM = 1,
    VALUE_TYPE_MV = 4
} PNIValueType;

static gboolean      module_register(void);
static gint          pni_detect     (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name);
static GwyContainer* pni_load       (const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Pacific Nanotechnology PNI data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.8",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, pnifile)

static gboolean
module_register(void)
{
    gwy_file_func_register("pnifile",
                           N_("PNI files (.pni)"),
                           (GwyFileDetectFunc)&pni_detect,
                           (GwyFileLoadFunc)&pni_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
pni_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    guint32 firstbyte, xres, yres;
    const guchar *p;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;

    if (fileinfo->buffer_len < 0xa0)
        return 0;

    firstbyte = fileinfo->head[0];
    if ((firstbyte != 0 && firstbyte != 0315) /* octal */
        || fileinfo->head[1] != firstbyte
        || fileinfo->head[2] != firstbyte
        || fileinfo->head[3] != firstbyte)
        return 0;

    if ((memcmp(fileinfo->head + 4, MAGIC1, MAGIC_SIZE) != 0
         && memcmp(fileinfo->head + 4, MAGIC2, MAGIC_SIZE) != 0))
        return 0;

    p = fileinfo->head + 0x90;
    xres = gwy_get_guint32_le(&p);
    yres = gwy_get_guint32_le(&p);
    gwy_debug("%u %u", xres, yres);
    if (fileinfo->file_size == DATA_START + 2*xres*yres)
        return 95;

    return 0;
}

static GwyContainer*
pni_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    enum {
        /* Absolute in file (file header). */
        RESOLUTION_OFFSET = 0x90,
        REAL_XSIZE_OFFSET = 0xa8,
        REAL_YSIZE_OFFSET = 0xb0,
        VALUE_SCALE_OFFSET = 0xbc,

        /* Relative to DATA_HEADER_START. */
        DATA_TYPE_OFFSET1 = 0x000a,
        /* XXX: There are two candidate positions for data type, 0x01aa and
         * 0x019e.  Cannot tell which is value type and which something
         * strongly correlated to it for available files. */
        DATA_TYPE_OFFSET2 = 0x019e,
        DIRECTION_OFFSET1 = 0x000e,
        VALUE_TYPE_OFFSET1 = 0x0046,
        VALUE_TYPE_OFFSET2 = 0x01b2,
    };

    static const GwyEnum titles1[] = {
        { "Height",  DATA_TYPE1_HGT, },
        { "Sens",    DATA_TYPE1_SEN, },
        { "Dem",     DATA_TYPE1_DEM, },
        { "Error",   DATA_TYPE1_ERR, },
        { "L-R",     DATA_TYPE1_L_R, },
    };

    static const GwyEnum titles2[] = {
        { "Phase",      DATA_TYPE2_PHASE,     },
        { "Z Error",    DATA_TYPE2_ERROR,     },
        { "Z Actuator", DATA_TYPE2_ZACTUATOR, },
    };

    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    const guchar *p;
    gint xres, yres, version;
    PNIValueType value_type;
    PNIDirection direction;
    PNIDataType data_type;
    gdouble xreal, yreal, zscale;
    GwySIUnit *siunit;
    const gchar *title;
    gchar *s;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (size < DATA_START + 2) {
        err_TOO_SHORT(error);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    if (memcmp(buffer + 4, MAGIC1, MAGIC_SIZE) == 0)
        version = 1;
    else if (memcmp(buffer + 4, MAGIC2, MAGIC_SIZE) == 0)
        version = 2;
    else {
        err_FILE_TYPE(error, "PNI");
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    /* Information read from the file header. */
    p = buffer + RESOLUTION_OFFSET;
    xres = gwy_get_guint32_le(&p);
    yres = gwy_get_guint32_le(&p);
    gwy_debug("%d %d", xres, yres);
    if (err_DIMENSION(error, xres)
        || err_DIMENSION(error, yres)
        || err_SIZE_MISMATCH(error, DATA_START + 2*xres*yres, size, TRUE)) {
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    p = buffer + REAL_XSIZE_OFFSET;
    xreal = gwy_get_gfloat_le(&p);
    p = buffer + REAL_YSIZE_OFFSET;
    yreal = gwy_get_gfloat_le(&p);
    /* Use negated positive conditions to catch NaNs */
    if (!((xreal = fabs(xreal)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        xreal = 1.0;
    }
    if (!((yreal = fabs(yreal)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        yreal = 1.0;
    }
    xreal *= Micrometer;
    yreal *= Micrometer;

    p = buffer + VALUE_SCALE_OFFSET;
    zscale = gwy_get_gfloat_le(&p);

    /* Information read from the data header. */
    p = buffer + DATA_HEADER_START;
    if (version == 1)
        data_type = p[DATA_TYPE_OFFSET1];
    else
        data_type = p[DATA_TYPE_OFFSET2];

    if (version == 1)
        value_type = p[VALUE_TYPE_OFFSET1];
    else
        value_type = p[VALUE_TYPE_OFFSET2];

    if (version == 1)
        direction = p[DIRECTION_OFFSET1];
    else
        direction = DIRECTION_FORWARD;  /* XXX: Whatever. */

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
    gwy_convert_raw_data(buffer + DATA_START, xres*yres, 1,
                         GWY_RAW_DATA_SINT16, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield), zscale, 0.0);

    gwy_file_abandon_contents(buffer, size, NULL);

    siunit = gwy_si_unit_new("m");
    gwy_data_field_set_si_unit_xy(dfield, siunit);
    g_object_unref(siunit);

    switch (value_type) {
        case VALUE_TYPE_NM:
        siunit = gwy_si_unit_new("m");
        gwy_data_field_multiply(dfield, Nanometer);
        break;

        case VALUE_TYPE_MV:
        siunit = gwy_si_unit_new("V");
        gwy_data_field_multiply(dfield, Milivolt);
        break;

        default:
        g_warning("Value type %d is unknown", value_type);
        siunit = gwy_si_unit_new(NULL);
        break;
    }
    gwy_data_field_set_si_unit_z(dfield, siunit);
    g_object_unref(siunit);

    container = gwy_container_new();
    gwy_container_set_object_by_name(container, "/0/data", dfield);
    g_object_unref(dfield);

    if (version == 1)
        title = gwy_enum_to_string(data_type, titles1, G_N_ELEMENTS(titles1));
    else
        title = gwy_enum_to_string(data_type, titles2, G_N_ELEMENTS(titles2));

    if (title) {
        s = g_strdup_printf("%s (%s)",
                            title,
                            direction ? "Backward" : "Forward");
        gwy_container_set_string_by_name(container, "/0/data/title", s);
    }
    else
        g_warning("Data type %d is unknown", data_type);

    /* TODO: Put version to metadata? */

    gwy_file_channel_import_log_add(container, 0, NULL, filename);

    return container;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
