/*
 *  $Id: spmlabf.c 23822 2021-06-10 06:58:57Z yeti-dn $
 *  Copyright (C) 2008 David Necas (Yeti).
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
 * <mime-type type="application/x-spmlab-float-spm">
 *   <comment>SPMLab floating-point SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="[Data Version]\r\nProgram=SPMLab"/>
 *   </magic>
 *   <glob pattern="*.flt"/>
 *   <glob pattern="*.FLT"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # SpmLab floating point.
 * 0 string [Data\ Version]\x0d\x0aProgram=SPMLab SpmLab floating-point SPM data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Thermicroscopes SPMLab floating point
 * .flt
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

/* Not a real magic header, but should catch the stuff */
#define MAGIC "[Data Version]\r\nProgram=SPMLab"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define DATA_MAGIC "\r\n[Data]\r\n"
#define DATA_MAGIC_SIZE (sizeof(DATA_MAGIC)-1)

#define EXTENSION ".flt"

static gboolean      module_register(void);
static gint          slf_detect     (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name);
static GwyContainer* slf_load       (const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);
static GwyContainer* add_metadata   (GHashTable *hash,
                                     ...);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports SPMLab floating-point files."),
    "Yeti <yeti@gwyddion.net>",
    "0.6",
    "David Nečas (Yeti)",
    "2008",
};

GWY_MODULE_QUERY2(module_info, spmlabf)

static gboolean
module_register(void)
{
    gwy_file_func_register("spmlabf",
                           N_("SPMLab floating-point files (.flt)"),
                           (GwyFileDetectFunc)&slf_detect,
                           (GwyFileLoadFunc)&slf_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
slf_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 10 : 0;

    if (fileinfo->file_size < MAGIC_SIZE + 2
        || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    return 100;
}

static GwyContainer*
slf_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *meta = NULL, *container = NULL;
    GwyTextHeaderParser parser;
    GHashTable *hash = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    GwySIUnit *siunitx, *siunity, *siunitz, *siunit;
    const guchar *p;
    const gchar *val;
    gchar *header = NULL, *end, *s;
    guint data_offset, xres, yres;
    gdouble xreal, yreal, q, off;
    gint power10;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (size < MAGIC_SIZE + 2) {
        err_TOO_SHORT(error);
        goto fail;
    }

    if (memcmp(buffer, MAGIC, MAGIC_SIZE) != 0) {
        err_FILE_TYPE(error, "SPMLab floating-point");
        goto fail;
    }

    p = strstr(buffer, DATA_MAGIC);
    if (!p) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Missing data start marker [Data]."));
        goto fail;
    }

    header = g_memdup(buffer, p - buffer + 1);
    header[p - buffer] = '\0';
    /* Comment prefix [ means we ignore sections. */
    gwy_clear(&parser, 1);
    parser.comment_prefix = "[";
    parser.key_value_separator = "=";
    hash = gwy_text_header_parse(header, &parser, NULL, NULL);

    if (!require_keys(hash, error,
                      "DataOffset", "ScanRangeX", "ScanRangeY",
                      "ResolutionX", "ResolutionY", "ZTransferCoefficient",
                      NULL))
        goto fail;

    p += DATA_MAGIC_SIZE;
    data_offset = atoi(g_hash_table_lookup(hash, "DataOffset"));
    if (p - buffer > data_offset)
        g_warning("DataOffset %d points before end of [Data] at %u",
                  data_offset, (unsigned int)(p - buffer));
    p = buffer + data_offset;

    xres = atoi(g_hash_table_lookup(hash, "ResolutionX"));
    yres = atoi(g_hash_table_lookup(hash, "ResolutionY"));
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        goto fail;

    if (err_SIZE_MISMATCH(error, data_offset + 4*xres*yres, size, TRUE))
        goto fail;

    xreal = g_ascii_strtod(g_hash_table_lookup(hash, "ScanRangeX"), &end);
    if ((s = g_hash_table_lookup(hash, "XYUnit")))
        siunitx = gwy_si_unit_new_parse(s, &power10);
    else
        siunitx = gwy_si_unit_new_parse(end, &power10);
    xreal *= pow10(power10);
    /* Use negated positive conditions to catch NaNs */
    if (!((xreal = fabs(xreal)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        xreal = 1.0;
    }

    yreal = g_ascii_strtod(g_hash_table_lookup(hash, "ScanRangeY"), &end);
    if ((s = g_hash_table_lookup(hash, "XYUnit")))
        siunity = gwy_si_unit_new_parse(s, &power10);
    else
        siunity = gwy_si_unit_new_parse(end, &power10);
    yreal *= pow10(power10);
    /* Use negated positive conditions to catch NaNs */
    if (!((yreal = fabs(yreal)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        yreal = 1.0;
    }

    q = g_ascii_strtod(g_hash_table_lookup(hash, "ZTransferCoefficient"), &end);
    if ((s = g_hash_table_lookup(hash, "ZUnit")))
        siunitz = gwy_si_unit_new_parse(s, &power10);
    else {
        siunitz = gwy_si_unit_new_parse(end, &power10);
        siunit = gwy_si_unit_new("V");
        gwy_si_unit_multiply(siunit, siunitz, siunitz);
        g_object_unref(siunit);
    }
    q *= pow10(power10);

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
    gwy_convert_raw_data(p, xres*yres, 1,
                         GWY_RAW_DATA_FLOAT, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield), q, 0.0);
    gwy_data_field_invert(dfield, TRUE, FALSE, FALSE);

    val = g_hash_table_lookup(hash, "OffsetX");
    if (val) {
        off = g_ascii_strtod(val, &end);
        siunit = gwy_si_unit_new_parse(end, &power10);
        off *= pow10(power10);
        if (!gwy_si_unit_equal(siunitx, siunit))
            g_warning("Incompatible x and x-offset units");
        gwy_data_field_set_xoffset(dfield, off);
        g_object_unref(siunit);
    }

    val = g_hash_table_lookup(hash, "OffsetY");
    if (val) {
        off = g_ascii_strtod(val, &end);
        siunit = gwy_si_unit_new_parse(end, &power10);
        off *= pow10(power10);
        if (!gwy_si_unit_equal(siunitx, siunit))
            g_warning("Incompatible y and y-offset units");
        gwy_data_field_set_yoffset(dfield, off);
        g_object_unref(siunit);
    }

    if (!gwy_si_unit_equal(siunitx, siunity))
        g_warning("Incompatible x and y units");

    gwy_data_field_set_si_unit_xy(dfield, siunitx);
    g_object_unref(siunitx);
    g_object_unref(siunity);

    gwy_data_field_set_si_unit_z(dfield, siunitz);
    g_object_unref(siunitz);

    container = gwy_container_new();
    gwy_container_set_object_by_name(container, "/0/data", dfield);

    if ((s = g_hash_table_lookup(hash, "DataName")))
        gwy_container_set_string_by_name(container, "/0/data/title",
                                         g_strdup(s));
    else
        gwy_app_channel_title_fall_back(container, 0);

    if ((meta = add_metadata(hash,
                             "CreationTime", "DataID", "ScanningRate",
                             "ScanDirection", "Leveling", "Mode", "SetPoint",
                             "X Transfer Coefficient", "Y Transfer Coefficient",
                             "Z Transfer Coefficient", "Rotation",
                             "GainP", "GainI", "GainD",
                             "XLinGainP", "XLinGainI", "XLinGainD",
                             "YLinGainP", "YLinGainI", "YLinGainD",
                             "DriveFrequency", "DriveAmplitude", "DrivePhase",
                             "InputGainSelector", NULL)))
        gwy_container_set_object_by_name(container, "/0/meta", meta);

    gwy_file_channel_import_log_add(container, 0, NULL, filename);

fail:
    g_free(header);
    if (hash)
        g_hash_table_destroy(hash);
    GWY_OBJECT_UNREF(meta);
    GWY_OBJECT_UNREF(dfield);
    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

static GwyContainer*
add_metadata(GHashTable *hash,
             ...)
{
    va_list ap;
    const gchar *key, *value;
    gchar *v;
    GwyContainer *meta = NULL;

    va_start(ap, hash);
    while ((key = va_arg(ap, const gchar *))) {
        if ((value = g_hash_table_lookup(hash, key))) {
            if (!meta)
                meta = gwy_container_new();
            v = g_convert(value, -1, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
            if (v)
                gwy_container_set_string_by_name(meta, key, v);
        }
    }
    va_end(ap);

    return meta;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

