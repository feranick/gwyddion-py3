/*
 *  $Id: zeisslsm.c 22565 2019-10-11 09:49:53Z yeti-dn $
 *  Copyright (C) 2017 David Necas (Yeti), Daniil Bratashov (dn2010).
 *  E-mail: dn2010@gwyddion.net.
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

 /*
  * It is easier to re-implement parts of (not so) TIFF loading here
  * than to add kludges into GwyTIFF.
  *
  * LZW Compression is unimplemented now.
  *
  * It is based on LSMfile description from:
  * http://ibb.gsf.de/homepage/karsten.rodenacker/IDL/Lsmfile.doc
  * Please note that it has incorrect TIF_CZ_LSMINFO tag layout,
  * 3 elements of type gdouble with X, Y and Z offsets are skipped
  * there.
  *
  * Also BioImage XD source code was used as more modern reference
  * about format features.
  */

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-zeiss-lsm-spm">
 *   <comment>Carl Zeiss CLSM images</comment>
 *   <glob pattern="*.lsm"/>
 *   <glob pattern="*.LSM"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Carl Zeiss CLSM images
 * .lsm
 * Read Volume
 **/

#include "config.h"
#include <stdlib.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>
#include "err.h"
#include "gwytiff.h"

#define EXTENSION ".lsm"

enum {
    ZEISS_LSM_HEADER_TAG = 34412,
};

typedef enum {
    LSM_TIFF_SUB_FILE_TYPE_IMAGE     = 0,
    LSM_TIFF_SUB_FILE_TYPE_THUMBNAIL = 1,
} LSMTIFFSubFileType;

typedef enum {
    LSM_SCANTYPE_XYZ                 = 0,
    LSM_SCANTYPE_XZ                  = 1,
    LSM_SCANTYPE_LINE                = 2,
    LSM_SCANTYPE_TIMESERIES_XY       = 3,
    LSM_SCANTYPE_TIMESERIES_XZ       = 4,
    LSM_SCANTYPE_TIMESERIES_MEAN_ROI = 5,
    LSM_SCANTYPE_TIMESERIES_XYZ      = 6,
    LSM_SCANTYPE_SPLINE              = 7,
    LSM_SCANTYPE_SPLINE_XZ           = 8,
    LSM_SCANTYPE_TIMESERIES_SPLINE   = 9,
    LSM_SCANTYPE_TIMESERIES_POINT    = 10,
} LSMHeaderScanType;

typedef enum {
    LSM_LUT_NORMAL   = 0,
    LSM_LUT_ORIGINAL = 1,
    LSM_LUT_RAMP     = 2,
    LSM_LUT_POLYLINE = 3,
    LSM_LUT_SPLINE   = 4,
    LSM_LUT_GAMMA    = 5,
} LSMLUTType;

typedef enum {
    LSM_SUBBLOCK_RECORDING              = 0x10000000,
    LSM_SUBBLOCK_LASERS                 = 0x30000000,
    LSM_SUBBLOCK_LASER                  = 0x50000000,
    LSM_SUBBLOCK_TRACKS                 = 0x20000000,
    LSM_SUBBLOCK_TRACK                  = 0x40000000,
    LSM_SUBBLOCK_DETECTION_CHANNELS     = 0x60000000,
    LSM_SUBBLOCK_DETECTION_CHANNEL      = 0x70000000,
    LSM_SUBBLOCK_ILLUMINATION_CHANNELS  = 0x80000000,
    LSM_SUBBLOCK_ILLUMINATION_CHANNEL   = 0x90000000,
    LSM_SUBBLOCK_BEAM_SPLITTERS         = 0xA0000000,
    LSM_SUBBLOCK_BEAM_SPLITTER          = 0xB0000000,
    LSM_SUBBLOCK_DATA_CHANNELS          = 0xC0000000,
    LSM_SUBBLOCK_DATA_CHANNEL           = 0xD0000000,
    LSM_SUBBLOCK_TIMERS                 = 0x11000000,
    LSM_SUBBLOCK_TIMER                  = 0x12000000,
    LSM_SUBBLOCK_MARKERS                = 0x13000000,
    LSM_SUBBLOCK_MARKER                 = 0x14000000,
    LSM_SUBBLOCK_END                    = 0xFFFFFFFF,
} LSMScanInfoEntry;

typedef enum {
    LSM_RECORDING_ENTRY_NAME                    = 0x10000001,
    LSM_RECORDING_ENTRY_DESCRIPTION             = 0x10000002,
    LSM_RECORDING_ENTRY_NOTES                   = 0x10000003,
    LSM_RECORDING_ENTRY_OBJECTIVE               = 0x10000004,
    LSM_RECORDING_ENTRY_PROCESSING_SUMMARY      = 0x10000005,
    LSM_RECORDING_ENTRY_SPECIAL_SCAN_MODE       = 0x10000006,
    LSM_RECORDING_ENTRY_SCAN_TYPE               = 0x10000007,
    LSM_RECORDING_ENTRY_SCAN_MODE               = 0x10000008,
    LSM_RECORDING_ENTRY_NUMBER_OF_STACKS        = 0x10000009,
    LSM_RECORDING_ENTRY_LINES_PER_PLANE         = 0x1000000A,
    LSM_RECORDING_ENTRY_SAMPLES_PER_LINE        = 0x1000000B,
    LSM_RECORDING_ENTRY_PLANES_PER_VOLUME       = 0x1000000C,
    LSM_RECORDING_ENTRY_IMAGES_WIDTH            = 0x1000000D,
    LSM_RECORDING_ENTRY_IMAGES_HEIGHT           = 0x1000000E,
    LSM_RECORDING_ENTRY_IMAGES_NUMBER_PLANES    = 0x1000000F,
    LSM_RECORDING_ENTRY_IMAGES_NUMBER_STACKS    = 0x10000010,
    LSM_RECORDING_ENTRY_IMAGES_NUMBER_CHANNELS  = 0x10000011,
    LSM_RECORDING_ENTRY_LINSCAN_XY_SIZE         = 0x10000012,
    LSM_RECORDING_ENTRY_SCAN_DIRECTION          = 0x10000013,
    LSM_RECORDING_ENTRY_TIME_SERIES             = 0x10000014,
    LSM_RECORDING_ENTRY_ORIGINAL_SCAN_DATA      = 0x10000015,
    LSM_RECORDING_ENTRY_ZOOM_X                  = 0x10000016,
    LSM_RECORDING_ENTRY_ZOOM_Y                  = 0x10000017,
    LSM_RECORDING_ENTRY_ZOOM_Z                  = 0x10000018,
    LSM_RECORDING_ENTRY_SAMPLE_0X               = 0x10000019,
    LSM_RECORDING_ENTRY_SAMPLE_0Y               = 0x1000001A,
    LSM_RECORDING_ENTRY_SAMPLE_0Z               = 0x1000001B,
    LSM_RECORDING_ENTRY_SAMPLE_SPACING          = 0x1000001C,
    LSM_RECORDING_ENTRY_LINE_SPACING            = 0x1000001D,
    LSM_RECORDING_ENTRY_PLANE_SPACING           = 0x1000001E,
    LSM_RECORDING_ENTRY_PLANE_WIDTH             = 0x1000001F,
    LSM_RECORDING_ENTRY_PLANE_HEIGHT            = 0x10000020,
    LSM_RECORDING_ENTRY_VOLUME_DEPTH            = 0x10000021,
    LSM_RECORDING_ENTRY_ROTATION                = 0x10000034,
    LSM_RECORDING_ENTRY_NUTATION                = 0x10000023,
    LSM_RECORDING_ENTRY_PRECESSION              = 0x10000035,
    LSM_RECORDING_ENTRY_SAMPLE_0TIME            = 0x10000036,
    LSM_RECORDING_ENTRY_START_SCAN_TRIGGER_IN   = 0x10000037,
    LSM_RECORDING_ENTRY_START_SCAN_TRIGGER_OUT  = 0x10000038,
    LSM_RECORDING_ENTRY_START_SCAN_EVENT        = 0x10000039,
    LSM_RECORDING_ENTRY_START_SCAN_TIME         = 0x10000040,
    LSM_RECORDING_ENTRY_STOP_SCAN_TRIGGER_IN    = 0x10000041,
    LSM_RECORDING_ENTRY_STOP_SCAN_TRIGGER_OUT   = 0x10000042,
    LSM_RECORDING_ENTRY_STOP_SCAN_EVENT         = 0x10000043,
    LSM_RECORDING_ENTRY_STOP_SCAN_TIME          = 0x10000044,
    LSM_RECORDING_ENTRY_USE_ROIS                = 0x10000045,
    LSM_RECORDING_ENTRY_USE_REDUCED_MEMORY_ROIS,
} LSMEntryRecordingMarkers;

typedef enum {
    LSM_TRACK_ENTRY_MULTIPLEX_TYPE              = 0x40000001,
    LSM_TRACK_ENTRY_MULTIPLEX_ORDER             = 0x40000002,
    LSM_TRACK_ENTRY_SAMPLING_MODE               = 0x40000003,
    LSM_TRACK_ENTRY_SAMPLING_METHOD             = 0x40000004,
    LSM_TRACK_ENTRY_SAMPLING_NUMBER             = 0x40000005,
    LSM_TRACK_ENTRY_ACQUIRE                     = 0x40000006,
    LSM_TRACK_ENTRY_SAMPLE_OBSERVATION_TIME     = 0x40000007,
    LSM_TRACK_ENTRY_TIME_BETWEEN_STACKS         = 0x4000000B,
    LSM_TRACK_ENTRY_NAME                        = 0x4000000C,
    LSM_TRACK_ENTRY_COLLIMATOR1_NAME            = 0x4000000D,
    LSM_TRACK_ENTRY_COLLIMATOR1_POSITION        = 0x4000000E,
    LSM_TRACK_ENTRY_COLLIMATOR2_NAME            = 0x4000000F,
    LSM_TRACK_ENTRY_COLLIMATOR2_POSITION        = 0x40000010,
    LSM_TRACK_ENTRY_IS_BLEACH_TRACK             = 0x40000011,
    LSM_TRACK_ENTRY_IS_BLEACH_AFTER_SCAN_NUMBER = 0x40000012,
    LSM_TRACK_ENTRY_BLEACH_SCAN_NUMBER          = 0x40000013,
    LSM_TRACK_ENTRY_TRIGGER_IN                  = 0x40000014,
    LSM_TRACK_ENTRY_TRIGGER_OUT                 = 0x40000015,
    LSM_TRACK_ENTRY_IS_RATIO_TRACK              = 0x40000016,
    LSM_TRACK_ENTRY_BLEACH_COUNT                = 0x40000017,
} LSMTrackMarkers;

typedef enum {
    LSM_LASER_ENTRY_NAME                        = 0x50000001,
    LSM_LASER_ENTRY_ACQUIRE                     = 0x50000002,
    LSM_LASER_ENTRY_POWER                       = 0x50000003,
} LSMLaserMarkers;

typedef enum {
    LSM_DETCHANNEL_ENTRY_INTEGRATION_MODE       = 0x70000001,
    LSM_DETCHANNEL_ENTRY_SPECIAL_MODE           = 0x70000002,
    LSM_DETCHANNEL_ENTRY_DETECTOR_GAIN_FIRST    = 0x70000003,
    LSM_DETCHANNEL_ENTRY_DETECTOR_GAIN_LAST     = 0x70000004,
    LSM_DETCHANNEL_ENTRY_AMPLIFIER_GAIN_FIRST   = 0x70000005,
    LSM_DETCHANNEL_ENTRY_AMPLIFIER_GAIN_LAST    = 0x70000006,
    LSM_DETCHANNEL_ENTRY_AMPLIFIER_OFFS_FIRST   = 0x70000007,
    LSM_DETCHANNEL_ENTRY_AMPLIFIER_OFFS_LAST    = 0x70000008,
    LSM_DETCHANNEL_ENTRY_PINHOLE_DIAMETER       = 0x70000009,
    LSM_DETCHANNEL_ENTRY_COUNTING_TRIGGER       = 0x7000000A,
    LSM_DETCHANNEL_ENTRY_ACQUIRE                = 0x7000000B,
    LSM_DETCHANNEL_POINT_DETECTOR_NAME          = 0x7000000C,
    LSM_DETCHANNEL_AMPLIFIER_NAME               = 0x7000000D,
    LSM_DETCHANNEL_PINHOLE_NAME                 = 0x7000000E,
    LSM_DETCHANNEL_FILTER_SET_NAME              = 0x7000000F,
    LSM_DETCHANNEL_FILTER_NAME                  = 0x70000010,
    LSM_DETCHANNEL_INTEGRATOR_NAME              = 0x70000013,
    LSM_DETCHANNEL_DETECTION_CHANNEL_NAME       = 0x70000014,
} LSMDetectorMarkers;

typedef enum {
    LSM_TYPE_SUBBLOCK = 0,
    LSM_TYPE_LONG     = 4,
    LSM_TYPE_RATIONAL = 5,
    LSM_TYPE_ASCII    = 2,
} LSMScanInfoType;

typedef struct {
    LSMTIFFSubFileType filetype;
    guint64 image_width;
    guint64 image_height;
    guint strips_number;
    guint *bits_per_sample;
    GwyTIFFCompression  compression;
    GwyTIFFPhotometric  photometric;
    guint32 *strip_offsets;
    guint samples_per_pixel;
    guint32 *strip_byte_counts;
    GwyTIFFPlanarConfig planar_config;
} LSMTIFFDirectory;

typedef struct {
    guint32 magic_number;
    gint32  size;
    gint32  xres;
    gint32  yres;
    gint32  zres;
    gint32  channels;
    gint32  time_res;
    gint32  intensity_datatype;
    gint32  thumbnail_xres;
    gint32  thumbnail_yres;
    gdouble x_voxel_size;
    gdouble y_voxel_size;
    gdouble z_voxel_size;
    gdouble x_origin;
    gdouble y_origin;
    gdouble z_origin;
    guint32 scan_type;
    guint32 datatype;
    guint32 offset_vector_overlay;
    guint32 offset_input_lut;
    guint32 offset_output_lut;
    guint32 offset_channel_colors_names;
    gdouble time_interval;
    guint32 offset_channel_data_types;
    guint32 offset_scan_information;
    guint32 offset_ks_data;
    guint32 offset_timestamps;
    guint32 offset_events_list;
    guint32 offset_roi;
    guint32 offset_bleach_roi;
    guint32 offset_next_recording;
    guint32 reserved[90]; /* Must be zeros */
} LSMHeaderTag;

typedef struct {
    gint32 block_size;
    gint32 numcolors;
    gint32 numnames;
    gint32 offset_colors;
    gint32 offset_names;
    gint32 mono;
    GArray *colors;
    GPtrArray *names;
} LSMNamesColors;

typedef struct {
    guint32 block_size;
    guint32 number_of_subblocks;
    guint32 channels_number;
    LSMLUTType lut_type; /* guint32 */
    guint32 advanced;
    guint32 actual_channel;
    guint32 reserved[9];
} LSMLookupTable;

typedef struct {
    guint32          entry; /* guint32 */
    LSMScanInfoType  type;  /* guint32 */
    guint32          size;
    gpointer         data;
} LSMEntry;

typedef struct {
    gchar *name;
    gchar *description;
    gchar *notes;
    gchar *objective;
    gchar *processing_summary;
    gchar *special_scan_mode;
    gchar *scan_mode;
    guint32 number_of_stacks;
    guint32 lines_per_plane;
    guint32 samples_per_line;
    guint32 planes_per_volume;
    guint32 images_width;
    guint32 images_height;
    guint32 images_number_planes;
    guint32 images_number_stacks;
    guint32 images_number_channels;
    guint32 linscan_xy_size;
    guint32 scan_direction;
    guint32 time_series;
    guint32 original_scan_data;
    gdouble zoomx;
    gdouble zoomy;
    gdouble zoomz;
    gdouble sample0x;
    gdouble sample0y;
    gdouble sample0z;
    gdouble sample_spacing;
    gdouble line_spacing;
    gdouble plane_spacing;
    gdouble plane_width;
    gdouble plane_height;
    gdouble volume_depth;
    gdouble rotation;
    gdouble nutation;
    gdouble precession;
    gdouble sample0_time;
    gchar *start_scan_trigger_in;
    gchar *start_scan_trigger_out;
    guint32 start_scan_event;
    gdouble start_scan_time;
    gchar *stop_scan_trigger_in;
    gchar *stop_scan_trigger_out;
    guint32 stop_scan_event;
    gdouble stop_scan_time;
    guint32 use_rois;
    guint32 use_reduced_memory_rois;
    gchar *laser_name;
    guint32 laser_acquire;
    gdouble laser_power;
} LSMEntryRecording;

static gboolean           module_register       (void);
static gint               lsm_detect            (const GwyFileDetectInfo *fileinfo,
                                                 gboolean only_name);
static GwyContainer*      lsm_load              (const gchar *filename,
                                                 GwyRunType mode,
                                                 GError **error);
static GwyContainer*      lsm_load_tiff         (const GwyTIFF *tiff,
                                                 const gchar *filename,
                                                 GError **error);
static LSMTIFFDirectory*  lsm_read_directory    (const GwyTIFF *tiff,
                                                 guint dirno,
                                                 GError **error);
static LSMHeaderTag*      lsm_read_header_tag   (const GwyTIFF *tiff,
                                                 const GwyTIFFEntry *tag,
                                                 GError **error);
static LSMNamesColors*    lsm_read_names_colors (const GwyTIFF *tiff,
                                                 guint32 offset,
                                                 GError **error);
static LSMEntryRecording* lsm_read_recording    (const GwyTIFF *tiff,
                                                 GwyContainer *meta,
                                                 guint32 offset,
                                                 GError **error);
static LSMEntry*          lsm_read_entry        (const GwyTIFF *tiff,
                                                 guint32 offset,
                                                 GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    module_register,
    N_("Imports Carl Zeiss CLSM images."),
    "Daniil Bratashov <dn2010@gwyddion.net>",
    "0.4",
    "Daniil Bratashov (dn2010), David Nečas (Yeti)",
    "2017",
};

GWY_MODULE_QUERY2(module_info, zeisslsm)

static gboolean
module_register(void)
{
    gwy_file_func_register("zeisslsm",
                           N_("Carl Zeiss CLSM images (.lsm)"),
                           (GwyFileDetectFunc)&lsm_detect,
                           (GwyFileLoadFunc)&lsm_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
lsm_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    GwyTIFF *tiff = NULL;
    gint score = 0;
    GwyTIFFVersion version = GWY_TIFF_CLASSIC;
    guint byteorder = G_LITTLE_ENDIAN;
    const GwyTIFFEntry *lsm_tag = NULL;

    if (only_name)
        return (g_str_has_suffix(fileinfo->name_lowercase, EXTENSION))
                ? 20 : 0;

    /* Weed out non-TIFFs */
    if (!gwy_tiff_detect(fileinfo->head, fileinfo->buffer_len,
                         &version, &byteorder))
        return 0;

    if ((tiff = gwy_tiff_load(fileinfo->name, NULL))
        && (lsm_tag = gwy_tiff_find_tag(tiff, 0, ZEISS_LSM_HEADER_TAG))) {
        score = 100;
    }

    if (tiff)
        gwy_tiff_free(tiff);

    return score;
}

static GwyContainer*
lsm_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{

    GwyTIFF *tiff;
    GwyContainer *container = NULL;

    tiff = gwy_tiff_load(filename, error);
    if (!tiff)
        return NULL;

    container = lsm_load_tiff(tiff, filename, error);

    gwy_tiff_free(tiff);

    return container;
}

static GwyContainer*
lsm_load_tiff(const GwyTIFF *tiff,
              const gchar *filename,
              GError **error)
{
    GwyContainer *container = NULL, *meta = NULL;
    GwyDataLine *dataline = NULL;
    GwyGraphCurveModel *gcmodel;
    GwyGraphModel *gmodel = NULL;
    GwyDataField *dfield = NULL;
    GwyBrick *brick;
    GwySIUnit *siunit;
    gint i, j, k, l, volumes, ndirs, z, xres = 0, yres = 0, zres = 0;
    gint color;
    gdouble xreal = 1.0, yreal = 1.0, zreal = 1.0;
    gchar *key, *name, *lutname;
    gdouble *data, *bdata;
    const GwyTIFFEntry *lsm_tag;
    LSMTIFFDirectory *directory;
    LSMHeaderTag *header_tag = NULL;
    LSMNamesColors *names_colors = NULL;
    LSMEntryRecording *recording = NULL;
    const guchar *p;
    gboolean is_image = FALSE, is_volume = FALSE, is_line = FALSE;
    GArray *bricks, *bricks_preview;

    if (!(lsm_tag = gwy_tiff_find_tag(tiff, 0, ZEISS_LSM_HEADER_TAG))) {
        err_FILE_TYPE(error, "Carl Zeiss LSM");
        goto fail;
    }

    if (!(header_tag = lsm_read_header_tag(tiff, lsm_tag, error))) {
        err_FILE_TYPE(error, "Carl Zeiss LSM");
        goto fail;
    }

    names_colors = lsm_read_names_colors(tiff,
                                         header_tag->offset_channel_colors_names,
                                         error);
    meta = gwy_container_new();
    recording = lsm_read_recording(tiff, meta,
                                   header_tag->offset_scan_information,
                                   error);

    ndirs = gwy_tiff_get_n_dirs(tiff);
    gwy_debug("ndirs=%u", ndirs);

    container = gwy_container_new();
    k = 0; /* number of images in resulting file */
    volumes = 0;

    is_image = FALSE;
    is_volume = FALSE;
    is_line = FALSE;
    bricks = g_array_new(FALSE, FALSE, sizeof(GwyBrick*));
    bricks_preview = g_array_new(FALSE, FALSE, sizeof(GwyBrick*));

    for (i = 0; i < ndirs; i++) {
        gwy_debug("directory #%u", i);
        if (!(directory = lsm_read_directory(tiff, i, error))) {
            err_FILE_TYPE(error, "Carl Zeiss LSM");
            goto fail;
        }

        switch (header_tag->scan_type) {
            case LSM_SCANTYPE_XYZ:
                xres = directory->image_width;
                yres = directory->image_height;
                zres = header_tag->zres;
                xreal = xres * header_tag->x_voxel_size;
                yreal = yres * header_tag->y_voxel_size;
                zreal = zres * header_tag->z_voxel_size;
                if (directory->image_width != header_tag->xres) {
                    xreal = header_tag->xres * header_tag->x_voxel_size;
                    yreal = header_tag->yres * header_tag->y_voxel_size;
                }
                if (header_tag->zres > 1)
                    is_volume = TRUE;
                else
                    is_image = TRUE;
            break;
            case LSM_SCANTYPE_XZ:
                xres = directory->image_width;
                yres = directory->image_height;
                xreal = xres * header_tag->x_voxel_size;
                yreal = yres * header_tag->z_voxel_size;
                if (directory->image_width != header_tag->xres) {
                    xreal = header_tag->xres * header_tag->x_voxel_size;
                    yreal = header_tag->zres * header_tag->z_voxel_size;
                }
                is_image = TRUE;
            break;
            case LSM_SCANTYPE_LINE:
                xres = directory->image_width;
                yres = directory->image_height;
                xreal = xres * header_tag->x_voxel_size;
                yreal = 1.0;
                if (directory->image_width != header_tag->xres)
                    xreal = header_tag->xres * header_tag->x_voxel_size;
                is_line = TRUE;
                siunit = gwy_si_unit_new("m");
                gmodel = g_object_new(GWY_TYPE_GRAPH_MODEL,
                                      "si-unit-x", siunit,
                                      NULL);
                g_object_unref(siunit);
            break;
            case LSM_SCANTYPE_TIMESERIES_XY:
                xres = directory->image_width;
                yres = directory->image_height;
                zres = ndirs / 2;
                xreal = xres * header_tag->x_voxel_size;
                yreal = yres * header_tag->y_voxel_size;
                if (directory->image_width != header_tag->xres) {
                    xreal = header_tag->xres * header_tag->x_voxel_size;
                    yreal = header_tag->yres * header_tag->y_voxel_size;
                }
                zreal = ndirs / 2 * header_tag->time_interval;
                is_volume = TRUE;
            break;
            case LSM_SCANTYPE_TIMESERIES_XZ:
                xres = directory->image_width;
                yres = directory->image_height;
                zres = ndirs / 2;
                xreal = xres * header_tag->x_voxel_size;
                yreal = yres * header_tag->z_voxel_size;
                zreal = ndirs / 2 * header_tag->time_interval;
                if (directory->image_width != header_tag->xres) {
                    xreal = header_tag->xres * header_tag->x_voxel_size;
                    yreal = header_tag->zres * header_tag->z_voxel_size;
                }
                is_volume = TRUE;
            break;
            case LSM_SCANTYPE_TIMESERIES_MEAN_ROI:
                xres = directory->image_width;
                yres = directory->image_height;
                xreal = directory->image_width;
                yreal = directory->image_height * header_tag->time_interval;
                is_image = TRUE;
            break;
            default:
                // FIXME: there is files with broken scantype
                xres = directory->image_width;
                yres = directory->image_height;
                zres = header_tag->zres;
                xreal = xres * header_tag->x_voxel_size;
                yreal = yres * header_tag->y_voxel_size;
                zreal = zres * header_tag->z_voxel_size;
                if (directory->image_width != header_tag->xres) {
                    xreal = header_tag->xres * header_tag->x_voxel_size;
                    yreal = header_tag->yres * header_tag->y_voxel_size;
                }
                if (header_tag->zres > 1)
                    is_volume = TRUE;
                else
                    is_image = TRUE;
            break;
        }

        for (j = 0; j < directory->strips_number; j++) {
            if ((is_image) || (is_volume)) {
                dfield = gwy_data_field_new(xres, yres,
                                            xreal, yreal, TRUE);
                data = gwy_data_field_get_data(dfield);
            }
            else if (is_line) {
                dataline = gwy_data_line_new(xres, xreal, TRUE);
                data = gwy_data_line_get_data(dataline);
            }
            else {
                err_FILE_TYPE(error, "Carl Zeiss LSM");
                goto fail;
            }

            p = tiff->data + directory->strip_offsets[j];
            if (directory->bits_per_sample[j] == 8)
                for (l = 0; l < xres * yres; l++)
                    *(data++) = *(p++);
            else if ((directory->bits_per_sample[j] == 12)
                  || (directory->bits_per_sample[j] == 16))
                for (l = 0; l < xres * yres; l++)
                    *(data++) = gwy_get_guint16_le(&p);
            else if (directory->bits_per_sample[j] == 32)
                for (l = 0; l < xres * yres; l++)
                    *(data++) = gwy_get_gfloat_le(&p);
            else {
                if (is_line)
                    g_object_unref(dataline);
                else
                    g_object_unref(dfield);
                continue;
            }

            if (is_line) {
                gcmodel = g_object_new(GWY_TYPE_GRAPH_CURVE_MODEL,
                                       "mode", GWY_GRAPH_CURVE_LINE,
                                       "color", gwy_graph_get_preset_color(k),
                                       NULL);
                gwy_graph_curve_model_set_data_from_dataline(gcmodel,
                                                             dataline,
                                                             0, 0);
                gwy_graph_model_add_curve(gmodel, gcmodel);
                g_object_unref(gcmodel);
            }

            if (is_image) {
                siunit = gwy_si_unit_new("m");
                gwy_data_field_set_si_unit_xy(dfield, siunit);
                g_object_unref(siunit);
                key = g_strdup_printf("/%d/data", k);
                gwy_container_set_object_by_name(container,
                                                 key, dfield);
                g_free(key);
                g_object_unref(dfield);
                if (gwy_container_get_n_items(meta)) {
                    key = g_strdup_printf("/%d/meta", k);
                    gwy_container_set_object_by_name(container, key,
                                                     meta);
                    g_free(key);
                }
                gwy_file_channel_import_log_add(container,
                                                k, NULL, filename);

                if (names_colors && (i % 2 == 0)) {
                    key = g_strdup_printf("/%d/data/title", k);
                    name = (gchar *)g_ptr_array_index(names_colors->names,
                                                      j);
                    gwy_container_set_string_by_name(container, key,
                                                     g_strdup(name));
                    g_free(key);

                    color = g_array_index(names_colors->colors,
                                          gint32, j);
                    if (color == 255) {
                        lutname = g_strdup_printf("RGB-Red");
                    }
                    else if (color == 65280) {
                        lutname = g_strdup_printf("RGB-Green");
                    }
                    else if (color == 16711680) {
                        lutname = g_strdup_printf("RGB-Blue");
                    }
                    else {
                        lutname = g_strdup_printf("Gray");
                    }
                    key = g_strdup_printf("/%u/base/palette",
                                          k);
                    gwy_container_set_string_by_name(container,
                                                     key,
                                                     lutname);
                    g_free(key);
                }
                else {
                    key = g_strdup_printf("/%d/data/title", k);
                    gwy_container_set_string_by_name(container, key,
                            g_strdup_printf("LSM Image %u (channel %u)",
                                            i / 2, j));
                    g_free(key);

                    if ((directory->photometric
                                            == GWY_TIFF_PHOTOMETRIC_RGB)
                     && (j == 0))
                        lutname = g_strdup_printf("RGB-Red");
                    else if ((directory->photometric
                                            == GWY_TIFF_PHOTOMETRIC_RGB)
                     && (j == 1))
                        lutname = g_strdup_printf("RGB-Green");
                    else if ((directory->photometric
                                            == GWY_TIFF_PHOTOMETRIC_RGB)
                     && (j == 2))
                        lutname = g_strdup_printf("RGB-Blue");
                    else  {
                        lutname = g_strdup_printf("Gray");
                    }
                    key = g_strdup_printf("/%u/base/palette", k);
                    gwy_container_set_string_by_name(container,
                                                     key,
                                                     lutname);
                } /* else */
            }

            if (is_volume) {
                if ((i % (zres * 2) == 0)
                    || (i % (zres * 2) == 1)) {
                    brick = gwy_brick_new(xres, yres, zres,
                                          xreal, yreal, zreal,
                                          TRUE);

                    siunit = gwy_si_unit_new("m");
                    gwy_brick_set_si_unit_x(brick, siunit);
                    g_object_unref(siunit);
                    siunit = gwy_si_unit_new("m");
                    gwy_brick_set_si_unit_y(brick, siunit);
                    g_object_unref(siunit);
                    if (header_tag->scan_type == LSM_SCANTYPE_XYZ) {
                        siunit = gwy_si_unit_new("m");
                        gwy_brick_set_si_unit_x(brick, siunit);
                        g_object_unref(siunit);
                    }
                    else {
                        siunit = gwy_si_unit_new("s");
                        gwy_brick_set_si_unit_x(brick, siunit);
                        g_object_unref(siunit);
                    }

                    if (i % (zres * 2) == 0)
                        g_array_insert_val(bricks, j, brick);
                    else
                        g_array_insert_val(bricks_preview, j, brick);

                    key = g_strdup_printf("/brick/%d", volumes + 1);
                    gwy_container_set_object_by_name(container,
                                                     key, brick);
                    g_free(key);
                    g_object_unref(brick);

                    if (gwy_container_get_n_items(meta)) {
                        key = g_strdup_printf("brick/%d/meta",
                                              volumes + 1);
                        gwy_container_set_object_by_name(container, key,
                                                         meta);
                        g_free(key);
                    }

                    if (names_colors && (i % 2 == 0)) {
                        key = g_strdup_printf("/brick/%d/title",
                                              volumes + 1);
                        name = (gchar *)g_ptr_array_index(names_colors->names,
                                                          j);
                        gwy_container_set_string_by_name(container, key,
                                                         g_strdup(name));
                        g_free(key);

                        color = g_array_index(names_colors->colors,
                                              gint32, j);
                        if (color == 255) {
                            lutname = g_strdup_printf("RGB-Red");
                        }
                        else if (color == 65280) {
                            lutname = g_strdup_printf("RGB-Green");
                        }
                        else if (color == 16711680) {
                            lutname = g_strdup_printf("RGB-Blue");
                        }
                        else {
                            lutname = g_strdup_printf("Gray");
                        }
                        key = g_strdup_printf("/brick/%d/preview/palette",
                                              volumes + 1);
                        gwy_container_set_string_by_name(container,
                                                         key,
                                                         lutname);
                        g_free(key);
                    }
                    else {
                        key = g_strdup_printf("/brick/%d/title",
                                              volumes + 1);
                        gwy_container_set_string_by_name(container, key,
                           g_strdup_printf("LSM Volume %d (channel %u)",
                                           i / 2 / zres,
                                           j));
                        g_free(key);

                        if ((directory->photometric
                                                == GWY_TIFF_PHOTOMETRIC_RGB)
                         && (j == 0))
                            lutname = g_strdup_printf("RGB-Red");
                        else if ((directory->photometric
                                                == GWY_TIFF_PHOTOMETRIC_RGB)
                         && (j == 1))
                            lutname = g_strdup_printf("RGB-Green");
                        else if ((directory->photometric
                                                == GWY_TIFF_PHOTOMETRIC_RGB)
                         && (j == 2))
                            lutname = g_strdup_printf("RGB-Blue");
                        else  {
                            lutname = g_strdup_printf("Gray");
                        }
                        key = g_strdup_printf("/brick/%d/preview/palette",
                                              volumes + 1);
                        gwy_container_set_string_by_name(container,
                                                         key,
                                                         lutname);
                        g_free(key);
                    } /* else */
                    volumes++;
                }

                z = (i % zres) / 2;
                if (i % 2)
                    brick = g_array_index(bricks_preview, GwyBrick*, j);
                else
                    brick = g_array_index(bricks, GwyBrick*, j);

                bdata = gwy_brick_get_data(brick);
                data = gwy_data_field_get_data(dfield);
                memcpy(bdata + z * directory->image_width * directory->image_height,
                       data, directory->image_width * directory->image_height * sizeof(gdouble));
            }
            k++;
        } /* for j */

        g_free(directory->bits_per_sample);
        g_free(directory->strip_offsets);
        g_free(directory->strip_byte_counts);
        g_free(directory);
    } /* for i */

    g_array_free(bricks, TRUE);
    g_array_free(bricks_preview, TRUE);

    if (is_volume) {
        for (i = 0; i < volumes; i++) {
            key = g_strdup_printf("/brick/%d", i + 1);
            gwy_container_gis_object_by_name(container,
                                             key,
                                             &brick);
            g_free(key);
            gwy_file_volume_import_log_add(container,
                                           i+1, NULL, filename);
        }
    }

    if (is_line) {
        gwy_container_set_object_by_name(container,
                                         "/0/graph/graph/1", gmodel);
        g_object_unref(gmodel);
    }

fail:
    if (header_tag) {
        g_free(header_tag);
    }
    if (names_colors) {
        g_array_free(names_colors->colors, TRUE);
        g_ptr_array_free(names_colors->names, TRUE);
        g_free(names_colors);
    }
    /* The strings are already eaten by meta. */
    g_free(recording);

    return container;
}

static LSMTIFFDirectory*
lsm_read_directory(const GwyTIFF *tiff, guint dirno, GError **error)
{
    guint i, j, offset;
    GArray *direntries;
    GwyTIFFEntry *tag;
    const guchar *p;
    LSMTIFFDirectory *lsmdir;

    lsmdir = g_new(LSMTIFFDirectory, 1);
    direntries = g_ptr_array_index(tiff->dirs, dirno);
    for (i = 0; i < direntries->len; i++) {
        tag = &g_array_index(direntries, GwyTIFFEntry, i);
        gwy_debug("tag=%u type=%d count=%" G_GUINT64_FORMAT "",
                  tag->tag, tag->type, tag->count);
        switch (tag->tag) {
            case GWY_TIFFTAG_SUB_FILE_TYPE:
                p = tag->value;
                lsmdir->filetype = gwy_get_guint32_le(&p);
                gwy_debug("filetype=%d", lsmdir->filetype);
            break;
            case GWY_TIFFTAG_IMAGE_WIDTH:
                p = tag->value;
                lsmdir->image_width = gwy_get_guint32_le(&p);
                gwy_debug("imgwidth=%" G_GUINT64_FORMAT "",
                          lsmdir->image_width);
            break;
            case GWY_TIFFTAG_IMAGE_LENGTH:
                p = tag->value;
                lsmdir->image_height = gwy_get_guint32_le(&p);
                gwy_debug("imgheight=%" G_GUINT64_FORMAT "",
                          lsmdir->image_height);
            break;
            case GWY_TIFFTAG_BITS_PER_SAMPLE:
                p = tag->value;
                offset = gwy_get_guint32_le(&p);
                gwy_debug("offset = %d", offset);
                lsmdir->bits_per_sample
                                    = g_new0(guint, MAX(tag->count, 1));
                if (tag->count == 1)
                    lsmdir->bits_per_sample[0] = offset;
                else {
                    p = tiff->data + offset;
                    for (j = 0; j < tag->count; j++) {
                        lsmdir->bits_per_sample[j]
                                               = gwy_get_guint16_le(&p);
                        gwy_debug("bps[%d]=%d",
                                  j , lsmdir->bits_per_sample[j]);
                    }
                }
            break;
            case GWY_TIFFTAG_COMPRESSION:
                p = tag->value;
                lsmdir->compression = gwy_get_guint16_le(&p);
                gwy_debug("compression=%d", lsmdir->compression);
                if (lsmdir->compression != GWY_TIFF_COMPRESSION_NONE) {
                    // FIXME: LZW is unsupported
                    g_set_error(error,
                                GWY_MODULE_FILE_ERROR,
                                GWY_MODULE_FILE_ERROR_DATA,
                                _("Compression type %u is not supported."),
                                lsmdir->compression);
                    g_free(lsmdir);
                    return NULL;
                }
            break;
            case GWY_TIFFTAG_PHOTOMETRIC:
                p = tag->value;
                lsmdir->photometric = gwy_get_guint16_le(&p);
                gwy_debug("photometric=%d", lsmdir->photometric);
            break;
            case GWY_TIFFTAG_STRIP_OFFSETS:
                p = tag->value;
                offset = gwy_get_guint32_le(&p);
                gwy_debug("offset = %d", offset);
                lsmdir->strips_number = tag->count;
                lsmdir->strip_offsets= g_new0(guint, MAX(tag->count, 1));
                if (tag->count == 1)
                    lsmdir->strip_offsets[0] = offset;
                else {
                    p = tiff->data + offset;
                    for (j = 0; j < tag->count; j++) {

                        lsmdir->strip_offsets[j]
                                               = gwy_get_guint32_le(&p);
                        gwy_debug("strip offset[%d]=%d",
                                  j , lsmdir->strip_offsets[j]);
                    }
                }
            break;
            case GWY_TIFFTAG_SAMPLES_PER_PIXEL:
                p = tag->value;
                lsmdir->samples_per_pixel = gwy_get_guint16_le(&p);
                gwy_debug("samples per pixel=%d",
                          lsmdir->samples_per_pixel);
            break;
            case GWY_TIFFTAG_STRIP_BYTE_COUNTS:
                p = tag->value;
                offset = gwy_get_guint32_le(&p);
                gwy_debug("offset = %d", offset);
                lsmdir->strip_byte_counts
                                    = g_new0(guint, MAX(tag->count, 1));
                if (tag->count == 1)
                    lsmdir->strip_byte_counts[0] = offset;
                else {
                    p = tiff->data + offset;
                    for (j = 0; j < tag->count; j++) {
                        lsmdir->strip_byte_counts[j]
                                               = gwy_get_guint32_le(&p);
                        gwy_debug("strip byte counts[%d]=%d",
                                  j , lsmdir->strip_byte_counts[j]);
                    }
                }
            break;
            case GWY_TIFFTAG_PLANAR_CONFIG:
                p = tag->value;
                lsmdir->planar_config = gwy_get_guint16_le(&p);
                gwy_debug("planar config=%d", lsmdir->planar_config);
            break;
            case ZEISS_LSM_HEADER_TAG:
            break;
            default:
            break;
        }
    }

    return lsmdir;
}

static LSMHeaderTag*
lsm_read_header_tag(const GwyTIFF *tiff,
                    const GwyTIFFEntry *tag, GError **error)
{
    LSMHeaderTag *header_tag;
    guint offset;
    const guchar *p;

    header_tag = g_new0(LSMHeaderTag, 1);
    p = tag->value;
    offset = gwy_get_guint32_le(&p);
    p = tiff->data + offset;
    header_tag->magic_number = gwy_get_guint32_le(&p);
    if ((header_tag->magic_number != 0x00300494C)
     && (header_tag->magic_number != 0x00400494C)) {
        err_FILE_TYPE(error, "Carl Zeiss LSM");
        g_free(header_tag);
        return NULL;
    }
    gwy_debug("magic=%x", header_tag->magic_number);
    header_tag->size = gwy_get_gint32_le(&p);
    header_tag->xres = gwy_get_gint32_le(&p);
    header_tag->yres = gwy_get_gint32_le(&p);
    header_tag->zres = gwy_get_gint32_le(&p);
    header_tag->channels = gwy_get_gint32_le(&p);
    header_tag->time_res = gwy_get_gint32_le(&p);
    header_tag->intensity_datatype = gwy_get_gint32_le(&p);
    header_tag->thumbnail_xres = gwy_get_gint32_le(&p);
    header_tag->thumbnail_yres = gwy_get_gint32_le(&p);
    header_tag->x_voxel_size = gwy_get_gdouble_le(&p);
    header_tag->y_voxel_size = gwy_get_gdouble_le(&p);
    header_tag->z_voxel_size = gwy_get_gdouble_le(&p);
    header_tag->x_origin = gwy_get_gdouble_le(&p);
    header_tag->y_origin = gwy_get_gdouble_le(&p);
    header_tag->z_origin = gwy_get_gdouble_le(&p);
    header_tag->scan_type = gwy_get_guint32_le(&p);
    header_tag->datatype = gwy_get_guint32_le(&p);
    header_tag->offset_vector_overlay = gwy_get_guint32_le(&p);
    header_tag->offset_input_lut = gwy_get_guint32_le(&p);
    header_tag->offset_output_lut = gwy_get_guint32_le(&p);
    header_tag->offset_channel_colors_names = gwy_get_guint32_le(&p);
    header_tag->time_interval = gwy_get_gdouble_le(&p);
    header_tag->offset_channel_data_types = gwy_get_guint32_le(&p);
    header_tag->offset_scan_information = gwy_get_guint32_le(&p);
    header_tag->offset_ks_data = gwy_get_guint32_le(&p);
    header_tag->offset_timestamps = gwy_get_guint32_le(&p);
    header_tag->offset_events_list = gwy_get_guint32_le(&p);
    header_tag->offset_roi = gwy_get_guint32_le(&p);
    header_tag->offset_bleach_roi = gwy_get_guint32_le(&p);
    header_tag->offset_next_recording = gwy_get_guint32_le(&p);

    gwy_debug("channels=%d", header_tag->channels);
    gwy_debug("scan type=%u", header_tag->scan_type);
    gwy_debug("xres=%d yres=%d zres=%d",
              header_tag->xres,
              header_tag->yres,
              header_tag->zres);
    gwy_debug("xsize=%g, ysize=%g zsize=%g timesize=%g",
              header_tag->x_voxel_size,
              header_tag->y_voxel_size,
              header_tag->z_voxel_size,
              header_tag->time_interval);
    return header_tag;
}

static LSMNamesColors *
lsm_read_names_colors(const GwyTIFF *tiff,
                      guint32 offset,
                      G_GNUC_UNUSED GError **error)
{
    LSMNamesColors *names_colors;
    const guchar *p;
    gchar *name, *nameu;
    gint32 color;
    gint i;
    gsize len, size;

    if (offset == 0) {
        gwy_debug("No names and colors structure");
        return NULL;
    }
    names_colors = g_new0(LSMNamesColors, 1);
    p = tiff->data + offset;

    names_colors->block_size = gwy_get_gint32_le(&p);
    names_colors->numcolors = gwy_get_gint32_le(&p);
    names_colors->numnames = gwy_get_gint32_le(&p);
    names_colors->offset_colors = gwy_get_gint32_le(&p);
    names_colors->offset_names = gwy_get_gint32_le(&p);
    names_colors->mono = gwy_get_gint32_le(&p);
    names_colors->colors = g_array_sized_new(FALSE, TRUE,
                                             sizeof(gint32),
                                             names_colors->numcolors);
    p = tiff->data + offset + names_colors->offset_colors;
    for (i = 0; i < names_colors->numcolors; i++) {
        color = gwy_get_gint32_le(&p);
        g_array_append_val (names_colors->colors, color);
        gwy_debug("color [%d] = %d", i, color);
    }
    p = tiff->data + offset + names_colors->offset_names;
    names_colors->names = g_ptr_array_sized_new(names_colors->numnames);
    gwy_debug("num names=%d", names_colors->numnames);
    len = 0;
    size = names_colors->block_size - names_colors->offset_names - len;
    for (i = 0; i < names_colors->numnames; i++) {
        name = g_new0(gchar, size + 1);
        while ((*(p++) < 32) && ((len++) < size));
        while ((*p) && (len < size)) {
            *(name + (len++)) = *(p++);
        }
        gwy_debug("name[%d]=%s", i, name);
        nameu = g_convert(name, len, "UTF-8", "ISO-8859-1",
                          NULL, NULL, NULL);
        g_free(name);
        g_ptr_array_add(names_colors->names, (gpointer)nameu);
    }

    return names_colors;
}

static LSMEntryRecording*
lsm_read_recording (const GwyTIFF *tiff,
                    GwyContainer *meta,
                    guint32 offset,
                    G_GNUC_UNUSED GError **error)
{
    LSMEntryRecording *recording;
    LSMEntry *entry;
    const guchar *p;

    if (offset == 0) {
        gwy_debug("No recordings");
        return NULL;
    }

    p = tiff->data + offset;
    entry = g_new0(LSMEntry, 1);
    recording = g_new0(LSMEntryRecording, 1);
    while (entry->entry != LSM_SUBBLOCK_END) {
        if (entry)
            g_free(entry);
        entry = lsm_read_entry(tiff, offset, error);

        gwy_debug("entry = %x type=%d size=%d",
                  entry->entry, entry->type, entry->size);
        if (entry->entry == LSM_RECORDING_ENTRY_NAME) {
            recording->name = g_convert(entry->data, entry->size,
                                        "UTF-8", "ISO-8859-1",
                                        NULL, NULL, NULL);
            gwy_container_set_string_by_name(meta, "Name",
                                             recording->name);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_DESCRIPTION) {
            recording->description = g_convert(entry->data, entry->size,
                                               "UTF-8", "ISO-8859-1",
                                               NULL, NULL, NULL);
            gwy_container_set_string_by_name(meta, "Description",
                                             recording->description);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_NOTES) {
            recording->notes = g_convert(entry->data, entry->size,
                                         "UTF-8", "ISO-8859-1",
                                         NULL, NULL, NULL);
            gwy_container_set_string_by_name(meta, "Notes",
                                             recording->notes);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_OBJECTIVE) {
            recording->objective = g_convert(entry->data, entry->size,
                                             "UTF-8", "ISO-8859-1",
                                             NULL, NULL, NULL);
            gwy_container_set_string_by_name(meta, "Objective",
                                             recording->objective);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_PROCESSING_SUMMARY) {
            recording->processing_summary = g_convert(entry->data,
                                                      entry->size,
                                                      "UTF-8",
                                                      "ISO-8859-1",
                                                      NULL, NULL, NULL);
            gwy_container_set_string_by_name(meta, "Processing summary",
                                             recording->processing_summary);

        }
        else if (entry->entry == LSM_RECORDING_ENTRY_SPECIAL_SCAN_MODE) {
            recording->special_scan_mode = g_convert(entry->data,
                                                     entry->size,
                                                     "UTF-8",
                                                     "ISO-8859-1",
                                                     NULL, NULL, NULL);
            gwy_container_set_string_by_name(meta, "Special scan mode",
                                             recording->special_scan_mode);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_SCAN_TYPE) {
            /* Should be empty string */
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_SCAN_MODE) {
            recording->scan_mode = g_convert(entry->data, entry->size,
                                             "UTF-8", "ISO-8859-1",
                                             NULL, NULL, NULL);
            gwy_container_set_string_by_name(meta, "Scan mode",
                                             recording->scan_mode);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_NUMBER_OF_STACKS) {
            p = entry->data;
            recording->number_of_stacks = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Number of stacks",
                    g_strdup_printf("%d", recording->number_of_stacks));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_LINES_PER_PLANE) {
            p = entry->data;
            recording->lines_per_plane = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Lines per plane",
                     g_strdup_printf("%d", recording->lines_per_plane));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_SAMPLES_PER_LINE) {
            p = entry->data;
            recording->samples_per_line = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Samples per line",
                     g_strdup_printf("%d", recording->samples_per_line));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_PLANES_PER_VOLUME) {
            p = entry->data;
            recording->planes_per_volume = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Planes per volume",
                   g_strdup_printf("%d", recording->planes_per_volume));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_IMAGES_WIDTH) {
            p = entry->data;
            recording->images_width = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Images width",
                        g_strdup_printf("%d", recording->images_width));

        }
        else if (entry->entry == LSM_RECORDING_ENTRY_IMAGES_HEIGHT) {
            p = entry->data;
            recording->images_height = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Images height",
                       g_strdup_printf("%d", recording->images_height));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_IMAGES_NUMBER_PLANES) {
            p = entry->data;
            recording->images_number_planes = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta,
                                             "Images number of planes",
                      g_strdup_printf("%d",
                                      recording->images_number_planes));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_IMAGES_NUMBER_STACKS) {
            p = entry->data;
            recording->images_number_stacks = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta,
                                             "Images number of stacks",
                      g_strdup_printf("%d",
                                      recording->images_number_stacks));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_IMAGES_NUMBER_CHANNELS) {
            p = entry->data;
            recording->images_number_channels = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta,
                                             "Images number of channels",
                    g_strdup_printf("%d",
                                    recording->images_number_channels));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_LINSCAN_XY_SIZE) {
            p = entry->data;
            recording->linscan_xy_size = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Linescan XY size",
                     g_strdup_printf("%d", recording->linscan_xy_size));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_SCAN_DIRECTION) {
            p = entry->data;
            recording->scan_direction = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Scan direction",
                g_strdup_printf("%s", recording->scan_direction ?
                                   "Bidirectional" : "Unidirectional"));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_TIME_SERIES) {
            p = entry->data;
            recording->time_series = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Time series",
                g_strdup_printf("%s", recording->time_series ?
                                                     "True" : "False"));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_ORIGINAL_SCAN_DATA) {
            p = entry->data;
            recording->original_scan_data = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Original scan data",
                g_strdup_printf("%s", recording->original_scan_data ?
                                              "Original" : "Modified"));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_ZOOM_X) {
            p = entry->data;
            recording->zoomx = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "X zoom",
                               g_strdup_printf("%g", recording->zoomx));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_ZOOM_Y) {
            p = entry->data;
            recording->zoomy = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Y zoom",
                               g_strdup_printf("%g", recording->zoomy));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_ZOOM_Z) {
            p = entry->data;
            recording->zoomz = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Z zoom",
                               g_strdup_printf("%g", recording->zoomz));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_SAMPLE_0X) {
            p = entry->data;
            recording->sample0x = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Sample 0 X",
                        g_strdup_printf("%g mkm", recording->sample0x));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_SAMPLE_0Y) {
            p = entry->data;
            recording->sample0y = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Sample 0 Y",
                        g_strdup_printf("%g mkm", recording->sample0y));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_SAMPLE_0Z) {
            p = entry->data;
            recording->sample0z = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Sample 0 Z",
                        g_strdup_printf("%g mkm", recording->sample0z));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_SAMPLE_SPACING) {
            p = entry->data;
            recording->sample_spacing = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Sample spacing",
                  g_strdup_printf("%g mkm", recording->sample_spacing));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_LINE_SPACING) {
            p = entry->data;
            recording->line_spacing = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Line spacing",
                    g_strdup_printf("%g mkm", recording->line_spacing));

        }
        else if (entry->entry == LSM_RECORDING_ENTRY_PLANE_SPACING) {
            p = entry->data;
            recording->plane_spacing = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Plane spacing",
                   g_strdup_printf("%g mkm", recording->plane_spacing));

        }
        else if (entry->entry == LSM_RECORDING_ENTRY_PLANE_WIDTH) {
            p = entry->data;
            recording->plane_width = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Plane width",
                     g_strdup_printf("%g mkm", recording->plane_width));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_PLANE_HEIGHT) {
            p = entry->data;
            recording->plane_height = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Plane height",
                    g_strdup_printf("%g mkm", recording->plane_height));

        }
        else if (entry->entry == LSM_RECORDING_ENTRY_VOLUME_DEPTH) {
            p = entry->data;
            recording->volume_depth = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Volume depth",
                    g_strdup_printf("%g mkm", recording->volume_depth));

        }
        else if (entry->entry == LSM_RECORDING_ENTRY_ROTATION) {
            p = entry->data;
            recording->rotation = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Rotation",
                    g_strdup_printf("%g degrees", recording->rotation));

        }
        else if (entry->entry == LSM_RECORDING_ENTRY_NUTATION) {
            p = entry->data;
            recording->nutation = gwy_get_gdouble_le(&p);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_PRECESSION) {
            p = entry->data;
            recording->precession = gwy_get_gdouble_le(&p);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_SAMPLE_0TIME) {
            p = entry->data;
            recording->sample0_time = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Sample 0 time",
                        g_strdup_printf("%g", recording->sample0_time));

        }
        else if (entry->entry == LSM_RECORDING_ENTRY_START_SCAN_TRIGGER_IN) {
            recording->start_scan_trigger_in = g_convert(entry->data,
                                                         entry->size,
                                                         "UTF-8",
                                                         "ISO-8859-1",
                                                         NULL, NULL,
                                                         NULL);
            gwy_container_set_string_by_name(meta, "Start scan trigger in",
                                             recording->start_scan_trigger_in);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_START_SCAN_TRIGGER_OUT) {
            recording->start_scan_trigger_out = g_convert(entry->data,
                                                          entry->size,
                                                          "UTF-8",
                                                          "ISO-8859-1",
                                                          NULL, NULL,
                                                          NULL);
            gwy_container_set_string_by_name(meta, "Start scan trigger out",
                                             recording->start_scan_trigger_out);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_START_SCAN_EVENT) {
            p = entry->data;
            recording->start_scan_event = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Start scan event",
                    g_strdup_printf("%d", recording->start_scan_event));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_START_SCAN_TIME) {
            p = entry->data;
            recording->start_scan_time = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Start scan time",
                     g_strdup_printf("%g", recording->start_scan_time));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_STOP_SCAN_TRIGGER_IN) {
            recording->stop_scan_trigger_in = g_convert(entry->data,
                                                        entry->size,
                                                        "UTF-8",
                                                        "ISO-8859-1",
                                                        NULL, NULL,
                                                        NULL);
            gwy_container_set_string_by_name(meta, "Stop scan trigger in",
                                             recording->stop_scan_trigger_in);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_STOP_SCAN_TRIGGER_OUT) {
            recording->stop_scan_trigger_out = g_convert(entry->data,
                                                         entry->size,
                                                         "UTF-8",
                                                         "ISO-8859-1",
                                                         NULL, NULL,
                                                         NULL);
            gwy_container_set_string_by_name(meta, "Stop scan trigger out",
                                             recording->start_scan_trigger_out);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_STOP_SCAN_EVENT) {
            p = entry->data;
            recording->stop_scan_event = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Stop scan event",
                    g_strdup_printf("%d", recording->stop_scan_event));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_STOP_SCAN_TIME) {
            p = entry->data;
            recording->stop_scan_time = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Stop scan time",
                     g_strdup_printf("%g", recording->stop_scan_time));
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_USE_ROIS) {
            p = entry->data;
            recording->use_rois = gwy_get_guint32_le(&p);
        }
        else if (entry->entry == LSM_RECORDING_ENTRY_USE_REDUCED_MEMORY_ROIS) {
            p = entry->data;
            recording->use_reduced_memory_rois = gwy_get_guint32_le(&p);
        }
        else if (entry->entry == LSM_LASER_ENTRY_NAME) {
            recording->laser_name = g_convert(entry->data, entry->size,
                                              "UTF-8", "ISO-8859-1",
                                              NULL, NULL, NULL);
            gwy_container_set_string_by_name(meta, "Laser name",
                                             recording->laser_name);
        }
        else if (entry->entry == LSM_LASER_ENTRY_ACQUIRE) {
            p = entry->data;
            recording->laser_acquire = gwy_get_guint32_le(&p);
            gwy_container_set_string_by_name(meta, "Laser acquire",
                g_strdup_printf("%s", recording->laser_acquire ?
                                               "Enabled" : "Disabled"));
        }
        else if (entry->entry == LSM_LASER_ENTRY_POWER) {
            p = entry->data;
            recording->laser_power = gwy_get_gdouble_le(&p);
            gwy_container_set_string_by_name(meta, "Laser power",
                     g_strdup_printf("%g mW", recording->laser_power));
        }

        offset += 12 + entry->size;
    }


    if (entry) {
        g_free(entry);
    }

    return recording;
}

static LSMEntry*
lsm_read_entry (const GwyTIFF *tiff,
                guint32 offset,
                G_GNUC_UNUSED GError **error)
{
    LSMEntry *entry;
    const guchar *p;

    entry = g_new0(LSMEntry, 1);
    p = tiff->data + offset;
    entry->entry = gwy_get_guint32_le(&p);
    entry->type = gwy_get_guint32_le(&p);
    entry->size = gwy_get_guint32_le(&p);
    entry->data = (gpointer)p;

    return entry;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
