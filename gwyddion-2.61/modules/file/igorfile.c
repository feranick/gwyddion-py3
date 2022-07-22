/*
 *  $Id: igorfile.c 24775 2022-04-26 15:03:27Z yeti-dn $
 *  Copyright (C) 2009-2019 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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
 * <mime-type type="application/x-igor-binary-wave">
 *   <comment>Igor binary wave</comment>
 *   <glob pattern="*.ibw"/>
 *   <glob pattern="*.IBW"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * WaveMetrics IGOR binary wave v5
 * .ibw
 * Read Export
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"
#include "get.h"

#define EXTENSION ".ibw"

enum {
    MAXDIMS = 4,
    MAX_UNIT_CHARS = 3,
    MAX_WAVE_NAME2 = 18,
    MAX_WAVE_NAME5 = 31,
    MIN_FILE_SIZE = 8 + 110 + 16,
    HEADER_SIZE1 = 8,
    HEADER_SIZE2 = 16,
    HEADER_SIZE3 = 20,
    HEADER_SIZE5 = 64,
    WAVE_SIZE2 = 110,
    WAVE_SIZE5 = 320,
    ASYLUM_PALETTE_SIZE = 3*256,
};

typedef enum {
    IGOR_BASE         = 0,
    IGOR_ASYLUM_MPF3D = 1,
    IGOR_ASYLUM_FORCE = 2,
} IgorFileProducerVariant;

/* The value is also the number of dimensions, i.e. one less than the number of non-zero items in n_dims[]. */
typedef enum {
    IGOR_DATA_UNKNOWN = 0,
    IGOR_DATA_CURVE   = 1,
    IGOR_DATA_IMAGE   = 2,
    IGOR_DATA_VOLUME  = 3,
} IgorDataShape;

typedef enum {
    IGOR_TEXT     = 0x00,
    IGOR_COMPLEX  = 0x01,   /* Flag */
    IGOR_SINGLE   = 0x02,
    IGOR_DOUBLE   = 0x04,
    IGOR_INT8     = 0x08,
    IGOR_INT16    = 0x10,
    IGOR_INT32    = 0x20,
    IGOR_UNSIGNED = 0x40,   /* Flag for integers */
} IgorDataType;

/* The header fields we read, they are stored differently in different versions */
typedef struct {
    gint version;    /* Version number */
    gint checksum;   /* Checksum of this header and the wave header */
    guint wfm_size;     /* Size of the WaveHeader5 data structure plus the wave data. */
    guint formula_size; /* The size of the dependency formula, if any. */
    guint note_size;    /* The size of the note text. */
    guint pict_size;    /* Reserved (0). */
    guint data_e_units_size; /* The size of optional extended data units. */
    guint dim_e_units_size[MAXDIMS]; /* The size of optional extended dimension units. */
    guint dim_labels_size[MAXDIMS]; /* The size of optional dimension labels. */
    guint indices_size;   /* The size of string indicies if this is a text wave. */
    guint options_size1;  /* Reserved (0). */
    guint options_size2;  /* Reserved (0). */
} IgorBinHeader;

typedef struct {
    guint next;           /* Pointer, ignore. */
    guint creation_date;  /* DateTime of creation. */
    guint mod_date;       /* DateTime of last modification. */
    guint npts;           /* Total number of points (multiply dimensions up to first zero). */
    IgorDataType type;
    guint lock;           /* Reserved (0). */
    gchar whpad1[6];      /* Reserved (0). */
    guint wh_version;     /* Reserved (1). */
    gchar bname[MAX_WAVE_NAME5+1];  /* Wave name, nul-terminated. */
    guint whpad2;         /* Reserved (0). */
    guint dfolder;        /* Pointer, ignore. */

    /* Dimensioning info. [0] == rows, [1] == cols etc */
    guint n_dim[MAXDIMS];   /* Number of items in a dimension, 0 means no data. */
    gdouble sfA[MAXDIMS];   /* Index value for element e of dimension d = sfA[d]*e + sfB[d]. */
    gdouble sfB[MAXDIMS];

    /* SI units */
    gchar data_units[MAX_UNIT_CHARS+1];  /* Natural data units, nul if none. */
    gchar dim_units[MAXDIMS][MAX_UNIT_CHARS+1];   /* Natural dimension units, nul if none. */

    gboolean fs_valid;           /* TRUE if full scale values have meaning. */
    guint whpad3;                /* Reserved (0). */
    gdouble top_full_scale;      /* The instrument max full scale value. */
    gdouble bot_full_scale;      /* The instrument min full scale value. */

    /* There is more stuff.  But it's either marked reserved, unused or private to Igor.  Do not bother with that...
     */
} IgorWaveHeader5;

typedef struct {
    gchar *name;
    const gchar *units;
} AsylumChannelInfo;

typedef struct {
    guint16 (*get_guint16)(const guchar **p);
    gint16 (*get_gint16)(const guchar **p);
    guint32 (*get_guint32)(const guchar **p);
    gint32 (*get_gint32)(const guchar **p);
    gfloat (*get_gfloat)(const guchar **p);
    gdouble (*get_gdouble)(const guchar **p);
    IgorFileProducerVariant variant;
    guint wave_header_size;
    guint headers_size;
    guint type_size;
    gboolean lsb;
    IgorBinHeader header;
    IgorWaveHeader5 wave5;
    IgorDataShape data_shape;
    guint nchannels;
    /* Maybe only in Asylum Research files.  Titles dynamically allocated. */
    GPtrArray *titles;
    /* Only in Asylum Research files... */
    GHashTable *meta;
    AsylumChannelInfo *channel_info;
    IgorDataShape asylum_shape;
    /* Processing data */
    const gchar **ignore_prefixes;
    GwyContainer *channelmeta;
} IgorFile;

static gboolean      module_register         (void);
static gint          igor_detect             (const GwyFileDetectInfo *fileinfo,
                                              gboolean only_name);
static GwyContainer* igor_load               (const gchar *filename,
                                              GwyRunType mode,
                                              GError **error);
static GwyContainer* igor_load_single        (const gchar *filename,
                                              GError **error,
                                              gint *ngraphs,
                                              gint *nfields,
                                              gint *nbricks);
static GwyContainer* igor_read_images        (IgorFile *igorfile,
                                              const gchar *buffer,
                                              const gchar *filename,
                                              gint *nfields);
static GwyContainer* igor_read_volumes       (IgorFile *igorfile,
                                              const gchar *buffer,
                                              const gchar *filename,
                                              gint *nbricks);
static GwyContainer* igor_read_curves        (IgorFile *igorfile,
                                              const gchar *buffer,
                                              const gchar *filename,
                                              gint *nfields);
static gboolean      igor_export             (GwyContainer *data,
                                              const gchar *filename,
                                              GwyRunType mode,
                                              GError **error);
static guint         igor_read_headers       (IgorFile *igorfile,
                                              const guchar *buffer,
                                              gsize size,
                                              gboolean check_only,
                                              GError **error);
static void          read_asylum_footer      (IgorFile *igorfile,
                                              const guchar *buffer,
                                              gsize size);
static guint         igor_checksum           (gconstpointer buffer,
                                              gsize size,
                                              gboolean lsb);
static guint         igor_data_type_size     (IgorDataType type);
static GwyDataField* igor_read_data_field    (const IgorFile *igorfile,
                                              const guchar *buffer,
                                              guint i,
                                              const gchar *zunits,
                                              gboolean is_imaginary);
static GwyBrick*     igor_read_brick         (const IgorFile *igorfile,
                                              const guchar *buffer,
                                              guint i,
                                              const gchar *wunits,
                                              gboolean is_imaginary);
static GwyDataLine*  igor_read_data_line     (const IgorFile *igorfile,
                                              const guchar *buffer,
                                              guint i,
                                              const gchar *yunits,
                                              gboolean is_imaginary);
static GwyContainer* igor_get_metadata       (IgorFile *igorfile,
                                              guint id);
static GPtrArray*    read_channel_labels     (const gchar *p,
                                              guint n,
                                              guint l);
static gchar*        canonicalize_title      (const gchar *title);
static const gchar*  channel_title_to_units  (const gchar *title);
static gint*         find_compatible_channels(GwyContainer *container,
                                              GwyDataField *dfield,
                                              guint *n);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Igor binary waves (.ibw)."),
    "Yeti <yeti@gwyddion.net>",
    "0.13",
    "David Nečas (Yeti)",
    "2009",
};

GWY_MODULE_QUERY2(module_info, igorfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("igorfile",
                           N_("Igor binary waves (.ibw)"),
                           (GwyFileDetectFunc)&igor_detect,
                           (GwyFileLoadFunc)&igor_load,
                           NULL,
                           (GwyFileSaveFunc)&igor_export);

    return TRUE;
}

static gint
igor_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 10 : 0;

    if (fileinfo->buffer_len >= MIN_FILE_SIZE) {
       IgorFile igorfile;

       if (igor_read_headers(&igorfile, fileinfo->head, fileinfo->buffer_len, TRUE, NULL))
           return 100;
    }

    return 0;
}

static GwyContainer*
igor_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    GwyContainer *container;
    gint ngraphs, nfields, nbricks;

    container = igor_load_single(filename, error, &ngraphs, &nfields, &nbricks);
    if (!container)
        return NULL;

    if (!nbricks) {
        gwy_debug("not volume data, just returning the single file content");
        return container;
    }

    gwy_debug("volume data, trying to merge with other files");
    /* TODO */
    return container;
}

static GwyContainer*
igor_load_single(const gchar *filename, GError **error,
                 gint *ngraphs, gint *nfields, gint *nbricks)
{
    GwyContainer *container = NULL;
    GwyTextHeaderParser parser;
    IgorFile igorfile;
    IgorWaveHeader5 *wave5;
    GError *err = NULL;
    guchar *p, *buffer = NULL;
    gsize expected_size, n, size, remaining = 0;
    gchar *note = NULL;
    const gchar *value;
    GString *str = NULL;
    guint i, nlabels;

    *ngraphs = *nfields = *nbricks = 0;
    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    gwy_clear(&igorfile, 1);
    if (!igor_read_headers(&igorfile, buffer, size, FALSE, error))
        goto fail;

    /* Only accept v5 files because older do not support 2D data */
    if (igorfile.header.version != 5) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Format version is %d.  Only version 5 is supported."), igorfile.header.version);
        goto fail;
    }

    /* Detect Asylum research files, leave it at generic if not detected.  Unfortunately, some Asylum Research files
     * seem to have empty producer variant too. */
    gwy_debug("variantstr: <%.5s>", buffer + size-5);
    if (memcmp(buffer + size-5, "MFP3D", 5) == 0)
        igorfile.variant = IGOR_ASYLUM_MPF3D;
    else if (memcmp(buffer + size-5, "Force", 5) == 0)
        igorfile.variant = IGOR_ASYLUM_FORCE;
    gwy_debug("producer variant %u", igorfile.variant);

    /* Figure out the data shape and dimensions. */
    wave5 = &igorfile.wave5;
    if (wave5->n_dim[3]) {
        /* We could perhaps deal with multichannel volume data files, but the channels seem to be split into multiple
         * files in such case. */
        if (wave5->n_dim[3] != 1) {
            err_UNSUPPORTED(error, "n_dim[3]");
            goto fail;
        }
        igorfile.data_shape = IGOR_DATA_VOLUME;
    }
    else if (wave5->n_dim[2])
        igorfile.data_shape = IGOR_DATA_IMAGE;
    else if (wave5->n_dim[1])
        igorfile.data_shape = IGOR_DATA_CURVE;
    else {
        err_UNSUPPORTED(error, "n_dim[1]");
        goto fail;
    }

    /* XXX: Fix some weird data with zero n_dim[2] which actually seem to be images. */
    if (igorfile.data_shape == IGOR_DATA_CURVE && wave5->n_dim[1] == wave5->n_dim[0]) {
        g_warning("Fixing data with zero n_dim[2] to a single image.");
        igorfile.data_shape = IGOR_DATA_IMAGE;
        wave5->n_dim[igorfile.data_shape] = 1;
    }

    n = 1;
    for (i = 0; i <= igorfile.data_shape; i++) {
        if (err_DIMENSION(error, wave5->n_dim[i]))
            goto fail;
        n *= wave5->n_dim[i];
    }
    igorfile.nchannels = wave5->n_dim[igorfile.data_shape];

    igorfile.type_size = igor_data_type_size(wave5->type);
    if (!igorfile.type_size) {
        err_DATA_TYPE(error, wave5->type);
        goto fail;
    }

    str = g_string_new(NULL);
    if (wave5->npts != n) {
        g_string_printf(str, "%d", wave5->n_dim[0]);
        for (i = 1; wave5->n_dim[i]; i++)
            g_string_append_printf(str, "×%d", wave5->n_dim[i]);
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Number of data points %u does not match resolutions %s."), wave5->npts, str->str);
        goto fail;
    }

    if (igorfile.header.wfm_size <= igorfile.wave_header_size) {
        err_INVALID(error, "wfmSize");
        goto fail;
    }

    expected_size = igorfile.header.wfm_size - igorfile.wave_header_size;
    if (expected_size != wave5->npts*igorfile.type_size) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Data size %u does not match the number of data points %u×%u."),
                    (guint)expected_size, wave5->npts, igorfile.type_size);
    }

    if (err_SIZE_MISMATCH(error, expected_size + igorfile.headers_size, size, FALSE))
        goto fail;

    p = buffer + igorfile.headers_size + expected_size;
    gwy_debug("remaining data size: %lu", (gulong)(size - (p - buffer)));

    p += igorfile.header.formula_size;
    if (igorfile.header.note_size && (p - buffer) + igorfile.header.note_size <= size) {
        if (igorfile.variant == IGOR_BASE)
            g_warning("Trying to parse the note for apparently base producer variant as Asylum Research note.");
        note = g_strndup((const gchar*)p, size);
        gwy_clear(&parser, 1);
        parser.key_value_separator = ":";
        igorfile.meta = gwy_text_header_parse(note, &parser, NULL, NULL);
    }
    p += igorfile.header.note_size;

    /* FIXME: Support extended units for non-Asylum files! */
    p += igorfile.header.data_e_units_size;
    for (i = 0; i < MAXDIMS; i++) {
        gwy_debug("dim_e_units[%d] = <%.*s>", i, igorfile.header.dim_e_units_size[i], p);
        p += igorfile.header.dim_e_units_size[i];
    }

    /* Skip labels of lower dimensions, we don't know what to do with them. */
    for (i = 0; i < igorfile.data_shape; i++) {
        gwy_debug("dim_labels[%d] = <%.*s>", i, igorfile.header.dim_labels_size[i], p);
        p += igorfile.header.dim_labels_size[i];
    }

    /* FIXME: The labels are mandatory only in Asylum Research files. */
    nlabels = igorfile.header.dim_labels_size[igorfile.data_shape]/(MAX_WAVE_NAME5+1);
    expected_size = (MAX_WAVE_NAME5 + 1)*nlabels;
    if ((p - buffer) + expected_size > size) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Cannot read channel labels."));
        goto fail;
    }
    igorfile.titles = read_channel_labels(p, igorfile.nchannels+1, nlabels);
    for (i = igorfile.data_shape; i < MAXDIMS; i++)
        p += igorfile.header.dim_labels_size[i];

    remaining = size - (p - buffer);
    gwy_debug("remaining %lu bytes", (gulong)remaining);
    if (remaining > ASYLUM_PALETTE_SIZE)
        read_asylum_footer(&igorfile, p + ASYLUM_PALETTE_SIZE, remaining - ASYLUM_PALETTE_SIZE);

    if (igorfile.meta) {
        igorfile.channel_info = g_new0(AsylumChannelInfo, igorfile.nchannels);
        for (i = 0; i < igorfile.nchannels; i++) {
            AsylumChannelInfo *chinfo = igorfile.channel_info + i;
            const gchar *title = g_ptr_array_index(igorfile.titles, i+1);

            if (title) {
                chinfo->name = canonicalize_title(title);
                g_string_printf(str, "%sUnit", chinfo->name);
                value = g_hash_table_lookup(igorfile.meta, str->str);
                chinfo->units = value ? value : channel_title_to_units(chinfo->name);
            }
        }
    }

    /* XXX: We also have igorfile.asylum_shape.  But only sometimes so it is not clear how to use it. */
    if (igorfile.data_shape == IGOR_DATA_IMAGE)
        container = igor_read_images(&igorfile, buffer, filename, nfields);
    else if (igorfile.data_shape == IGOR_DATA_CURVE)
        container = igor_read_curves(&igorfile, buffer, filename, ngraphs);
    else if (igorfile.data_shape == IGOR_DATA_VOLUME)
        container = igor_read_volumes(&igorfile, buffer, filename, nbricks);
    else {
        g_assert_not_reached();
    }

fail:
    gwy_file_abandon_contents(buffer, size, NULL);
    g_free(note);
    if (str)
        g_string_free(str, TRUE);
    if (igorfile.channel_info) {
        for (i = 0; i < igorfile.nchannels; i++)
            g_free(igorfile.channel_info[i].name);
        g_free(igorfile.channel_info);
    }
    if (igorfile.meta)
        g_hash_table_destroy(igorfile.meta);
    if (igorfile.titles) {
        g_ptr_array_foreach(igorfile.titles, (GFunc)g_free, NULL);
        g_ptr_array_free(igorfile.titles, TRUE);
    }

    return container;
}

static GwyContainer*
igor_read_images(IgorFile *igorfile, const gchar *buffer, const gchar *filename, gint *nfields)
{
    IgorWaveHeader5 *wave5 = &igorfile->wave5;
    GwyDataField *dfield = NULL, *maskfield = NULL;
    GwyContainer *container, *meta = NULL;
    gint i, chid;

    container = gwy_container_new();
    for (i = chid = 0; i < igorfile->nchannels; i++, chid++) {
        const gchar *title = g_ptr_array_index(igorfile->titles, i+1);
        const gchar *zunits = NULL;

        if (igorfile->channel_info) {
            AsylumChannelInfo *chinfo = igorfile->channel_info + i;
            zunits = chinfo->units;
            meta = igor_get_metadata(igorfile, i + 1);
        }
        dfield = igor_read_data_field(igorfile, buffer, i, zunits, FALSE);
        maskfield = gwy_app_channel_mask_of_nans(dfield, TRUE);
        gwy_container_set_object(container, gwy_app_get_data_key_for_id(chid), dfield);
        g_object_unref(dfield);
        if (maskfield)
            gwy_container_set_object(container, gwy_app_get_mask_key_for_id(chid), maskfield);
        if (meta)
            gwy_container_set_object(container, gwy_app_get_data_meta_key_for_id(chid), meta);

        if (title)
            gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(chid), title);
        gwy_app_channel_title_fall_back(container, chid);

        if (wave5->type & IGOR_COMPLEX) {
            chid++;
            dfield = igor_read_data_field(igorfile, buffer, i, zunits, TRUE);
            gwy_container_set_object(container, gwy_app_get_data_key_for_id(chid), dfield);
            g_object_unref(dfield);
            if (meta) {
                /* container still holds a reference */
                g_object_unref(meta);
                meta = gwy_container_duplicate(meta);
                gwy_container_set_object(container, gwy_app_get_data_meta_key_for_id(chid), meta);
            }
            if (maskfield) {
                /* container still holds a reference */
                g_object_unref(maskfield);
                maskfield = gwy_data_field_duplicate(maskfield);
                gwy_container_set_object(container, gwy_app_get_mask_key_for_id(chid), maskfield);
            }

            if (title)
                gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(chid), title);
            gwy_app_channel_title_fall_back(container, chid);
        }
        GWY_OBJECT_UNREF(meta);
        GWY_OBJECT_UNREF(maskfield);

        gwy_file_channel_import_log_add(container, chid, NULL, filename);
    }
    *nfields = chid;

    return container;
}

static GwyContainer*
igor_read_volumes(IgorFile *igorfile, const gchar *buffer, const gchar *filename, gint *nbricks)
{
    IgorWaveHeader5 *wave5 = &igorfile->wave5;
    GwyBrick *brick;
    GwyContainer *container, *meta = NULL;
    gint i, chid;

    container = gwy_container_new();
    for (i = chid = 0; i < igorfile->nchannels; i++, chid++) {
        const gchar *title = g_ptr_array_index(igorfile->titles, i+1);
        const gchar *wunits = NULL;

        if (igorfile->channel_info) {
            AsylumChannelInfo *chinfo = igorfile->channel_info + i;
            wunits = chinfo->units;
            meta = igor_get_metadata(igorfile, i + 1);
        }
        brick = igor_read_brick(igorfile, buffer, i, wunits, FALSE);
        gwy_container_set_object(container, gwy_app_get_brick_key_for_id(chid), brick);
        g_object_unref(brick);
        if (meta)
            gwy_container_set_object(container, gwy_app_get_brick_meta_key_for_id(chid), meta);

        if (title)
            gwy_container_set_const_string(container, gwy_app_get_brick_title_key_for_id(chid), title);
        //gwy_app_channel_title_fall_back(container, chid);

        if (wave5->type & IGOR_COMPLEX) {
            chid++;
            brick = igor_read_brick(igorfile, buffer, i, wunits, TRUE);
            gwy_container_set_object(container, gwy_app_get_brick_key_for_id(chid), brick);
            g_object_unref(brick);
            if (meta) {
                /* container still holds a reference */
                g_object_unref(meta);
                meta = gwy_container_duplicate(meta);
                gwy_container_set_object(container, gwy_app_get_brick_meta_key_for_id(chid), meta);
            }

            if (title)
                gwy_container_set_const_string(container, gwy_app_get_brick_title_key_for_id(chid), title);
            //gwy_app_channel_title_fall_back(container, chid);
        }
        GWY_OBJECT_UNREF(meta);

        gwy_file_volume_import_log_add(container, chid, NULL, filename);
    }
    *nbricks = chid;

    return container;
}

static GwyContainer*
igor_read_curves(IgorFile *igorfile, const gchar *buffer, const gchar *filename, gint *ngraphs)
{
    IgorWaveHeader5 *wave5 = &igorfile->wave5;
    GwyDataLine *dline;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwyContainer *container;
    gint i, chid;

    container = gwy_container_new();

    for (i = chid = 0; i < igorfile->nchannels; i++, chid++) {
        const gchar *title = g_ptr_array_index(igorfile->titles, i+1);
        gchar *s;

        /* XXX: For images we use channel info gathered from the note.  For graphs the units seem to be either stored
         * normally in wave5 or who knows where. */
        dline = igor_read_data_line(igorfile, buffer, i, NULL, FALSE);
        gmodel = gwy_graph_model_new();
        gwy_graph_model_set_units_from_data_line(gmodel, dline);
        g_object_set(gmodel, "title", title, NULL);
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, dline, 0, 0);
        g_object_unref(dline);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(0),
                     "description", title,
                     NULL);
        if (wave5->type & IGOR_COMPLEX) {
            s = g_strconcat(title, " ", "(Re)", NULL);
            g_object_set(gcmodel, "description", s, NULL);
            g_free(s);
        }

        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);

        if (wave5->type & IGOR_COMPLEX) {
            dline = igor_read_data_line(igorfile, buffer, i, NULL, TRUE);
            gcmodel = gwy_graph_curve_model_new();
            gwy_graph_curve_model_set_data_from_dataline(gcmodel, dline, 0, 0);
            g_object_unref(dline);
            s = g_strconcat(title, " ", "(Im)", NULL);
            g_object_set(gcmodel,
                         "mode", GWY_GRAPH_CURVE_LINE,
                         "color", gwy_graph_get_preset_color(1),
                         "description", s,
                         NULL);
            g_free(s);
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);
        }

        gwy_container_set_object(container, gwy_app_get_graph_key_for_id(chid+1), gmodel);
        g_object_unref(gmodel);
        gwy_file_channel_import_log_add(container, chid+1, NULL, filename);
    }

    *ngraphs = chid;

    return container;
}

/* Reads @header and initializes @reader for the correct byte order.  Returns the number of bytes read, 0 on error. */
static guint
igor_read_headers(IgorFile *igorfile,
                  const guchar *buffer,
                  gsize size,
                  gboolean check_only,
                  GError **error)
{
    IgorBinHeader *header;
    gsize headers_size;
    guint version, chksum, i;
    const guchar *p = buffer;

    if (size < HEADER_SIZE1) {
        err_TOO_SHORT(error);
        return 0;
    }

    /* The lower byte of version is nonzero.  Use it to detect endianess. */
    version = gwy_get_guint16_le(&p);
    gwy_debug("raw version: 0x%04x", version);

    /* Keep the rejection code path fast by performing version sanity check as the very first thing. */
    gwy_clear(igorfile, 1);
    if ((igorfile->lsb = (version & 0xff))) {
        gwy_debug("little endian");
    }
    else {
        gwy_debug("big endian");
        version /= 0x100;
    }

    /* Check if version is known and the buffer size */
    if (version == 1)
        headers_size = HEADER_SIZE1 + WAVE_SIZE2;
    else if (version == 2)
        headers_size = HEADER_SIZE2 + WAVE_SIZE2;
    else if (version == 3)
        headers_size = HEADER_SIZE3 + WAVE_SIZE2;
    else if (version == 5)
        headers_size = HEADER_SIZE5 + WAVE_SIZE5;
    else {
        err_FILE_TYPE(error, "IGOR Pro");
        return 0;
    }
    gwy_debug("expected headers_size %lu", (gulong)headers_size);
    if (size < headers_size) {
        err_TOO_SHORT(error);
        return 0;
    }

    /* Check the checksum */
    chksum = igor_checksum(buffer, headers_size, igorfile->lsb);
    gwy_debug("checksum %u", chksum);
    if (chksum) {
        err_FILE_TYPE(error, "IGOR Pro");
        return 0;
    }

    /* If only detection is required, we can stop now. */
    if (check_only)
        return headers_size;

    /* If the checksum is correct the file is likely IGOR file and we can start the expensive actions. */
    header = &igorfile->header;
    header->version = version;
    igorfile->headers_size = headers_size;
    gwy_debug("format version: %u", header->version);

    if (igorfile->lsb) {
        igorfile->get_guint16 = gwy_get_guint16_le;
        igorfile->get_gint16 = gwy_get_gint16_le;
        igorfile->get_guint32 = gwy_get_guint32_le;
        igorfile->get_gint32 = gwy_get_gint32_le;
        igorfile->get_gfloat = gwy_get_gfloat_le;
        igorfile->get_gdouble = gwy_get_gdouble_le;
    }
    else {
        igorfile->get_guint16 = gwy_get_guint16_be;
        igorfile->get_gint16 = gwy_get_gint16_be;
        igorfile->get_guint32 = gwy_get_guint32_be;
        igorfile->get_gint32 = gwy_get_gint32_be;
        igorfile->get_gfloat = gwy_get_gfloat_be;
        igorfile->get_gdouble = gwy_get_gdouble_be;
    }

    /* Read the rest of the binary header */
    if (header->version == 1) {
        igorfile->wave_header_size = 110;
        header->wfm_size = igorfile->get_guint32(&p);
        header->checksum = igorfile->get_guint16(&p);
    }
    else if (header->version == 2) {
        igorfile->wave_header_size = 110;
        header->wfm_size = igorfile->get_guint32(&p);
        header->note_size = igorfile->get_guint32(&p);
        header->pict_size = igorfile->get_guint32(&p);
        header->checksum = igorfile->get_guint16(&p);
    }
    else if (header->version == 3) {
        igorfile->wave_header_size = 110;
        header->wfm_size = igorfile->get_guint32(&p);
        header->note_size = igorfile->get_guint32(&p);
        header->formula_size = igorfile->get_guint32(&p);
        header->pict_size = igorfile->get_guint32(&p);
        header->checksum = igorfile->get_guint16(&p);
    }
    else if (header->version == 5) {
        igorfile->wave_header_size = 320;
        header->checksum = igorfile->get_guint16(&p);
        header->wfm_size = igorfile->get_guint32(&p);
        header->formula_size = igorfile->get_guint32(&p);
        gwy_debug("formula_size: %u", header->formula_size);
        header->note_size = igorfile->get_guint32(&p);
        gwy_debug("note_size: %u", header->note_size);
        header->data_e_units_size = igorfile->get_guint32(&p);
        gwy_debug("data_e_units_size: %u", header->data_e_units_size);
        for (i = 0; i < MAXDIMS; i++) {
            header->dim_e_units_size[i] = igorfile->get_guint32(&p);
            gwy_debug("dim_e_units_size[%u]: %u", i, header->dim_e_units_size[i]);
        }
        for (i = 0; i < MAXDIMS; i++) {
            header->dim_labels_size[i] = igorfile->get_guint32(&p);
            gwy_debug("dim_labels_size[%u]: %u", i, header->dim_labels_size[i]);
        }
        header->indices_size = igorfile->get_guint32(&p);
        header->options_size1 = igorfile->get_guint32(&p);
        header->options_size2 = igorfile->get_guint32(&p);
    }
    else {
        g_assert_not_reached();
    }

    gwy_debug("wfm_size: %u", header->wfm_size);

    /* Read the wave header */
    if (version == 5) {
        IgorWaveHeader5 *wave5 = &igorfile->wave5;

        wave5->next = igorfile->get_guint32(&p);
        wave5->creation_date = igorfile->get_guint32(&p);
        wave5->mod_date = igorfile->get_guint32(&p);
        wave5->npts = igorfile->get_guint32(&p);
        wave5->type = igorfile->get_guint16(&p);
        gwy_debug("type: %u, npts: %u", wave5->type, wave5->npts);
        wave5->lock = igorfile->get_guint16(&p);
        get_CHARARRAY(wave5->whpad1, &p);
        wave5->wh_version = igorfile->get_guint16(&p);
        get_CHARARRAY0(wave5->bname, &p);
        gwy_debug("bname %s", wave5->bname);
        wave5->whpad2 = igorfile->get_guint32(&p);
        wave5->dfolder = igorfile->get_guint32(&p);
        for (i = 0; i < MAXDIMS; i++) {
            wave5->n_dim[i] = igorfile->get_guint32(&p);
            gwy_debug("n_dim[%u]: %u", i, wave5->n_dim[i]);
        }
        for (i = 0; i < MAXDIMS; i++)
            wave5->sfA[i] = igorfile->get_gdouble(&p);
        for (i = 0; i < MAXDIMS; i++)
            wave5->sfB[i] = igorfile->get_gdouble(&p);
        get_CHARARRAY0(wave5->data_units, &p);
        gwy_debug("data_units: <%s>", wave5->data_units);
        for (i = 0; i < MAXDIMS; i++) {
            get_CHARARRAY0(wave5->dim_units[i], &p);
            gwy_debug("dim_units[%u]: <%s>", i, wave5->dim_units[i]);
        }
        wave5->fs_valid = !!igorfile->get_guint16(&p);
        wave5->whpad3 = igorfile->get_guint16(&p);
        wave5->top_full_scale = igorfile->get_gdouble(&p);
        wave5->bot_full_scale = igorfile->get_gdouble(&p);
    }

    return headers_size;
}

/* The footer mostly duplicates information which is already present in standard binary wave structure. */
static void
read_asylum_footer(IgorFile *igorfile, const guchar *buffer, gsize size)
{
    GString *str = g_string_new(NULL);
    const guchar *p, *sep;

    p = buffer;
    while ((sep = memchr(p, ';', size))) {
        gint len = sep - p;
        gchar *colon, *value;

        gwy_debug("%.*s", len, p);
        g_string_set_size(str, len);
        memcpy(str->str, p, len);
        size -= len+1;
        p = sep+1;

        if ((colon = strchr(str->str, ':'))) {
            *colon = '\0';
            value = colon + 1;
            if (gwy_strequal(str->str, "IsImage") && gwy_strequal(value, "1")) {
                gwy_debug("found IsImage:1");
                igorfile->asylum_shape = IGOR_DATA_IMAGE;
            }
            else if (gwy_strequal(str->str, "IsForce") && gwy_strequal(value, "1")) {
                gwy_debug("found IsForce:1");
                igorfile->asylum_shape = IGOR_DATA_CURVE;
            }
        }
    }
    g_string_free(str, TRUE);
}

/* The way the checksum is constructed (header->checksum is the complement), the return value is expected to be zero
 */
static guint
igor_checksum(gconstpointer buffer, gsize size, gboolean lsb)
{
    const guint16 *p = (const guint16*)buffer;
    guint i, sum;

    /* This ignores the last byte should the size be odd, IGOR seems to do the same. */
    if (lsb) {
        for (sum = 0, i = 0; i < size/2; ++i)
            sum += GUINT16_FROM_LE(p[i]);
    }
    else {
        for (sum = 0, i = 0; i < size/2; ++i)
            sum += GUINT16_FROM_BE(p[i]);
    }

    return sum & 0xffff;
}

static GwyRawDataType
igor_data_type_to_raw_type(IgorDataType type)
{
    gboolean isunsigned = (type & IGOR_UNSIGNED);
    IgorDataType basetype = type & ~(IGOR_UNSIGNED | IGOR_COMPLEX);

    if (basetype == IGOR_INT8)
        return isunsigned ? GWY_RAW_DATA_UINT8 : GWY_RAW_DATA_SINT8;
    if (basetype == IGOR_INT16)
        return isunsigned ? GWY_RAW_DATA_UINT16 : GWY_RAW_DATA_SINT16;
    if (basetype == IGOR_INT32)
        return isunsigned ? GWY_RAW_DATA_UINT32 : GWY_RAW_DATA_SINT32;
    if (isunsigned)
        return (GwyRawDataType)-1;
    if (basetype == IGOR_SINGLE)
        return GWY_RAW_DATA_FLOAT;
    if (basetype == IGOR_DOUBLE)
        return GWY_RAW_DATA_DOUBLE;
    return (GwyRawDataType)-1;
}

static guint
igor_data_type_size(IgorDataType type)
{
    GwyRawDataType rawtype = igor_data_type_to_raw_type(type);

    if (rawtype == (GwyRawDataType)-1)
        return 0;

    return gwy_raw_data_size(rawtype)*((type & IGOR_COMPLEX) ? 2 : 1);
}

static GwyDataField*
igor_read_data_field(const IgorFile *igorfile,
                     const guchar *buffer,
                     guint i,
                     const gchar *zunits,
                     gboolean is_imaginary)
{
    const IgorWaveHeader5 *wave5;
    GwyRawDataType rawtype;
    GwyByteOrder byteorder;
    guint n, xres, yres;
    GwyDataField *dfield;
    gdouble *data;
    const guchar *p;
    gint power10, stride;
    gdouble q;

    wave5 = &igorfile->wave5;
    xres = wave5->n_dim[0];
    yres = wave5->n_dim[1];
    n = xres*yres;
    p = buffer + igorfile->headers_size + n*igorfile->type_size*i;

    dfield = gwy_data_field_new(xres, yres, wave5->sfA[0]*xres, wave5->sfA[1]*yres, FALSE);
    data = gwy_data_field_get_data(dfield);

    g_return_val_if_fail(!is_imaginary || (wave5->type & IGOR_COMPLEX), dfield);
    if (is_imaginary) {
        stride = 2;
        p += igorfile->type_size/2;
    }
    else
        stride = 1;

    rawtype = igor_data_type_to_raw_type(wave5->type);
    g_return_val_if_fail(rawtype != (GwyRawDataType)-1, dfield);
    byteorder = (igorfile->lsb ? GWY_BYTE_ORDER_LITTLE_ENDIAN : GWY_BYTE_ORDER_BIG_ENDIAN);

    /* TODO: Support extended units */
    gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_xy(dfield), wave5->dim_units[0], &power10);
    gwy_data_field_set_xreal(dfield, pow10(power10)*wave5->sfA[0]*xres);
    gwy_data_field_set_yreal(dfield, pow10(power10)*wave5->sfA[1]*yres);

    gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_z(dfield),
                                      zunits ? zunits : wave5->data_units, &power10);
    q = pow10(power10);

    gwy_convert_raw_data(p, n, stride, rawtype, byteorder, data, q, 0.0);
    gwy_data_field_invert(dfield, TRUE, FALSE, FALSE);

    return dfield;
}

static GwyBrick*
igor_read_brick(const IgorFile *igorfile,
                const guchar *buffer,
                guint i,
                const gchar *wunits,
                gboolean is_imaginary)
{
    const IgorWaveHeader5 *wave5;
    GwyRawDataType rawtype;
    GwyByteOrder byteorder;
    guint n, xres, yres, zres;
    GwyBrick *brick;
    gdouble *data;
    const guchar *p;
    gint power10, stride;
    gdouble q;

    wave5 = &igorfile->wave5;
    xres = wave5->n_dim[0];
    yres = wave5->n_dim[1];
    zres = wave5->n_dim[2];
    n = xres*yres*zres;
    p = buffer + igorfile->headers_size + n*igorfile->type_size*i;

    brick = gwy_brick_new(xres, yres, zres,
                          wave5->sfA[0]*xres, wave5->sfA[1]*yres, wave5->sfA[2]*zres,
                          FALSE);
    data = gwy_brick_get_data(brick);

    g_return_val_if_fail(!is_imaginary || (wave5->type & IGOR_COMPLEX), brick);
    if (is_imaginary) {
        stride = 2;
        p += igorfile->type_size/2;
    }
    else
        stride = 1;

    rawtype = igor_data_type_to_raw_type(wave5->type);
    g_return_val_if_fail(rawtype != (GwyRawDataType)-1, brick);
    byteorder = (igorfile->lsb ? GWY_BYTE_ORDER_LITTLE_ENDIAN : GWY_BYTE_ORDER_BIG_ENDIAN);

    /* TODO: Support extended units */
    gwy_si_unit_set_from_string_parse(gwy_brick_get_si_unit_x(brick), wave5->dim_units[0], &power10);
    gwy_brick_set_xreal(brick, pow10(power10)*wave5->sfA[0]*xres);
    gwy_si_unit_set_from_string_parse(gwy_brick_get_si_unit_y(brick), wave5->dim_units[1], &power10);
    gwy_brick_set_yreal(brick, pow10(power10)*wave5->sfA[1]*yres);
    gwy_si_unit_set_from_string_parse(gwy_brick_get_si_unit_z(brick), wave5->dim_units[2], &power10);
    gwy_brick_set_zreal(brick, pow10(power10)*wave5->sfA[2]*zres);

    gwy_si_unit_set_from_string_parse(gwy_brick_get_si_unit_w(brick),
                                      wunits ? wunits : wave5->data_units, &power10);
    q = pow10(power10);

    gwy_convert_raw_data(p, n, stride, rawtype, byteorder, data, q, 0.0);
    //gwy_brick_invert(brick, TRUE, FALSE, FALSE);

    return brick;
}

static GwyDataLine*
igor_read_data_line(const IgorFile *igorfile,
                    const guchar *buffer,
                    guint i,
                    const gchar *yunits,
                    gboolean is_imaginary)
{
    const IgorWaveHeader5 *wave5;
    GwyRawDataType rawtype;
    GwyByteOrder byteorder;
    guint n, res;
    GwyDataLine *dline;
    gdouble *data;
    const guchar *p;
    gint power10, stride;
    gdouble q;

    wave5 = &igorfile->wave5;
    res = wave5->n_dim[0];
    n = res;
    p = buffer + igorfile->headers_size + n*igorfile->type_size*i;

    dline = gwy_data_line_new(res, wave5->sfA[0]*res, FALSE);
    data = gwy_data_line_get_data(dline);

    g_return_val_if_fail(!is_imaginary || (wave5->type & IGOR_COMPLEX), dline);
    if (is_imaginary) {
        stride = 2;
        p += igorfile->type_size/2;
    }
    else
        stride = 1;

    rawtype = igor_data_type_to_raw_type(wave5->type);
    g_return_val_if_fail(rawtype != (GwyRawDataType)-1, dline);
    byteorder = (igorfile->lsb ? GWY_BYTE_ORDER_LITTLE_ENDIAN : GWY_BYTE_ORDER_BIG_ENDIAN);

    /* TODO: Support extended units */
    gwy_si_unit_set_from_string_parse(gwy_data_line_get_si_unit_x(dline), wave5->dim_units[0], &power10);
    gwy_data_line_set_real(dline, pow10(power10)*wave5->sfA[0]*res);

    gwy_si_unit_set_from_string_parse(gwy_data_line_get_si_unit_y(dline),
                                      yunits ? yunits : wave5->data_units, &power10);
    q = pow10(power10);

    gwy_convert_raw_data(p, n, stride, rawtype, byteorder, data, q, 0.0);
    gwy_data_line_invert(dline, TRUE, FALSE);

    return dline;
}

static GPtrArray*
read_channel_labels(const gchar *p,
                    guint n, guint l)
{
    GPtrArray *array = g_ptr_array_sized_new(n);
    guint i;

    for (i = 0; i < l; i++) {
        g_ptr_array_add(array, g_strndup(p + i*(MAX_WAVE_NAME5 + 1), MAX_WAVE_NAME5));
        gwy_debug("label%u=%s", i, (gchar*)g_ptr_array_index(array, i));
    }
    for (i = l; i < n; i++) {
        g_ptr_array_add(array, NULL);
        gwy_debug("label%u=NULL", i);
    };

    return array;
}

static gchar*
canonicalize_title(const gchar *title)
{
    gchar *name, *s;
    guint len;

    name = g_strdup(title);
    len = strlen(name);

    if ((s = strstr(name, "Mod"))) {
        gchar *t = s + strlen("Mod");
        while (g_ascii_isdigit(*t))
            t++;
        if (!*t)
            *s = '\0';
        len = s - name;
    }

    if (g_str_has_suffix(name, "Trace"))
        name[len - strlen("Trace")] = '\0';
    else if (g_str_has_suffix(name, "Retrace"))
        name[len - strlen("Retrace")] = '\0';

    return name;
}

static const gchar*
channel_title_to_units(const gchar *title)
{
    static const struct {
        const gchar *prefix;
        const gchar *unit;
    } unit_table[] = {
        { "Height",      "m",   },
        { "ZSensor",     "m",   },
        { "Deflection",  "m",   },
        { "Amplitude",   "m",   },
        { "Phase",       "deg", },
        { "Current",     "A",   },
        { "Frequency",   "Hz",  },
        { "Capacitance", "F",   },
        { "Potential",   "V",   },
        { "Count",       NULL,  },
        { "QFactor",     NULL,  },
    };
    guint i;

    /* If the title becomes empty here it will end up as Volts anyway which is fine for DAC. */
    if (g_str_has_prefix(title, "DAC"))
        title += 3;
    else if (g_str_has_prefix(title, "Nap"))
        title += 3;

    for (i = 0; i < G_N_ELEMENTS(unit_table); i++) {
        if (g_str_has_prefix(title, unit_table[i].prefix))
            return unit_table[i].unit;
    }
    /* Everything else is in Volts. */
    return "V";
}

static void
gather_channel_meta(gpointer hkey,
                    gpointer hvalue,
                    gpointer user_data)
{
    const gchar *key = (const gchar*)hkey;
    const gchar *value = (const gchar*)hvalue;
    IgorFile *igorfile = (IgorFile*)user_data;
    guint i;

    if (!*value)
        return;
    for (i = 0; i < igorfile->nchannels; i++) {
        if (igorfile->channel_info[i].name && g_str_has_prefix(key, igorfile->channel_info[i].name))
            return;
    }
    for (i = 0; igorfile->ignore_prefixes[i]; i++) {
        if (g_str_has_prefix(key, igorfile->ignore_prefixes[i]))
            return;
    }

    if (g_utf8_validate(value, -1, NULL))
        value = g_strdup(value);
    else
        value = g_convert(value, -1, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);

    if (value)
        gwy_container_set_string_by_name(igorfile->channelmeta, key, value);
}

static GwyContainer*
igor_get_metadata(IgorFile *igorfile,
                  G_GNUC_UNUSED guint id)
{
    static const gchar *ignore_prefixes[] = {
        "Channel", "ColorMap", "Display", "Flatten", "PlaneFit", "Planefit", NULL
    };

    if (!igorfile->meta)
        return NULL;

    if (!igorfile->ignore_prefixes)
        igorfile->ignore_prefixes = ignore_prefixes;

    igorfile->channelmeta = gwy_container_new();
    g_hash_table_foreach(igorfile->meta, gather_channel_meta, igorfile);

    return igorfile->channelmeta;
}

static inline guint
append_uint16(GByteArray *content, guint16 u16)
{
    guint pos = content->len;

    u16 = GUINT16_TO_LE(u16);
    g_byte_array_append(content, (guint8*)&u16, sizeof(guint16));
    return pos;
}

static inline guint
append_uint32(GByteArray *content, guint32 u32)
{
    guint pos = content->len;

    u32 = GUINT32_TO_LE(u32);
    g_byte_array_append(content, (guint8*)&u32, sizeof(guint32));
    return pos;
}

static inline void
append_double(GByteArray *content, gdouble dbl)
{
    union { guchar pp[8]; double d; } u;

    u.d = dbl;
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    GWY_SWAP(guchar, u.pp[0], u.pp[7]);
    GWY_SWAP(guchar, u.pp[1], u.pp[6]);
    GWY_SWAP(guchar, u.pp[2], u.pp[5]);
    GWY_SWAP(guchar, u.pp[3], u.pp[4]);
#endif
    g_byte_array_append(content, u.pp, sizeof(gdouble));
}

static inline void
append_zeros(GByteArray *content, guint len)
{
    guint8 *zeros;
    guint64 z64 = 0;

    if (len <= 8) {
        g_byte_array_append(content, (guint8*)&z64, len);
        return;
    }

    zeros = g_new0(guint8, len);
    g_byte_array_append(content, zeros, len);
    g_free(zeros);
}

static inline void
append_string(GByteArray *content, const gchar *str, guint maxlen)
{
    guint len;

    if (!str) {
        append_zeros(content, maxlen+1);
        return;
    }

    len = strlen(str);
    g_byte_array_append(content, str, MIN(len, maxlen));
    append_zeros(content, maxlen+1 - MIN(len, maxlen));
}

static gboolean
igor_export(GwyContainer *data,
            const gchar *filename,
            G_GNUC_UNUSED GwyRunType mode,
            GError **error)
{
    GByteArray *header = NULL;
    GwyDataField *dfield;
    GwySIUnit *xyunit, *zunit;
    const gdouble *d, *drow;
    gfloat *dfl = NULL, *frow;
    gint *ids = NULL;
    FILE *fh = NULL;
    gboolean ok = FALSE;
    guint chksumpos;
    guint xres, yres, n, npts, nchannels, i, j, k, wantlen;
    gdouble xreal, yreal;
    guint16 chksum;
    gchar *title, *title_latin1, *unitstr;
    gchar bname[MAX_WAVE_NAME5+1];

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield, 0);
    if (!dfield) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    ids = find_compatible_channels(data, dfield, &nchannels);
    g_return_val_if_fail(nchannels > 0, FALSE);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        g_free(ids);
        return FALSE;
    }

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    xreal = gwy_data_field_get_xreal(dfield);
    yreal = gwy_data_field_get_yreal(dfield);
    xyunit = gwy_data_field_get_si_unit_xy(dfield);
    zunit = gwy_data_field_get_si_unit_z(dfield);

    n = xres*yres;
    npts = nchannels*n;

    wantlen = HEADER_SIZE5 + WAVE_SIZE5;
    header = g_byte_array_sized_new(wantlen);

    /* File header. */
    append_uint16(header, 0x0005u); /* Version. */
    chksumpos = append_uint16(header, 0); /* Checksum - TBD later. */
    append_uint32(header, WAVE_SIZE5 + npts*sizeof(gfloat)); /* WFM size */
    append_uint32(header, 0); /* formula size */
    append_uint32(header, 0); /* note size */
    append_uint32(header, 0); /* extended data units size */
    for (i = 0; i < MAXDIMS; i++)
        append_uint32(header, 0); /* extended dimension unit sizes */

    /* extended dimension label sizes: x, y, channel, 4D */
    append_uint32(header, 0);
    append_uint32(header, 0);
    append_uint32(header, nchannels*(MAX_WAVE_NAME5 + 1));
    append_uint32(header, 0);

    append_uint32(header, 0); /* string indices for text waves */
    append_uint32(header, 0); /* options1, reserved */
    append_uint32(header, 0); /* options2, reserved */

    /* Wave header. */
    append_uint32(header, 0); /* next */
    append_uint32(header, 0); /* creation date */
    append_uint32(header, 0); /* modification date */
    append_uint32(header, npts); /* npts */
    append_uint16(header, IGOR_SINGLE); /* type */
    append_uint16(header, 0); /* lock */
    append_zeros(header, 6); /* whpad1 */
    append_uint16(header, 1); /* wh_version */
    /* Igor complains if the field is empty.  Also it does not like it being the same as something else... */
    g_snprintf(bname, sizeof(bname), "gwy%u", g_random_int());
    append_string(header, bname, MAX_WAVE_NAME5); /* bname */
    append_zeros(header, 4); /* whpad2 */
    append_uint32(header, 0); /* dfolder */

    append_uint32(header, xres); /* n_dim[0] */
    append_uint32(header, yres); /* n_dim[1] */
    append_uint32(header, nchannels); /* n_dim[2] */
    append_uint32(header, 0); /* n_dim[3] */

    append_double(header, xreal/xres); /* sfA[0] */
    append_double(header, yreal/yres); /* sfA[1] */
    append_double(header, 1.0); /* sfA[2] */
    append_double(header, 1.0); /* sfA[3] */

    /* FIXME: We could store offsets but they may differ among the fields. */
    append_double(header, 0.0); /* sfB[0] */
    append_double(header, 0.0); /* sfB[1] */
    append_double(header, 0.0); /* sfB[2] */
    append_double(header, 0.0); /* sfB[3] */

    /* natural data units */
    unitstr = gwy_si_unit_get_string(zunit, GWY_SI_UNIT_FORMAT_PLAIN);
    append_string(header, strlen(unitstr) == 1 ? unitstr : NULL, MAX_UNIT_CHARS);

    /* dimension units */
    unitstr = gwy_si_unit_get_string(xyunit, GWY_SI_UNIT_FORMAT_PLAIN);
    append_string(header, strlen(unitstr) == 1 ? unitstr : NULL, MAX_UNIT_CHARS);
    append_string(header, strlen(unitstr) == 1 ? unitstr : NULL, MAX_UNIT_CHARS);
    g_free(unitstr);
    for (i = 2; i < MAXDIMS; i++)
        append_string(header, NULL, MAX_UNIT_CHARS);

    append_uint16(header, 0); /* fsValid */
    append_uint16(header, 0); /* whpad3 */
    append_double(header, 0.0); /* top full scale */
    append_double(header, 0.0); /* bottom full scale */

    gwy_debug("header len %u", header->len);

    if (header->len < wantlen)
        append_zeros(header, wantlen - header->len);
    chksum = igor_checksum(header->data, header->len, TRUE);
    gwy_debug("checksum %04x", chksum);
    chksum = ((chksum + 0xffff) & 0xffff) ^ 0xffff;
    header->data[chksumpos] = chksum % 0x100;
    header->data[chksumpos+1] = chksum/0x100;

    if (!(ok = (fwrite(header->data, 1, header->len, fh) == header->len))) {
        err_WRITE(error);
        goto fail;
    }

    dfl = g_new(gfloat, n);
    for (k = 0; ids[k] >= 0; k++) {
        dfield = gwy_container_get_object(data, gwy_app_get_data_key_for_id(ids[k]));
        d = gwy_data_field_get_data_const(dfield);
        for (i = 0; i < yres; i++) {
            frow = dfl + i*xres;
            drow = d + (yres-1 - i)*xres;
            for (j = 0; j < xres; j++) {
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
                {
                    union { guchar pp[4]; gfloat f; } u;

                    u.f = drow[j];
                    GWY_SWAP(guchar, u.pp[0], u.pp[3]);
                    GWY_SWAP(guchar, u.pp[1], u.pp[2]);
                    frow[j] = u.f;
                }
#else
                frow[j] = drow[j];
#endif
            }
        }

        if (!(ok = (fwrite(dfl, sizeof(gfloat), n, fh) == n))) {
            err_WRITE(error);
            goto fail;
        }
    }

    g_byte_array_set_size(header, 0);
    for (k = 0; ids[k] >= 0; k++) {
        title = gwy_app_get_data_field_title(data, ids[k]);
        title_latin1 = g_convert(title, -1, "ISO-8859-1", "UTF-8", NULL, NULL, NULL);
        append_string(header, title_latin1, MAX_WAVE_NAME5);
        g_free(title_latin1);
        g_free(title);
    }

    if (!(ok = (fwrite(header->data, 1, header->len, fh) == header->len))) {
        err_WRITE(error);
        goto fail;
    }

    ok = TRUE;

fail:
    fclose(fh);
    if (!ok)
        g_unlink(filename);
    g_free(ids);
    g_free(dfl);
    if (header)
        g_byte_array_free(header, TRUE);

    return ok;
}

static gint*
find_compatible_channels(GwyContainer *container,
                         GwyDataField *dfield,
                         guint *n)
{
    GwyDataField *otherfield;
    GQuark quark;
    guint i, j;
    gint *ids;

    ids = gwy_app_data_browser_get_data_ids(container);
    for (i = j = 0; ids[i] >= 0; i++) {
        quark = gwy_app_get_data_key_for_id(ids[i]);
        otherfield = gwy_container_get_object(container, quark);
        if (gwy_data_field_check_compatibility(dfield, otherfield, GWY_DATA_COMPATIBILITY_ALL) != 0)
            continue;

        ids[j++] = ids[i];
    }
    g_assert(j > 0);
    *n = j;
    ids[j] = -1;

    return ids;
}

/* vim: set cin et columns=120 tw=118 ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
