/*
 *  $Id: jspmfile.c 20677 2017-12-18 18:22:52Z yeti-dn $
 *  Copyright (C) 2014 David Necas (Yeti).
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

/* This format seems similar in spirit to jeol.c, but newer, more complicated
 * and, above all, lacking proper documentation. */

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-jeol-jspm">
 *   <comment>JEOL JSPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="II\x2a\x00">
 *       <match type="string" offset="30" value="JEOL SPM">
 *         <match type="string" offset="62" value="WinSPM "/>
 *       </match>
 *     </match>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # JEOL JSPM data (new, complex)
 * 0 string II\x2a\x00
 * >30 string JEOL\x20SPM
 * >>62 string WinSPM JEOL JSPM data,
 * >>>10 leshort x version %d
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * JEOL JSPM
 * .tif
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdarg.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "get.h"

#define MAGIC      "II\x2a\x00"
#define MAGIC_SIZE (sizeof(MAGIC) - 1)

#define JEOL_MAGIC1 "JEOL SPM"
#define JEOL_MAGIC1_SIZE (sizeof(JEOL_MAGIC1) - 1)

#define JEOL_MAGIC2 "WinSPM "
#define JEOL_MAGIC2_SIZE (sizeof(JEOL_MAGIC2) - 1)

#define Nanometer (1e-9)
#define Picometer (1e-12)
#define Nanoampere (1e-9)

enum {
    TIFF_HEADER_SIZE = 0x000a,
};

typedef enum {
    JSPM_SIGNAL_UNKNOWN = 0,
    JSPM_SIGNAL_TOPOGRAPHY,
    JSPM_SIGNAL_LOG_I,
    JSPM_SIGNAL_LIN_I,
    JSPM_SIGNAL_AUX1,
    JSPM_SIGNAL_AUX2,
    JSPM_SIGNAL_AUX3,
    JSPM_SIGNAL_EXT_VOLTAGE,
    JSPM_SIGNAL_FORCE,
    JSPM_SIGNAL_AFM,
    JSPM_SIGNAL_FRICTION,
    JSPM_SIGNAL_PHASE,
    JSPM_SIGNAL_MFM,
    JSPM_SIGNAL_ELASTICITY,
    JSPM_SIGNAL_VISCOSITY,
    JSPM_SIGNAL_FFM_FRICTION,
    JSPM_SIGNAL_SURFACE_V,
    JSPM_SIGNAL_PRESCAN,
    JSPM_SIGNAL_RMS,
    JSPM_SIGNAL_FMD,
} JSPMSignalName;

typedef enum {
    JSPM_UNIT_NANOAMPERE = 0,
    JSPM_UNIT_LOG_NANOAMPERE,
    JSPM_UNIT_VOLT,
    JSPM_UNIT_NANOMETRE,
    JSPM_UNIT_NANONEWTON,
    JSPM_UNIT_DEGREE,
    JSPM_UNIT_HERTZ,
    JSPM_UNIT_NONE = 255
} JSPMUnitType;

typedef enum {
    JSPM_MEAS_IMAGE = 1,
    JSPM_MEAS_VCO,
    JSPM_MEAS_SINGLE_SPS,
    JSPM_MEAS_SPS_MAPPING,
    JSPM_MEAS_INTERRUPT_SPS,
    JSPM_MEAS_LOCK_IN_AMP,
    JSPM_MEAS_MAP_LITOGRAPHIC_ORIG_IMAGE,
    JSPM_MEAS_TEMPERATURE_CHANGE_CONT_PROFILE,
} JSPMMeasurementType;

typedef struct {
    guint offset;
    guint len;
    /* I don't know what these numbers really mean but seems a good guess. */
    guint type;
    guint version;
} JSPMHeaderBlock;

typedef struct {
    GArray *blocks;
    /* Useful information we are actually able to extract from the blocks. */
    guint winspm_version;
    guint meas_type;
    guint signal_name;
    guint unit;
    guint xres;
    guint yres;
    gdouble xreal;
    gdouble yreal;
    guint data_offset;
    gchar *comment;
    gdouble piezo_a;
    gdouble piezo_b;
    gdouble piezo_c;
} JSPMFile;

static gboolean      module_register        (void);
static gint          jspm_detect            (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static GwyContainer* jspm_load              (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static gboolean      meas_header_seems_ok   (const gchar *buffer);
static gboolean      jspm_read_headers      (JSPMFile *jspmfile,
                                             const guchar *buffer,
                                             gsize size,
                                             GError **error);
static gboolean      read_image_header_block(JSPMFile *jspmfile,
                                             const guchar *buffer,
                                             gsize size,
                                             GError **error);
static gboolean      read_file_header_block (JSPMFile *jspmfile,
                                             const guchar *buffer,
                                             gsize size,
                                             GError **error);
static gboolean      read_piezo_header_block(JSPMFile *jspmfile,
                                             const guchar *buffer,
                                             gsize size,
                                             GError **error);
static void          jspm_add_data_field    (const JSPMFile *jspmfile,
                                             const guchar *buffer,
                                             GwyContainer *container);
static void          jspm_add_meta          (JSPMFile *jspmfile,
                                             GwyContainer *container);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    module_register,
    N_("Imports JEOL JSPM data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.2",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY2(module_info, jspmfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("jspmfile",
                           N_("JEOL JSPM data files (.tif)"),
                           (GwyFileDetectFunc)&jspm_detect,
                           (GwyFileLoadFunc)&jspm_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
jspm_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    if (only_name)
        return 0;

    if (fileinfo->buffer_len <= MAGIC_SIZE
        || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    if (fileinfo->buffer_len < 0x48
        || !meas_header_seems_ok(fileinfo->head))
        return 0;

    return 100;
}

static GwyContainer*
jspm_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    JSPMFile jspmfile;
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;

    gwy_clear(&jspmfile, 1);

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (size < 0x48) {
        err_TOO_SHORT(error);
        goto fail;
    }
    if (!meas_header_seems_ok(buffer)) {
        err_FILE_TYPE(error, "JEOL JSPM");
        goto fail;
    }

    if (!jspm_read_headers(&jspmfile, buffer, size, error))
        goto fail;

    container = gwy_container_new();
    jspm_add_data_field(&jspmfile, buffer, container);
    jspm_add_meta(&jspmfile, container);
    gwy_file_channel_import_log_add(container, 0, NULL, filename);

fail:
    g_free(jspmfile.comment);
    if (jspmfile.blocks)
        g_array_free(jspmfile.blocks, TRUE);
    gwy_file_abandon_contents(buffer, size, NULL);
    return container;
}

static gboolean
meas_header_seems_ok(const gchar *buffer)
{
    return (memcmp(buffer + 0x1e, JEOL_MAGIC1, JEOL_MAGIC1_SIZE) == 0
            && memcmp(buffer + 0x3e, JEOL_MAGIC2, JEOL_MAGIC2_SIZE) == 0);
}

static void
err_JSPM_BLOCK(GError **error, guint i)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Header block %u has invalid position or size."), i+1);
}

/* We kind of know how to walk through the physical structure of the file
 * headers.  We are at a loss as to what to do with their content though. */
static gboolean
jspm_read_headers(JSPMFile *jspmfile,
                  const guchar *buffer,
                  gsize size,
                  GError **error)
{
    JSPMHeaderBlock block;
    const guchar *p = buffer;

    block.offset = TIFF_HEADER_SIZE;
    p += block.offset;
    /* Normally next and block size are bytes 4-8 and 9-10.  But the version
     * at the begining of the first block seems extra so we have to look
     * two bytes later. */
    jspmfile->winspm_version = gwy_get_guint16_le(&p);
    gwy_debug("version: %u", jspmfile->winspm_version);

    jspmfile->blocks = g_array_new(FALSE, FALSE, sizeof(JSPMHeaderBlock));
    do {
        guint next;

        if (block.offset >= size - (4 + 4 + 2)) {
            err_JSPM_BLOCK(error, jspmfile->blocks->len);
            return FALSE;
        }

        block.type = gwy_get_guint16_le(&p);
        block.version = gwy_get_guint16_le(&p);
        next = gwy_get_guint32_le(&p);
        block.len = gwy_get_guint16_le(&p);
        gwy_debug("block #%u of type %u (v%u): 0x%x bytes at 0x%x",
                  jspmfile->blocks->len+1,
                  block.type, block.version,
                  block.len, block.offset);
        if (block.offset + block.len > size) {
            err_JSPM_BLOCK(error, jspmfile->blocks->len);
            return FALSE;
        }
        g_array_append_val(jspmfile->blocks, block);

        if (next && next < block.offset + block.len) {
            err_JSPM_BLOCK(error, jspmfile->blocks->len);
            return FALSE;
        }

        block.offset = next;
        p = buffer + next;
    } while (block.offset);

    if (!read_file_header_block(jspmfile, buffer, size, error))
        return FALSE;
    if (!read_image_header_block(jspmfile, buffer, size, error))
        return FALSE;
    if (!read_piezo_header_block(jspmfile, buffer, size, error))
        return FALSE;

    return TRUE;
}

static gboolean
read_image_header_block(JSPMFile *jspmfile,
                        const guchar *buffer, gsize size,
                        GError **error)
{
    enum {
        DATAPOS_OFFSET = 0x0a,
        RES_OFFSET = 0x18,
        REAL_OFFSET = 0x1c,
        DATATYPE_OFFSET = 0x28,
    };

    JSPMHeaderBlock *block;
    gsize expected_size;
    const guchar *p;

    if (jspmfile->blocks->len < 2) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Cannot find image header block."));
        return FALSE;
    }

    block = &g_array_index(jspmfile->blocks, JSPMHeaderBlock, 1);
    if (block->type != 10 || block->len < 0x30) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Cannot find image header block."));
        return FALSE;
    }

    p = buffer + block->offset + DATAPOS_OFFSET;
    jspmfile->data_offset = gwy_get_guint32_le(&p);
    gwy_debug("data_offset 0x%04x", jspmfile->data_offset);

    p = buffer + block->offset + RES_OFFSET;
    jspmfile->xres = gwy_get_guint16_le(&p);
    jspmfile->yres = gwy_get_guint16_le(&p);
    gwy_debug("res %ux%u", jspmfile->xres, jspmfile->yres);

    p = buffer + block->offset + REAL_OFFSET;
    jspmfile->xreal = gwy_get_gfloat_le(&p);
    jspmfile->yreal = gwy_get_gfloat_le(&p);
    gwy_debug("real %gx%g", jspmfile->xreal, jspmfile->yreal);

    /* Don't know what they really mean.  But they appear 100% correlated with
     * the data type. */
    p = buffer + block->offset + DATATYPE_OFFSET;
    jspmfile->signal_name = gwy_get_guint16_le(&p);
    jspmfile->unit = gwy_get_guint16_le(&p);
    gwy_debug("mode %u, %u", jspmfile->signal_name, jspmfile->unit);

    if (err_DIMENSION(error, jspmfile->xres)
        || err_DIMENSION(error, jspmfile->yres))
        return FALSE;

    expected_size = jspmfile->xres*jspmfile->yres*sizeof(guint32);
    if (err_SIZE_MISMATCH(error, jspmfile->data_offset + expected_size, size,
                          FALSE))
        return FALSE;

    /* Use negated positive conditions to catch NaNs */
    if (!((jspmfile->xreal = fabs(jspmfile->xreal)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        jspmfile->xreal = 1.0;
    }
    if (!((jspmfile->yreal = fabs(jspmfile->yreal)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        jspmfile->yreal = 1.0;
    }

    return TRUE;
}

static gboolean
read_file_header_block(JSPMFile *jspmfile,
                       const guchar *buffer, G_GNUC_UNUSED gsize size,
                       GError **error)
{
    enum {
        COMMENT_OFFSET = 0x66,
        MEAS_OFFSET = 0x01a6,
    };

    JSPMHeaderBlock *block;
    const guchar *p;

    if (jspmfile->blocks->len < 1) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Cannot find image header block."));
        return FALSE;
    }

    block = &g_array_index(jspmfile->blocks, JSPMHeaderBlock, 0);
    if (block->type != 1 || block->len < 0x70) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Cannot find image header block."));
        return FALSE;
    }

    p = buffer + block->offset + COMMENT_OFFSET;
    if (*p) {
        gchar *comment = g_strndup(p, block->len - COMMENT_OFFSET);
        jspmfile->comment = g_convert(comment, -1,
                                      "iso-8859-1", "utf-8", NULL, NULL, NULL);
        g_free(comment);
        if (jspmfile->comment)
            g_strdelimit(jspmfile->comment, "\n\r", ' ');
        gwy_debug("comment %s", jspmfile->comment);
    }

    if (block->len >= MEAS_OFFSET + sizeof(gint16)) {
        p = buffer + block->offset + MEAS_OFFSET;
        jspmfile->meas_type = gwy_get_guint16_le(&p);
        gwy_debug("meas_type %u", jspmfile->meas_type);
    }

    return TRUE;
}

static gboolean
read_piezo_header_block(JSPMFile *jspmfile,
                        const guchar *buffer, G_GNUC_UNUSED gsize size,
                        GError **error)
{
    enum {
        ABC_OFFSET = 0x146,
    };

    JSPMHeaderBlock *block = NULL;
    const guchar *p;
    guint i;

    for (i = 0; i < jspmfile->blocks->len; i++) {
        block = &g_array_index(jspmfile->blocks, JSPMHeaderBlock, i);
        if (block->type == 30 && block->len >= ABC_OFFSET + 3*sizeof(gfloat))
            break;
        block = NULL;
    }
    if (!block) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Cannot find piezo header block."));
        return FALSE;
    }

    p = buffer + block->offset + ABC_OFFSET;
    jspmfile->piezo_a = gwy_get_gfloat_le(&p);
    jspmfile->piezo_b = gwy_get_gfloat_le(&p);
    jspmfile->piezo_c = gwy_get_gfloat_le(&p);
    /* XXX: According to JEOL info, there should be a topography conversion
     * formula of the form ax²+bx+c, x being the raw value (at present a=c=0).
     * But I can't get anything reasonable this way. */
    gwy_debug("piezo a=%g, b=%g, c=%g",
              jspmfile->piezo_a, jspmfile->piezo_b, jspmfile->piezo_c);

    return TRUE;
}

static void
jspm_add_data_field(const JSPMFile *jspmfile,
                    const guchar *buffer,
                    GwyContainer *container)

{
    static const GwyEnum signal_names[] = {
        { "Topography",       JSPM_SIGNAL_TOPOGRAPHY,   },
        { "Log Current (nA)", JSPM_SIGNAL_LOG_I,        },
        { "Lin Current",      JSPM_SIGNAL_LIN_I,        },
        { "AUX1",             JSPM_SIGNAL_AUX1,         },
        { "AUX2",             JSPM_SIGNAL_AUX2,         },
        { "AUX3",             JSPM_SIGNAL_AUX3,         },
        { "EXT (Voltage)",    JSPM_SIGNAL_EXT_VOLTAGE,  },
        { "Force",            JSPM_SIGNAL_FORCE,        },
        { "AFM",              JSPM_SIGNAL_AFM,          },
        { "Friction",         JSPM_SIGNAL_FRICTION,     },
        { "Phase",            JSPM_SIGNAL_PHASE,        },
        { "MFM",              JSPM_SIGNAL_MFM,          },
        { "Elasticity",       JSPM_SIGNAL_ELASTICITY,   },
        { "Viscosity",        JSPM_SIGNAL_VISCOSITY,    },
        { "FFM_Friction",     JSPM_SIGNAL_FFM_FRICTION, },
        { "Surface V",        JSPM_SIGNAL_SURFACE_V,    },
        { "Prescan",          JSPM_SIGNAL_PRESCAN,      },
        { "RMS",              JSPM_SIGNAL_RMS,          },
        { "FMD",              JSPM_SIGNAL_FMD,          },
    };

    static const GwyEnum unit_types[] = {
        { "nA",  JSPM_UNIT_NANOAMPERE,     },
        { "",    JSPM_UNIT_LOG_NANOAMPERE, }, /* Can't do log(I) properly. */
        { "V",   JSPM_UNIT_VOLT,           },
        { "nm",  JSPM_UNIT_NANOMETRE,      },
        { "nN",  JSPM_UNIT_NANONEWTON,     },
        { "deg", JSPM_UNIT_DEGREE,         },
        { "Hz",  JSPM_UNIT_HERTZ,          },
        { "",    JSPM_UNIT_NONE,           },
    };

    GwyDataField *dfield;
    GwySIUnit *zunit;
    gdouble q = 1.0, z0 = 0.0;
    gint power10 = 0;
    const gchar *unitstr, *title;

    dfield = gwy_data_field_new(jspmfile->xres, jspmfile->yres,
                                Nanometer*jspmfile->xreal,
                                Nanometer*jspmfile->yreal,
                                FALSE);

    title = gwy_enum_to_string(jspmfile->signal_name,
                               signal_names, G_N_ELEMENTS(signal_names));
    if (!title || !strlen(title))
        title = "Raw data";

    unitstr = gwy_enum_to_string(jspmfile->unit,
                                 unit_types, G_N_ELEMENTS(unit_types));

    zunit = gwy_data_field_get_si_unit_z(dfield);
    gwy_si_unit_set_from_string_parse(zunit, unitstr, &power10);

#if 0
    /* This is what we apparently should be doing for topography but it does
     * not work. */
    q = 1e-9 * jspmfile->piezo_b * 400.0/2097151.0;
    /* And this for voltages; this one kind of works. */
    q = 20.0/65535.0;
    z0 = -10.0;
#endif

    if (jspmfile->unit == JSPM_UNIT_NANOMETRE) {
        q = Picometer;   /* (sic) */
    }
    else if (jspmfile->unit == JSPM_UNIT_NANOAMPERE) {
        /* Probably not right */
        q = 1.0/32767.0 * Nanoampere;
        z0 = -1.5 * Nanoampere;
    }
    else if (jspmfile->unit == JSPM_UNIT_DEGREE) {
        /* Phase FIXME the factor might be good, the offset is rubbish. */
        q = 1000.0/262143.0;
        z0 = -40000.0*q;
    }
    else if (jspmfile->unit == JSPM_UNIT_VOLT) {
        /* Voltage */
        q = 10.0/32767.0;
        z0 = -10.0;
    }
    else {
        g_warning("Unknown data type %u.%u, importing as raw.",
                  jspmfile->signal_name, jspmfile->unit);
        q = 1.0/32767.0 * pow10(power10);
    }

    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");

    gwy_convert_raw_data(buffer + jspmfile->data_offset,
                         jspmfile->xres*jspmfile->yres, 1,
                         GWY_RAW_DATA_UINT32, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield), q, z0);
    gwy_data_field_invalidate(dfield);

    gwy_container_set_object_by_name(container, "/0/data", dfield);
    g_object_unref(dfield);

    gwy_container_set_string_by_name(container, "/0/data/title",
                                     g_strdup(title));
}

static void
format_meta(GwyContainer *meta,
            const gchar *name,
            const gchar *format,
            ...)
{
    gchar *s;
    va_list ap;

    va_start(ap, format);
    s = g_strdup_vprintf(format, ap);
    va_end(ap);
    gwy_container_set_string_by_name(meta, name, s);
}

static void
jspm_add_meta(JSPMFile *jspmfile, GwyContainer *container)
{
    GwyContainer *meta = gwy_container_new();

    format_meta(meta, "WinSPM Version", "%.2f", jspmfile->winspm_version/100.0);
    if (jspmfile->comment)
        format_meta(meta, "Comment", "%s", jspmfile->comment);

    gwy_container_set_object_by_name(container, "/0/meta", meta);
    g_object_unref(meta);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
