/*
 *  $Id: seiko.c 22642 2019-11-03 11:46:07Z yeti-dn $
 *  Copyright (C) 2006 David Necas (Yeti), Markus Pristovsek.
 *  E-mail: yeti@gwyddion.net, prissi@gift.physik.tu-berlin.de.
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
 * <mime-type type="application/x-seiko-spm">
 *   <comment>Seiko SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="SPIZ000AFM"/>
 *     <match type="string" offset="0" value="SPIZ000DFM"/>
 *     <match type="string" offset="0" value="SPIZ000STM"/>
 *     <match type="string" offset="0" value="NPXZ000AFM"/>
 *     <match type="string" offset="0" value="NPXZ000DFM"/>
 *   </magic>
 *   <glob pattern="*.xqb"/>
 *   <glob pattern="*.XQB"/>
 *   <glob pattern="*.xqd"/>
 *   <glob pattern="*.XQD"/>
 *   <glob pattern="*.xqt"/>
 *   <glob pattern="*.XQT"/>
 *   <glob pattern="*.xqp"/>
 *   <glob pattern="*.XQP"/>
 *   <glob pattern="*.xqj"/>
 *   <glob pattern="*.XQJ"/>
 *   <glob pattern="*.xqi"/>
 *   <glob pattern="*.XQI"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Seiko
 * # Several variants, not sure if assignable to specific microscopy types.
 * # More can perhaps exist.
 * 0 string SPIZ000AFM Seiko SII SPM data
 * 0 string SPIZ000DFM Seiko SII SPM data
 * 0 string SPIZ000STM Seiko SII SPM data
 * 0 string NPXZ000AFM Seiko SII SPM data
 * 0 string NPXZ000DFM Seiko SII SPM data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Seiko SII
 * .xqb, .xqd, .xqt, .xqp, .xqj, .xqi
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
#include "get.h"

#define MAGIC1 "SPIZ000AFM"
#define MAGIC2 "SPIZ000DFM"
#define MAGIC3 "NPXZ000AFM"
#define MAGIC4 "NPXZ000DFM"
#define MAGIC5 "SPIZ000STM"
#define MAGIC_SIZE (sizeof(MAGIC1)-1)

#define EXTENSION1 ".xqb"
#define EXTENSION2 ".xqd"
#define EXTENSION3 ".xqt"
#define EXTENSION4 ".xqp"
#define EXTENSION5 ".xqj"
#define EXTENSION6 ".xqi"

#define Nanometer 1e-9
#define NanoAmpere 1e-9

enum { HEADER_SIZE = 2944 };

typedef enum {
    SEIKO_TOPOGRAPHY = 0,
    SEIKO_PHASE      = 1,
    SEIKO_CURRENT    = 2,
} SeikoDataType;

static gboolean      module_register   (void);
static gint          seiko_detect      (const GwyFileDetectInfo *fileinfo,
                                        gboolean only_name);
static GwyContainer* seiko_load        (const gchar *filename,
                                        GwyRunType mode,
                                        GError **error);
static GwyDataField* read_data_field   (const guchar *buffer,
                                        guint size,
                                        SeikoDataType datatype,
                                        GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Seiko XQB, XQD, XQT and XQP files."),
    "Yeti <yeti@gwyddion.net>",
    "0.13",
    "David Nečas (Yeti) & Markus Pristovsek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, seiko)

static gboolean
module_register(void)
{
    gwy_file_func_register("seiko",
                           N_("Seiko files (.xqb, .xqd, .xqt, .xqp)"),
                           (GwyFileDetectFunc)&seiko_detect,
                           (GwyFileLoadFunc)&seiko_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
seiko_detect(const GwyFileDetectInfo *fileinfo,
             gboolean only_name)
{
    gint score = 0;

    if (only_name) {
        if (g_str_has_suffix(fileinfo->name_lowercase, EXTENSION1)
            || g_str_has_suffix(fileinfo->name_lowercase, EXTENSION2)
            || g_str_has_suffix(fileinfo->name_lowercase, EXTENSION3)
            || g_str_has_suffix(fileinfo->name_lowercase, EXTENSION4)
            || g_str_has_suffix(fileinfo->name_lowercase, EXTENSION5)
            || g_str_has_suffix(fileinfo->name_lowercase, EXTENSION6))
            return 20;
        return 0;
    }

    if (fileinfo->buffer_len > MAGIC_SIZE
        && fileinfo->file_size >= HEADER_SIZE + 2
        && (memcmp(fileinfo->head, MAGIC1, MAGIC_SIZE) == 0
            || memcmp(fileinfo->head, MAGIC2, MAGIC_SIZE) == 0
            || memcmp(fileinfo->head, MAGIC3, MAGIC_SIZE) == 0
            || memcmp(fileinfo->head, MAGIC4, MAGIC_SIZE) == 0
            || memcmp(fileinfo->head, MAGIC5, MAGIC_SIZE) == 0))
        score = 100;

    return score;
}

static GwyContainer*
seiko_load(const gchar *filename,
           G_GNUC_UNUSED GwyRunType mode,
           GError **error)
{
    enum {
        COMMENT_OFFSET = 0x480,
        COMMENT_SIZE = 0x80,
    };

    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    SeikoDataType datatype = SEIKO_TOPOGRAPHY;
    gchar *comment;
    const gchar *extension;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (size < HEADER_SIZE + 2) {
        err_TOO_SHORT(error);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    if (memcmp(buffer, MAGIC1, MAGIC_SIZE) != 0
        && memcmp(buffer, MAGIC2, MAGIC_SIZE) != 0
        && memcmp(buffer, MAGIC3, MAGIC_SIZE) != 0
        && memcmp(buffer, MAGIC4, MAGIC_SIZE) != 0
        && memcmp(buffer, MAGIC5, MAGIC_SIZE) != 0) {
        err_FILE_TYPE(error, "Seiko");
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    /* FIXME: I was not able to reverse-engineer what identifies the type.
     * May need a large set of files.  */
    if ((extension = strrchr(filename, '.'))) {
       if (gwy_stramong(extension+1, "xqp", "XQP", "xqpx", "XQPX", NULL))
           datatype = SEIKO_PHASE;
       else if (gwy_stramong(extension+1, "xqi", "XQI", "xqix", "XQIX", NULL))
           datatype = SEIKO_CURRENT;
    }

    dfield = read_data_field(buffer, size, datatype, error);
    if (!dfield) {
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    container = gwy_container_new();
    gwy_container_set_object_by_name(container, "/0/data", dfield);
    g_object_unref(dfield);
    comment = g_strndup(buffer + COMMENT_OFFSET, COMMENT_SIZE);
    g_strstrip(comment);
    if (strlen(comment))
        gwy_container_set_string_by_name(container, "/0/data/title", comment);
    else {
        g_free(comment);
        gwy_app_channel_title_fall_back(container, 0);
    }

    gwy_app_channel_check_nonsquare(container, 0);
    gwy_file_channel_import_log_add(container, 0, NULL, filename);

    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

static GwyDataField*
read_data_field(const guchar *buffer,
                guint size,
                SeikoDataType datatype,
                GError **error)
{
    enum {
        VERSION_OFFSET   = 0x10,
        ENDFILE_OFFSET   = 0x14,
        DATASTART_OFFSET = 0x18,
        XRES_OFFSET      = 0x57a,
        YRES_OFFSET      = 0x57c,
        XSCALE_OFFSET    = 0x98,
        YSCALE_OFFSET    = 0xa0,
        ZSCALE_OFFSET    = 0xa8,
        ZOFFSET_OFFSET   = 0xe0,
    };
    gint xres, yres;
    G_GNUC_UNUSED guint version;
    guint endfile, datastart, imgsize;
    gdouble xreal, yreal, q, z0;
    GwyDataField *dfield;
    GwySIUnit *siunit;
    const guchar *p;

    p = buffer + VERSION_OFFSET;
    version = gwy_get_guint32_le(&p);
    p = buffer + ENDFILE_OFFSET;
    endfile = gwy_get_guint32_le(&p);
    p = buffer + DATASTART_OFFSET;
    datastart = gwy_get_guint32_le(&p);
    gwy_debug("version: %u, endfile: %u, datastart: %u",
              version, endfile, datastart);

    if (err_SIZE_MISMATCH(error, endfile, size, TRUE))
        return NULL;

    p = buffer + XRES_OFFSET;
    xres = gwy_get_guint16_le(&p);
    p = buffer + YRES_OFFSET;
    yres = gwy_get_guint16_le(&p);
    gwy_debug("xres: %d, yres %d", xres, yres);
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        return NULL;

    imgsize = xres*yres*sizeof(guint16);
    if (err_SIZE_MISMATCH(error, imgsize, endfile - datastart, TRUE)) {
        /* The XQ?X files can have multiple images.  And each also comes with
         * an extra headers.  We at least try to import the first one. */
        guint nimages = (endfile - datastart)/imgsize;

        gwy_debug("nimages: %u", nimages);
        if (endfile - datastart == nimages*imgsize + (nimages-1)*HEADER_SIZE)
            g_clear_error(error);
        else
            return NULL;
    }

    p = buffer + XSCALE_OFFSET;
    xreal = gwy_get_gdouble_le(&p) * Nanometer;
    p = buffer + YSCALE_OFFSET;
    yreal = gwy_get_gdouble_le(&p) * Nanometer;
    p = buffer + ZSCALE_OFFSET;
    q = gwy_get_gdouble_le(&p);
    if (datatype == SEIKO_TOPOGRAPHY)
        q *= Nanometer;
    else if (datatype == SEIKO_CURRENT)
        q *= NanoAmpere;
    gwy_debug("xscale: %g, yscale: %g, zreal: %g",
              xreal/Nanometer, yreal/Nanometer, q);

    p = buffer + ZOFFSET_OFFSET;
    z0 = -q*gwy_get_gdouble_le(&p);
    gwy_debug("z0: %g", z0);

    xreal *= xres;
    yreal *= yres;

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
    gwy_convert_raw_data(buffer + HEADER_SIZE, xres*yres, 1,
                         GWY_RAW_DATA_UINT16, G_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield), q, z0);
    siunit = gwy_si_unit_new("m");
    gwy_data_field_set_si_unit_xy(dfield, siunit);
    g_object_unref(siunit);

    if (datatype == SEIKO_PHASE)
        siunit = gwy_si_unit_new("deg");
    else if (datatype == SEIKO_CURRENT)
        siunit = gwy_si_unit_new("A");
    else
        siunit = gwy_si_unit_new("m");

    gwy_data_field_set_si_unit_z(dfield, siunit);
    g_object_unref(siunit);

    return dfield;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
