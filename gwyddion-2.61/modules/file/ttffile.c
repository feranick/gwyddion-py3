/*
 *  $Id: ttffile.c 24789 2022-04-28 13:19:47Z yeti-dn $
 *  Copyright (C) 2019-2021 David Necas (Yeti), Thomas Wagner.
 *  E-mail: yeti@gwyddion.net, hirschbeutel@gmail.com.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/**
 * [FILE-MAGIC-USERGUIDE]
 * Corning Tropel UltraSort topograhpical data
 * .ttf
 * Read
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Corning Tropel exported CSV data
 * .csv
 * Read
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Indistinguishable from CSV.  Avoding clash with a standard file format.
 **/

#include "config.h"
#include <stdlib.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/correct.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>
#include "err.h"
#include "gwytiff.h"

#define MAGIC "Mapid: "
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define Micrometre 1e-6

/* NB: These are decimal values, not 0x8000 as one would expect. */
enum {
    /* This is simply their private alias of GWY_TIFF_DOUBLE.  Dunno why they have it. */
    CORNING_TIFF_DOUBLE = 8000,

    CORNING_TIFFTAG_FIRST = 8001,  /* The first tag, seems always 0. */
    /* 8002 seems always 65536 */
    CORNING_TIFFTAG_TIME = 8003,
    CORNING_TIFFTAG_SENS = 8004,
    /* 8005 is a double, seems always 0 */
    CORNING_TIFFTAG_XC = 8006,
    CORNING_TIFFTAG_YC = 8007,
    /* 8008 is some double, usually 200-something-ish. */
    CORNING_TIFFTAG_INVDX = 8009,  /* p/mm */
    CORNING_TIFFTAG_XT = 8010,  /* tilt calibration */
    CORNING_TIFFTAG_YT = 8011,
    /* 8012 seems always an empty string */
    /* 8014 seems always 579 */
    CORNING_TIFFTAG_XSQ = 8016,
    CORNING_TIFFTAG_YSQ = 8017,
    /* 8018 is either 0 or 2082 */
    /* 8019 is either 2 or 65535 */
    /* 8020 seems always 65535 */
    /* 8021 seems always 65535 */
    /* 8022 seems always 0 */
    /* 8023 seems always 0 */
    /* 8024 is 0 or 5 */
    /* 8025–8027 seem always 0 */
    CORNING_TIFFTAG_RECIPE = 8028,  /* recipe filename */
    /* 8029 seems always the string UNKNOWN */
    CORNING_TIFFTAG_REFERENCE = 8030,  /* reference filename */
    CORNING_TIFFTAG_OUTSIDE = 8031,
    CORNING_TIFFTAG_SCALE = 8032,
    /* 8033 seems always 0 */
    /* 8034 is a double, seems always 0 */
    CORNING_TIFFTAG_LXLY = 8035,
    /* 8036 is a double, seems always 14 */
    /* 8037 is a double, seems always 0 */
    /* 8038 is a double, seems always 0 */
    /* 8039 is a double, seems 675 or 700 */
    /* 8040–8043 are doubles, seem always 0 */
    /* 8044 seems always 4 */
    CORNING_TIFFTAG_OD = 8045,
    /* 8046 is a double, seems always 0 */
    CORNING_TIFFTAG_GG = 8047,
    CORNING_TIFFTAG_GAMP = 8048,
    /* 8049 is 0 or 1 */
    /* 8050–8053 are doubles, seem always 0 */
    /* 8054 seems always an empty string */
    /* 8056 is some three-component double vector */
    /* 8057 is 0 or 3 */
    /* 8058 is 0 or 2 */
    /* 8059 seems always 0 */
    /* 8060 is empty string or "0" */
    /* 8061 is empty string or "wafer number" */
    /* 8062–8065 seem always empty strings */
    CORNING_TIFFTAG_WAFERNUM = 8066, /* as string */
    /* 8067–8070 seem always empty strings */
    /* 8072 seems always a zero-component double vector */
    /* 8074 seems always 0 */
    /* 8075 seems always a zero-component int vector */
    /* 8076 seems always 0 */
    /* 8077 is a double, seems always 0 */
    CORNING_TIFFTAG_TEMP = 8078,
    /* 8079 is a double, seems always 0 */
    /* 8080 is a double, seems always 0 */
    /* 8081 seems always a zero-component double vector */
    /* 8082 seems always an empty string */
    CORNING_TIFFTAG_THRESHOLD = 8088,
    CORNING_TIFFTAG_HEADGAP = 8091,
    CORNING_TIFFTAG_FSENSE = 8095,
    CORNING_TIFFTAG_CANGLE = 8096,
    CORNING_TIFFTAG_TOPDIAM = 8097,
    CORNING_TIFFTAG_PARTTYPE = 8100,
    CORNING_TIFFTAG_MAPID = 8106,
    CORNING_TIFFTAG_XRES = 8152,
    CORNING_TIFFTAG_YRES = 8153,
    CORNING_TIFFTAG_DATA = 8154,
    CORNING_TIFFTAG_XMLRECIPE = 8512,  /* a huge XML with the entire recipe, apparently */
};

typedef struct {
    gint xres;
    gint yres;
    gdouble xreal;
    gdouble yreal;
    gchar *units;
} CorningCSVHeader;

static gboolean      module_register        (void);
static gint          ttf_detect             (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static GwyContainer* ttf_load               (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static gboolean      ttf_load_image         (GwyTIFF *tiff,
                                             GwyContainer *container,
                                             gint dirno,
                                             GError **error);
static guint         read_image_data        (const guchar *p,
                                             gulong size,
                                             gdouble *data,
                                             gdouble *mdata,
                                             guint delta_nbits,
                                             guint data_nbits,
                                             guint nan_count_nbits,
                                             guint n,
                                             gdouble q);
static gboolean      fix_corning_double_tags(GwyTIFF *tiff,
                                             GError **error);
static GwyContainer* ttf_get_meta           (GwyTIFF *tiff,
                                             gint dirno);
static gint          ccsv_detect            (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static GwyContainer* ccsv_load              (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static gchar*        ccsv_read_header       (CorningCSVHeader *header,
                                             gchar *p,
                                             GError **error);
static gboolean      ccsv_read_images       (const CorningCSVHeader *header,
                                             gchar *p,
                                             GwyDataField **dfield,
                                             GwyDataField **mask,
                                             GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    module_register,
    N_("Imports Corning Tropel UltraSort files."),
    "Yeti <yeti@gwyddion.net>, Thomas Wagner <hirschbeutel@gmail.com>",
    "1.0",
    "David Nečas (Yeti), Thomas Wagner",
    "2019",
};

GWY_MODULE_QUERY2(module_info, ttffile)

static gboolean
module_register(void)
{
    gwy_file_func_register("ttffile",
                           N_("Corning Tropel UltraSort data (.ttf)"),
                           (GwyFileDetectFunc)&ttf_detect,
                           (GwyFileLoadFunc)&ttf_load,
                           NULL,
                           NULL);
    gwy_file_func_register("corningcsvfile",
                           N_("Corning Tropel UltraSort CSV export (.csv)"),
                           (GwyFileDetectFunc)&ccsv_detect,
                           (GwyFileLoadFunc)&ccsv_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
ttf_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    GwyTIFF *tiff;
    guint score = 0;
    GwyTIFFVersion version = GWY_TIFF_CLASSIC;
    guint byteorder = G_LITTLE_ENDIAN;
    gchar *make = NULL, *model = NULL;
    const GwyTIFFEntry *entry;
    guint xres, yres;

    if (only_name)
        return score;

    /* Weed out non-TIFFs */
    if (!gwy_tiff_detect(fileinfo->head, fileinfo->buffer_len, &version, &byteorder))
        return 0;

    /* Use GwyTIFF for detection to avoid problems with fragile libtiff. Progressively try finer tests. */
    if ((tiff = gwy_tiff_load(fileinfo->name, NULL))
         && gwy_tiff_get_string0(tiff, GWY_TIFFTAG_MAKE, &make)
         && gwy_tiff_get_string0(tiff, GWY_TIFFTAG_MODEL, &model)
         && gwy_tiff_find_tag(tiff, 0, CORNING_TIFFTAG_FIRST)
         && (entry = gwy_tiff_find_tag(tiff, 0, CORNING_TIFFTAG_INVDX))
         && entry->type == (guint)CORNING_TIFF_DOUBLE
         && (entry = gwy_tiff_find_tag(tiff, 0, CORNING_TIFFTAG_SENS))
         && entry->type == (guint)CORNING_TIFF_DOUBLE
         && (entry = gwy_tiff_find_tag(tiff, 0, CORNING_TIFFTAG_DATA))
         && entry->type == (guint)GWY_TIFF_LONG
         && gwy_tiff_get_uint0(tiff, CORNING_TIFFTAG_XRES, &xres)
         && gwy_tiff_get_uint0(tiff, CORNING_TIFFTAG_YRES, &yres))
        score = 100;

    /* We could check these for "UltraSort" and "Corning Tropel", but who knows how stable they are. */
    g_free(model);
    g_free(make);
    if (tiff)
        gwy_tiff_free(tiff);

    return score;
}

static GwyContainer*
ttf_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyTIFF *tiff;
    GwyContainer *container = NULL, *meta;
    gint ndirs, i;

    tiff = gwy_tiff_load(filename, error);
    if (!tiff)
        return NULL;

    if (!(ndirs = gwy_tiff_get_n_dirs(tiff))) {
        err_NO_DATA(error);
        goto fail;
    }
    if (!fix_corning_double_tags(tiff, error))
        goto fail;

    container = gwy_container_new();
    for (i = 0; i < ndirs; i++) {
        if (!ttf_load_image(tiff, container, i, error)) {
            GWY_OBJECT_UNREF(container);
            break;
        }
        meta = ttf_get_meta(tiff, i);
        gwy_container_set_object(container, gwy_app_get_data_meta_key_for_id(i), meta);
        g_object_unref(meta);
    }

fail:
    gwy_tiff_free(tiff);

    return container;
}

static gboolean
ttf_load_image(GwyTIFF *tiff, GwyContainer *container, gint dirno, GError **error)
{
    GwyDataField *field = NULL, *mask = NULL;
    const GwyTIFFEntry *entry;
    const guchar *p;
    gulong size, offset;
    G_GNUC_UNUSED guint bps;
    guint data_nbits, delta_nbits, nan_count_nbits;
    guint xres, yres, scale, n, nread;
    gdouble sens, invdx;
    gchar *title;

    if (!(entry = gwy_tiff_find_tag(tiff, dirno, CORNING_TIFFTAG_DATA)) || entry->type != (guint)GWY_TIFF_LONG) {
        err_FILE_TYPE(error, "Corning Tropel UltraSort");
        return FALSE;
    }
    if (entry->count <= 4) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Data block is truncated."));
        return FALSE;
    }

#ifdef DEBUG
    for (n = CORNING_TIFFTAG_FIRST; n < 8517; n++) {
        gdouble tmp;
        if (gwy_tiff_get_float(tiff, dirno, n, &tmp))
            gwy_debug("float tag%u = %g", n, tmp);
    }
#endif

    /* Required parameters */
    if (!(gwy_tiff_get_uint(tiff, dirno, CORNING_TIFFTAG_XRES, &xres)
          && gwy_tiff_get_uint(tiff, dirno, CORNING_TIFFTAG_YRES, &yres)
          && gwy_tiff_get_float(tiff, dirno, CORNING_TIFFTAG_INVDX, &invdx)
          && gwy_tiff_get_float(tiff, dirno, CORNING_TIFFTAG_SENS, &sens)
          && gwy_tiff_get_uint(tiff, dirno, CORNING_TIFFTAG_SCALE, &scale))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Parameter tag set is incomplete."));
        return FALSE;
    }
    gwy_debug("xres %d, yres %d, invdx %g", xres, yres, invdx);
    gwy_debug("sens %g, scale %d", sens, scale);
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        return FALSE;
    n = xres*yres;

    if (!(invdx > 0.0)) {
        g_warning("Real pixel width is 0.0, fixing to 1.0");
        invdx = 1.0;
    }

    /* This means tag data are never stored within the tag; just the pointer. */
    size = 4*entry->count;
    gwy_debug("data tag %d, type %d, size in bytes %lu", entry->tag, entry->type, size);
    p = entry->value;
    offset = tiff->get_guint32(&p);
    p = tiff->data + offset;

    bps = gwy_get_guint32_le(&p);  /* XXX: Wagner does not use this one at all, it should probably be always 4. */
    data_nbits = gwy_get_guint32_le(&p);
    delta_nbits = gwy_get_guint32_le(&p);
    nan_count_nbits = gwy_get_guint32_le(&p);
    size -= 4*sizeof(guint32);
    gwy_debug("bps %u, data_nbits %u, delta_nbits %u, nan_count_nbits %u",
              bps, data_nbits, delta_nbits, nan_count_nbits);
    gwy_debug("remaining size %lu", size);

    if (data_nbits < 1 || data_nbits > 32) {
        err_INVALID(error, "DataNBits");
        return FALSE;
    }
    if (delta_nbits < 3 || delta_nbits > 32) {
        err_INVALID(error, "DeltaNBits");
        return FALSE;
    }
    if (nan_count_nbits < 1 || nan_count_nbits > 32) {
        err_INVALID(error, "NaNCountNBits");
        return FALSE;
    }
    field = gwy_data_field_new(xres, yres, 1e-3*xres/invdx, 1e-3*yres/invdx, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(field), "m");
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(field), "m");

    mask = gwy_data_field_new_alike(field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mask), NULL);

    nread = read_image_data(p, size, gwy_data_field_get_data(field), gwy_data_field_get_data(mask),
                            delta_nbits, data_nbits, nan_count_nbits, n, sens/scale * Micrometre);
    if (nread < n) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Data block is truncated."));
        g_object_unref(mask);
        g_object_unref(field);
        return FALSE;
    }

    if (!gwy_app_channel_remove_bad_data(field, mask))
        GWY_OBJECT_UNREF(mask);

    gwy_container_set_object(container, gwy_app_get_data_key_for_id(dirno), field);
    g_object_unref(field);
    if (mask) {
        gwy_container_set_object(container, gwy_app_get_mask_key_for_id(dirno), mask);
        g_object_unref(mask);
    }

    if (gwy_tiff_get_string(tiff, dirno, CORNING_TIFFTAG_MAPID, &title))
        gwy_container_set_string(container, gwy_app_get_data_title_key_for_id(dirno), title);

    return TRUE;
}

/* Convert the unsigned nbits integer in t into signed glong. There is probably a smarter way to do this... */
static inline glong
fix_to_signed(guint64 t, guint nbits)
{
    return (t & 1ul << (nbits - 1)) ? (glong)t - (1ul << nbits) : (glong)t;
}

static guint
read_image_data(const guchar *p, gulong size,
                gdouble *data, gdouble *mdata,
                guint delta_nbits, guint data_nbits, guint nan_count_nbits,
                guint n, gdouble q)
{
    typedef enum {
        ITEM_DELTA = 0,
        ITEM_DATA  = 1,
        ITEM_NANS  = 2,
        ITEM_NTYPES
    } ItemType;

    guint item_sizes[ITEM_NTYPES] = { delta_nbits, data_nbits, nan_count_nbits };
    gulong pos, xpos;
    guint is_nan, is_data, is_jump_up, is_jump_down, have_bits, need_bits, i;
    glong current_value;
    guint64 bits, t;
    ItemType itemtype;

    item_sizes[ITEM_DELTA] = delta_nbits;
    item_sizes[ITEM_DATA] = data_nbits;
    item_sizes[ITEM_NANS] = nan_count_nbits;

    /* All data are deltas by default and have delta_nbits bits.  The following special values are special values of
     * the deltas.  This implies delta_nbits ≥ 3 because otherwise they would not even fit. */

    /* Indicates a block of NaNs; next nan_count_nbits bits give the number of following NaN values. */
    is_nan = 1u << (delta_nbits - 1);
    /* Indicates a direct (absolute) data value; next data_nbits bits contain the data value. */
    is_data = is_nan + 1;
    /* These are deltas which change the current value, but the result should not be used.  In other words, they are
     * used to split big jumps into pieces. */
    is_jump_up = is_data + 1;
    is_jump_down = is_nan - 1;
    gwy_debug("is_nan %u, is_data %u, is_jump_up %u, is_jump_down %u", is_nan, is_data, is_jump_up, is_jump_down);

    current_value = 0;
    bits = 0;
    pos = 0;
    i = 0;
    have_bits = 0;
    itemtype = ITEM_DELTA;
    while (i < n && (have_bits || pos < size)) {
        need_bits = item_sizes[itemtype];
        if (have_bits < need_bits) {
            xpos = (pos & ~3ul) | ((pos & 3ul) ^ 3ul);
            bits = (bits << 8ul) | p[xpos];
            pos++;
            have_bits += 8;
            continue;
        }

        /* Extract the highest need_bits bits into t. */
        t = bits >> (gulong)(have_bits - need_bits);
        bits &= ~(t << (gulong)(have_bits - need_bits));
        have_bits -= need_bits;

        if (itemtype == ITEM_NANS) {
            /* Output a block of NaNs. */
            i += MIN(t+1, n-i);
            itemtype = ITEM_DELTA;
            continue;
        }

        if (itemtype == ITEM_DATA) {
            current_value = fix_to_signed(t, data_nbits);
            itemtype = ITEM_DELTA;
        }
        else if (itemtype == ITEM_DELTA) {
            if (t == is_data) {
                itemtype = ITEM_DATA;
                continue;
            }
            if (t == is_nan) {
                itemtype = ITEM_NANS;
                continue;
            }
            current_value += fix_to_signed(t, delta_nbits);
            /* Do not output any value for jumps. */
            if (t == is_jump_up || t == is_jump_down)
                continue;
        }

        /* Output the current value. */
        mdata[i] = 1.0;
        data[i] = q*current_value;
        i++;
    }

    return i;
}

/* Change CORNING_TIFF_DOUBLE to GWY_TIFF_DOUBLE and revalidate all tags.  This is the easiest way of dealing with
 * them. */
static gboolean
fix_corning_double_tags(GwyTIFF *tiff, GError **error)
{
    GArray *tags;
    GwyTIFFEntry *entry;
    guint dirno, n, i;

    for (dirno = 0; dirno < tiff->dirs->len; dirno++) {
        tags = (GArray*)g_ptr_array_index(tiff->dirs, dirno);
        n = tags->len;

        for (i = 0; i < n; i++) {
            entry = &g_array_index(tags, GwyTIFFEntry, i);
            if ((guint)entry->type == CORNING_TIFF_DOUBLE)
                entry->type = GWY_TIFF_DOUBLE;
        }

    }

    return gwy_tiff_tags_valid(tiff, error);
}

static GwyContainer*
ttf_get_meta(GwyTIFF *tiff, gint dirno)
{
    /* Strings present only in directory 0. */
    static const GwyEnum dir0_tags[] = {
        { "Make",     GWY_TIFFTAG_MAKE,     },
        { "Model",    GWY_TIFFTAG_MODEL,    },
        { "Software", GWY_TIFFTAG_SOFTWARE, },
    };
    static const GwyEnum double_tags[] = {
        { "Sensitivity",        CORNING_TIFFTAG_SENS,      },
        { "Xc",                 CORNING_TIFFTAG_XC,        },
        { "Yc",                 CORNING_TIFFTAG_YC,        },
        { "p/mm",               CORNING_TIFFTAG_INVDX,     },
        { "Xt",                 CORNING_TIFFTAG_XT,        },
        { "Yt",                 CORNING_TIFFTAG_YT,        },
        { "Xsq",                CORNING_TIFFTAG_XSQ,       },
        { "Ysq",                CORNING_TIFFTAG_YSQ,       },
        { "Lx, Ly",             CORNING_TIFFTAG_LXLY,      },
        { "OD",                 CORNING_TIFFTAG_OD,        },
        { "GG",                 CORNING_TIFFTAG_GG,        },
        { "Temperature",        CORNING_TIFFTAG_TEMP,      },
        { "Threshold",          CORNING_TIFFTAG_THRESHOLD, },
        { "Head gap",           CORNING_TIFFTAG_HEADGAP,   },
        { "F sense",            CORNING_TIFFTAG_FSENSE,    },
        { "Outer/top diameter", CORNING_TIFFTAG_TOPDIAM,   },
    };
    static const GwyEnum uint_tags[] = {
        { "Time",               CORNING_TIFFTAG_TIME,      },
        { "Outside",            CORNING_TIFFTAG_OUTSIDE,   },
        { "Scale",              CORNING_TIFFTAG_SCALE,     },
        { "Gamp",               CORNING_TIFFTAG_GAMP,      },
        { "C angle",            CORNING_TIFFTAG_CANGLE,    },
        { "Part type",          CORNING_TIFFTAG_PARTTYPE,  },
    };
    static const GwyEnum string_tags[] = {
        { "Recipe",             CORNING_TIFFTAG_RECIPE,    },
        { "Reference",          CORNING_TIFFTAG_REFERENCE, },
        { "Wafer number",       CORNING_TIFFTAG_WAFERNUM,  },
        { "Map id",             CORNING_TIFFTAG_MAPID,     },
    };

    GwyContainer *meta;
    gchar *s;
    gdouble d;
    guint i, u;
    gchar buf[32];

    meta = gwy_container_new();

    for (i = 0; i < G_N_ELEMENTS(dir0_tags); i++) {
        if (gwy_tiff_get_string0(tiff, dir0_tags[i].value, &s))
            gwy_container_set_string_by_name(meta, dir0_tags[i].name, s);
    }
    for (i = 0; i < G_N_ELEMENTS(string_tags); i++) {
        if (gwy_tiff_get_string(tiff, dirno, string_tags[i].value, &s))
            gwy_container_set_string_by_name(meta, string_tags[i].name, s);
    }
    for (i = 0; i < G_N_ELEMENTS(uint_tags); i++) {
        if (gwy_tiff_get_uint(tiff, dirno, uint_tags[i].value, &u)) {
            g_snprintf(buf, sizeof(buf), "%u", u);
            gwy_container_set_const_string_by_name(meta, uint_tags[i].name, buf);
        }
    }
    for (i = 0; i < G_N_ELEMENTS(double_tags); i++) {
        if (gwy_tiff_get_float(tiff, dirno, double_tags[i].value, &d)) {
            g_snprintf(buf, sizeof(buf), "%g", d);
            gwy_container_set_const_string_by_name(meta, double_tags[i].name, buf);
        }
    }

    return meta;
}

static gint
ccsv_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    /* They many not be all there, for instance of the last four we expect about two... */
    const gchar *wanted_strings[] = {
        "Time: ", "Size: ", "Zoom: ", "Units: ", "ZRes: ", "Outside: ", "Sensitivity: ", "Scale: ", "Mapformat: ",
        "Tropel", "Corning", "UltraSort", "TMSPlot",
    };
    guint i, is_not_ccsv = 100;

    if (only_name)
        return 0;

    if (strncmp(fileinfo->head, MAGIC, MAGIC_SIZE))
        return 0;

    for (i = 0; i < G_N_ELEMENTS(wanted_strings); i++) {
        if (strstr(fileinfo->head, wanted_strings[i])) {
            gwy_debug("found %s", wanted_strings[i]);
            is_not_ccsv = 2*is_not_ccsv/3;
        }
    }
    gwy_debug("is_not %d", is_not_ccsv);

    return 100 - is_not_ccsv;
}

static GwyContainer*
ccsv_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    CorningCSVHeader header;
    GwyContainer *container = NULL;
    GwyDataField *dfield = NULL, *mask = NULL;
    GError *err = NULL;
    gchar *buffer, *p;
    GQuark quark;
    gsize size;

    gwy_clear(&header, 1);
    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    if (strncmp(buffer, MAGIC, MAGIC_SIZE)) {
        err_FILE_TYPE(error, "Corning CSV");
        goto fail;
    }
    if (!(p = ccsv_read_header(&header, buffer, error)))
        goto fail;
    if (err_DIMENSION(error, header.xres) || err_DIMENSION(error, header.yres))
        goto fail;
    if (!ccsv_read_images(&header, p, &dfield, &mask, error))
        goto fail;

    container = gwy_container_new();
    quark = gwy_app_get_data_key_for_id(0);
    gwy_container_set_object(container, quark, dfield);
    quark = gwy_app_get_mask_key_for_id(0);
    gwy_container_set_object(container, quark, mask);
    gwy_app_channel_title_fall_back(container, 0);
    gwy_file_channel_import_log_add(container, 0, NULL, filename);

fail:
    GWY_OBJECT_UNREF(mask);
    GWY_OBJECT_UNREF(dfield);
    g_free(buffer);
    g_free(header.units);

    return container;
}

#define free_regex(r) if (r) g_regex_unref(r); r = NULL
#define free_matchinfo(i) if (i) g_match_info_free(i); i = NULL

/* The header is split to lines, but the split is somewhat arbitrary. Especially when a field is empty, the next field
 * tends to continue on the same line.  Do not try to parse it as a well-formatted header... */
static gchar*
ccsv_read_header(CorningCSVHeader *header,
                 gchar *p, GError **error)
{
    GMatchInfo *info = NULL;
    GRegex *regex;
    gchar *s, *retval = NULL;
    gdouble mmp, pmm;

    s = strstr(p, "Units: ");
    if (!s) {
        err_MISSING_FIELD(error, "Units");
        return NULL;
    }

    s += sizeof("Units: ")-1;
    while (*s != '\0' && *s != '\n' && *s != '\r')
        s++;
    if (*s == '\0') {
        err_TRUNCATED_HEADER(error);
        return NULL;
    }
    *s = '\0';

    regex = g_regex_new("\\bSize:\\s*(?P<xres>[0-9]+)x(?P<yres>[0-9]+)", G_REGEX_NO_AUTO_CAPTURE, 0, NULL);
    g_return_val_if_fail(regex, NULL);
    if (!g_regex_match(regex, p, 0, &info)) {
        err_MISSING_FIELD(error, "Size");
        goto fail;
    }
    header->xres = atoi(g_match_info_fetch_named(info, "xres"));
    header->yres = atoi(g_match_info_fetch_named(info, "yres"));
    gwy_debug("xres %d, yres %d", header->xres, header->yres);
    free_matchinfo(info);
    free_regex(regex);

    regex = g_regex_new("(?P<pixmm>-?[0-9.]+)\\s+p/mm\\s+(?P<mmpix>-?[0-9.]+)\\s+mm/p\\b",
                        G_REGEX_NO_AUTO_CAPTURE | G_REGEX_MULTILINE | G_REGEX_DOTALL, 0, NULL);
    g_return_val_if_fail(regex, NULL);
    if (!g_regex_match(regex, p, 0, &info)) {
        err_MISSING_FIELD(error, "Box");
        goto fail;
    }
    pmm = g_strtod(g_match_info_fetch_named(info, "pixmm"), NULL);
    mmp = g_strtod(g_match_info_fetch_named(info, "mmpix"), NULL);
    free_matchinfo(info);
    free_regex(regex);
    pmm = sqrt(fabs(pmm/mmp));
    header->xreal = 1e-3 * header->xres/pmm;
    header->yreal = 1e-3 * header->yres/pmm;
    gwy_debug("xreal %g, yreal %g", header->xreal, header->yreal);
    if (!(header->xreal > 0.0)) {
        g_warning("Real pixel width is 0.0, fixing to 1.0");
        header->xreal = header->yreal = 1.0;
    }

    regex = g_regex_new("\\bUnits:\\s+(?P<units>\\S+)", G_REGEX_NO_AUTO_CAPTURE, 0, NULL);
    g_return_val_if_fail(regex, NULL);
    if (!g_regex_match(regex, p, 0, &info)) {
        err_MISSING_FIELD(error, "Units");
        goto fail;
    }
    header->units = g_strdup(g_match_info_fetch_named(info, "units"));
    gwy_debug("units %s", header->units);
    free_matchinfo(info);
    free_regex(regex);

    retval = s+1;

fail:
    free_matchinfo(info);
    free_regex(regex);

    return retval;
}

static gboolean
ccsv_read_images(const CorningCSVHeader *header,
                 gchar *p,
                 GwyDataField **dfield, GwyDataField **mask,
                 GError **error)
{
    gdouble *d, *m;
    gchar *line, *end;
    gint xres, yres, i, j;

    xres = header->xres;
    yres = header->yres;

    *dfield = gwy_data_field_new(xres, yres, header->xreal, header->yreal, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(*dfield), "m");
    *mask = gwy_data_field_new_alike(*dfield, TRUE);
    if (!gwy_strequal(header->units, "Microns")) {
        g_warning("Units are not Microns, setting to metre anyway.");
    }
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(*dfield), "m");
    d = gwy_data_field_get_data(*dfield);
    m = gwy_data_field_get_data(*mask);

    while (*p == '\r' || *p == '\n')
        p++;

    for (i = 0; i < yres; i++) {
        line = gwy_str_next_line(&p);
        if (!line) {
            err_TRUNCATED_PART(error, "data");
            return FALSE;
        }
        for (j = 0; j < xres; j++) {
            if (strncmp(line, "NaN", 3) == 0) {
                m[i*xres + j] = 1.0;
                line += 3;
            }
            else {
                d[i*xres + j] = Micrometre*g_strtod(line, &end);
                if (end == line) {
                    err_TRUNCATED_PART(error, "data");
                    return FALSE;
                }
                line = end;
            }
            while (*line == ',' || g_ascii_isspace(*line))
                line++;
        }
    }

    gwy_data_field_laplace_solve(*dfield, *mask, -1, 1.0);

    return TRUE;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
