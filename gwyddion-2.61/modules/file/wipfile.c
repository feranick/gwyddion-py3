/*
 *  $Id: wipfile.c 24585 2022-02-05 10:52:39Z yeti-dn $
 *  Copyright (C) 2010-2011 David Necas (Yeti), Petr Klapetek,
 *  Daniil Bratashov (dn2010)
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, dn2010@gmail.com
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
 * Thanks to GSXM project crew for description of tag format and
 * datatypes.
 *
 * TODO: metadata loading
 */

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-wipfile-spm">
 *   <comment>WITec Project data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="WIT_PRCT"/>
 *     <match type="string" offset="0" value="WIT_PR06"/>
 *   </magic>
 *   <glob pattern="*.wip"/>
 *   <glob pattern="*.WIP"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # WITec
 * 0 string WIT_PRCT WITec Project data
 * 0 string WIT_PR06 WITec Project data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * WITec Project data
 * .wip
 * Read SPS Volume
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libprocess/spectra.h>
#include <libprocess/brick.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwygraphbasics.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "get.h"

#define MAGIC "WIT_PRCT"
#define MAGIC2 "WIT_PR06"
#define MAGIC_SIZE (8)

#define EXTENSION ".wip"

#define H (4.135667662E-15)
#define C (299792458)

typedef enum {
    WIP_TAG_LIST     = 0, /* list of other tags */
    WIP_TAG_EXTENDED = 1, /* x86 FPU native type, 10 bytes */
    WIP_TAG_DOUBLE   = 2,
    WIP_TAG_FLOAT    = 3,
    WIP_TAG_INT64    = 4,
    WIP_TAG_INT32    = 5,
    WIP_TAG_UINT32   = 6,
    WIP_TAG_CHAR     = 7,
    WIP_TAG_BOOL     = 8, /* 1 byte */
    WIP_TAG_STRING   = 9  /* int32 = nchars, n bytes = string */
} WIPTagType;

gsize WIPTagDataSize[10] = { 0, 10, 8, 4, 8, 4, 4, 1, 1, 0 };

typedef enum {
    WIP_DATA_LIST     = 0, /* list of tags */
    WIP_DATA_INT64    = 1,
    WIP_DATA_INT32    = 2,
    WIP_DATA_INT16    = 3,
    WIP_DATA_INT8     = 4,
    WIP_DATA_UINT32   = 5,
    WIP_DATA_UINT16   = 6,
    WIP_DATA_UINT8    = 7,
    WIP_DATA_BOOL     = 8, /* 1 byte */
    WIP_DATA_FLOAT    = 9,
    WIP_DATA_DOUBLE   = 10,
    WIP_DATA_EXTENDED = 11 /* x86 FPU native type, 10 bytes */
} WIPDataType;

gsize WIPDataSize[12] = { 0, 8, 4, 2, 1, 4, 2, 1, 1, 4, 8, 10 };

typedef enum {
    WIP_UNIT_NANOMETER   = 0,
    WIP_UNIT_MIKROMETER  = 1,
    WIP_UNIT_SM_1        = 2, /* 1/cm */
    WIP_UNIT_RAMAN_SHIFT = 3, /* 1/cm relative*/
    WIP_UNIT_EV          = 4,
    WIP_UNIT_MEV         = 5, /* meV m = milli*/
    WIP_UNIT_EV_REL      = 6,
    WIP_UNIT_MEV_REL     = 7
} WIPUnitIndex;

typedef struct {
    guint32       name_length;
    const guchar *name; /* name_length bytes */
    WIPTagType    type;
    gint64        data_start;
    gint64        data_end;
    const guchar *data;
} WIPTag;

/* TD*Interpretation */
typedef struct {
    guint    id;
    gchar   *unitname;
    gdouble  unitmultiplier;
    gdouble  laser_wl; /* for 1/cm axis only */
} WIPAxis;

/* TDSpectralTransformation for optical spectra;
 * to recalculate x spectral data from
 * spectrometer calibrations */
typedef struct {
    guint    id;
    guint    transform_type; /* should be 1 */
    gdouble  polynom [3]; /* polynomial coeffs. should be zeros */
    gdouble  nc; /* central pixel number */
    gdouble  lambdac; /* central pixel lambda in nm */
    gdouble  gamma; /* angle between incident and diffracted light */
    gdouble  delta; /* CCD inclination */
    gdouble  m; /* diffraction order */
    gdouble  d; /* 1e6/lines per mm */
    gdouble  x; /* pixel size */
    gdouble  f; /* focal distance */
    gchar   *unitname; /* nm */
} WIPSpectralTransform;

/* TDSpectralInterpretation for spectra */
typedef struct {
    guint        id;
    WIPUnitIndex unitindex;
    gdouble      excitation_wavelength;
} WIPSpectralInterpretation;

typedef struct {
    guint id;
    gchar *unitname;
    gdouble scale[9];
} WIPSpaceTransform;

typedef struct {
    guint dimension;
    WIPDataType datatype;
    guint xrange, yrange;
    gpointer data;
} WIPGraphData;

typedef struct {
    guint sizex;
    guint sizey;
    guint sizegraph;
    guint spacetransformid;
    guint xtransformid;
    guint xinterpid;
    guint zinterpid;
    guint dimension;
    WIPDataType datatype;
    guint xrange;
    guint yrange;
    gsize datasize;
    const guchar *data;
} WIPGraph;

typedef struct {
    guint version;
    guint sizex;
    guint sizey;
    guint postransformid;
    guint zinterpid;
    guint dimension;
    WIPDataType datatype;
    guint xrange;
    guint yrange;
    gsize datasize;
    const guchar *data;
} WIPImage;

typedef struct {
    guint spacetransformid;
    gsize streamsize;
    gsize datasize;
    const guchar *data;
} WIPBitmap;

typedef struct {
    guint numgraph;
    guint numimages;
    guint numbricks;
    GwyContainer *data;
    const gchar *filename;
} WIPFile;

typedef struct {
    guint id;
    GNode *node;
} WIPIdNode;

static gboolean       module_register           (void);
static gint           wip_detect                (const GwyFileDetectInfo *fileinfo,
                                                 gboolean only_name);
static WIPTag*        wip_read_tag              (guchar **pos,
                                                 gsize *start,
                                                 gsize *end);
static void           wip_free_tag              (WIPTag *tag);
static GwyContainer*  wip_load                  (const gchar *filename,
                                                 GwyRunType mode,
                                                 GError **error);
static void           wip_read_all_tags         (const guchar *buffer,
                                                 gsize start,
                                                 gsize end,
                                                 GNode *tagtree,
                                                 gint n);
static gboolean       wip_free_leave            (GNode *node,
                                                 G_GNUC_UNUSED gpointer data);
static gboolean       wip_read_graph_tags       (GNode *node,
                                                 gpointer header);
static gboolean       wip_read_sp_transform_tags(GNode *node,
                                                 gpointer transform);
static gboolean       wip_read_sp_interpr_tags  (GNode *node,
                                                 gpointer interp);
static gboolean       wip_read_space_tr_tag     (GNode *node,
                                                 gpointer transform);
static gboolean       wip_read_axis_tags        (GNode *node,
                                                 gpointer axis);
static gboolean       wip_read_bitmap_tags      (GNode *node,
                                                 gpointer data);
static gboolean       wip_find_by_id            (GNode *node,
                                                 gpointer idnode);
static gboolean       wip_read_caption          (GNode *node,
                                                 gpointer caption);
static GwyDataField*  wip_read_bmp              (const guchar *bmpdata,
                                                 gsize datasize,
                                                 gdouble xscale,
                                                 gdouble yscale,
                                                 gint power10xy);
static gdouble        wip_pixel_to_lambda       (gint i,
                                                 WIPSpectralTransform *transform);
static GwyGraphModel* wip_read_graph            (GNode *node);
static GwyBrick*      wip_read_graph_image      (GNode *node);
static GwyDataField*  wip_read_image            (GNode *node);
static GwyDataField*  wip_read_bitmap           (GNode *node);
static gboolean       wip_read_data             (GNode *node,
                                                 gpointer filedata);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports WItec Project data files."),
    "Daniil Bratashov <dn2010@gmail.com>",
    "0.11",
    "David Nečas (Yeti) & Petr Klapetek & Daniil Bratashov",
    "2010",
};

GWY_MODULE_QUERY2(module_info, wipfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("wipfile",
                           N_("WItec Project files (.wip)"),
                           (GwyFileDetectFunc)&wip_detect,
                           (GwyFileLoadFunc)&wip_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
wip_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION)
                                                               ? 20 : 0;

    if (fileinfo->buffer_len > MAGIC_SIZE
        && (memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) == 0
        || memcmp(fileinfo->head, MAGIC2, MAGIC_SIZE) == 0))
        score = 100;

    return score;
}

static WIPTag*
wip_read_tag(guchar **pos, gsize *start, gsize *end)
{
    WIPTag *tag;
    const guchar *p;
    gsize maxsize;

    p = *pos;
    maxsize = *end - *start;
    if (maxsize < 4)
        return NULL;
    tag = g_new0(WIPTag, 1);
    tag->name_length = gwy_get_guint32_le(&p);
    if (maxsize < 24+tag->name_length) {
        g_free(tag);
        return NULL;
    }
    tag->name = g_strndup(p, tag->name_length);
    p += tag->name_length;
    tag->type = (WIPTagType)gwy_get_guint32_le(&p);
    tag->data_start = gwy_get_gint64_le(&p);
    tag->data_end = gwy_get_gint64_le(&p);
    if ((tag->data_start < *start) || (tag->data_end > *end)
     || (tag->data_end - tag->data_start < 0)) {
        g_free(tag);
        return NULL;
    }
    tag->data = (gpointer)p;

    gwy_debug("%d %s %d %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT "\n",
              tag->name_length, tag->name, tag->type, tag->data_start,
              tag->data_end);

    *pos = (guchar *)p;

    return tag;
}

static void
wip_free_tag(WIPTag *tag)
{
    g_free((gpointer)tag->name);
    g_free(tag);
}

static void
wip_read_all_tags(const guchar *buffer, gsize start,
                  gsize end, GNode *tagtree, gint n)
{
    guchar *p;
    gsize cur;
    WIPTag *tag;
    GNode *tagpos;

    cur = start;
    while (cur < end) {
        p = (guchar *)(buffer + cur);
        if (!(tag = wip_read_tag(&p, &cur, &end))) {
            /* error: tag cannot be read */
        }
        else {
            tagpos = g_node_insert_data(tagtree, -1, tag);
            if ((!tag->type) && (n < 255))
                wip_read_all_tags(buffer, tag->data_start,
                                  tag->data_end, tagpos, n+1);
            cur = tag->data_end;
        }
    }
}

static gboolean
wip_free_leave(GNode *node,
               G_GNUC_UNUSED gpointer data)
{
    wip_free_tag((WIPTag *)node->data);
    node->data = NULL;

    return FALSE;
}

static gboolean
wip_read_graph_tags(GNode *node, gpointer header)
{
    WIPTag *tag;
    WIPGraph *graphheader;
    const guchar *p;

    tag = node->data;
    graphheader = (WIPGraph *)header;
    p = tag->data;
    if (!strncmp(tag->name, "SizeX", 5))
        graphheader->sizex = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "SizeY", 5))
        graphheader->sizey = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "SizeGraph", 9))
        graphheader->sizegraph = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "SpaceTransformationID", 21))
        graphheader->spacetransformid = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "XTransformationID", 17))
        graphheader->xtransformid = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "XInterpretationID", 17))
        graphheader->xinterpid = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "ZInterpretationID", 17))
        graphheader->zinterpid = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "Dimension", 9))
        graphheader->dimension = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "DataType", 8))
        graphheader->datatype = (WIPDataType)gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "Ranges", 6)) {
        graphheader->xrange = gwy_get_gint32_le(&p);
        graphheader->yrange = gwy_get_gint32_le(&p);
    }
    else if (!strncmp(tag->name, "Data", 4)) {
        graphheader->data = p;
        graphheader->datasize = (gsize)(tag->data_end-tag->data_start);
    }

    return FALSE;
}

static gboolean
wip_read_image_tags(GNode *node, gpointer header)
{
    WIPTag *tag;
    WIPImage *imageheader;
    const guchar *p;

    tag = node->data;
    imageheader = (WIPImage *)header;
    p = tag->data;
    if (!strncmp(tag->name, "Version", 7))
        imageheader->version = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "SizeX", 5))
        imageheader->sizex = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "SizeY", 5))
        imageheader->sizey = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "PositionTransformationID", 24))
        imageheader->postransformid = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "ZInterpretationID", 17))
        imageheader->zinterpid = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "Dimension", 9))
        imageheader->dimension = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "DataType", 8))
        imageheader->datatype = (WIPDataType)gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "Ranges", 6)) {
        imageheader->xrange = gwy_get_gint32_le(&p);
        imageheader->yrange = gwy_get_gint32_le(&p);
    }
    else if (!strncmp(tag->name, "Data", 4)) {
        imageheader->data = p;
        imageheader->datasize = (gsize)(tag->data_end-tag->data_start);
    }

    return FALSE;
}

static gboolean
wip_read_sp_transform_tags(GNode *node,
                           gpointer transform)
{
    WIPTag *tag;
    WIPSpectralTransform *sp_transform;
    const guchar *p;
    gint i, str_len;
    gchar *str;

    tag = node->data;
    sp_transform = (WIPSpectralTransform *)transform;
    p = tag->data;
    if (!strncmp(tag->name, "SpectralTransformationType", 26))
        sp_transform->transform_type = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "Polynom", 7)) {
        for (i = 0; i < 3; i++)
            sp_transform->polynom[i] = gwy_get_gdouble_le(&p);
    }
    else if (!strncmp(tag->name, "nC", 2))
        sp_transform->nc = gwy_get_gdouble_le(&p);
    else if (!strncmp(tag->name, "LambdaC", 7))
        sp_transform->lambdac = gwy_get_gdouble_le(&p);
    else if (!strncmp(tag->name, "Gamma", 5))
        sp_transform->gamma = gwy_get_gdouble_le(&p);
    else if (!strncmp(tag->name, "Delta", 5))
        sp_transform->delta = gwy_get_gdouble_le(&p);
    else if (!strncmp(tag->name, "m", 1))
        sp_transform->m = gwy_get_gdouble_le(&p);
    else if (!strncmp(tag->name, "d", 1))
        sp_transform->d = gwy_get_gdouble_le(&p);
    else if (!strncmp(tag->name, "x", 1))
        sp_transform->x = gwy_get_gdouble_le(&p);
    else if (!strncmp(tag->name, "f", 1))
        sp_transform->f = gwy_get_gdouble_le(&p);
    else if (!strncmp(tag->name, "StandardUnit", 12)) {
        str_len = gwy_get_gint32_le(&p);
        str = g_strndup(p, str_len);
        sp_transform->unitname = g_convert(str, str_len, "UTF-8",
                                           "ISO-8859-1",
                                           NULL, NULL, NULL);
        g_free(str);
    }

    return FALSE;
}

static gboolean
wip_read_sp_interpr_tags(GNode *node,
                         gpointer interp)
{
    WIPTag *tag;
    WIPSpectralInterpretation *sp_interp;
    const guchar *p;

    tag = node->data;
    sp_interp = (WIPSpectralInterpretation *)interp;
    p = tag->data;
    if (!strncmp(tag->name, "UnitIndex", 9))
        sp_interp->unitindex = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "ExcitationWaveLength", 20))
        sp_interp->excitation_wavelength = gwy_get_gdouble_le(&p);

    return FALSE;
}

static gboolean
wip_read_space_tr_tag(GNode *node, gpointer transform)
{
    WIPTag *tag;
    WIPSpaceTransform *sp_transform;
    const guchar *p;
    gint i, str_len;
    gchar *str;

    tag = node->data;
    sp_transform = (WIPSpaceTransform *)transform;
    p = tag->data;
    if (!strncmp(tag->name, "Scale", 5)) {
        for (i = 0; i < 9; i++)
            sp_transform->scale[i] = gwy_get_gdouble_le(&p);
    }
    else if (!strncmp(tag->name, "StandardUnit", 12)) {
        str_len = gwy_get_gint32_le(&p);
        str = g_strndup(p, str_len);
        sp_transform->unitname = g_convert(str, str_len, "UTF-8",
                                           "ISO-8859-1",
                                           NULL, NULL, NULL);
        g_free(str);
    }

    return FALSE;
}

static gboolean
wip_read_axis_tags(GNode *node, gpointer axis)
{
    WIPTag *tag;
    WIPAxis *tmp_axis;
    const guchar *p;
    gint str_len;
    gchar *str;

    tag = node->data;
    tmp_axis = (WIPAxis *)axis;
    p = tag->data;
    if (!strncmp(tag->name, "UnitName", 8)) {
        str_len = gwy_get_gint32_le(&p);
        str = g_strndup(p, str_len);
        tmp_axis->unitname = g_convert(str, str_len, "UTF-8",
                                           "ISO-8859-1",
                                           NULL, NULL, NULL);
        g_free(str);
    }

    return FALSE;
}

static gboolean
wip_read_bitmap_tags(GNode *node, gpointer data)
{
    WIPTag *tag;
    WIPBitmap *bitmap;
    const guchar *p;

    tag = node->data;
    bitmap = (WIPBitmap *)data;
    p = tag->data;
    if (!strncmp(tag->name, "SpaceTransformationID", 21))
        bitmap->spacetransformid = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "StreamSize", 10))
        bitmap->streamsize = gwy_get_gint32_le(&p);
    else if (!strncmp(tag->name, "StreamData", 10)) {
        bitmap->data = p;
        bitmap->datasize = (gsize)(tag->data_end-tag->data_start);
    }

    return FALSE;
}

static gboolean
wip_find_by_id(GNode *node, gpointer idnode)
{
    WIPTag *tag;
    WIPIdNode *idnode_tmp;
    const guchar *p;
    gint id_temp;

    tag = node->data;
    p = tag->data;
    idnode_tmp = (WIPIdNode *)idnode;
    id_temp = 0;
    if (!strncmp(tag->name, "ID", 2)) {
        id_temp = gwy_get_gint32_le(&p);
        if (id_temp == idnode_tmp->id) {
            idnode_tmp->node = node;

            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
wip_read_caption(GNode *node, gpointer caption)
{
    gchar *str;
    gint str_len;
    WIPTag *tag;
    const guchar *p;

    tag = node->data;
    if (!strncmp(tag->name, "Caption", 7)) {
        p = tag->data;
        str_len = gwy_get_gint32_le(&p);
        str = g_strndup(p, str_len);
        g_string_printf(caption, "%s", str);
        g_free(str);
        return TRUE;
    }

    return FALSE;
}

// FIXME: Imported as greyscale
static GwyDataField*
wip_read_bmp(const guchar *bmpdata,
             gsize datasize,
             gdouble xscale, gdouble yscale,
             gint power10xy)
{
    GwyDataField *dfield = NULL;
    gdouble *data;
    gint i, j, width, height, rowstride, bpp;
    GdkPixbufLoader *loader;
    GError *err = NULL;
    GdkPixbuf *pixbuf = NULL;
    guchar *pixels, *pix_p;

    if (!bmpdata) {
        return NULL;
    }

    loader = gdk_pixbuf_loader_new();
    if (!gdk_pixbuf_loader_write(loader, bmpdata, datasize, &err)) {
        g_object_unref(loader);
        g_clear_error(&err);
        return NULL;
    }
    gwy_debug("Closing the loader.");
    if (!gdk_pixbuf_loader_close(loader, &err)) {
        g_object_unref(loader);
        g_clear_error(&err);
        return NULL;
    }
    gwy_debug("Trying to get the pixbuf.");
    pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    gwy_debug("Pixbuf is: %p.", pixbuf);
    g_assert(pixbuf);
    g_object_ref(pixbuf);
    gwy_debug("Finalizing loader.");
    g_object_unref(loader);

    if (pixbuf) {
        pixels = gdk_pixbuf_get_pixels(pixbuf);
        width = gdk_pixbuf_get_width(pixbuf);
        height = gdk_pixbuf_get_height(pixbuf);
        rowstride = gdk_pixbuf_get_rowstride(pixbuf);
        bpp = gdk_pixbuf_get_has_alpha(pixbuf) ? 4 : 3;

        dfield = gwy_data_field_new(width, height,
                                 width * xscale * pow10(power10xy),
                                 height * yscale * pow10(power10xy),
                                 TRUE);
        data = gwy_data_field_get_data(dfield);
        for (i = 0; i < height; i++) {
            pix_p = pixels + i * rowstride;
            for (j = 0; j < width; j++) {
                guchar red = pix_p[bpp*j];
                guchar green = pix_p[bpp*j+1];
                guchar blue = pix_p[bpp*j+2];

                *(data + i * width + j) = (0.2126 * red
                                         + 0.7152 * green
                                         + 0.0722 * blue) / 255.0;
            }
        }
    }

    return dfield;
}

/*
 * spectral transform from here:
 * http://www.horiba.com/us/en/scientific/products/optics-tutorial/wavelength-pixel-position/
 */
static gdouble
wip_pixel_to_lambda(gint i,
                    WIPSpectralTransform *transform)
{
    gdouble lambda, alpha, betac, hc, lh, hi, betah, betai;

    if ((transform->d == 0.0) || (transform->m == 0.0)
     || (cos(transform->gamma / 2.0) == 0.0)
     || (transform->lambdac * transform->m / transform->d / 2.0
       / cos(transform->gamma / 2.0) > 1.0)
     || (transform->lambdac * transform->m / transform->d / 2.0
       / cos(transform->gamma / 2.0) < -1.0))
        return i;
    alpha = asin(transform->lambdac * transform->m / transform->d / 2.0
               / cos(transform->gamma / 2.0)) - transform->gamma / 2.0;
    betac = transform->gamma + alpha;
    hc = transform->f * sin(transform->delta);
    lh = transform->f * cos(transform->delta);
    hi = transform->x * (transform->nc - i) + hc;
    betah = betac + transform->delta;
    if (lh == 0.0)
        return i;
    betai = betah - atan(hi / lh);
    lambda = transform->d / transform->m * (sin(alpha) + sin(betai));

    return lambda;
}

static GwyGraphModel*
wip_read_graph(GNode *node)
{
    WIPGraph *header;
    WIPSpectralTransform *xtransform;
    WIPSpectralInterpretation *xinterp;
    WIPAxis *yaxis;
    WIPIdNode *idnode;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwySIUnit *siunitx, *siunity;
    gdouble *xdata, *ydata;
    gint numpoints, i;
    GString *caption;
    const guchar *p;

    header = g_new0(WIPGraph, 1);

    g_node_traverse(node, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_graph_tags, (gpointer)header);

    if ((header->sizex != 1) || (header->sizey != 1)) { /* image */
        g_free(header);
        return NULL;
    }

    numpoints = header->yrange;
    if ((numpoints <= 0)
     || (header->datatype != WIP_DATA_FLOAT)
     || (header->datasize != WIPDataSize[header->datatype]
       * numpoints)) {
        g_free(header);
        return NULL;
    }

    xdata = g_new(gdouble, numpoints);
    ydata = g_new(gdouble, numpoints);

    /* Read ydata, fallback xdata */
    p = header->data;
    for (i = 0; i < numpoints; i++) {
        xdata[i] = i;
        ydata[i] = gwy_get_gfloat_le(&p);
    }

    /* Read caption */
    caption = g_string_new(NULL);
    g_node_traverse(node->parent, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_caption, (gpointer)caption);
    if (!caption->str)
        g_string_printf(caption, "Unnamed graph");

    /* Try to read xdata */
    idnode = g_new0(WIPIdNode, 1);
    idnode->id = header->xtransformid;
    g_node_traverse(g_node_get_root(node),
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_find_by_id, (gpointer)idnode);

    xtransform = g_new0(WIPSpectralTransform, 1);
    g_node_traverse(idnode->node->parent->parent,
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_sp_transform_tags,
                    (gpointer)xtransform);
    if ((xtransform->transform_type != 1)
     || (xtransform->m < 0.01) || (xtransform->f < 0.01)
     || (xtransform->nc < 0.0) || (xtransform->nc > numpoints)) {
        /* xtransform not read correctly, fallback to point numbers */
    }
    else {
        for (i = 0; i < numpoints; i++)
            xdata[i] = wip_pixel_to_lambda(i, xtransform);
    }

    idnode->id = header->xinterpid;
    g_node_traverse(g_node_get_root(node),
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_find_by_id, (gpointer)idnode);

    xinterp = g_new0(WIPSpectralInterpretation, 1);
    g_node_traverse(idnode->node->parent->parent,
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_sp_interpr_tags,
                    (gpointer)xinterp);

    /* recalculating to right units */
    if (xtransform->unitname != NULL) {
        if ((xinterp->unitindex == WIP_UNIT_NANOMETER)
         || (xinterp->unitindex == WIP_UNIT_MIKROMETER)) {
            siunitx = gwy_si_unit_new("m");
            for (i = 0; i < numpoints; i++)
                xdata[i] = xdata[i] * 1e-9;
        }
        else if (xinterp->unitindex == WIP_UNIT_SM_1) {
            siunitx = gwy_si_unit_new("1/m");
            for (i = 0; i < numpoints; i++)
                xdata[i] = 1.0 / (xdata[i] * 1e-9);
        }
        else if (xinterp->unitindex == WIP_UNIT_RAMAN_SHIFT) {
            if (xinterp->excitation_wavelength == 0.0) {
                siunitx = gwy_si_unit_new("m");
                for (i = 0; i < numpoints; i++)
                    xdata[i] = xdata[i] * 1e-9;
            }
            else {
                siunitx = gwy_si_unit_new("1/m");
                for (i = 0; i < numpoints; i++)
                    xdata[i]
                         = 1.0 / (xinterp->excitation_wavelength * 1e-9)
                                              - 1.0 / (xdata[i] * 1e-9);
            }
        }
        else if ((xinterp->unitindex == WIP_UNIT_EV)
              || (xinterp->unitindex == WIP_UNIT_MEV)) {
            siunitx = gwy_si_unit_new("eV");
            for (i = 0; i < numpoints; i++)
                xdata[i] = H * C / (xdata[i] * 1e-9);
        }
        else if ((xinterp->unitindex == WIP_UNIT_EV_REL)
              || (xinterp->unitindex == WIP_UNIT_MEV_REL)) {
            if (xinterp->excitation_wavelength == 0.0) {
                siunitx = gwy_si_unit_new("m");
                for (i = 0; i < numpoints; i++)
                    xdata[i] = xdata[i] * 1e-9;
            }
            else {
                siunitx = gwy_si_unit_new("eV");
                for (i = 0; i < numpoints; i++)
                    xdata[i] = (H * C / (xinterp->excitation_wavelength * 1e-9)
                                - H * C / (xdata[i] * 1e-9));
            }
        }
        else {
            siunitx = gwy_si_unit_new("m");
            for (i = 0; i < numpoints; i++)
                xdata[i] = xdata[i] * 1e-9;
        }
    }
    else
        siunitx = gwy_si_unit_new("pixels");

    if (!xtransform->unitname) {
        g_free(xtransform->unitname);
    }
    g_free(xtransform);
    g_free(xinterp);

    /* Try to read y units */
    idnode->id = header->zinterpid;
    g_node_traverse(g_node_get_root(node),
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_find_by_id, (gpointer)idnode);
    yaxis = g_new0(WIPAxis, 1);
    g_node_traverse(idnode->node->parent->parent,
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_axis_tags,
                    (gpointer)yaxis);
    if (yaxis->unitname)
        siunity = gwy_si_unit_new(yaxis->unitname);
    else
        siunity = gwy_si_unit_new(NULL);
    g_free(yaxis);

    /* Packing */
    gmodel = g_object_new(GWY_TYPE_GRAPH_MODEL,
                          "title", caption->str,
                          "si-unit-x", siunitx,
                          "si-unit-y", siunity,
                          NULL);
    gcmodel = g_object_new(GWY_TYPE_GRAPH_CURVE_MODEL,
                           "description", caption->str,
                           "mode", GWY_GRAPH_CURVE_LINE,
                           "color", gwy_graph_get_preset_color(0),
                           NULL);
    g_free(idnode);
    g_object_unref(siunitx);
    g_object_unref(siunity);
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, numpoints);
    g_free(xdata);
    g_free(ydata);
    gwy_graph_curve_model_enforce_order(gcmodel);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);
    g_string_free(caption, TRUE);
    g_free(header);

    return gmodel;
}

static GwyBrick*
wip_read_graph_image(GNode *node)
{
    WIPGraph *header;
    WIPSpectralTransform *xtransform;
    WIPSpectralInterpretation *xinterp;
    WIPAxis *waxis;
    WIPSpaceTransform *xyaxis;
    WIPIdNode *idnode;
    GwyBrick *brick = NULL;
    GwyDataLine *cal;
    GwySIUnit *siunitxy = NULL, *siunitz = NULL, *siunitw = NULL;
    gdouble *data;
    gdouble xscale, yscale, wscale;
    gint i, j, k;
    gint xres, yres, zres;
    gint power10xy = 0, power10w = 0;
    const guchar *p;

    header = g_new0(WIPGraph, 1);

    g_node_traverse(node, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                     wip_read_graph_tags, (gpointer)header);

    if ((header->sizex <= 1) && (header->sizey <= 1)) { /* not an image */
        g_free(header);
        return NULL;
    }

    gwy_debug("sizex = %d sizey = %d\n", header->sizex, header->sizey);
    gwy_debug("sizegraph = %d\n", header->sizegraph);
    gwy_debug("dimension=%d\n", header->dimension);
    gwy_debug("datatype = %d\n", header->datatype);
    gwy_debug("xrange = %d yrange = %d\n", header->xrange, header->yrange);

    xres = header->sizex;
    yres = header->sizey;
    zres = header->sizegraph;

    gwy_debug("numpoints * databytes = %" G_GSIZE_FORMAT ", datasize = %" G_GSIZE_FORMAT "\n",
              xres * yres * zres * WIPDataSize[header->datatype],
              header->datasize);
    if ((xres * yres * zres <= 0)
        || (header->datatype > 10)
        || (header->datasize != (WIPDataSize[header->datatype]
                                 * xres * yres * zres))) {
        g_free(header);
        return NULL;
    }

    /* Try to read xy units and scale */
    idnode = g_new0(WIPIdNode, 1);
    idnode->id = header->spacetransformid;
    g_node_traverse(g_node_get_root(node),
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_find_by_id, (gpointer)idnode);

    xyaxis = g_new0(WIPSpaceTransform, 1);
    g_node_traverse(idnode->node->parent->parent,
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_space_tr_tag, (gpointer)xyaxis);

    if (xyaxis->unitname)
        siunitxy = gwy_si_unit_new_parse(xyaxis->unitname, &power10xy);
    else
        siunitxy = gwy_si_unit_new(NULL);
    xscale = xyaxis->scale[0];
    yscale = xyaxis->scale[4];
    if (xscale == 0.0) {
        g_warning("Wrong x-scale");
        xscale = 1.0;
    }
    if (yscale == 0.0) {
        g_warning("Wrong y-scale");
        yscale = 1.0;
    }
    if (yscale < 0.0) {
        yscale = fabs(yscale);
    }
    if (xscale < 0.0) {
        xscale = fabs(xscale);
    }
    g_free(xyaxis);

    /* Try to read w units */
    idnode->id = header->zinterpid;
    g_node_traverse(g_node_get_root(node),
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_find_by_id, (gpointer)idnode);
    waxis = g_new0(WIPAxis, 1);
    g_node_traverse(idnode->node->parent->parent,
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_axis_tags,
                    (gpointer)waxis);
    if (waxis->unitname)
        siunitw = gwy_si_unit_new_parse(waxis->unitname, &power10w);
    else
        siunitw = gwy_si_unit_new(NULL);
    g_free(waxis);
    wscale = pow10(power10w);
    if (wscale == 0.0)
        wscale = 1.0;

    brick = gwy_brick_new(xres, yres, zres,
                          xres * pow10(power10xy) * xscale,
                          yres * pow10(power10xy) * yscale,
                          zres, TRUE);
    data = gwy_brick_get_data(brick);
    p = header->data;

    switch (header->datatype) {
        case WIP_DATA_LIST:
        case WIP_DATA_EXTENDED:
            /* cannot read this */
            break;
        case WIP_DATA_FLOAT:
            for (k = 0; k < zres; k++) {
                p = header->data;
                p += k * sizeof(gfloat);
                for (i = 0; i < xres; i++)
                    for (j = 0; j < yres; j++) {
                        *(data + i + j * xres + k * xres * yres)
                              = wscale * (gdouble)gwy_get_gfloat_le(&p);
                        p += (zres - 1) * sizeof(gfloat);
                    }
            }
            break;
        case WIP_DATA_DOUBLE:
            for (k = 0; k < zres; k++) {
                p = header->data;
                p += k * sizeof(gdouble);
                for (i = 0; i < xres; i++)
                    for (j = 0; j < yres; j++) {
                        *(data + i + j * xres + k * xres * yres)
                                      = wscale * gwy_get_gdouble_le(&p);
                        p += (zres - 1) * sizeof(gdouble);
                    }
            }
            break;
        case WIP_DATA_INT8:
        case WIP_DATA_UINT8:
        case WIP_DATA_BOOL:
            for (k = 0; k < zres; k++) {
                p = header->data + k;
                for (i = 0; i < xres; i++)
                    for (j = 0; j < yres; j++) {
                        *(data + i + j * xres + k * xres * yres)
                                                        = wscale * (*p);
                        p += zres;
                    }
            }
            break;
        case WIP_DATA_INT64:
            for (k = 0; k < zres; k++) {
                p = header->data + k * 8;
                for (i = 0; i < xres; i++)
                    for (j = 0; j < yres; j++) {
                        *(data + i + j * xres + k * xres * yres)
                          = wscale * GINT64_FROM_LE(*(const gint64 *)p);
                        p += zres * 8;
                    }
            }
            break;
        case WIP_DATA_INT32:
            for (k = 0; k < zres; k++) {
                p = header->data + k * 4;
                for (i = 0; i < xres; i++)
                    for (j = 0; j < yres; j++) {
                        *(data + i + j * xres + k * xres * yres)
                          = wscale * GINT32_FROM_LE(*(const gint32 *)p);
                        p += zres * 4;
                    }
            }
            break;
        case WIP_DATA_INT16:
            for (k = 0; k < zres; k++) {
                p = header->data + k * 2;
                for (i = 0; i < xres; i++)
                    for (j = 0; j < yres; j++) {
                        *(data + i + j * xres + k * xres * yres)
                          = wscale * GINT16_FROM_LE(*(const gint16 *)p);
                        p += zres * 2;
                    }
            }
            break;
        case WIP_DATA_UINT32:
            for (k = 0; k < zres; k++) {
                p = header->data + k * 4;
                for (i = 0; i < xres; i++)
                    for (j = 0; j < yres; j++) {
                        *(data + i + j * xres + k * xres * yres)
                        = wscale * GUINT32_FROM_LE(*(const guint32 *)p);
                        p += zres * 4;
                    }
            }
            break;
        case WIP_DATA_UINT16:
            for (k = 0; k < zres; k++) {
                p = header->data + k * 2;
                for (i = 0; i < xres; i++)
                    for (j = 0; j < yres; j++) {
                        *(data + i + j * xres + k * xres * yres)
                        = wscale * GUINT16_FROM_LE(*(const guint16 *)p);
                        p += zres * 2;
                    }
            }
            break;
        default:
            g_warning("Wrong datatype");
    }

    idnode->id = header->xinterpid;
    g_node_traverse(g_node_get_root(node),
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_find_by_id, (gpointer)idnode);

    xinterp = g_new0(WIPSpectralInterpretation, 1);
    g_node_traverse(idnode->node->parent->parent,
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_sp_interpr_tags,
                    (gpointer)xinterp);

    /* Try to read zcalibration */
    idnode->id = header->xtransformid;
    g_node_traverse(g_node_get_root(node),
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_find_by_id, (gpointer)idnode);

    xtransform = g_new0(WIPSpectralTransform, 1);
    g_node_traverse(idnode->node->parent->parent,
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_sp_transform_tags,
                    (gpointer)xtransform);
    if ((xtransform->transform_type != 1)
     || (xtransform->m < 0.01) || (xtransform->f < 0.01)
     || (xtransform->nc < 0.0) || (xtransform->nc > zres)) {
        /* xtransform not read correctly, fallback to point numbers */
    }
    else {
        cal = gwy_data_line_new(zres, zres, FALSE);
        data = gwy_data_line_get_data(cal);
        for (i = 0; i < zres; i++)
            data[i] = wip_pixel_to_lambda(i, xtransform);

        /* recalculating to right units */
        if (xtransform->unitname != NULL) {
            if ((xinterp->unitindex == WIP_UNIT_NANOMETER)
             || (xinterp->unitindex == WIP_UNIT_MIKROMETER)) {
                siunitz = gwy_si_unit_new("m");
                for (i = 0; i < zres; i++)
                    data[i] = data[i] * 1e-9;
            }
            else if (xinterp->unitindex == WIP_UNIT_SM_1) {
                siunitz = gwy_si_unit_new("1/m");
                for (i = 0; i < zres; i++)
                    data[i] = 1.0 / (data[i] * 1e-9);
            }
            else if (xinterp->unitindex == WIP_UNIT_RAMAN_SHIFT) {
                if (xinterp->excitation_wavelength == 0.0) {
                    siunitz = gwy_si_unit_new("m");
                    for (i = 0; i < zres; i++)
                        data[i] = data[i] * 1e-9;
                }
                else {
                    siunitz = gwy_si_unit_new("1/m");
                    for (i = 0; i < zres; i++)
                        data[i]
                         = 1.0 / (xinterp->excitation_wavelength * 1e-9)
                                               - 1.0 / (data[i] * 1e-9);
                }
            }
            else if ((xinterp->unitindex == WIP_UNIT_EV)
                  || (xinterp->unitindex == WIP_UNIT_MEV)) {
                siunitz = gwy_si_unit_new("eV");
                for (i = 0; i < zres; i++)
                    data[i] = H * C / (data[i] * 1e-9);
            }
            else if ((xinterp->unitindex == WIP_UNIT_EV_REL)
                  || (xinterp->unitindex == WIP_UNIT_MEV_REL)) {
                if (xinterp->excitation_wavelength == 0.0) {
                    siunitz = gwy_si_unit_new("m");
                    for (i = 0; i < zres; i++)
                        data[i] = data[i] * 1e-9;
                }
                else {
                    siunitz = gwy_si_unit_new("eV");
                    for (i = 0; i < zres; i++)
                        data[i]
                         = H*C / (xinterp->excitation_wavelength * 1e-9)
                                             - H * C / (data[i] * 1e-9);
                }
            }
            else {
                siunitz = gwy_si_unit_new("m");
                for (i = 0; i < zres; i++)
                    data[i] = data[i] * 1e-9;
            }
        }
        else
            siunitz = gwy_si_unit_new("pixels");

        gwy_data_line_set_si_unit_y(cal, siunitz);
        gwy_brick_set_zcalibration(brick, cal);
        g_object_unref(cal);
    }

    if (!siunitz)
        siunitz = gwy_si_unit_new("pixels");

    if (xtransform->unitname) {
        g_free(xtransform->unitname);
    }
    g_free(xtransform);
    g_free(xinterp);

    gwy_brick_set_si_unit_x(brick, siunitxy);
    gwy_brick_set_si_unit_y(brick, siunitxy);
    gwy_brick_set_si_unit_z(brick, siunitz);
    gwy_brick_set_si_unit_w(brick, siunitw);

    g_object_unref(siunitxy);
    g_object_unref(siunitz);
    g_object_unref(siunitw);

    g_free(idnode);

    return brick;
}

static GwyDataField*
wip_read_image(GNode *node)
{
    WIPImage *header;
    WIPAxis *zaxis;
    WIPSpaceTransform *xyaxis;
    WIPIdNode *idnode;
    GwyDataField *dfield, *dfield2;
    GwySIUnit *siunitxy, *siunitz;
    gdouble *data;
    gdouble xscale, yscale, zscale;
    gint i;
    gint power10z = 0;
    gint power10xy = 0;
    const guchar *p;
    gboolean mirrory = FALSE;
    gboolean mirrorx = FALSE;

    header = g_new0(WIPImage, 1);

    g_node_traverse(node, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_image_tags, (gpointer)header);

    if ((header->datatype > 11)
        || (header->sizex != header->xrange)
        || (header->sizey != header->yrange)
        || (header->datasize != (WIPDataSize[header->datatype]
                                 * header->sizex * header->sizey))) {
        g_free(header);
        return NULL;
    }

    /* Try to read z units */
    idnode = g_new0(WIPIdNode, 1);
    idnode->id = header->zinterpid;
    g_node_traverse(g_node_get_root(node),
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_find_by_id, (gpointer)idnode);
    zaxis = g_new0(WIPAxis, 1);
    g_node_traverse(idnode->node->parent->parent,
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_axis_tags,
                    (gpointer)zaxis);
    if (zaxis->unitname)
        siunitz = gwy_si_unit_new_parse(zaxis->unitname, &power10z);
    else
        siunitz = gwy_si_unit_new(NULL);
    g_free(zaxis);

    /* Try to read xy units and scale */
    idnode->id = header->postransformid;
    g_node_traverse(g_node_get_root(node),
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_find_by_id, (gpointer)idnode);
    xyaxis = g_new0(WIPSpaceTransform, 1);
    g_node_traverse(idnode->node->parent->parent,
                    G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_space_tr_tag, (gpointer)xyaxis);
    if (xyaxis->unitname)
        siunitxy = gwy_si_unit_new_parse(xyaxis->unitname, &power10xy);
    else
        siunitxy = gwy_si_unit_new(NULL);
    xscale = xyaxis->scale[0];
    yscale = xyaxis->scale[4];
    if (yscale == 0.0) {
        g_warning("Wrong y-scale");
        yscale = 1.0;
    }
    if (xscale == 0.0) {
        g_warning("Wrong x-scale");
        xscale = 1.0;
    }
    if (yscale < 0.0) {
        mirrory = TRUE;
        yscale = fabs(yscale);
    }
    if (xscale < 0.0) {
        mirrorx = TRUE;
        xscale = fabs(xscale);
    }
    g_free(xyaxis);
    g_free(idnode);

    zscale = pow10(power10z);
    if (zscale == 0.0)
        zscale = 1.0;
    p = header->data;

    /* Reading actual data
     *
     * FIXME
     *
     * data is stored in strange way in TDImage, so it is more
     * simple to swap X and Y axes here to read them correctly
     * and rotate datafield in the end of procedure */
    if (header->version == 0) {
        dfield2 = gwy_data_field_new(header->sizey, header->sizex,
                                     header->sizey * pow10(power10xy) * yscale,
                                     header->sizex * pow10(power10xy) * xscale,
                                     FALSE);
        data = gwy_data_field_get_data(dfield2);

        switch (header->datatype) {
            case WIP_DATA_LIST:
            case WIP_DATA_EXTENDED:
                /* cannot read this */
                break;
            case WIP_DATA_FLOAT:
                for (i = 0; i < header->sizex * header->sizey; i++)
                    data[i] = zscale * gwy_get_gfloat_le(&p);
                break;
            case WIP_DATA_DOUBLE:
                for (i = 0; i < header->sizex * header->sizey; i++)
                    data[i] = zscale * gwy_get_gdouble_le(&p);
                break;
            case WIP_DATA_INT8:
            case WIP_DATA_UINT8:
            case WIP_DATA_BOOL:
                for (i = 0; i < header->sizex * header->sizey; i++)
                    data[i] = *(p++) * zscale;
                break;
            case WIP_DATA_INT64:
                for (i = 0; i < header->sizex * header->sizey; i++) {
                    data[i] = GINT64_FROM_LE(*(const gint64 *)p) * zscale;
                    p += 8;
                }
                break;
            case WIP_DATA_INT32:
                for (i = 0; i < header->sizex * header->sizey; i++) {
                    data[i] = GINT32_FROM_LE(*(const gint32 *)p) * zscale;
                    p += 4;
                }
                break;
            case WIP_DATA_INT16:
                for (i = 0; i < header->sizex * header->sizey; i++) {
                    data[i] = GINT16_FROM_LE(*(const gint16 *)p) * zscale;
                    p += 2;
                }
                break;
            case WIP_DATA_UINT32:
                for (i = 0; i < header->sizex * header->sizey; i++) {
                    data[i] = GUINT32_FROM_LE(*(const guint32 *)p) * zscale;
                    p += 4;
                }
                break;
            case WIP_DATA_UINT16:
                for (i = 0; i < header->sizex * header->sizey; i++) {
                    data[i] = GUINT16_FROM_LE(*(const guint16 *)p) * zscale;
                    p += 2;
                }
                break;
            default:
                g_warning("Wrong datatype");
        }

        dfield = gwy_data_field_new_rotated_90(dfield2, TRUE);
        g_object_unref(dfield2);
    }
    else {
        dfield = gwy_data_field_new(header->sizex, header->sizey,
                                     header->sizex * pow10(power10xy) * xscale,
                                     header->sizey * pow10(power10xy) * yscale,
                                     FALSE);
        data = gwy_data_field_get_data(dfield);

        switch (header->datatype) {
            case WIP_DATA_LIST:
            case WIP_DATA_EXTENDED:
                /* cannot read this */
                break;
            case WIP_DATA_FLOAT:
                for (i = 0; i < header->sizex * header->sizey; i++)
                    data[i] = zscale * gwy_get_gfloat_le(&p);
                break;
            case WIP_DATA_DOUBLE:
                for (i = 0; i < header->sizex * header->sizey; i++)
                    data[i] = zscale * gwy_get_gdouble_le(&p);
                break;
            case WIP_DATA_INT8:
            case WIP_DATA_UINT8:
            case WIP_DATA_BOOL:
                for (i = 0; i < header->sizex * header->sizey; i++)
                    data[i] = *(p++) * zscale;
                break;
            case WIP_DATA_INT64:
                for (i = 0; i < header->sizex * header->sizey; i++) {
                    data[i] = GINT64_FROM_LE(*(const gint64 *)p) * zscale;
                    p += 8;
                }
                break;
            case WIP_DATA_INT32:
                for (i = 0; i < header->sizex * header->sizey; i++) {
                    data[i] = GINT32_FROM_LE(*(const gint32 *)p) * zscale;
                    p += 4;
                }
                break;
            case WIP_DATA_INT16:
                for (i = 0; i < header->sizex * header->sizey; i++) {
                    data[i] = GINT16_FROM_LE(*(const gint16 *)p) * zscale;
                    p += 2;
                }
                break;
            case WIP_DATA_UINT32:
                for (i = 0; i < header->sizex * header->sizey; i++) {
                    data[i] = GUINT32_FROM_LE(*(const guint32 *)p) * zscale;
                    p += 4;
                }
                break;
            case WIP_DATA_UINT16:
                for (i = 0; i < header->sizex * header->sizey; i++) {
                    data[i] = GUINT16_FROM_LE(*(const guint16 *)p) * zscale;
                    p += 2;
                }
                break;
            default:
                g_warning("Wrong datatype");
        }
    }

    gwy_data_field_set_si_unit_z(dfield, siunitz);
    gwy_data_field_set_si_unit_xy(dfield, siunitxy);
    g_object_unref(siunitz);
    g_object_unref(siunitxy);
    gwy_data_field_invert(dfield, mirrory, mirrorx, FALSE);

    return dfield;
}

static GwyDataField*
wip_read_bitmap(GNode *node)
{
    WIPBitmap *header;
    WIPSpaceTransform *xyaxis;
    WIPIdNode *idnode;
    GwyDataField *dfield;
    GwySIUnit *siunitxy;
    gdouble xscale, yscale;
    gint power10xy = 0;
    gboolean mirrorx = FALSE, mirrory = FALSE;

    header = g_new0(WIPBitmap, 1);

    g_node_traverse(node, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                     wip_read_bitmap_tags, (gpointer)header);

    /* Try to read xy units and scale */
    idnode = g_new0(WIPIdNode, 1);
    idnode->id = header->spacetransformid;
    g_node_traverse(g_node_get_root(node),
                     G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                     wip_find_by_id, (gpointer)idnode);
    xyaxis = g_new0(WIPSpaceTransform, 1);
    g_node_traverse(idnode->node->parent->parent,
                     G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                     wip_read_space_tr_tag, (gpointer)xyaxis);
    if (xyaxis->unitname)
        siunitxy = gwy_si_unit_new_parse(xyaxis->unitname, &power10xy);
    else
        siunitxy = gwy_si_unit_new(NULL);
    xscale = xyaxis->scale[0];
    yscale = xyaxis->scale[4];
    if (yscale == 0.0) {
        g_warning("Wrong y-scale");
        xscale = 1.0;
    }
    if (xscale == 0.0) {
        g_warning("Wrong x-scale");
        xscale = 1.0;
    }
    if (yscale < 0.0) {
        mirrory = TRUE;
        yscale = fabs(yscale);
    }
    if (xscale < 0.0) {
        mirrorx = TRUE;
        xscale = fabs(xscale);
    }
    g_free(xyaxis);
    g_free(idnode);

    dfield = wip_read_bmp(header->data, header->datasize,
                          xscale, yscale, power10xy);
    if (!dfield) {
        /* Error: failed to read BMP data */
    }
    else {
        gwy_data_field_set_si_unit_xy(dfield, siunitxy);
        gwy_data_field_invert(dfield, mirrory, mirrorx, FALSE);
    }

    g_object_unref(siunitxy);
    g_free(header);

    return dfield;
}

static gboolean
wip_read_data(GNode *node, gpointer filedata)
{
    WIPTag *tag;
    WIPFile *filecontent;
    GwyGraphModel *gmodel;
    GwyDataField *image;
    GwyBrick *brick;
    GString *key;
    GString *caption;

    tag = node->data;
    filecontent = (WIPFile *)filedata;
    key = g_string_new(NULL);
    if (!strncmp(tag->name, "TDGraph", 7)) {
        gmodel = wip_read_graph(node);
        if (!gmodel) {
            brick = wip_read_graph_image(node);
            if (!brick) {
                /* some error in brick read */
            }
            else {
                (filecontent->numbricks)++;
                g_string_printf(key, "/brick/%d",
                                filecontent->numbricks);
                gwy_container_set_object_by_name(filecontent->data,
                                                 key->str, brick);
                caption = g_string_new(NULL);
                g_node_traverse(node->parent, G_LEVEL_ORDER,
                                G_TRAVERSE_ALL, -1,
                                wip_read_caption, (gpointer)caption);
                if (!caption->str)
                    g_string_printf(caption, "Unnamed spectral image");
                g_string_append(key, "/title");
                gwy_container_set_string_by_name(filecontent->data,
                                                 key->str,
                                                 g_strdup(caption->str));
                g_string_free(caption, TRUE);
                g_object_unref(brick);
                gwy_file_volume_import_log_add(filecontent->data,
                                               filecontent->numbricks,
                                               NULL, filecontent->filename);
            }
        }
        else {
            (filecontent->numgraph)++;
            g_string_printf(key, "/0/graph/graph/%d",
                            filecontent->numgraph);
            gwy_container_set_object_by_name(filecontent->data,
                                             key->str, gmodel);
            g_object_unref(gmodel);
        }
    }
    else if (!strncmp(tag->name, "TDImage", 7)) {
        image = wip_read_image(node);
        if (!image) {
            /* some error in image read */
        }
        else {
            (filecontent->numimages)++;
            caption = g_string_new(NULL);
            g_node_traverse(node->parent, G_LEVEL_ORDER, G_TRAVERSE_ALL,
                            -1, wip_read_caption, (gpointer)caption);
            if (!caption->str)
                g_string_printf(caption, "Unnamed data");
            g_string_printf(key, "/%d/data", filecontent->numimages);
            gwy_container_set_object_by_name(filecontent->data,
                                             key->str, image);
            g_string_append(key, "/title");
            gwy_container_set_string_by_name(filecontent->data,
                                             key->str,
                                             g_strdup(caption->str));

            g_string_free(caption, TRUE);
            g_object_unref(image);

            gwy_file_channel_import_log_add(filecontent->data,
                                            filecontent->numimages,
                                            NULL, filecontent->filename);
        }
    }
    else if (!strncmp(tag->name, "TDBitmap", 8)) {
        image = wip_read_bitmap(node->parent);
        if (!image) {
            /* some error in bitmap read */
        }
        else {
            (filecontent->numimages)++;
            caption = g_string_new(NULL);
            g_node_traverse(node->parent, G_LEVEL_ORDER, G_TRAVERSE_ALL,
                            -1, wip_read_caption, (gpointer)caption);
            if (!caption->str)
                g_string_printf(caption, "Unnamed data");
            g_string_printf(key, "/%d/data", filecontent->numimages);
            gwy_container_set_object_by_name(filecontent->data,
                                             key->str, image);
            g_string_append(key, "/title");
            gwy_container_set_string_by_name(filecontent->data,
                                             key->str,
                                             g_strdup(caption->str));

            g_string_free(caption, TRUE);
            g_object_unref(image);

            gwy_file_channel_import_log_add(filecontent->data,
                                            filecontent->numimages,
                                            NULL, filecontent->filename);
        }
    }

    g_string_free(key, TRUE);

    return FALSE;
}


static GwyContainer*
wip_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    guchar *buffer;
    gsize size, cur;
    GError *err = NULL;
    guchar *p;
    WIPTag *tag;
    WIPFile *filedata;
    GNode *tagtree;
    GwyContainer *data;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    p = buffer + 8; /* skip magic header */
    cur = 8;
    if (!(tag = wip_read_tag(&p, &cur, &size))) {
        /* Error: tag cannot be read */
    }

    if ((tag->type)
     || (strncmp(tag->name, "WITec Project ", tag->name_length))) {
        err_FILE_TYPE(error, "WITec Project");
        wip_free_tag(tag);
        return NULL;
    }
    else {
        tagtree = g_node_new((gpointer)tag);
        wip_read_all_tags(buffer, tag->data_start,
                          tag->data_end, tagtree, 1);
    }

    data = gwy_container_new();
    filedata = g_new0(WIPFile, 1);
    filedata->numgraph = 0;
    filedata->data = data;
    filedata->filename = filename;

    g_node_traverse(tagtree, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_read_data, (gpointer)filedata);

    g_node_traverse(tagtree, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
                    wip_free_leave, NULL);

    g_node_destroy(tagtree);
    g_free(filedata);
    gwy_file_abandon_contents(buffer, size, NULL);

    return data;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
