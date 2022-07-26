/*
 *  $Id: apefile.c 22665 2019-11-10 10:11:19Z yeti-dn $
 *  Copyright (C) 2005 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  Date conversion code copied from Wine, see below.
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
 * <mime-type type="application/x-ape-spm">
 *   <comment>A.P.E. Research SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="234" value="APERES"/>
 *   </magic>
 *   <magic priority="80">
 *     <match type="byte" offset="0" value="2">
 *       <match type="little16" offset="3" value="7">
 *         <match type="byte" offset="1" value="0"/>
 *         <match type="byte" offset="1" value="1"/>
 *         <match type="byte" offset="1" value="2"/>
 *         <match type="byte" offset="1" value="3"/>
 *         <match type="byte" offset="1" value="4"/>
 *       </match>
 *     </match>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # A.P.E. Research old SPM data (old DAT).
 * # This detects version 2.x files since some x.  Not sure about which.  Older
 * # versions lack the APERES and cannot be recognised reliably.
 * 0 byte =2
 * >3 leshort <5
 * >>234 string APERES A.P.E. Research SPM data, DAT version 2
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * A.P.E. Research DAT
 * .dat
 * Read
 **/

#include <string.h>
#include <time.h>
#include "config.h"
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

/* Just guessing, format has no real magic header */
#define HEADER_SIZE 240

typedef enum {
    SPM_MODE_SNOM = 0,
    SPM_MODE_AFM_NONCONTACT = 1,
    SPM_MODE_AFM_CONTACT = 2,
    SPM_MODE_STM = 3,
    SPM_MODE_PHASE_DETECT_AFM = 4,
    SPM_MODE_LAST
} SPMModeType;

typedef enum {
    /* They call height `Out' */
    APE_HEIGHT   = 0,
    APE_HEIGHT_R = 1,
    APE_NSOM     = 2,
    APE_NSOM_R   = 3,
    APE_ERROR    = 4,
    APE_ERROR_R  = 5,
    APE_NSOM2    = 6,
    APE_NSOM2_R  = 7,
    APE_AUX1     = 8,
    APE_AUX2     = 9,
    APE_AUX1_R   = 10,
    APE_AUX2_R   = 11,
} APEChannel;

typedef enum {
    APE_HVA_UNKNOWN = -1,
    APE_HVA_LV = 0,
    APE_HVA_HVOL = 1,
    APE_HVA_HVCL = 2,
    APE_HVA_HVOLBIP = 3,
    APE_HVA_LAST
} APEHVAStatus;

typedef struct {
    guint version;
    SPMModeType spm_mode;
    gdouble scan_date;
    gdouble maxr_x;
    gdouble maxr_y;
    gdouble xreal;   /* computed */
    gdouble yreal;   /* computed */
    guint x_offset;
    guint y_offset;
    guint size_flag;
    guint res;   /* computed, 2^(4+size_flag) */
    gdouble acquire_delay;
    gdouble raster_delay;
    gdouble tip_dist;
    gdouble v_ref;
    gdouble vpmt1;
    gdouble vpmt2;
    gchar *remark;   /* 120 chars */
    guint x_piezo_factor;   /* nm/V */
    guint y_piezo_factor;
    guint z_piezo_factor;
    gdouble hv_gain;
    gdouble freq_osc_tip;
    gdouble rotate;
    gdouble slope_x;
    gdouble slope_y;
    guint topo_means;
    guint optical_means;
    guint error_means;
    guint channels;  /* bit mask, bits 0, 1, 4 and 5 should be always present */
    guint ndata;   /* computed, number of nonzero bits in channels */
    gdouble range_x;
    gdouble range_y;
    guint subversion;
    /* The new fields change the conversion rules, however, the simple factor
     * above are written so that old software performs the conversion
     * corerectly (they contain pre-multiplied or pre-divided values).  So we
     * use that and do not care about hv_gain_z or fast2_* */
    /* Since 2.1 */
    gdouble hv_gain_z;
    /* Since 2.2 */
    gdouble fast2_0;
    gdouble fast2_1;
    gdouble fast2_2;
    gdouble fast2_3;
    /* Since 2.3 */
    gboolean pg850_image;
    /* Since 2.4 */
    gint xy_hv_status;
    gint z_hv_status;
    /* The data */
    GwyDataField **data;
} APEFile;

static gboolean      module_register     (void);
static gint          apefile_detect      (const GwyFileDetectInfo *fileinfo,
                                          gboolean only_name);
static GwyContainer* apefile_load        (const gchar *filename,
                                          GwyRunType mode,
                                          GError **error);
static void          fill_data_fields    (APEFile *apefile,
                                          const guchar *buffer);
static GwyContainer* apefile_get_metadata(APEFile *apefile);
static gchar*        format_vt_date      (gdouble vt_date);

static const GwyEnum spm_modes[] = {
    { "SNOM",                  SPM_MODE_SNOM },
    { "AFM Non-contact",       SPM_MODE_AFM_NONCONTACT },
    { "AFM Contact",           SPM_MODE_AFM_CONTACT },
    { "STM",                   SPM_MODE_STM },
    { "Phase detection AFM",   SPM_MODE_PHASE_DETECT_AFM },
};

static const GwyEnum hva_statuses[] = {
    { "N/A",       APE_HVA_UNKNOWN, },
    { "LV",        APE_HVA_LV,      },
    { "HV OL",     APE_HVA_HVOL,    },
    { "HV CL",     APE_HVA_HVCL,    },
    { "HV bip OL", APE_HVA_HVOLBIP, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports APE (Applied Physics and Engineering) data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.13",
    "David Nečas (Yeti) & Petr Klapetek",
    "2005",
};

GWY_MODULE_QUERY2(module_info, apefile)

static gboolean
module_register(void)
{
    gwy_file_func_register("apefile",
                           N_("APE files (.dat)"),
                           (GwyFileDetectFunc)&apefile_detect,
                           (GwyFileLoadFunc)&apefile_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
apefile_detect(const GwyFileDetectInfo *fileinfo,
               gboolean only_name)
{
    const guchar *buffer;
    gint score = 0;
    guint version, mode, vbtype;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, ".dat") ? 10 : 0;

    if (fileinfo->buffer_len < HEADER_SIZE)
        return 0;

    buffer = fileinfo->head;
    version = *(buffer++);
    mode = *(buffer++);
    vbtype = gwy_get_guint16_le(&buffer);
    if (version >= 1 && version <= 2
        && mode < SPM_MODE_LAST+2  /* reserve */
        && vbtype == 7) {
        score = 60;
        /* This works for new file format only */
        if (!strncmp(fileinfo->head + 234, "APERES", 6))
            score = 100;
    }

    return score;
}

static GwyContainer*
apefile_load(const gchar *filename,
             G_GNUC_UNUSED GwyRunType mode,
             GError **error)
{
    APEFile apefile;
    GwyContainer *meta, *container = NULL;
    guchar *buffer = NULL;
    const guchar *p;
    gsize size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    guint b, i, n;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    p = buffer;
    apefile.version = *(p++);
    if (size < 1294) {
        err_TOO_SHORT(error);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    apefile.spm_mode = *(p++);
    p += 2;   /* Skip VisualBasic VARIANT type type */
    apefile.scan_date = gwy_get_gdouble_le(&p);
    apefile.maxr_x = gwy_get_gfloat_le(&p);
    apefile.maxr_y = gwy_get_gfloat_le(&p);
    apefile.x_offset = gwy_get_guint32_le(&p);
    apefile.y_offset = gwy_get_guint32_le(&p);
    apefile.size_flag = gwy_get_guint16_le(&p);
    apefile.res = 16 << apefile.size_flag;
    if (err_DIMENSION(error, apefile.res)) {
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }
    apefile.acquire_delay = gwy_get_gfloat_le(&p);
    apefile.raster_delay = gwy_get_gfloat_le(&p);
    apefile.tip_dist = gwy_get_gfloat_le(&p);
    apefile.v_ref = gwy_get_gfloat_le(&p);
    if (apefile.version == 1) {
        apefile.vpmt1 = gwy_get_guint16_le(&p);
        apefile.vpmt2 = gwy_get_guint16_le(&p);
    }
    else {
        apefile.vpmt1 = gwy_get_gfloat_le(&p);
        apefile.vpmt2 = gwy_get_gfloat_le(&p);
    }
    apefile.remark = g_strndup(p, 120);
    p += 120;
    apefile.x_piezo_factor = gwy_get_guint32_le(&p);
    apefile.y_piezo_factor = gwy_get_guint32_le(&p);
    apefile.z_piezo_factor = gwy_get_guint32_le(&p);
    apefile.hv_gain = gwy_get_gfloat_le(&p);
    apefile.freq_osc_tip = gwy_get_gdouble_le(&p);
    apefile.rotate = gwy_get_gfloat_le(&p);
    apefile.slope_x = gwy_get_gfloat_le(&p);
    apefile.slope_y = gwy_get_gfloat_le(&p);
    apefile.topo_means = gwy_get_guint16_le(&p);
    apefile.optical_means = gwy_get_guint16_le(&p);
    apefile.error_means = gwy_get_guint16_le(&p);
    /*
    g_printerr("%04x %04x %04x\n",
               apefile.topo_means, apefile.optical_means, apefile.error_means);
               */
    apefile.channels = gwy_get_guint32_le(&p);
    apefile.ndata = 0;
    for (b = apefile.channels; b; b = b >> 1)
        apefile.ndata += (b & 1);
    apefile.range_x = gwy_get_gfloat_le(&p);
    apefile.range_y = gwy_get_gfloat_le(&p);
    apefile.subversion = gwy_get_guint16_le(&p);
    /* Read everything since the header is long enough, check the version
     * later when we decide whether to use these values or not. */
    /* Since 2.1 */
    apefile.hv_gain_z = gwy_get_gfloat_le(&p);
    /* Since 2.2 */
    apefile.fast2_0 = gwy_get_gdouble_le(&p);
    apefile.fast2_1 = gwy_get_gdouble_le(&p);
    apefile.fast2_2 = gwy_get_gdouble_le(&p);
    apefile.fast2_3 = gwy_get_gdouble_le(&p);
    /* Since 2.3 */
    apefile.pg850_image = !!gwy_get_guint16_le(&p);
    /* Since 2.4 */
    apefile.xy_hv_status = gwy_get_gint16_le(&p);
    apefile.z_hv_status = gwy_get_gint16_le(&p);
    /* reserved */
    p += 2;

    apefile.xreal = apefile.maxr_x * apefile.x_piezo_factor * apefile.range_x
                    * apefile.hv_gain/65535.0 * 1e-9;
    apefile.yreal = apefile.maxr_y * apefile.y_piezo_factor * apefile.range_y
                    * apefile.hv_gain/65535.0 * 1e-9;
    /* Use negated positive conditions to catch NaNs */
    if (!((apefile.xreal = fabs(apefile.xreal)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        apefile.xreal = 1.0;
    }
    if (!((apefile.yreal = fabs(apefile.yreal)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        apefile.yreal = 1.0;
    }

    gwy_debug("version = %u.%u, spm_mode = %u",
              apefile.version, apefile.subversion, apefile.spm_mode);
    gwy_debug("scan_date = %f", apefile.scan_date);
    gwy_debug("maxr_x = %g, maxr_y = %g", apefile.maxr_x, apefile.maxr_y);
    gwy_debug("x_offset = %u, y_offset = %u",
              apefile.x_offset, apefile.y_offset);
    gwy_debug("size_flag = %u", apefile.size_flag);
    gwy_debug("acquire_delay = %g, raster_delay = %g, tip_dist = %g",
              apefile.acquire_delay, apefile.raster_delay, apefile.tip_dist);
    gwy_debug("v_ref = %g, vpmt1 = %g, vpmt2 = %g",
              apefile.v_ref, apefile.vpmt1, apefile.vpmt2);
    gwy_debug("x_piezo_factor = %u, y_piezo_factor = %u, z_piezo_factor = %u",
              apefile.x_piezo_factor, apefile.y_piezo_factor,
              apefile.z_piezo_factor);
    gwy_debug("hv_gain = %g, freq_osc_tip = %g, rotate = %g",
              apefile.hv_gain, apefile.freq_osc_tip, apefile.rotate);
    gwy_debug("slope_x = %g, slope_y = %g",
              apefile.slope_x, apefile.slope_y);
    gwy_debug("topo_means = %u, optical_means = %u, error_means = %u",
              apefile.topo_means, apefile.optical_means, apefile.error_means);
    gwy_debug("channel bitmask = %03x, ndata = %u",
              apefile.channels, apefile.ndata);
    gwy_debug("range_x = %g, range_y = %g",
              apefile.range_x, apefile.range_y);

    n = (apefile.res + 1)*(apefile.res + 1)*sizeof(float);
    if (size - (p - buffer) != n*apefile.ndata) {
        g_warning("Expected data size %u, but it's %u.",
                  n*apefile.ndata, (guint)(size - (p - buffer)));
        gwy_debug("original ndata %u, corrected ndata %u (n = %u)",
                  apefile.ndata, (guint)(size - (p - buffer))/n, n);
        apefile.ndata = MIN(apefile.ndata, (size - (p - buffer))/n);
    }
    if (!apefile.ndata) {
        err_NO_DATA(error);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }
    fill_data_fields(&apefile, p);
    gwy_file_abandon_contents(buffer, size, NULL);

    container = gwy_container_new();
    /* All metadata seems to be per-file (global) */
    meta = apefile_get_metadata(&apefile);
    for (b = apefile.channels, n = 0, i = 0; b; b = b >> 1, i++) {
        GwyContainer *tmp;
        gchar *title;
        gchar key[32];

        if (!(b & 1))
            continue;
        if (n == apefile.ndata)
            break;

        g_snprintf(key, sizeof(key), "/%d/data", n);
        dfield = apefile.data[n];
        gwy_container_set_object_by_name(container, key, dfield);
        g_object_unref(apefile.data[n]);

        g_snprintf(key, sizeof(key), "/%d/data/title", n);

        /*
         * Channel labelling based on SPM Mode
         */
        switch (apefile.spm_mode) {
            case SPM_MODE_SNOM:
            title = gwy_enuml_to_string(i,
                                        "Height",   APE_HEIGHT,
                                        "Height-R", APE_HEIGHT_R,
                                        "NSOM",     APE_NSOM,
                                        "NSOM-R",   APE_NSOM_R,
                                        "Error",    APE_ERROR,
                                        "Error-R",  APE_ERROR_R,
                                        "NSOM2",    APE_NSOM2,
                                        "NSOM2-R",  APE_NSOM2_R,
                                        "Lateral",     APE_AUX1,
                                        "Z-Z0",     APE_AUX2,
                                        "Lateral-R",   APE_AUX1_R,
                                        "Z-Z0-R",   APE_AUX2_R,
                                        NULL);
            break;

            case SPM_MODE_AFM_NONCONTACT:
            case SPM_MODE_AFM_CONTACT:
            case SPM_MODE_PHASE_DETECT_AFM:
            title = gwy_enuml_to_string(i,
                                        "Height",   APE_HEIGHT,
                                        "Height-R", APE_HEIGHT_R,
                                        "IN1",     APE_NSOM,
                                        "IN1-R",   APE_NSOM_R,
                                        "Error",    APE_ERROR,
                                        "Error-R",  APE_ERROR_R,
                                        "IN2",    APE_NSOM2,
                                        "IN2-R",  APE_NSOM2_R,
                                        "Lateral",     APE_AUX1,
                                        "Z-Z0",     APE_AUX2,
                                        "Lateral-R",   APE_AUX1_R,
                                        "Z-Z0-R",   APE_AUX2_R,
                                        NULL);
            break;

            default:
            title = gwy_enuml_to_string(i,
                                        "Height",   APE_HEIGHT,
                                        "Height-R", APE_HEIGHT_R,
                                        "IN1",     APE_NSOM,
                                        "IN1-R",   APE_NSOM_R,
                                        "Error",    APE_ERROR,
                                        "Error-R",  APE_ERROR_R,
                                        "IN2",    APE_NSOM2,
                                        "IN2-R",  APE_NSOM2_R,
                                        "Aux1",     APE_AUX1,
                                        "Z-Z0",     APE_AUX2,
                                        "Aux1-R",   APE_AUX1_R,
                                        "Z-Z0-R",   APE_AUX2_R,
                                        NULL);
            break;
        }
        if (title && *title)
            gwy_container_set_string_by_name(container, key, g_strdup(title));

        tmp = gwy_container_duplicate(meta);
        g_snprintf(key, sizeof(key), "/%d/meta", n);
        gwy_container_set_object_by_name(container, key, tmp);
        g_object_unref(tmp);

        gwy_file_channel_import_log_add(container, n, NULL, filename);

        n++;
    }

    g_object_unref(meta);
    g_free(apefile.remark);

    return container;
}

static void
fill_data_fields(APEFile *apefile,
                 const guchar *buffer)
{
    GwyDataField *dfield;
    GwySIUnit *unit;
    gdouble *data;
    const gchar *zunit;
    guint b, n, i, k;
    gdouble q;

    apefile->data = g_new0(GwyDataField*, apefile->ndata);
    for (b = apefile->channels, n = 0, k = 0; b; b = b >> 1, k++) {
        if (!(b & 1))
            continue;
        if (n == apefile->ndata)
            break;

        gwy_debug("reading field %u (bit 0x%08x)", n, (1 << k));
        dfield = gwy_data_field_new(apefile->res, apefile->res,
                                    apefile->xreal, apefile->yreal, FALSE);
        unit = gwy_data_field_get_si_unit_xy(dfield);
        gwy_si_unit_set_from_string(unit, "m");

        data = gwy_data_field_get_data(dfield);
        buffer += (apefile->res + 1)*sizeof(float);
        switch (k) {
            case APE_HEIGHT:
            case APE_HEIGHT_R:
            case APE_AUX2:
            case APE_AUX2_R:
            q = apefile->z_piezo_factor * 1e-9;
            zunit = "m";
            break;

            case APE_AUX1:
            case APE_AUX1_R:
            q = 1.0;
            zunit = apefile->pg850_image ? "A" : "V";
            break;

            default:
            q = 1.0;
            zunit = "V";
            break;
        }

        unit = gwy_data_field_get_si_unit_z(dfield);
        gwy_si_unit_set_from_string(unit, zunit);

        for (i = 0; i < apefile->res; i++) {
            /* There is always one ignored record, do not ask me why... */
            buffer += sizeof(float);
            gwy_convert_raw_data(buffer, apefile->res, 1,
                                 GWY_RAW_DATA_FLOAT,
                                 GWY_BYTE_ORDER_LITTLE_ENDIAN,
                                 data + i*apefile->res, q, 0.0);
            buffer += apefile->res*sizeof(float);
        }
        apefile->data[n++] = dfield;
    }
}

#define HASH_STORE(key, fmt, field) \
    gwy_container_set_string_by_name(meta, key, \
                                     g_strdup_printf(fmt, apefile->field))

static GwyContainer*
apefile_get_metadata(APEFile *apefile)
{
    GwyContainer *meta;
    gchar *p;

    meta = gwy_container_new();

    gwy_container_set_string_by_name(meta, "Version",
                                     g_strdup_printf("%u.%u", apefile->version,
                                                     apefile->subversion));

    /*
     * In SPM_MODE_SNOM or SPM_MODE_AFM_NONCONTACT freq_osc_tip holds the tip
     * oscillation frequency.
     * If spm_mode=SPM_MODE_AFM_CONTACT then the freq_osc_tip field holds the
     * force the tip applies to the sample in nN.
     * If spm_mode=SPM_MODE_STM then freq_osc_tip holds the bias voltage in
     * Volts.
     */
    switch (apefile->spm_mode) {
        case SPM_MODE_SNOM:
        case SPM_MODE_AFM_NONCONTACT:
            HASH_STORE("Tip oscillation frequency", "%g Hz", freq_osc_tip);
            break;
        case SPM_MODE_AFM_CONTACT:
            HASH_STORE("Force", "%g nN", freq_osc_tip);
            break;
        case SPM_MODE_STM:
            HASH_STORE("Bias", "%g V", freq_osc_tip);
            break;
        default:
            HASH_STORE("Tip oscillation frequency", "%g Hz", freq_osc_tip);
            break;
    }

    /* We will fetch the HV Amplifier status only if file version>= 2.4*/
    if (apefile->version > 1 && apefile->subversion >= 4) {
        gwy_container_set_string_by_name
            (meta, "XY HV Status",
             g_strdup(gwy_enum_to_string(apefile->xy_hv_status, hva_statuses,
                                         G_N_ELEMENTS(hva_statuses))));
        gwy_container_set_string_by_name
            (meta, "Z HV Status",
             g_strdup(gwy_enum_to_string(apefile->z_hv_status, hva_statuses,
                                         G_N_ELEMENTS(hva_statuses))));
    }

    HASH_STORE("Acquire delay", "%.6f s", acquire_delay);
    HASH_STORE("Raster delay", "%.6f s", raster_delay);
    HASH_STORE("Tip distance", "%g nm", tip_dist);

    if (apefile->remark && *apefile->remark
        && (p = g_convert(apefile->remark, strlen(apefile->remark),
                          "UTF-8", "ISO-8859-1", NULL, NULL, NULL)))
        gwy_container_set_string_by_name(meta, "Comment", p);
    gwy_container_set_string_by_name
                    (meta, "SPM mode",
                     g_strdup(gwy_enum_to_string(apefile->spm_mode, spm_modes,
                                                 G_N_ELEMENTS(spm_modes))));
    p = format_vt_date(apefile->scan_date);
    if (p)
        gwy_container_set_string_by_name(meta, "Date", p);

    return meta;
}

/******************** Wine date conversion code *****************************/

/*
 * Copyright (C) the Wine project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA
 */

typedef guint WORD;
typedef guint USHORT;
typedef guint8 BYTE;

typedef struct {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME;

typedef struct {
    SYSTEMTIME st;
    USHORT wDayOfYear;
} UDATE;

#define DATE_MIN -657434
#define DATE_MAX 2958465

#define IsLeapYear(y) (((y % 4) == 0) && (((y % 100) != 0) || ((y % 400) == 0)))

/* Convert a VT_DATE value to a Julian Date */
static inline int
VARIANT_JulianFromDate(int dateIn)
{
    int julianDays = dateIn;

    julianDays -= DATE_MIN; /* Convert to + days from 1 Jan 100 AD */
    julianDays += 1757585;  /* Convert to + days from 23 Nov 4713 BC (Julian) */
    return julianDays;
}

/* Convert a Julian Date to a VT_DATE value */
G_GNUC_UNUSED
static inline int
VARIANT_DateFromJulian(int dateIn)
{
    int julianDays = dateIn;

    julianDays -= 1757585;  /* Convert to + days from 1 Jan 100 AD */
    julianDays += DATE_MIN; /* Convert to +/- days from 1 Jan 1899 AD */
    return julianDays;
}

/* Convert a Julian date to Day/Month/Year - from PostgreSQL */
static inline void
VARIANT_DMYFromJulian(int jd, USHORT *year, USHORT *month, USHORT *day)
{
    int j, i, l, n;

    l = jd + 68569;
    n = l * 4 / 146097;
    l -= (n * 146097 + 3) / 4;
    i = (4000 * (l + 1)) / 1461001;
    l += 31 - (i * 1461) / 4;
    j = (l * 80) / 2447;
    *day = l - (j * 2447) / 80;
    l = j / 11;
    *month = (j + 2) - (12 * l);
    *year = 100 * (n - 49) + i + l;
}

/* Roll a date forwards or backwards to correct it */
static gboolean
VARIANT_RollUdate(UDATE *lpUd)
{
    static const BYTE days[] = {
        0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    /* Years < 100 are treated as 1900 + year */
    if (lpUd->st.wYear < 100)
        lpUd->st.wYear += 1900;

    if (!lpUd->st.wMonth) {
        /* Roll back to December of the previous year */
        lpUd->st.wMonth = 12;
        lpUd->st.wYear--;
    }
    else {
        while (lpUd->st.wMonth > 12) {
            /* Roll forward the correct number of months */
            lpUd->st.wYear++;
            lpUd->st.wMonth -= 12;
        }
    }

    if (lpUd->st.wYear > 9999 || lpUd->st.wHour > 23
        || lpUd->st.wMinute > 59 || lpUd->st.wSecond > 59)
        return FALSE; /* Invalid values */

    if (!lpUd->st.wDay) {
        /* Roll back the date one day */
        if (lpUd->st.wMonth == 1) {
            /* Roll back to December 31 of the previous year */
            lpUd->st.wDay   = 31;
            lpUd->st.wMonth = 12;
            lpUd->st.wYear--;
        }
        else {
            lpUd->st.wMonth--; /* Previous month */
            if (lpUd->st.wMonth == 2 && IsLeapYear(lpUd->st.wYear))
                lpUd->st.wDay = 29; /* Februaury has 29 days on leap years */
            else
                /* Last day of the month */
                lpUd->st.wDay = days[lpUd->st.wMonth];
        }
    }
    else if (lpUd->st.wDay > 28) {
        int rollForward = 0;

        /* Possibly need to roll the date forward */
        if (lpUd->st.wMonth == 2 && IsLeapYear(lpUd->st.wYear))
            /* Februaury has 29 days on leap years */
            rollForward = lpUd->st.wDay - 29;
        else
            rollForward = lpUd->st.wDay - days[lpUd->st.wMonth];

        if (rollForward > 0) {
            lpUd->st.wDay = rollForward;
            lpUd->st.wMonth++;
            if (lpUd->st.wMonth > 12) {
                /* Roll forward into January of the next year */
                lpUd->st.wMonth = 1;
                lpUd->st.wYear++;
            }
        }
    }

    return TRUE;
}

/* WINAPI, prefix with gwy_ */
static gboolean
gwy_VarUdateFromDate(double dateIn, UDATE *lpUdate)
{
    /* Cumulative totals of days per month */
    static const USHORT cumulativeDays[] = {
        0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    double datePart, timePart;
    int julianDays;

    if (dateIn <= (DATE_MIN - 1.0) || dateIn >= (DATE_MAX + 1.0))
        return FALSE;

    datePart = dateIn < 0.0 ? ceil(dateIn) : floor(dateIn);
    /* Compensate for int truncation (always downwards) */
    timePart = dateIn - datePart + 0.00000000001;
    if (timePart >= 1.0)
        timePart -= 0.00000000001;

    /* Date */
    julianDays = VARIANT_JulianFromDate(dateIn);
    VARIANT_DMYFromJulian(julianDays, &lpUdate->st.wYear, &lpUdate->st.wMonth,
                          &lpUdate->st.wDay);

    datePart = (datePart + 1.5) / 7.0;
    lpUdate->st.wDayOfWeek = (datePart - floor(datePart)) * 7;
    if (lpUdate->st.wDayOfWeek == 0)
        lpUdate->st.wDayOfWeek = 5;
    else if (lpUdate->st.wDayOfWeek == 1)
        lpUdate->st.wDayOfWeek = 6;
    else
        lpUdate->st.wDayOfWeek -= 2;

    if (lpUdate->st.wMonth > 2 && IsLeapYear(lpUdate->st.wYear))
        lpUdate->wDayOfYear = 1; /* After February, in a leap year */
    else
        lpUdate->wDayOfYear = 0;

    lpUdate->wDayOfYear += cumulativeDays[lpUdate->st.wMonth];
    lpUdate->wDayOfYear += lpUdate->st.wDay;

    /* Time */
    timePart *= 24.0;
    lpUdate->st.wHour = timePart;
    timePart -= lpUdate->st.wHour;
    timePart *= 60.0;
    lpUdate->st.wMinute = timePart;
    timePart -= lpUdate->st.wMinute;
    timePart *= 60.0;
    lpUdate->st.wSecond = timePart;
    timePart -= lpUdate->st.wSecond;
    lpUdate->st.wMilliseconds = 0;
    if (timePart > 0.5) {
        /* Round the milliseconds, adjusting the time/date forward if needed */
        if (lpUdate->st.wSecond < 59)
            lpUdate->st.wSecond++;
        else {
            lpUdate->st.wSecond = 0;
            if (lpUdate->st.wMinute < 59)
                lpUdate->st.wMinute++;
            else {
                lpUdate->st.wMinute = 0;
                if (lpUdate->st.wHour < 23)
                    lpUdate->st.wHour++;
                else {
                    lpUdate->st.wHour = 0;
                    /* Roll over a whole day */
                    if (++lpUdate->st.wDay > 28)
                        VARIANT_RollUdate(lpUdate);
                }
            }
        }
    }

    return TRUE;
}

static gchar*
format_vt_date(gdouble vt_date)
{
    struct tm tm;
    UDATE udate;

    if (!gwy_VarUdateFromDate(vt_date, &udate))
        return NULL;

    gwy_clear(&tm, 1);
    gwy_debug("Date: %d-%d-%d %d:%d:%d",
              udate.st.wYear, udate.st.wMonth, udate.st.wDay,
              udate.st.wHour, udate.st.wMinute, udate.st.wSecond);
    tm.tm_year = udate.st.wYear - 1900;
    tm.tm_mon = udate.st.wMonth - 1;
    tm.tm_mday = udate.st.wDay;
    tm.tm_hour = udate.st.wHour;
    tm.tm_min = udate.st.wMinute;
    tm.tm_sec = udate.st.wSecond;
    tm.tm_wday = udate.st.wDayOfWeek;
    tm.tm_yday = udate.wDayOfYear;
    tm.tm_isdst = -1;

    return g_strstrip(g_strdup(asctime(&tm)));
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

