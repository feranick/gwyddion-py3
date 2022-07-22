/*
 *  $Id: keyence.c 24459 2021-11-03 17:28:15Z yeti-dn $
 *  Copyright (C) 2015-2021 David Necas (Yeti).
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
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-keyence-vk4">
 *   <comment>Keyence VK4 profilometry data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="VK4_"/>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-keyence-vk6">
 *   <comment>Keyence VK6 profilometry data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="VK6">
 *       <match type="string" offset="7" value="BM"/>
 *     </match>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Keyence VK4.
 * 0 string VK4_ Keyence profilometry VK4 data
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Keyence VK6.
 * 0 string VK6
 * >7 string BM Keyence profilometry VK4 data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Keyence profilometry VK4, VK6
 * .vk4, .vk6
 * Read
 **/

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>
#include "gwyzip.h"
#include "get.h"
#include "err.h"

#define MAGIC4 "VK4_"
#define MAGIC4_SIZE (sizeof(MAGIC4)-1)

#define MAGIC6 "VK6"
#define MAGIC6_SIZE (sizeof(MAGIC6)-1)

#define MAGICBMP "BM"
#define MAGICBMP_SIZE (sizeof(MAGICBMP)-1)

#define MAGIC0 "\x00\x00\x00\x00"
#define MAGIC0_SIZE (sizeof(MAGIC0)-1)

#define EXTENSION4 ".vk4"
#define EXTENSION6 ".vk6"

#define Picometre (1e-12)

enum {
    KEYENCE4_HEADER_SIZE = 12,
    KEYENCE4_OFFSET_TABLE_SIZE = 72,
    KEYENCE4_MEASUREMENT_CONDITIONS_MIN_SIZE = 304,
    KEYENCE4_ASSEMBLY_INFO_SIZE = 16,
    KEYENCE4_ASSEMBLY_CONDITIONS_SIZE = 8,
    KEYENCE4_ASSEMBLY_HEADERS_SIZE = (KEYENCE4_ASSEMBLY_INFO_SIZE + KEYENCE4_ASSEMBLY_CONDITIONS_SIZE),
    KEYENCE4_ASSEMBLY_FILE_SIZE = 532,
    KEYENCE4_TRUE_COLOR_IMAGE_MIN_SIZE = 20,
    KEYENCE4_FALSE_COLOR_IMAGE_MIN_SIZE = 796,
    KEYENCE4_LINE_MEASUREMENT_LEN = 1024,
    KEYENCE4_LINE_MEASUREMENT_SIZE = 18440,

    KEYENCE6_HEADER_SIZE = 7,
    BMP_HEADER_SIZE = 14 + 40,  /* This is for the NT/3.1 BMP version, which what they actually use. */
    HDR_IMAGE_HEADER_SIZE = 16,
};

typedef enum {
    KEYENCE4_NORMAL_FILE = 0,
    KEYENCE4_ASSEMBLY_FILE = 1,
    KEYENCE4_ASSEMBLY_FILE_UNICODE = 2,
} Keyence4FileType;

typedef struct {
    guchar magic[4];
    guchar dll_version[4];
    guchar file_type[4];
} Keyence4Header;

typedef struct {
    guint setting;
    guint color_peak;
    guint color_light;
    guint light[3];
    guint height[3];
    guint color_peak_thumbnail;
    guint color_thumbnail;
    guint light_thumbnail;
    guint height_thumbnail;
    guint assemble;
    guint line_measure;
    guint line_thickness;
    guint string_data;
    guint reserved;
} Keyence4OffsetTable;

typedef struct {
    guint size;
    guint year;
    guint month;
    guint day;
    guint hour;
    guint minute;
    guint second;
    gint diff_utc_by_minutes;
    guint image_attributes;
    guint user_interface_mode;
    guint color_composite_mode;
    guint num_layer;
    guint run_mode;
    guint peak_mode;
    guint sharpening_level;
    guint speed;
    guint distance;
    guint pitch;
    guint optical_zoom;
    guint num_line;
    guint line0_pos;
    guint reserved1[3];
    guint lens_mag;
    guint pmt_gain_mode;
    guint pmt_gain;
    guint pmt_offset;
    guint nd_filter;
    guint reserved2;
    guint persist_count;
    guint shutter_speed_mode;
    guint shutter_speed;
    guint white_balance_mode;
    guint white_balance_red;
    guint white_balance_blue;
    guint camera_gain;
    guint plane_compensation;
    guint xy_length_unit;
    guint z_length_unit;
    guint xy_decimal_place;
    guint z_decimal_place;
    guint x_length_per_pixel;
    guint y_length_per_pixel;
    guint z_length_per_digit;
    guint reserved3[5];
    guint light_filter_type;
    guint reserved4;
    guint gamma_reverse;
    guint gamma;
    guint gamma_offset;
    guint ccd_bw_offset;
    guint numerical_aperture;
    guint head_type;
    guint pmt_gain2;
    guint omit_color_image;
    guint lens_id;
    guint light_lut_mode;
    guint light_lut_in0;
    guint light_lut_out0;
    guint light_lut_in1;
    guint light_lut_out1;
    guint light_lut_in2;
    guint light_lut_out2;
    guint light_lut_in3;
    guint light_lut_out3;
    guint light_lut_in4;
    guint light_lut_out4;
    guint upper_position;
    guint lower_position;
    guint light_effective_bit_depth;
    guint height_effective_bit_depth;
    /* XXX: There is much more... */
} Keyence4MeasurementConditions;

typedef struct {
    guint size;   /* The size of *all* assembly-related blocks. */
    Keyence4FileType file_type;
    guint stage_type;
    guint x_position;
    guint y_position;
} Keyence4AssemblyInformation;

typedef struct {
    guint auto_adjustment;
    guint source;
    guint thin_out;
    guint count_x;
    guint count_y;
} Keyence4AssemblyConditions;

typedef struct {
    guint16 source_file[260];   /* This is Microsoft's wchar_t. */
    guint pos_x;
    guint pos_y;
    guint datums_pos;
    guint fix_distance;
    guint distance_x;
    guint distance_y;
} Keyence4AssemblyFile;

typedef struct {
    guint width;
    guint height;
    guint bit_depth;
    guint compression;
    guint byte_size;
    const guchar *data;
} Keyence4TrueColorImage;

typedef struct {
    guint width;
    guint height;
    guint bit_depth;
    guint compression;
    guint byte_size;
    guint palette_range_min;
    guint palette_range_max;
    guchar palette[0x300];
    const guchar *data;
} Keyence4FalseColorImage;

typedef struct {
    guint size;
    guint line_width;
    const guchar *light[3];
    const guchar *height[3];
} Keyence4LineMeasurement;

typedef struct {
    gchar *title;
    gchar *lens_name;
} Keyence4CharacterStrings;

typedef struct {
    Keyence4Header header;
    Keyence4OffsetTable offset_table;
    Keyence4MeasurementConditions meas_conds;
    /* The rest is optional. */
    Keyence4AssemblyInformation assembly_info;
    Keyence4AssemblyConditions assembly_conds;
    guint assembly_nfiles;
    guint nimages;
    Keyence4AssemblyFile *assembly_files;
    Keyence4TrueColorImage color_peak;
    Keyence4TrueColorImage color_light;
    Keyence4FalseColorImage light[3];
    Keyence4FalseColorImage height[3];
    Keyence4LineMeasurement line_measure;
    Keyence4CharacterStrings char_strs;
    /* Raw file contents. */
    const guchar *buffer;
    gsize size;
} Keyence4File;

typedef struct {
    GwyContainer *meta;
    GString *path;
    GString *curr_element;
    GArray *compdepths;
    gint depth;
} Keyence6Meta;

static gboolean      module_register     (void);
static gint          keyence4_detect     (const GwyFileDetectInfo *fileinfo,
                                          gboolean only_name);
static GwyContainer* keyence4_load       (const gchar *filename,
                                          GwyRunType mode,
                                          GError **error);
static GwyContainer* keyence4_load_membuf(const guchar *buffer,
                                          gsize size,
                                          GError **error);
static void          keyence4_free       (Keyence4File *kfile);
static gboolean      read_header         (const guchar **p,
                                          gsize *size,
                                          Keyence4Header *header,
                                          GError **error);
static gboolean      read_offset_table   (const guchar **p,
                                          gsize *size,
                                          Keyence4OffsetTable *offsettable,
                                          GError **error);
static gboolean      read_meas_conds     (const guchar **p,
                                          gsize *size,
                                          Keyence4MeasurementConditions *measconds,
                                          GError **error);
static gboolean      read_assembly_info  (Keyence4File *kfile,
                                          GError **error);
static gboolean      read_data_images    (Keyence4File *kfile,
                                          GError **error);
static gboolean      read_color_images   (Keyence4File *kfile,
                                          GError **error);
static gboolean      read_line_meas      (Keyence4File *kfile,
                                          GError **error);
static gboolean      read_character_strs (Keyence4File *kfile,
                                          GError **error);
static GwyDataField* create_data_field   (const Keyence4FalseColorImage *image,
                                          const Keyence4MeasurementConditions *measconds,
                                          gboolean is_height);
static GwyDataField* create_color_field  (const Keyence4TrueColorImage *image,
                                          const Keyence4MeasurementConditions *measconds,
                                          gint channelid);
static GwyContainer* create_meta         (const Keyence4File *kfile);
static void          add_data_field      (GwyContainer *data,
                                          gint *id,
                                          GwyDataField *dfield,
                                          GwyContainer *meta,
                                          const gchar *title,
                                          gint i,
                                          const gchar *gradient);

#ifdef HAVE_GWYZIP
static gint          keyence6_detect           (const GwyFileDetectInfo *fileinfo,
                                                gboolean only_name);
static GwyContainer* keyence6_load             (const gchar *filename,
                                                GwyRunType mode,
                                                GError **error);
static GwyZipFile    make_temporary_zip_file   (const guchar *buffer,
                                                gsize size,
                                                const gchar *nametemplate,
                                                gchar **actualname,
                                                GError **error);
static void          add_vk6_hdr_images        (GwyContainer *data,
                                                GwyZipFile zipfile);
static gboolean      read_vk6_hdr_images       (const guchar *buffer,
                                                gsize size,
                                                GwyDataField **fields,
                                                guint nf);
static void          distribute_meta6          (GwyContainer *data,
                                                GwyContainer *addmeta);
static GwyContainer* read_vk6_measure_condition(GwyZipFile zipfile);
static GwyContainer* parse_xml_metadata        (const gchar *buffer,
                                                gsize size);
#endif

static const gchar *peaknames[3] = { "Peak Red", "Peak Green", "Peak Blue" };
static const gchar *lightnames[3] = { "Light Red", "Light Green", "Light Blue" };
static const gchar *hdrnames[3] = { "HDR Red", "HDR Green", "HDR Blue" };
static const gchar *gradientnames[3] = { "RGB-Red", "RGB-Green", "RGB-Blue" };

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Keyence VK4 and VK6 files."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, keyence)

static gboolean
module_register(void)
{
    gwy_file_func_register("keyence4",
                           N_("Keyence VK4 data files (.vk4)"),
                           (GwyFileDetectFunc)&keyence4_detect,
                           (GwyFileLoadFunc)&keyence4_load,
                           NULL,
                           NULL);
#ifdef HAVE_GWYZIP
    gwy_file_func_register("keyence6",
                           N_("Keyence VK6 data files (.vk6)"),
                           (GwyFileDetectFunc)&keyence6_detect,
                           (GwyFileLoadFunc)&keyence6_load,
                           NULL,
                           NULL);
#endif
    return TRUE;
}

static gint
keyence4_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION4) ? 15 : 0;

    if (fileinfo->buffer_len > MAGIC4_SIZE + KEYENCE4_HEADER_SIZE
        && memcmp(fileinfo->head, MAGIC4, MAGIC4_SIZE) == 0
        && memcmp(fileinfo->head + 8, MAGIC0, MAGIC0_SIZE) == 0)
        score = 100;

    return score;
}

#ifdef HAVE_GWYZIP
static gint
keyence6_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION6) ? 15 : 0;

    if (fileinfo->buffer_len <= KEYENCE6_HEADER_SIZE + BMP_HEADER_SIZE
        || memcmp(fileinfo->head, MAGIC6, MAGIC6_SIZE) != 0
        || memcmp(fileinfo->head + KEYENCE6_HEADER_SIZE, MAGICBMP, MAGICBMP_SIZE) != 0)
        return 0;

    return 100;
}
#endif

static GwyContainer*
keyence4_load(const gchar *filename,
              G_GNUC_UNUSED GwyRunType mode,
              GError **error)
{
    GwyContainer *data;
    GError *err = NULL;
    guchar *buffer;
    gsize size;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    data = keyence4_load_membuf(buffer, size, error);
    gwy_file_abandon_contents(buffer, size, NULL);
    return data;
}

static GwyContainer*
keyence4_load_membuf(const guchar *buffer,
                     gsize size,
                     GError **error)
{
    Keyence4File *kfile;
    GwyDataField *dfield;
    GwyContainer *data = NULL, *meta = NULL;
    const guchar *p;
    guint i, id;

    kfile = g_new0(Keyence4File, 1);
    kfile->buffer = buffer;
    kfile->size = size;
    p = kfile->buffer;

    if (!read_header(&p, &size, &kfile->header, error)
        || !read_offset_table(&p, &size, &kfile->offset_table, error)
        || !read_meas_conds(&p, &size, &kfile->meas_conds, error)
        || !read_assembly_info(kfile, error)
        || !read_data_images(kfile, error)
        || !read_color_images(kfile, error)
        || !read_line_meas(kfile, error)
        || !read_character_strs(kfile, error))
        goto fail;

    if (!kfile->nimages) {
        err_NO_DATA(error);
        goto fail;
    }

    data = gwy_container_new();
    meta = create_meta(kfile);
    id = 0;

    for (i = 0; i < G_N_ELEMENTS(kfile->height); i++) {
        if (kfile->height[i].data) {
            dfield = create_data_field(&kfile->height[i], &kfile->meas_conds, TRUE);
            add_data_field(data, &id, dfield, meta, "Height", i, NULL);
        }
    }

    for (i = 0; i < G_N_ELEMENTS(kfile->light); i++) {
        if (kfile->light[i].data) {
            dfield = create_data_field(&kfile->light[i], &kfile->meas_conds, FALSE);
            add_data_field(data, &id, dfield, meta, "Light", i, NULL);
        }
    }

    if (kfile->color_peak.data) {
        for (i = 0; i < 3; i++) {
            dfield = create_color_field(&kfile->color_peak, &kfile->meas_conds, i);
            add_data_field(data, &id, dfield, meta, peaknames[i], -1, gradientnames[i]);
        }
    }

    if (kfile->color_light.data) {
        for (i = 0; i < 3; i++) {
            dfield = create_color_field(&kfile->color_light, &kfile->meas_conds, i);
            add_data_field(data, &id, dfield, meta, lightnames[i], -1, gradientnames[i]);
        }
    }

    g_object_unref(meta);

fail:
    keyence4_free(kfile);
    return data;
}

static void
keyence4_free(Keyence4File *kfile)
{
    g_free(kfile->assembly_files);
    g_free(kfile->char_strs.title);
    g_free(kfile->char_strs.lens_name);
    g_free(kfile);
}

static gboolean
read_header(const guchar **p,
            gsize *size,
            Keyence4Header *header,
            GError **error)
{
    gwy_debug("remaining size 0x%08lx", (gulong)*size);
    if (*size < KEYENCE4_HEADER_SIZE) {
        err_TRUNCATED_PART(error, "Keyence4Header");
        return FALSE;
    }

    get_CHARARRAY(header->magic, p);
    get_CHARARRAY(header->dll_version, p);
    get_CHARARRAY(header->file_type, p);
    if (memcmp(header->magic, MAGIC4, MAGIC4_SIZE) != 0 || memcmp(header->file_type, MAGIC0, MAGIC0_SIZE) != 0) {
        err_FILE_TYPE(error, "Keyence VK4");
        return FALSE;
    }

    *size -= KEYENCE4_HEADER_SIZE;
    return TRUE;
}

static gboolean
read_offset_table(const guchar **p,
                  gsize *size,
                  Keyence4OffsetTable *offsettable,
                  GError **error)
{
    guint i;

    gwy_debug("remaining size 0x%08lx", (gulong)*size);
    if (*size < KEYENCE4_OFFSET_TABLE_SIZE) {
        err_TRUNCATED_PART(error, "Keyence4OffsetTable");
        return FALSE;
    }

    offsettable->setting = gwy_get_guint32_le(p);
    offsettable->color_peak = gwy_get_guint32_le(p);
    offsettable->color_light = gwy_get_guint32_le(p);
    for (i = 0; i < G_N_ELEMENTS(offsettable->light); i++)
        offsettable->light[i] = gwy_get_guint32_le(p);
    for (i = 0; i < G_N_ELEMENTS(offsettable->height); i++)
        offsettable->height[i] = gwy_get_guint32_le(p);
    offsettable->color_peak_thumbnail = gwy_get_guint32_le(p);
    offsettable->color_thumbnail = gwy_get_guint32_le(p);
    offsettable->light_thumbnail = gwy_get_guint32_le(p);
    offsettable->height_thumbnail = gwy_get_guint32_le(p);
    offsettable->assemble = gwy_get_guint32_le(p);
    offsettable->line_measure = gwy_get_guint32_le(p);
    offsettable->line_thickness = gwy_get_guint32_le(p);
    offsettable->string_data = gwy_get_guint32_le(p);
    offsettable->reserved = gwy_get_guint32_le(p);

    *size -= KEYENCE4_OFFSET_TABLE_SIZE;
    return TRUE;
}

static gboolean
read_meas_conds(const guchar **p,
                gsize *size,
                Keyence4MeasurementConditions *measconds,
                GError **error)
{
    guint i;

    gwy_debug("remaining size 0x%08lx", (gulong)*size);
    if (*size < KEYENCE4_MEASUREMENT_CONDITIONS_MIN_SIZE) {
        err_TRUNCATED_PART(error, "Keyence4MeasurementConditions");
        return FALSE;
    }

    measconds->size = gwy_get_guint32_le(p);
    if (*size < measconds->size) {
        err_TRUNCATED_PART(error, "Keyence4MeasurementConditions");
        return FALSE;
    }
    if (measconds->size < KEYENCE4_MEASUREMENT_CONDITIONS_MIN_SIZE) {
        err_INVALID(error, "MeasurementConditions::Size");
        return FALSE;
    }

    measconds->year = gwy_get_guint32_le(p);
    measconds->month = gwy_get_guint32_le(p);
    measconds->day = gwy_get_guint32_le(p);
    measconds->hour = gwy_get_guint32_le(p);
    measconds->minute = gwy_get_guint32_le(p);
    measconds->second = gwy_get_guint32_le(p);
    measconds->diff_utc_by_minutes = gwy_get_gint32_le(p);
    measconds->image_attributes = gwy_get_guint32_le(p);
    measconds->user_interface_mode = gwy_get_guint32_le(p);
    measconds->color_composite_mode = gwy_get_guint32_le(p);
    measconds->num_layer = gwy_get_guint32_le(p);
    measconds->run_mode = gwy_get_guint32_le(p);
    measconds->peak_mode = gwy_get_guint32_le(p);
    measconds->sharpening_level = gwy_get_guint32_le(p);
    measconds->speed = gwy_get_guint32_le(p);
    measconds->distance = gwy_get_guint32_le(p);
    measconds->pitch = gwy_get_guint32_le(p);
    measconds->optical_zoom = gwy_get_guint32_le(p);
    measconds->num_line = gwy_get_guint32_le(p);
    measconds->line0_pos = gwy_get_guint32_le(p);
    for (i = 0; i < G_N_ELEMENTS(measconds->reserved1); i++)
        measconds->reserved1[i] = gwy_get_guint32_le(p);
    measconds->lens_mag = gwy_get_guint32_le(p);
    measconds->pmt_gain_mode = gwy_get_guint32_le(p);
    measconds->pmt_gain = gwy_get_guint32_le(p);
    measconds->pmt_offset = gwy_get_guint32_le(p);
    measconds->nd_filter = gwy_get_guint32_le(p);
    measconds->reserved2 = gwy_get_guint32_le(p);
    measconds->persist_count = gwy_get_guint32_le(p);
    measconds->shutter_speed_mode = gwy_get_guint32_le(p);
    measconds->shutter_speed = gwy_get_guint32_le(p);
    measconds->white_balance_mode = gwy_get_guint32_le(p);
    measconds->white_balance_red = gwy_get_guint32_le(p);
    measconds->white_balance_blue = gwy_get_guint32_le(p);
    measconds->camera_gain = gwy_get_guint32_le(p);
    measconds->plane_compensation = gwy_get_guint32_le(p);
    measconds->xy_length_unit = gwy_get_guint32_le(p);
    measconds->z_length_unit = gwy_get_guint32_le(p);
    measconds->xy_decimal_place = gwy_get_guint32_le(p);
    measconds->z_decimal_place = gwy_get_guint32_le(p);
    measconds->x_length_per_pixel = gwy_get_guint32_le(p);
    measconds->y_length_per_pixel = gwy_get_guint32_le(p);
    measconds->z_length_per_digit = gwy_get_guint32_le(p);
    for (i = 0; i < G_N_ELEMENTS(measconds->reserved3); i++)
        measconds->reserved3[i] = gwy_get_guint32_le(p);
    measconds->light_filter_type = gwy_get_guint32_le(p);
    measconds->reserved4 = gwy_get_guint32_le(p);
    measconds->gamma_reverse = gwy_get_guint32_le(p);
    measconds->gamma = gwy_get_guint32_le(p);
    measconds->gamma_offset = gwy_get_guint32_le(p);
    measconds->ccd_bw_offset = gwy_get_guint32_le(p);
    measconds->numerical_aperture = gwy_get_guint32_le(p);
    measconds->head_type = gwy_get_guint32_le(p);
    measconds->pmt_gain2 = gwy_get_guint32_le(p);
    measconds->omit_color_image = gwy_get_guint32_le(p);
    measconds->lens_id = gwy_get_guint32_le(p);
    measconds->light_lut_mode = gwy_get_guint32_le(p);
    measconds->light_lut_in0 = gwy_get_guint32_le(p);
    measconds->light_lut_out0 = gwy_get_guint32_le(p);
    measconds->light_lut_in1 = gwy_get_guint32_le(p);
    measconds->light_lut_out1 = gwy_get_guint32_le(p);
    measconds->light_lut_in2 = gwy_get_guint32_le(p);
    measconds->light_lut_out2 = gwy_get_guint32_le(p);
    measconds->light_lut_in3 = gwy_get_guint32_le(p);
    measconds->light_lut_out3 = gwy_get_guint32_le(p);
    measconds->light_lut_in4 = gwy_get_guint32_le(p);
    measconds->light_lut_out4 = gwy_get_guint32_le(p);
    measconds->upper_position = gwy_get_guint32_le(p);
    measconds->lower_position = gwy_get_guint32_le(p);
    measconds->light_effective_bit_depth = gwy_get_guint32_le(p);
    measconds->height_effective_bit_depth = gwy_get_guint32_le(p);

    *size -= measconds->size;
    return TRUE;
}

static gboolean
read_assembly_info(Keyence4File *kfile,
                   GError **error)
{
    const guchar *p = kfile->buffer;
    gsize size = kfile->size;
    guint off = kfile->offset_table.assemble;
    guint remsize, nfiles, i, j;

    gwy_debug("0x%08x", off);
    if (!off)
        return TRUE;

    if (size <= KEYENCE4_ASSEMBLY_HEADERS_SIZE || off > size - KEYENCE4_ASSEMBLY_HEADERS_SIZE) {
        err_TRUNCATED_PART(error, "Keyence4AssemblyInformation");
        return FALSE;
    }

    p += off;

    kfile->assembly_info.size = gwy_get_guint32_le(&p);
    gwy_debug("assembly_info.size %u", kfile->assembly_info.size);
    kfile->assembly_info.file_type = gwy_get_guint16_le(&p);
    kfile->assembly_info.stage_type = gwy_get_guint16_le(&p);
    kfile->assembly_info.x_position = gwy_get_guint32_le(&p);
    kfile->assembly_info.y_position = gwy_get_guint32_le(&p);

    kfile->assembly_conds.auto_adjustment = *(p++);
    kfile->assembly_conds.source = *(p++);
    kfile->assembly_conds.thin_out = gwy_get_guint16_le(&p);
    kfile->assembly_conds.count_x = gwy_get_guint16_le(&p);
    kfile->assembly_conds.count_y = gwy_get_guint16_le(&p);
    gwy_debug("assembly counts %u, %u", kfile->assembly_conds.count_x, kfile->assembly_conds.count_y);

    nfiles = kfile->assembly_conds.count_x * kfile->assembly_conds.count_y;
    if (!nfiles)
        return TRUE;

    remsize = size - KEYENCE4_ASSEMBLY_HEADERS_SIZE - off;
    gwy_debug("remaining size %u", remsize);
    if (remsize/nfiles < KEYENCE4_ASSEMBLY_FILE_SIZE) {
        /* Apparently there can be large counts but no actual assembly data.
         * I do not understand but we to not use the infomation for anything
         * anyway. */
        kfile->assembly_conds.count_x = 0;
        kfile->assembly_conds.count_y = 0;
        kfile->assembly_nfiles = 0;
        return TRUE;
    }

    kfile->assembly_nfiles = nfiles;
    kfile->assembly_files = g_new(Keyence4AssemblyFile, nfiles);
    for (i = 0; i < nfiles; i++) {
        Keyence4AssemblyFile *kafile = kfile->assembly_files + i;

        for (j = 0; j < G_N_ELEMENTS(kafile->source_file); j++)
            kafile->source_file[j] = gwy_get_guint16_le(&p);
        kafile->pos_x = *(p++);
        kafile->pos_y = *(p++);
        kafile->datums_pos = *(p++);
        kafile->fix_distance = *(p++);
        kafile->distance_x = gwy_get_guint32_le(&p);
        kafile->distance_y = gwy_get_guint32_le(&p);
    }

    return TRUE;
}

static gboolean
read_data_image(Keyence4File *kfile,
                Keyence4FalseColorImage *image,
                guint offset,
                GError **error)
{
    const guchar *p = kfile->buffer;
    gsize size = kfile->size;
    guint bps;

    gwy_debug("0x%08x", offset);
    if (!offset)
        return TRUE;

    if (size <= KEYENCE4_FALSE_COLOR_IMAGE_MIN_SIZE || offset > size - KEYENCE4_FALSE_COLOR_IMAGE_MIN_SIZE) {
        err_TRUNCATED_PART(error, "Keyence4FalseColorImage");
        return FALSE;
    }

    p += offset;
    image->width = gwy_get_guint32_le(&p);
    if (err_DIMENSION(error, image->width))
        return FALSE;
    image->height = gwy_get_guint32_le(&p);
    if (err_DIMENSION(error, image->height))
        return FALSE;

    image->bit_depth = gwy_get_guint32_le(&p);
    if (image->bit_depth != 8 && image->bit_depth != 16 && image->bit_depth != 32) {
        err_BPP(error, image->bit_depth);
        return FALSE;
    }
    bps = image->bit_depth/8;

    image->compression = gwy_get_guint32_le(&p);
    image->byte_size = gwy_get_guint32_le(&p);
    if (err_SIZE_MISMATCH(error, image->width*image->height*bps, image->byte_size, TRUE))
        return FALSE;

    image->palette_range_min = gwy_get_guint32_le(&p);
    image->palette_range_max = gwy_get_guint32_le(&p);
    memcpy(image->palette, p, sizeof(image->palette));
    p += sizeof(image->palette);

    if (size - offset - KEYENCE4_FALSE_COLOR_IMAGE_MIN_SIZE < image->byte_size) {
        err_TRUNCATED_PART(error, "Keyence4FalseColorImage");
        return FALSE;
    }
    image->data = p;
    kfile->nimages++;

    return TRUE;
}

static gboolean
read_data_images(Keyence4File *kfile,
                 GError **error)
{
    const Keyence4OffsetTable *offtable = &kfile->offset_table;
    guint i;

    for (i = 0; i < G_N_ELEMENTS(kfile->light); i++) {
        if (!read_data_image(kfile, &kfile->light[i], offtable->light[i], error))
            return FALSE;
    }
    for (i = 0; i < G_N_ELEMENTS(kfile->height); i++) {
        if (!read_data_image(kfile, &kfile->height[i], offtable->height[i], error))
            return FALSE;
    }
    return TRUE;
}

static gboolean
read_color_image(Keyence4File *kfile,
                 Keyence4TrueColorImage *image,
                 guint offset,
                 GError **error)
{
    const guchar *p = kfile->buffer;
    gsize size = kfile->size;
    guint bps;

    gwy_debug("0x%08x", offset);
    if (!offset)
        return TRUE;

    if (size <= KEYENCE4_TRUE_COLOR_IMAGE_MIN_SIZE || offset > size - KEYENCE4_TRUE_COLOR_IMAGE_MIN_SIZE) {
        err_TRUNCATED_PART(error, "Keyence4TrueColorImage");
        return FALSE;
    }

    p += offset;
    image->width = gwy_get_guint32_le(&p);
    if (err_DIMENSION(error, image->width))
        return FALSE;
    image->height = gwy_get_guint32_le(&p);
    if (err_DIMENSION(error, image->height))
        return FALSE;

    image->bit_depth = gwy_get_guint32_le(&p);
    if (image->bit_depth != 24) {
        err_BPP(error, image->bit_depth);
        return FALSE;
    }
    bps = image->bit_depth/8;

    image->compression = gwy_get_guint32_le(&p);
    image->byte_size = gwy_get_guint32_le(&p);
    if (err_SIZE_MISMATCH(error, image->width*image->height*bps, image->byte_size, TRUE))
        return FALSE;

    if (size - offset - KEYENCE4_TRUE_COLOR_IMAGE_MIN_SIZE < image->byte_size) {
        err_TRUNCATED_PART(error, "Keyence4TrueColorImage");
        return FALSE;
    }
    image->data = p;

    return TRUE;
}

static gboolean
read_line_meas(Keyence4File *kfile,
               GError **error)
{
    Keyence4LineMeasurement *linemeas;
    const guchar *p = kfile->buffer;
    gsize size = kfile->size;
    guint off = kfile->offset_table.line_measure;
    guint i;

    gwy_debug("0x%08x", off);
    if (!off)
        return TRUE;

    if (size <= KEYENCE4_LINE_MEASUREMENT_SIZE || off > size - KEYENCE4_LINE_MEASUREMENT_SIZE) {
        err_TRUNCATED_PART(error, "Keyence4LineMeasurement");
        return FALSE;
    }

    p += off;
    linemeas = &kfile->line_measure;

    linemeas->size = gwy_get_guint32_le(&p);
    if (size < KEYENCE4_LINE_MEASUREMENT_SIZE) {
        err_TRUNCATED_PART(error, "Keyence4LineMeasurement");
        return FALSE;
    }
    linemeas->line_width = gwy_get_guint32_le(&p);
    /* XXX: We should use the real length even though the format description
     * seems to specify a fixed length.  Also note that only the first data
     * block is supposed to be used; the rest it reserved. */
    for (i = 0; i < G_N_ELEMENTS(linemeas->light); i++) {
        linemeas->light[i] = p;
        p += KEYENCE4_LINE_MEASUREMENT_LEN*sizeof(guint16);
    }
    for (i = 0; i < G_N_ELEMENTS(linemeas->height); i++) {
        linemeas->height[i] = p;
        p += KEYENCE4_LINE_MEASUREMENT_LEN*sizeof(guint32);
    }

    return TRUE;
}

static gboolean
read_color_images(Keyence4File *kfile,
                  GError **error)
{
    const Keyence4OffsetTable *offtable = &kfile->offset_table;

    if (!read_color_image(kfile, &kfile->color_peak, offtable->color_peak, error))
        return FALSE;
    if (!read_color_image(kfile, &kfile->color_light, offtable->color_light, error))
        return FALSE;

    return TRUE;
}

static gchar*
read_character_str(const guchar **p,
                   gsize *remsize,
                   GError **error)
{
    gchar *s;
    guint len;

    if (*remsize < sizeof(guint32)) {
        err_TRUNCATED_PART(error, "string");
        return NULL;
    }

    len = gwy_get_guint32_le(p);
    gwy_debug("%u", len);
    *remsize -= sizeof(guint32);

    if (!len)
        return g_strdup("");

    if (*remsize/2 < len) {
        err_TRUNCATED_PART(error, "string");
        return NULL;
    }

    s = gwy_utf16_to_utf8((const gunichar2*)*p, len, GWY_BYTE_ORDER_LITTLE_ENDIAN);
    if (!s) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Cannot convert string from UTF-16."));
        return FALSE;
    }
    gwy_debug("%s", s);

    *remsize -= 2*len;
    *p += 2*len;
    return s;
}

static gboolean
read_character_strs(Keyence4File *kfile,
                    GError **error)
{
    Keyence4CharacterStrings *charstrs;
    const guchar *p = kfile->buffer;
    gsize remsize = kfile->size;
    guint off = kfile->offset_table.string_data;

    gwy_debug("0x%08x", off);
    if (!off)
        return TRUE;

    if (remsize < off) {
        err_TRUNCATED_PART(error, "strings");
        return FALSE;
    }

    p += off;
    remsize -= off;
    charstrs = &kfile->char_strs;
    if (!(charstrs->title = read_character_str(&p, &remsize, error))
        || !(charstrs->lens_name = read_character_str(&p, &remsize, error)))
        return FALSE;

    return TRUE;
}

static GwyDataField*
create_data_field(const Keyence4FalseColorImage *image,
                  const Keyence4MeasurementConditions *measconds,
                  gboolean is_height)
{
    guint w = image->width, h = image->height;
    gdouble dx = measconds->x_length_per_pixel * Picometre;
    gdouble dy = measconds->y_length_per_pixel * Picometre;
    GwyRawDataType datatype = GWY_RAW_DATA_UINT8;
    GwyDataField *dfield;
    gdouble *data;
    gdouble q;

    /* The -1 is from comparison with original software. */
    dfield = gwy_data_field_new(w, h, dx*(w - 1.0), dy*(h - 1.0), FALSE);
    if (image->bit_depth == 16)
        datatype = GWY_RAW_DATA_UINT16;
    else if (image->bit_depth == 32)
        datatype = GWY_RAW_DATA_UINT32;

    q = (is_height
         ? measconds->z_length_per_digit * Picometre
         : gwy_powi(0.5, image->bit_depth));

    data = gwy_data_field_get_data(dfield);
    gwy_convert_raw_data(image->data, w*h, 1, datatype, GWY_BYTE_ORDER_LITTLE_ENDIAN, data, q, 0.0);

    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
    if (is_height)
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "m");

    return dfield;
}

static GwyDataField*
create_color_field(const Keyence4TrueColorImage *image,
                   const Keyence4MeasurementConditions *measconds,
                   gint channelid)
{
    guint w = image->width, h = image->height;
    gdouble dx = measconds->x_length_per_pixel * Picometre;
    gdouble dy = measconds->y_length_per_pixel * Picometre;
    GwyDataField *dfield;
    gdouble *data;

    /* The -1 is from comparison with original software. */
    dfield = gwy_data_field_new(w, h, dx*(w - 1.0), dy*(h - 1.0), FALSE);
    data = gwy_data_field_get_data(dfield);
    gwy_convert_raw_data(image->data + channelid, w*h, 3, GWY_RAW_DATA_UINT8, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         data, 1.0/255.0, 0.0);

    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
    return dfield;
}

#define store_int(c,n,i) \
    g_snprintf(buf, sizeof(buf), "%d", (i)); \
    gwy_container_set_const_string_by_name((c), (n), buf);

#define store_uint(c,n,i) \
    g_snprintf(buf, sizeof(buf), "%u", (i)); \
    gwy_container_set_const_string_by_name((c), (n), buf);

#define store_int2(c,n,i,u) \
    g_snprintf(buf, sizeof(buf), "%d %s", (i), (u)); \
    gwy_container_set_const_string_by_name((c), (n), buf);

#define store_uint2(c,n,i,u) \
    g_snprintf(buf, sizeof(buf), "%u %s", (i), (u)); \
    gwy_container_set_const_string_by_name((c), (n), buf);

#define store_float(c,n,v) \
    g_snprintf(buf, sizeof(buf), "%g", (v)); \
    gwy_container_set_const_string_by_name((c), (n), buf);

static GwyContainer*
create_meta(const Keyence4File *kfile)
{
    const Keyence4MeasurementConditions *measconds = &kfile->meas_conds;
    const Keyence4CharacterStrings *charstrs = &kfile->char_strs;
    GwyContainer *meta = gwy_container_new();
    gchar buf[48];

    g_snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
               kfile->header.dll_version[3], kfile->header.dll_version[2],
               kfile->header.dll_version[1], kfile->header.dll_version[0]);
    gwy_container_set_const_string_by_name(meta, "DLL version", buf);

    g_snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
               measconds->year, measconds->month, measconds->day,
               measconds->hour, measconds->minute, measconds->second);
    gwy_container_set_const_string_by_name(meta, "Date", buf);

    store_int2(meta, "Time difference to UTC", measconds->diff_utc_by_minutes, "min");
    store_uint(meta, "Image attributes", measconds->image_attributes);
    store_uint(meta, "User interface mode", measconds->user_interface_mode);
    store_uint(meta, "Color composition mode", measconds->color_composite_mode);
    store_uint(meta, "Image layer number", measconds->num_layer);
    store_uint(meta, "Run mode", measconds->run_mode);
    store_uint(meta, "Peak mode", measconds->peak_mode);
    store_uint(meta, "Sharpening level", measconds->sharpening_level);
    store_uint(meta, "Speed", measconds->speed);
    store_uint2(meta, "Distance", measconds->distance, "nm");
    store_uint2(meta, "Pitch", measconds->pitch, "nm");
    store_float(meta, "Optical zoom", measconds->optical_zoom/10.0);
    store_uint(meta, "Number of lines", measconds->num_line);
    store_uint(meta, "First line position", measconds->line0_pos);
    store_float(meta, "Lens magnification", measconds->lens_mag/10.0);
    store_uint(meta, "PMT gain mode", measconds->pmt_gain_mode);
    store_uint(meta, "PMT gain", measconds->pmt_gain);
    store_uint(meta, "PMT offset", measconds->pmt_offset);
    store_uint(meta, "ND filter", measconds->nd_filter);
    store_uint(meta, "Image average frequency", measconds->persist_count);
    store_uint(meta, "Shutter speed mode", measconds->shutter_speed_mode);
    store_uint(meta, "Shutter speed", measconds->shutter_speed);
    store_uint(meta, "White balance mode", measconds->white_balance_mode);
    store_uint(meta, "White balance red", measconds->white_balance_red);
    store_uint(meta, "White balance blue", measconds->white_balance_blue);
    store_uint2(meta, "Camera gain", 6*measconds->camera_gain, "dB");
    store_uint(meta, "Plane compensation", measconds->plane_compensation);
    store_uint(meta, "Light filter type", measconds->light_filter_type);
    store_uint(meta, "Gamma reverse", measconds->gamma_reverse);
    store_float(meta, "Gamma", measconds->gamma/100.0);
    store_float(meta, "Gamma correction offset", measconds->gamma_offset/65536.0);
    store_float(meta, "CCD BW offset", measconds->ccd_bw_offset/100.0);
    store_float(meta, "Numerical aperture", measconds->numerical_aperture/1000.0);
    store_uint(meta, "Head type", measconds->head_type);
    store_uint(meta, "PMT gain 2", measconds->pmt_gain2);
    store_uint(meta, "Omit color image", measconds->omit_color_image);
    store_uint(meta, "Lens ID", measconds->lens_id);
    store_uint(meta, "Light LUT mode", measconds->light_lut_mode);
    store_uint(meta, "Light LUT input 0", measconds->light_lut_in0);
    store_uint(meta, "Light LUT output 0", measconds->light_lut_out0);
    store_uint(meta, "Light LUT input 1", measconds->light_lut_in1);
    store_uint(meta, "Light LUT output 1", measconds->light_lut_out1);
    store_uint(meta, "Light LUT input 2", measconds->light_lut_in2);
    store_uint(meta, "Light LUT output 2", measconds->light_lut_out2);
    store_uint(meta, "Light LUT input 3", measconds->light_lut_in3);
    store_uint(meta, "Light LUT output 3", measconds->light_lut_out3);
    store_uint(meta, "Light LUT input 4", measconds->light_lut_in4);
    store_uint(meta, "Light LUT output 4", measconds->light_lut_out4);
    store_uint2(meta, "Upper position", measconds->upper_position, "nm");
    store_uint2(meta, "Lower position", measconds->lower_position, "nm");
    store_uint(meta, "Light effective bit depth", measconds->light_effective_bit_depth);
    store_uint(meta, "Height effective bit depth", measconds->height_effective_bit_depth);

    if (charstrs->title && strlen(charstrs->title))
        gwy_container_set_const_string_by_name(meta, "Title", charstrs->title);
    if (charstrs->lens_name && strlen(charstrs->lens_name))
        gwy_container_set_const_string_by_name(meta, "Lens name", charstrs->lens_name);

    return meta;
}

static void
add_data_field(GwyContainer *data, gint *id,
               GwyDataField *dfield, GwyContainer *meta,
               const gchar *title, gint i, const gchar *gradient)
{
    GwyContainer *tmpmeta;
    GQuark quark;
    gchar key[48];

    quark = gwy_app_get_data_key_for_id(*id);
    gwy_container_set_object(data, quark, dfield);
    g_object_unref(dfield);

    g_snprintf(key, sizeof(key), "/%u/data/title", *id);
    if (i >= 0) {
        gchar *t = g_strdup_printf("%s %u", title, i);
        gwy_container_set_string_by_name(data, key, t);
    }
    else
        gwy_container_set_const_string_by_name(data, key, title);

    if (meta) {
        g_snprintf(key, sizeof(key), "/%u/meta", *id);
        tmpmeta = gwy_container_duplicate(meta);
        gwy_container_set_object_by_name(data, key, tmpmeta);
        g_object_unref(tmpmeta);
    }

    if (gradient) {
        g_snprintf(key, sizeof(key), "/%u/base/palette", *id);
        gwy_container_set_const_string_by_name(data, key, gradient);
    }

    (*id)++;
}

#ifdef HAVE_GWYZIP
static GwyContainer*
keyence6_load(const gchar *filename,
              G_GNUC_UNUSED GwyRunType mode,
              GError **error)
{
    GwyContainer *data = NULL, *meta = NULL;
    GError *err = NULL;
    gchar *zipfilename = NULL;
    guchar *buffer = NULL, *vk4buf = NULL;
    const guchar *p;
    guint vk6skip, bmpsize;
    gsize size, zipsize, vk4size;
    GwyZipFile zipfile = NULL;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    if (size <= KEYENCE6_HEADER_SIZE + BMP_HEADER_SIZE
        || memcmp(buffer, MAGIC6, MAGIC6_SIZE) != 0
        || memcmp(buffer + KEYENCE6_HEADER_SIZE, MAGICBMP, MAGICBMP_SIZE) != 0) {
        err_FILE_TYPE(error, "Keyence VK6");
        goto fail;
    }

    /* Check if the VK6 miniheader agrees with the BMP header on the BMP file size.  This is a strong indication we
     * are dealing with a VK6 file. */
    p = buffer + MAGIC6_SIZE;
    vk6skip = gwy_get_guint32_le(&p);
    p = buffer + KEYENCE6_HEADER_SIZE + MAGICBMP_SIZE;
    bmpsize = gwy_get_guint32_le(&p);
    gwy_debug("VK6 skip %u, BMP size %u", vk6skip, bmpsize);
    if (vk6skip != bmpsize) {
        err_FILE_TYPE(error, "Keyence VK6");
        goto fail;
    }
    if (size - KEYENCE6_HEADER_SIZE <= bmpsize) {
        err_TRUNCATED_PART(error, "BMP");
        goto fail;
    }

    /* Something seems to follow the BMP preview.  Just try reading it as a ZIP file and see where it gets us. */
    zipsize = size - KEYENCE6_HEADER_SIZE - bmpsize;
    gwy_debug("remaining size for the ZIP %lu", (gulong)zipsize);
    p = buffer + KEYENCE6_HEADER_SIZE + bmpsize;

    /* There is a VK4 file inside, called Vk4File.  That's what we want to read, really. */
    if (!(zipfile = make_temporary_zip_file(p, zipsize, "gwyddion-keyence6-XXXXXX.zip", &zipfilename, error))
        || !(gwyzip_locate_file(zipfile, "Vk4File", 0, error))
        || !(vk4buf = gwyzip_get_file_content(zipfile, &vk4size, error)))
        goto fail;

    if (!(data = keyence4_load_membuf(vk4buf, vk4size, error)))
        goto fail;

    add_vk6_hdr_images(data, zipfile);
    if ((meta = read_vk6_measure_condition(zipfile)))
        distribute_meta6(data, meta);

fail:
    GWY_OBJECT_UNREF(meta);
    g_free(vk4buf);
    if (zipfile)
        gwyzip_close(zipfile);
    if (zipfilename) {
        g_unlink(zipfilename);
        g_free(zipfilename);
    }
    gwy_file_abandon_contents(buffer, size, NULL);

    return data;
}

static void
add_vk6_hdr_images(GwyContainer *data, GwyZipFile zipfile)
{
    GwyZipFile hdrzipfile;
    GwyDataField *rgbfield[3] = { NULL, NULL, NULL }, *errfield = NULL, *field = NULL, *mask;
    GwyContainer *meta = NULL;
    guchar *buffer;
    gchar *hdrfilename;
    gsize size;
    gint *ids;
    guint i;
    gint id;

    if (!gwyzip_locate_file(zipfile, "Vk6ImageData", 0, NULL)
        || !(buffer = gwyzip_get_file_content(zipfile, &size, NULL)))
        return;

    gwy_debug("found Vk6ImageData");
    if (!(hdrzipfile = make_temporary_zip_file(buffer, size, "gwyddion-keyence6hdr-XXXXXX.zip", &hdrfilename, NULL))) {
        g_free(buffer);
        return;
    }
    g_free(buffer);

    if (gwyzip_locate_file(hdrzipfile, "HdrImageData", 0, NULL)
        && (buffer = gwyzip_get_file_content(hdrzipfile, &size, NULL))) {
        gwy_debug("reading HdrImageData");
        read_vk6_hdr_images(buffer, size, rgbfield, G_N_ELEMENTS(rgbfield));
        g_free(buffer);
    }

    if (gwyzip_locate_file(hdrzipfile, "ErrorImageData", 0, NULL)
        && (buffer = gwyzip_get_file_content(hdrzipfile, &size, NULL))) {
        gwy_debug("reading ErrorImageData");
        read_vk6_hdr_images(buffer, size, &errfield, 1);
        g_free(buffer);

        /* Don't create masks if the error field is empty. */
        if (gwy_data_field_get_max(errfield) <= 0.0)
            GWY_OBJECT_UNREF(errfield);
    }

    ids = gwy_app_data_browser_get_data_ids(data);
    id = -1;
    for (i = 0; ids[i] >= 0; i++) {
        id = MAX(id, ids[i]);
        if (!field)
            field = gwy_container_get_object(data, gwy_app_get_data_key_for_id(ids[i]));
        if (!meta)
            gwy_container_gis_object(data, gwy_app_get_data_meta_key_for_id(ids[i]), (GObject**)&meta);
    }
    id++;
    g_free(ids);

    for (i = 0; i < G_N_ELEMENTS(rgbfield); i++) {
        if (!rgbfield[i])
            continue;
        if (field) {
            gwy_data_field_copy_units(field, rgbfield[i]);
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(rgbfield[i]), NULL);
            gwy_data_field_set_xreal(rgbfield[i], gwy_data_field_get_xreal(field));
            gwy_data_field_set_yreal(rgbfield[i], gwy_data_field_get_xreal(field));
        }
        /* This unrefs rgbfield[i]. */
        add_data_field(data, &id, rgbfield[i], meta, hdrnames[i], -1, gradientnames[i]);
    }

    ids = gwy_app_data_browser_get_data_ids(data);
    for (i = 0; ids[i] >= 0; i++) {
        if (errfield && !gwy_data_field_check_compatibility(field, errfield, GWY_DATA_COMPATIBILITY_RES)) {
            mask = gwy_data_field_new_alike(field, FALSE);
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mask), NULL);
            gwy_data_field_copy(errfield, mask, FALSE);
            /* FIXME: Should we apply Laplace interpolation here, as usual? */
            gwy_container_set_object(data, gwy_app_get_mask_key_for_id(ids[i]), mask);
            g_object_unref(mask);
        }
    }
    g_free(ids);

    GWY_OBJECT_UNREF(errfield);

    gwyzip_close(hdrzipfile);
    g_unlink(hdrfilename);
    g_free(hdrfilename);
}

static gboolean
read_vk6_hdr_images(const guchar *buffer, gsize size,
                    GwyDataField **fields, guint nf)
{
    guint xres, yres, bpr, bps, rowstride, i;
    GwyRawDataType rawtype;
    const guchar *p = buffer;

    if (size <= HDR_IMAGE_HEADER_SIZE)
        return FALSE;

    xres = gwy_get_guint32_le(&p);
    yres = gwy_get_guint32_le(&p);
    bpr = gwy_get_guint32_le(&p);
    rowstride = gwy_get_guint32_le(&p);
    gwy_debug("xres %u, yres %u, bytes per record %u, rowstride %u", xres, yres, bpr, rowstride);
    if (rowstride/bpr < xres) {
        gwy_debug("too small rowstride for row data");
        return FALSE;
    }
    if ((size - HDR_IMAGE_HEADER_SIZE)/rowstride < yres) {
        gwy_debug("too small file size for image data");
        return FALSE;
    }
    if (bpr % nf) {
        gwy_debug("bytes per record is not a multiple of expected number of fields");
        return FALSE;
    }
    bps = bpr/nf;
    if (bps == 1) {
        rawtype = GWY_RAW_DATA_UINT8;
        gwy_debug("assuming sample format uint8");
    }
    else if (bps == 4) {
        rawtype = GWY_RAW_DATA_FLOAT;
        gwy_debug("assuming sample format single");
    }
    else {
        gwy_debug("don't know what to do with bps of %u", bps);
        return FALSE;
    }

    for (i = 0; i < nf; i++) {
        fields[i] = gwy_data_field_new(xres, yres, xres, yres, FALSE);
        gwy_convert_raw_data(p + i*bps, xres*yres, nf, rawtype, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                             gwy_data_field_get_data(fields[i]), 1.0, 0.0);
    }
    gwy_debug("%u images read OK", nf);
    return TRUE;
}

static void
distribute_meta6(GwyContainer *data, GwyContainer *addmeta)
{
    gint *ids = gwy_app_data_browser_get_data_ids(data);
    gint i;

    for (i = 0; ids[i] >= 0; i++) {
        GQuark quark = gwy_app_get_data_meta_key_for_id(ids[i]);
        GwyContainer *meta;

        if (gwy_container_gis_object(data, quark, &meta))
            gwy_container_transfer(addmeta, meta, "", "", FALSE);
        else {
            meta = gwy_container_duplicate(addmeta);
            gwy_container_set_object(data, quark, meta);
            g_object_unref(meta);
        }
    }
}

static GwyContainer*
read_vk6_measure_condition(GwyZipFile zipfile)
{
    GwyZipFile mczipfile;
    GwyContainer *meta = NULL;
    guchar *buffer;
    gchar *mcfilename;
    gsize size;

    if (!gwyzip_locate_file(zipfile, "VK6MeasureCondition", 0, NULL)
        || !(buffer = gwyzip_get_file_content(zipfile, &size, NULL)))
        return NULL;

    gwy_debug("found VK6MeasureCondition");
    if (!(mczipfile = make_temporary_zip_file(buffer, size, "gwyddion-keyence6mc-XXXXXX.zip", &mcfilename, NULL))) {
        g_free(buffer);
        return NULL;
    }
    g_free(buffer);

    if (gwyzip_locate_file(mczipfile, "FocusCompositionCondition", 0, NULL)
        && (buffer = gwyzip_get_file_content(mczipfile, &size, NULL))) {
        gwy_debug("parsing FocusCompositionCondition");
        meta = parse_xml_metadata(buffer, size);
        g_free(buffer);
    }

    gwyzip_close(mczipfile);
    g_unlink(mcfilename);
    g_free(mcfilename);

    return meta;
}

static void
keyence6_start_element(G_GNUC_UNUSED GMarkupParseContext *context,
                       const gchar *element_name,
                       G_GNUC_UNUSED const gchar **attribute_names,
                       G_GNUC_UNUSED const gchar **attribute_values,
                       gpointer user_data,
                       G_GNUC_UNUSED GError **error)
{
    Keyence6Meta *vk6meta = (Keyence6Meta*)user_data;
    const gchar *colon;

    if ((colon = strchr(element_name, ':')))
        element_name = colon+1;

    g_string_assign(vk6meta->curr_element, element_name);
    vk6meta->depth++;
}

static void
keyence6_end_element(G_GNUC_UNUSED GMarkupParseContext *context,
                     const gchar *element_name,
                     gpointer user_data,
                     G_GNUC_UNUSED GError **error)
{
    Keyence6Meta *vk6meta = (Keyence6Meta*)user_data;
    const gchar *sep, *colon;
    GArray *compdepths = vk6meta->compdepths;
    GString *path = vk6meta->path;
    guint i;

    if ((colon = strchr(element_name, ':')))
        element_name = colon+1;

    vk6meta->depth--;
    if (gwy_strequal(element_name, "KeyValueOfstringanyType")) {
        for (i = 0; i < compdepths->len; i++) {
            if (g_array_index(compdepths, gint, i) >= vk6meta->depth)
                break;
        }
        i = compdepths->len - i;
        g_array_set_size(compdepths, compdepths->len - i);
        while (i--) {
            if ((sep = g_strrstr(path->str, "::")))
                g_string_truncate(path, sep - path->str);
            else
                g_string_truncate(path, 0);
        }
    }
}

static gboolean
string_is_uuid(const gchar *s, guint len)
{
    guint i;

    if (len != 36)
        return FALSE;

    for (i = 0; i < 36; i++) {
        if ((i == 8 || i == 13 || i == 18 || i == 23)) {
            if (s[i] != '-')
                return FALSE;
        }
        else if (!g_ascii_isxdigit(s[i]))
            return FALSE;
    }

    return TRUE;
}

static void
keyence6_text(G_GNUC_UNUSED GMarkupParseContext *context,
              const gchar *text,
              gsize text_len,
              gpointer user_data,
              G_GNUC_UNUSED GError **error)
{
    Keyence6Meta *vk6meta = (Keyence6Meta*)user_data;
    GString *path = vk6meta->path;

    if (!text_len)
        return;

    if (gwy_strequal(vk6meta->curr_element->str, "Key")) {
        while (*text == '_')
            text++;
        if (g_str_has_suffix(text, "_HasValue"))
            return;

        if (path->len)
            g_string_append(path, "::");
        g_string_append(path, text);
        if (g_str_has_suffix(path->str, "_Value"))
            g_string_truncate(path, path->len - strlen("_Value"));
        if (g_str_has_suffix(path->str, "Parameter"))
            g_string_truncate(path, path->len - strlen("Parameter"));
        g_array_append_val(vk6meta->compdepths, vk6meta->depth);
    }
    else if (gwy_strequal(vk6meta->curr_element->str, "Value") && !string_is_uuid(text, text_len)) {
        gwy_debug("%s <%s>", path->str, text);
        if (gwy_container_contains_by_name(vk6meta->meta, path->str)) {
            gchar *s = g_strconcat(gwy_container_get_string_by_name(vk6meta->meta, path->str), ", ", text, NULL);
            gwy_container_set_string_by_name(vk6meta->meta, path->str, s);
        }
        else
            gwy_container_set_const_string_by_name(vk6meta->meta, path->str, text);
    }
}

static GwyContainer*
parse_xml_metadata(const gchar *buffer, gsize size)
{
    GMarkupParser parser = {
        &keyence6_start_element,
        &keyence6_end_element,
        &keyence6_text,
        NULL,
        NULL,
    };
    GMarkupParseContext *context = NULL;
    Keyence6Meta vk6meta;

    vk6meta.meta = gwy_container_new();
    vk6meta.path = g_string_new(NULL);
    vk6meta.curr_element = g_string_new(NULL);
    vk6meta.compdepths = g_array_new(FALSE, FALSE, sizeof(gint));
    vk6meta.depth = 0;
    context = g_markup_parse_context_new(&parser, 0, &vk6meta, NULL);
    if (g_markup_parse_context_parse(context, buffer, size, NULL))
        g_markup_parse_context_end_parse(context, NULL);

    if (context)
        g_markup_parse_context_free(context);
    g_string_free(vk6meta.path, TRUE);
    g_string_free(vk6meta.curr_element, TRUE);
    g_array_free(vk6meta.compdepths, TRUE);
    if (!gwy_container_get_n_items(vk6meta.meta))
        GWY_OBJECT_UNREF(vk6meta.meta);

    return vk6meta.meta;
}

static GwyZipFile
make_temporary_zip_file(const guchar *buffer, gsize size,
                        const gchar *nametemplate, gchar **actualname, GError **error)
{
    GwyZipFile zipfile;
    GError *err = NULL;
    gssize bytes_written;
    gint fd;

    fd = g_file_open_tmp(nametemplate, actualname, &err);
    if (fd == -1) {
        err_OPEN_WRITE_GERROR(error, &err);
        return NULL;
    }
    gwy_debug("temporary ZIP file <%s>", *actualname);

    while (size) {
        bytes_written = write(fd, buffer, size);
        if (bytes_written <= 0) {
            /* We might want to try again when we get zero written bytes or an error such as EAGAIN, EWOULDBLOCK or
             * EINTR.  But in this context, screw it. */
            err_WRITE(error);
            close(fd);
            goto fail;
        }
        buffer += bytes_written;
        size -= bytes_written;
    }

    close(fd);
    if ((zipfile = gwyzip_open(*actualname, error))) {
#ifdef DEBUG
        gwyzip_first_file(zipfile, NULL);
        do {
            gchar *filename;
            gwyzip_get_current_filename(zipfile, &filename, NULL);
            gwy_debug("found file: <%s>", filename);
            g_free(filename);
        } while (gwyzip_next_file(zipfile, NULL));
#endif
        return zipfile;
    }

fail:
    g_unlink(*actualname);
    g_free(*actualname);
    *actualname = NULL;
    return NULL;
}
#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
