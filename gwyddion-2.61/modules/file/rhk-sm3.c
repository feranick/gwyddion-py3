/*
 *  $Id: rhk-sm3.c 24009 2021-08-17 14:36:26Z yeti-dn $
 *  Copyright (C) 2005,2008 David Necas (Yeti), Petr Klapetek.
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
 * <mime-type type="application/x-rhk-sm3-spm">
 *   <comment>RHK SM3 SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="2" value="S\0T\0i\0M\0a\0g\0e\0 \0\060\0\060\0\064\0.\0"/>
 *   </magic>
 *   <glob pattern="*.sm3"/>
 *   <glob pattern="*.SM3"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # RHK SM3
 * # The same as SM2, but in UTF-16.
 * 2 lestring16 STiMage\ 004. RHK Technology SM3 data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * RHK Instruments SM3
 * .sm3
 * Read SPS:Limited[1]
 * [1] Spectra curves are imported as graphs, positional information is lost.
 **/

#include "config.h"
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwygraphcurvemodel.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"
#include "get.h"

/* Ugly Microsoft UTF-16...
 * It reads: `STiMage 004.NNN N', but we do not check the NNN N */
static const guchar MAGIC[] = {
  0x53, 0x00, 0x54, 0x00, 0x69, 0x00, 0x4d, 0x00, 0x61, 0x00, 0x67, 0x00,
  0x65, 0x00, 0x20, 0x00, 0x30, 0x00, 0x30, 0x00, 0x34, 0x00, 0x2e, 0x00,
};

#define EXTENSION ".sm3"

enum {
    MAGIC_OFFSET = 2,
    MAGIC_SIZE = G_N_ELEMENTS(MAGIC),
    MAGIC_TOTAL_SIZE = 36,   /* including the version part we do not check */
    HEADER_SIZE = 2 + MAGIC_TOTAL_SIZE + 2*4 + 15*4 + 11*4 + 16
};

typedef enum {
    RHK_TYPE_IMAGE          = 0,
    RHK_TYPE_LINE           = 1,
    RHK_TYPE_ANNOTATED_LINE = 3
} RHKType;

typedef enum {
    RHK_PAGE_UNDEFINED                = 0,
    RHK_PAGE_TOPOGRAPHIC              = 1,
    RHK_PAGE_CURRENT                  = 2,
    RHK_PAGE_AUX                      = 3,
    RHK_PAGE_FORCE                    = 4,
    RHK_PAGE_SIGNAL                   = 5,
    RHK_PAGE_FFT                      = 6,
    RHK_PAGE_NOISE_POWER_SPECTRUM     = 7,
    RHK_PAGE_LINE_TEST                = 8,
    RHK_PAGE_OSCILLOSCOPE             = 9,
    RHK_PAGE_IV_SPECTRA               = 10,
    RHK_PAGE_IV_4x4                   = 11,
    RHK_PAGE_IV_8x8                   = 12,
    RHK_PAGE_IV_16x16                 = 13,
    RHK_PAGE_IV_32x32                 = 14,
    RHK_PAGE_IV_CENTER                = 15,
    RHK_PAGE_INTERACTIVE_SPECTRA      = 16,
    RHK_PAGE_AUTOCORRELATION          = 17,
    RHK_PAGE_IZ_SPECTRA               = 18,
    RHK_PAGE_4_GAIN_TOPOGRAPHY        = 19,
    RHK_PAGE_8_GAIN_TOPOGRAPHY        = 20,
    RHK_PAGE_4_GAIN_CURRENT           = 21,
    RHK_PAGE_8_GAIN_CURRENT           = 22,
    RHK_PAGE_IV_64x64                 = 23,
    RHK_PAGE_AUTOCORRELATION_SPECTRUM = 24,
    RHK_PAGE_COUNTER                  = 25,
    RHK_PAGE_MULTICHANNEL_ANALYSER    = 26,
    RHK_PAGE_AFM_100                  = 27
} RHKPageType;

typedef enum {
    RHK_LINE_NOT_A_LINE                     = 0,
    RHK_LINE_HISTOGRAM                      = 1,
    RHK_LINE_CROSS_SECTION                  = 2,
    RHK_LINE_LINE_TEST                      = 3,
    RHK_LINE_OSCILLOSCOPE                   = 4,
    RHK_LINE_NOISE_POWER_SPECTRUM           = 6,
    RHK_LINE_IV_SPECTRUM                    = 7,
    RHK_LINE_IZ_SPECTRUM                    = 8,
    RHK_LINE_IMAGE_X_AVERAGE                = 9,
    RHK_LINE_IMAGE_Y_AVERAGE                = 10,
    RHK_LINE_NOISE_AUTOCORRELATION_SPECTRUM = 11,
    RHK_LINE_MULTICHANNEL_ANALYSER_DATA     = 12,
    RHK_LINE_RENORMALIZED_IV                = 13,
    RHK_LINE_IMAGE_HISTOGRAM_SPECTRA        = 14,
    RHK_LINE_IMAGE_CROSS_SECTION            = 15,
    RHK_LINE_IMAGE_AVERAGE                  = 16
} RHKLineType;

typedef enum {
    RHK_SOURCE_RAW_PAGE        = 0,
    RHK_SOURCE_PROCESSED_PAGE  = 1,
    RHK_SOURCE_CALCULATED_PAGE = 2,
    RHK_SOURCE_IMPORTED_PAGE   = 3
} RHKSourceType;

typedef enum {
    RHK_IMAGE_NORMAL         = 0,
    RHK_IMAGE_AUTOCORRELATED = 1
} RHKImageType;

typedef enum {
    RHK_SCAN_RIGHT = 0,
    RHK_SCAN_LEFT  = 1,
    RHK_SCAN_UP    = 2,
    RHK_SCAN_DOWN  = 3
} RHKScanType;

typedef enum {
    RHK_STRING_LABEL,
    RHK_STRING_SYSTEM_TEXT,
    RHK_STRING_SESSION_TEXT,
    RHK_STRING_USER_TEXT,
    RHK_STRING_PATH,
    RHK_STRING_DATE,
    RHK_STRING_TIME,
    RHK_STRING_X_UNITS,
    RHK_STRING_Y_UNITS,
    RHK_STRING_Z_UNITS,
    RHK_STRING_X_LABEL,
    RHK_STRING_Y_LABEL,
    RHK_STRING_NSTRINGS
} RHKStringType;

typedef struct {
    guint size;
    /*
    gfloat *hue_start;
    gfloat *saturation_start;
    gfloat *brightness_start;
    gfloat *hue_end;
    gfloat *saturation_end;
    gfloat *brightness_end;
    gboolean *color_direction;
    guint *color_entries;
    */
} RHKColorInformation;

typedef struct {
    guint pageno;  /* Our counter */
    guint param_size;
    gchar version[36];
    guint string_count;
    RHKType type;
    RHKPageType page_type;
    guint data_sub_source;
    RHKLineType line_type;
    gint x_coord;
    gint y_coord;
    guint x_size;
    guint y_size;
    RHKSourceType source_type;
    RHKImageType image_type;
    RHKScanType scan_dir;
    guint group_id;
    guint data_size;
    guint min_z_value;
    guint max_z_value;
    gdouble x_scale;
    gdouble y_scale;
    gdouble z_scale;
    gdouble xy_scale;
    gdouble x_offset;
    gdouble y_offset;
    gdouble z_offset;
    gdouble period;
    gdouble bias;
    gdouble current;
    gdouble angle;
    guchar page_id[16];
    gchar *strings[RHK_STRING_NSTRINGS];
    /* The actual type depends.  The documentation is a bit confusing as to
     * which type is used when, but at least we know int32 and float32 can
     * occur. */
    const guchar *page_data;
    RHKColorInformation color_info;
} RHKPage;

static gboolean      module_register       (void);
static gint          rhk_sm3_detect        (const GwyFileDetectInfo *fileinfo,
                                            gboolean only_name);
static GwyContainer* rhk_sm3_load          (const gchar *filename,
                                            GwyRunType mode,
                                            GError **error);
static GwyContainer* rhk_sm3_get_metadata  (RHKPage *rhkpage);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports RHK Technology SM3 data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.16",
    "David Nečas (Yeti) & Petr Klapetek",
    "2005",
};

static const GwyEnum scan_directions[] = {
    { "Right", RHK_SCAN_RIGHT, },
    { "Left",  RHK_SCAN_LEFT,  },
    { "Up",    RHK_SCAN_UP,    },
    { "Down",  RHK_SCAN_DOWN,  },
};

GWY_MODULE_QUERY2(module_info, rhk_sm3)

static gboolean
module_register(void)
{
    gwy_file_func_register("rhk-sm3",
                           N_("RHK SM3 files (.sm3)"),
                           (GwyFileDetectFunc)&rhk_sm3_detect,
                           (GwyFileLoadFunc)&rhk_sm3_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
rhk_sm3_detect(const GwyFileDetectInfo *fileinfo,
               gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;

    if (fileinfo->buffer_len > MAGIC_TOTAL_SIZE
        && memcmp(fileinfo->head + MAGIC_OFFSET, MAGIC, MAGIC_SIZE) == 0)
        score = 100;

    return score;
}

static guchar*
rhk_sm3_read_string(const guchar **buffer,
                    gsize len)
{
    gchar *s;
    guint n;

    if (len < 2)
        return NULL;

    n = gwy_get_guint16_le(buffer);
    len -= 2;
    if (len < 2*n)
        return NULL;

    s = gwy_utf16_to_utf8((const gunichar2*)*buffer, n,
                          GWY_BYTE_ORDER_LITTLE_ENDIAN);
    *buffer += n*sizeof(gunichar2);
    g_strstrip(s);
    gwy_debug("String: <%s>", s);

    return s;
}

static void
rhk_sm3_page_free(RHKPage *page)
{
    guint i;

    for (i = 0; i < RHK_STRING_NSTRINGS; i++)
        g_free(page->strings[i]);
    g_free(page);
}

static RHKPage*
rhk_sm3_read_page(const guchar **buffer,
                  gsize *len,
                  GError **error)
{
    RHKPage *page;
    const guchar *p = *buffer;
    guint i, expected;

    if (!*len)
        return NULL;

    if (*len < HEADER_SIZE + 4) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("End of file reached in page header."));
        return NULL;
    }
    if (memcmp(p + MAGIC_OFFSET, MAGIC, MAGIC_SIZE) != 0) {
        err_INVALID(error, _("magic page header"));
        return NULL;
    }

    page = g_new0(RHKPage, 1);
    page->param_size = gwy_get_guint16_le(&p);
    gwy_debug("param_size = %u", page->param_size);
    if (*len < page->param_size + 4) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("End of file reached in page header."));
        goto fail;
    }
    /* TODO: Convert to UTF-8, store to meta */
    memcpy(page->version, p, MAGIC_TOTAL_SIZE);
    p += MAGIC_TOTAL_SIZE;
    page->string_count = gwy_get_guint16_le(&p);
    gwy_debug("string_count = %u", page->string_count);
    page->type = gwy_get_guint32_le(&p);
    gwy_debug("type = %u", page->type);
    page->page_type = gwy_get_guint32_le(&p);
    gwy_debug("page_type = %u", page->page_type);
    page->data_sub_source = gwy_get_guint32_le(&p);
    page->line_type = gwy_get_guint32_le(&p);
    page->x_coord = gwy_get_gint32_le(&p);
    page->y_coord = gwy_get_gint32_le(&p);
    page->x_size = gwy_get_guint32_le(&p);
    page->y_size = gwy_get_guint32_le(&p);
    gwy_debug("x_size = %u, y_size = %u", page->x_size, page->y_size);
    if (err_DIMENSION(error, page->x_size)
        || err_DIMENSION(error, page->y_size))
        goto fail;

    page->source_type = gwy_get_guint32_le(&p);
    page->image_type = gwy_get_guint32_le(&p);
    gwy_debug("image_type = %u", page->image_type);
    page->scan_dir = gwy_get_guint32_le(&p);
    gwy_debug("scan_dir = %u", page->scan_dir);
    page->group_id = gwy_get_guint32_le(&p);
    gwy_debug("group_id = %u", page->group_id);
    page->data_size = gwy_get_guint32_le(&p);
    gwy_debug("data_size = %u", page->data_size);
    page->min_z_value = gwy_get_gint32_le(&p);
    page->max_z_value = gwy_get_gint32_le(&p);
    gwy_debug("min,max_z_value = %d %d", page->min_z_value, page->max_z_value);
    page->x_scale = gwy_get_gfloat_le(&p);
    page->y_scale = gwy_get_gfloat_le(&p);
    page->z_scale = gwy_get_gfloat_le(&p);
    gwy_debug("x,y,z_scale = %g %g %g",
              page->x_scale, page->y_scale, page->z_scale);
    /* Use negated positive conditions to catch NaNs */
    if (!((page->x_scale = fabs(page->x_scale)) > 0)) {
        g_warning("Real x scale is 0.0, fixing to 1.0");
        page->x_scale = 1.0;
    }
    if (!((page->y_scale = fabs(page->y_scale)) > 0)) {
        g_warning("Real y scale is 0.0, fixing to 1.0");
        page->y_scale = 1.0;
    }
    page->xy_scale = gwy_get_gfloat_le(&p);
    page->x_offset = gwy_get_gfloat_le(&p);
    page->y_offset = gwy_get_gfloat_le(&p);
    page->z_offset = gwy_get_gfloat_le(&p);
    gwy_debug("x,y,z_offset = %g %g %g",
              page->x_offset, page->y_offset, page->z_offset);
    page->period = gwy_get_gfloat_le(&p);
    page->bias = gwy_get_gfloat_le(&p);
    page->current = gwy_get_gfloat_le(&p);
    page->angle = gwy_get_gfloat_le(&p);
    gwy_debug("period = %g, bias = %g, current = %g, angle = %g",
              page->period, page->bias, page->current, page->angle);
    get_CHARARRAY(page->page_id, &p);

    p = *buffer + 2 + page->param_size;
    for (i = 0; i < page->string_count; i++) {
        gchar *s;

        gwy_debug("position %04x", (guint)(p - *buffer));
        s = rhk_sm3_read_string(&p, *len - (p - *buffer));
        if (!s) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("End of file reached in string #%u."), i);
            goto fail;
        }
        if (i < RHK_STRING_NSTRINGS)
            page->strings[i] = s;
        else
            g_free(s);
    }

    expected = page->x_size * page->y_size * sizeof(gint32);
    gwy_debug("expecting %u bytes of page data now", expected);
    if (err_SIZE_MISMATCH(error, expected, *len - (p - *buffer), FALSE))
        goto fail;

    page->page_data = p;
    p += expected;

    if (page->type == RHK_TYPE_IMAGE) {
        if (*len < (p - *buffer) + 4) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("End of file reached in color data header."));
            goto fail;
        }
        /* XPMPro manual says the length field is 4bytes, but reality seems to
         * disagree vehemently. */
        page->color_info.size = gwy_get_guint16_le(&p);
        if (*len < (p - *buffer) + page->color_info.size) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("End of file reached in color data."));
            goto fail;
        }

        p += page->color_info.size;
    }

    *len -= p - *buffer;
    *buffer = p;
    return page;

fail:
    rhk_sm3_page_free(page);
    return NULL;
}

static GwyDataField*
rhk_sm3_page_to_data_field(const RHKPage *page)
{
    GwyDataField *dfield;
    GwySIUnit *siunit;
    const gchar *unit;
    gint xres, yres, i, j;
    const gint32 *pdata;
    gdouble *data;

    xres = page->x_size;
    yres = page->y_size;
    dfield = gwy_data_field_new(xres, yres,
                                xres*fabs(page->x_scale),
                                yres*fabs(page->y_scale),
                                FALSE);
    data = gwy_data_field_get_data(dfield);
    pdata = (const gint32*)page->page_data;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            data[i*xres + xres-1 - j] = GINT32_FROM_LE(pdata[i*xres + j])
                                        *page->z_scale + page->z_offset;
        }
    }

    if (page->strings[RHK_STRING_X_UNITS]
        && page->strings[RHK_STRING_Y_UNITS]) {
        if (!gwy_strequal(page->strings[RHK_STRING_X_UNITS],
                          page->strings[RHK_STRING_Y_UNITS]))
            g_warning("X and Y units differ, using X");
        unit = page->strings[RHK_STRING_X_UNITS];
    }
    else if (page->strings[RHK_STRING_X_UNITS])
        unit = page->strings[RHK_STRING_X_UNITS];
    else if (page->strings[RHK_STRING_Y_UNITS])
        unit = page->strings[RHK_STRING_Y_UNITS];
    else
        unit = "";
    siunit = gwy_si_unit_new(unit);
    gwy_data_field_set_si_unit_xy(dfield, siunit);
    g_object_unref(siunit);

    if (page->strings[RHK_STRING_Z_UNITS])
        unit = page->strings[RHK_STRING_Z_UNITS];
    else
        unit = "";
    /* Fix some silly units */
    if (gwy_strequal(unit, "N/sec"))
        unit = "s^-1";
    siunit = gwy_si_unit_new(unit);
    gwy_data_field_set_si_unit_z(dfield, siunit);
    g_object_unref(siunit);

    return dfield;
}

static GwyGraphModel*
rhk_sm3_page_to_spectra(const RHKPage *page)
{
    GwyGraphModel *gmodel;
    GwyDataLine *dline;
    GwySIUnit *siunit;
    const gchar *unit;
    gint res, ncurves, i, j;
    const guchar *p;
    gdouble *data;

    res = page->x_size;
    ncurves = page->y_size;
    gmodel = gwy_graph_model_new();
    dline = gwy_data_line_new(res, res*fabs(page->x_scale), FALSE);
    data = gwy_data_line_get_data(dline);
    p = page->page_data;

    if (page->strings[RHK_STRING_X_UNITS])
        unit = page->strings[RHK_STRING_X_UNITS];
    else
        unit = "";
    siunit = gwy_si_unit_new(unit);
    gwy_data_line_set_si_unit_x(dline, siunit);
    g_object_unref(siunit);

    if (page->strings[RHK_STRING_Z_UNITS])
        unit = page->strings[RHK_STRING_Z_UNITS];
    else
        unit = "";
    /* Fix some silly units */
    if (gwy_strequal(unit, "N/sec"))
        unit = "s^-1";
    siunit = gwy_si_unit_new(unit);
    gwy_data_line_set_si_unit_y(dline, siunit);
    g_object_unref(siunit);

    for (i = 0; i < ncurves; i++) {
        GwyGraphCurveModel *gcmodel = gwy_graph_curve_model_new();
        gchar *s = g_strdup_printf("%d", i+1);

        /* FIXME: The docs seem to talk about floats, but AFAICT the data looks
         * like integers. */
        for (j = 0; j < res; j++)
            data[j] = gwy_get_gint32_le(&p) * page->z_scale + page->z_offset;

        gwy_graph_curve_model_set_data_from_dataline(gcmodel, dline, 0, 0);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(i),
                     "description", s,
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
        g_free(s);
    }

    g_object_unref(dline);

    return gmodel;
}

static GwyContainer*
rhk_sm3_load(const gchar *filename,
             G_GNUC_UNUSED GwyRunType mode,
             GError **error)
{
    GPtrArray *rhkfile;
    RHKPage *rhkpage;
    GwyContainer *meta, *container = NULL;
    guchar *buffer = NULL;
    gsize fullsize, size = 0;
    GError *err = NULL;
    const guchar *p;
    GString *key;
    guint i, count;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    fullsize = size;
    if (size < HEADER_SIZE) {
        err_TOO_SHORT(error);
        gwy_file_abandon_contents(buffer, fullsize, NULL);
        return NULL;
    }

    rhkfile = g_ptr_array_new();

    p = buffer;
    count = 0;
    gwy_debug("position %04x", (guint)(p - buffer));
    while ((rhkpage = rhk_sm3_read_page(&p, &size, &err))) {
        gwy_debug("Page #%u read OK", count);
        count++;
        rhkpage->pageno = count;
        gwy_debug("position %04x", (guint)(p - buffer));
        if (rhkpage->type != RHK_TYPE_IMAGE && rhkpage->type != RHK_TYPE_LINE) {
            gwy_debug("Page is neither IMAGE nor LINE, skipping");
            rhk_sm3_page_free(rhkpage);
            continue;
        }
        g_ptr_array_add(rhkfile, rhkpage);
    }

    /* Be tolerant and don't fail when we were able to import at least
     * something */
    if (!rhkfile->len) {
        if (err)
            g_propagate_error(error, err);
        else
            err_NO_DATA(error);
        gwy_file_abandon_contents(buffer, fullsize, NULL);
        g_ptr_array_free(rhkfile, TRUE);
        return NULL;
    }
    g_clear_error(&err);

    container = gwy_container_new();
    key = g_string_new(NULL);

    /* Process fields and lines separately to get nicer data ids */
    /* So, first fields. */
    for (i = count = 0; i < rhkfile->len; i++) {
        GwyDataField *dfield;
        GQuark quark;
        const gchar *cs;
        gchar *s;

        rhkpage = g_ptr_array_index(rhkfile, i);
        if (rhkpage->type != RHK_TYPE_IMAGE)
            continue;

        dfield = rhk_sm3_page_to_data_field(rhkpage);
        quark = gwy_app_get_data_key_for_id(count);
        gwy_container_set_object(container, quark, dfield);
        g_object_unref(dfield);
        p = rhkpage->strings[RHK_STRING_LABEL];
        cs = gwy_enum_to_string(rhkpage->scan_dir,
                                scan_directions, G_N_ELEMENTS(scan_directions));
        if (p && *p) {
            g_string_assign(key, g_quark_to_string(quark));
            g_string_append(key, "/title");
            if (cs)
                s = g_strdup_printf("%s [%s]", p, cs);
            else
                s = g_strdup(p);
            gwy_container_set_string_by_name(container, key->str, s);
        }

        meta = rhk_sm3_get_metadata(rhkpage);
        g_string_printf(key, "/%d/meta", count);
        gwy_container_set_object_by_name(container, key->str, meta);
        g_object_unref(meta);

        gwy_app_channel_check_nonsquare(container, count);
        gwy_file_channel_import_log_add(container, count, NULL, filename);
        count++;
    }

    /* Then lines.  Since the spectra does not seem to have any positional
     * information attached, it makes not sense trying to construct
     * GwySpectra.  Just add them as graphs. */
    for (i = count = 0; i < rhkfile->len; i++) {
        GwyGraphModel *spectra;
        GQuark quark;
        gchar *s = NULL;

        rhkpage = g_ptr_array_index(rhkfile, i);
        if (rhkpage->type != RHK_TYPE_LINE)
            continue;

        p = rhkpage->strings[RHK_STRING_LABEL];
        if (!p || !*p)
            p = gwy_enuml_to_string
                         (rhkpage->line_type,
                          "Histogram", RHK_LINE_HISTOGRAM,
                          "Cross section", RHK_LINE_CROSS_SECTION,
                          "Line test", RHK_LINE_LINE_TEST,
                          "Oscilloscope", RHK_LINE_OSCILLOSCOPE,
                          "Noise power spectrum", RHK_LINE_NOISE_POWER_SPECTRUM,
                          "I-V spectrum", RHK_LINE_IV_SPECTRUM,
                          "I-Z spectrum", RHK_LINE_IZ_SPECTRUM,
                          "Image x average", RHK_LINE_IMAGE_X_AVERAGE,
                          "Image y average", RHK_LINE_IMAGE_Y_AVERAGE,
                          "Noise autocorrelation spectrum",
                          RHK_LINE_NOISE_AUTOCORRELATION_SPECTRUM,
                          "Multichannel analyser data",
                          RHK_LINE_MULTICHANNEL_ANALYSER_DATA,
                          "Renormalized I-V", RHK_LINE_RENORMALIZED_IV,
                          "Image histogram spectra",
                          RHK_LINE_IMAGE_HISTOGRAM_SPECTRA,
                          "Image cross section", RHK_LINE_IMAGE_CROSS_SECTION,
                          "Image avegrage", RHK_LINE_IMAGE_AVERAGE,
                          NULL);
        if (!p || !*p)
            p = s = g_strdup_printf(_("Unknown line %d"), count);

        spectra = rhk_sm3_page_to_spectra(rhkpage);
        g_object_set(spectra, "title", p, NULL);
        quark = gwy_app_get_graph_key_for_id(count);
        gwy_container_set_object(container, quark, spectra);
        g_object_unref(spectra);

        g_free(s);

        count++;
    }

    g_string_free(key, TRUE);

    gwy_file_abandon_contents(buffer, fullsize, NULL);
    for (i = 0; i < rhkfile->len; i++)
        rhk_sm3_page_free(g_ptr_array_index(rhkfile, i));
    g_ptr_array_free(rhkfile, TRUE);

    return container;
}

static GwyContainer*
rhk_sm3_get_metadata(RHKPage *rhkpage)
{
    GwyContainer *meta;
    const gchar *s;
    gchar *str;
    guint i;

    meta = gwy_container_new();

    s = gwy_enuml_to_string(rhkpage->page_type,
                            "Topographic", RHK_PAGE_TOPOGRAPHIC,
                            "Current", RHK_PAGE_CURRENT,
                            "Aux", RHK_PAGE_AUX,
                            "Force", RHK_PAGE_FORCE,
                            "Signal", RHK_PAGE_SIGNAL,
                            "FFT transform", RHK_PAGE_FFT,
                            "Noise power spectrum",
                            RHK_PAGE_NOISE_POWER_SPECTRUM,
                            "Line test", RHK_PAGE_LINE_TEST,
                            "Oscilloscope", RHK_PAGE_OSCILLOSCOPE,
                            "IV spectra", RHK_PAGE_IV_SPECTRA,
                            "Image IV 4x4", RHK_PAGE_IV_4x4,
                            "Image IV 8x8", RHK_PAGE_IV_8x8,
                            "Image IV 16x16", RHK_PAGE_IV_16x16,
                            "Image IV 32x32", RHK_PAGE_IV_32x32,
                            "Image IV Center", RHK_PAGE_IV_CENTER,
                            "Interactive spectra", RHK_PAGE_INTERACTIVE_SPECTRA,
                            "Autocorrelation", RHK_PAGE_AUTOCORRELATION,
                            "IZ spectra", RHK_PAGE_IZ_SPECTRA,
                            "4 gain topography", RHK_PAGE_4_GAIN_TOPOGRAPHY,
                            "8 gain topography", RHK_PAGE_8_GAIN_TOPOGRAPHY,
                            "4 gain current", RHK_PAGE_4_GAIN_CURRENT,
                            "8 gain current", RHK_PAGE_8_GAIN_CURRENT,
                            "Image IV 64x64", RHK_PAGE_IV_64x64,
                            "Autocorrelation spectrum",
                            RHK_PAGE_AUTOCORRELATION_SPECTRUM,
                            "Counter data", RHK_PAGE_COUNTER,
                            "Multichannel analyser",
                            RHK_PAGE_MULTICHANNEL_ANALYSER,
                            "AFM using AFM-100", RHK_PAGE_AFM_100,
                            NULL);
    if (s && *s)
        gwy_container_set_string_by_name(meta, "Type", g_strdup(s));

    s = gwy_enum_to_string(rhkpage->scan_dir,
                           scan_directions, G_N_ELEMENTS(scan_directions));
    if (s && *s)
        gwy_container_set_string_by_name(meta, "Scan Direction", g_strdup(s));

    s = gwy_enuml_to_string(rhkpage->source_type,
                            "Raw", RHK_SOURCE_RAW_PAGE,
                            "Processed", RHK_SOURCE_PROCESSED_PAGE,
                            "Calculated", RHK_SOURCE_CALCULATED_PAGE,
                            "Imported", RHK_SOURCE_IMPORTED_PAGE,
                            NULL);
    if (s && *s)
        gwy_container_set_string_by_name(meta, "Source", g_strdup(s));

    gwy_container_set_string_by_name(meta, "Bias",
                                     g_strdup_printf("%g V", rhkpage->bias));
    gwy_container_set_string_by_name(meta, "Rotation angle",
                                     g_strdup_printf("%f", rhkpage->angle));
    gwy_container_set_string_by_name(meta, "Period",
                                     g_strdup_printf("%f s", rhkpage->period));

    s = rhkpage->strings[RHK_STRING_DATE];
    if (s && *s) {
        str = g_strconcat(s, " ", rhkpage->strings[RHK_STRING_TIME], NULL);
        gwy_container_set_string_by_name(meta, "Date", str);
    }

    s = rhkpage->strings[RHK_STRING_LABEL];
    if (s && *s) {
        gwy_container_set_string_by_name(meta, "Label", g_strdup(s));
    }

    s = rhkpage->strings[RHK_STRING_PATH];
    if (s && *s)
        gwy_container_set_string_by_name(meta, "Path", g_strdup(s));

    s = rhkpage->strings[RHK_STRING_SYSTEM_TEXT];
    if (s && *s)
        gwy_container_set_string_by_name(meta, "System comment", g_strdup(s));

    s = rhkpage->strings[RHK_STRING_SESSION_TEXT];
    if (s && *s)
        gwy_container_set_string_by_name(meta, "Session comment", g_strdup(s));

    s = rhkpage->strings[RHK_STRING_USER_TEXT];
    if (s && *s)
        gwy_container_set_string_by_name(meta, "User comment", g_strdup(s));

    str = g_new(gchar, 33);
    for (i = 0; i < 16; i++) {
        static const gchar hex[] = "0123456789abcdef";

        str[2*i] = hex[rhkpage->page_id[i]/16];
        str[2*i + 1] = hex[rhkpage->page_id[i] % 16];
    }
    str[32] = '\0';
    gwy_container_set_string_by_name(meta, "Page ID", str);

    return meta;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

