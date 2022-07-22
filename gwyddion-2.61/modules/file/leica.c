/*
 *  $Id: leica.c 24205 2021-09-28 08:57:51Z yeti-dn $
 *  Copyright (C) 2014 Daniil Bratashov (dn2010), David Necas (Yeti)..
 *
 *  E-mail: dn2010@gmail.com.
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
 * <mime-type type="application/x-leica-spm">
 *   <comment>Leica LIF File</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="\x70\x00\x00\x00"/>
 *   </magic>
 *   <glob pattern="*.lif"/>
 *   <glob pattern="*.LIF"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Leica LIF
 * 0 string \x70\x00\x00\x00 Leica LIF File
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Leica LIF Data File
 * .lif
 * Read Volume
 **/

/*
 * TODO: laser and filter settings in metadata
 */

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
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "err.h"
#include "get.h"

#define MAGIC "\x70\x00\x00\x00"
#define MAGIC_SIZE (4)
#define TESTCODE 0x2a

#define EXTENSION ".lif"

typedef enum {
    DimNotValid = 0,
    DimX        = 1,
    DimY        = 2,
    DimZ        = 3,
    DimT        = 4,
    DimLambda   = 5,
    DimRotation = 6,
    DimXT       = 7,
    DimTSlice   = 8
} LIFDimID;

typedef struct {
    gint32 magic;
    guint32 size;
    gchar testcode;
    guint32 xmllen;
    gchar *xmlheader;
} LIFHeader;

typedef struct {
    gint32 magic;
    guint32 size;
    gchar testcode;
    guint64 memsize;
    guint32 desclen;
    gchar *memid;
    gpointer data;
} LIFMemBlock;

typedef struct {
    gint res;
    gdouble min;
    gdouble max;
    gchar *unit;
    gchar *lut;
    gsize bytesinc;
} LIFChannel;

typedef struct {
    gint dimid;
    gint res;
    gdouble origin;
    gdouble length;
    gchar *unit;
    gsize bytesinc;
} LIFDimension;

typedef struct {
    gchar  *name;
    gsize   memsize;
    gchar  *memid;
    GArray *channels;
    GArray *dimensions;
    GwyContainer *metadata;
} LIFElement;

typedef struct {
    gint        version;
    LIFHeader  *header;
    GArray     *elements;
    GHashTable *memblocks;
} LIFFile;

typedef struct {
    LIFFile *file;
    GPtrArray *elements;
} XMLParserData;

static gboolean      module_register     (void);
static gint          lif_detect          (const GwyFileDetectInfo *fileinfo,
                                          gboolean only_name);
static GwyContainer* lif_load            (const gchar *filename,
                                          GwyRunType mode,
                                          GError **error);
static LIFMemBlock*  lif_read_memblock   (const guchar *buffer,
                                          gsize *size,
                                          gint version);
static gboolean      lif_remove_memblock (gpointer key,
                                          gpointer value,
                                          gpointer user_data);
static void          header_start_element(GMarkupParseContext *context,
                                          const gchar *element_name,
                                          const gchar **attribute_names,
                                          const gchar **attribute_values,
                                          gpointer user_data,
                                          GError **error);
static void          header_end_element  (GMarkupParseContext *context,
                                          const gchar *element_name,
                                          gpointer user_data,
                                          GError **error);
static void          header_parse_text   (GMarkupParseContext *context,
                                          const gchar *text,
                                          gsize text_len,
                                          gpointer user_data,
                                          GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Leica CLSM image files (LIF)."),
    "Daniil Bratashov <dn2010@gmail.com>",
    "0.5",
    "Daniil Bratashov (dn2010), David Necas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, leica)

static gboolean
module_register(void)
{
    gwy_file_func_register("leica",
                           N_("Leica LIF image files (.lif)"),
                           (GwyFileDetectFunc)&lif_detect,
                           (GwyFileLoadFunc)&lif_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
lif_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return (g_str_has_suffix(fileinfo->name_lowercase, EXTENSION))
               ? 10 : 0;

    if (fileinfo->buffer_len > MAGIC_SIZE
        && memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) == 0)
        score = 100;

    return score;
}

static GwyContainer*
lif_load(const gchar *filename,
             G_GNUC_UNUSED GwyRunType mode,
             GError **error)
{
    GwyContainer *container = NULL;
    LIFHeader *header = NULL;
    LIFMemBlock *memblock = NULL;
    LIFFile *file = NULL;
    LIFElement *element = NULL;
    LIFDimension *dimension = NULL;
    LIFChannel *channel = NULL;
    gsize size = 0, memblock_size = 0;
    gint64 remaining = 0;
    gchar *buffer;
    const guchar *p, *tp;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    GwyBrick *brick = NULL;
    gdouble *data = NULL;
    gint i, j, channelno = 0, volumeno = 0;
    gchar *strkey, *lutname;
    GMarkupParser parser = {
        header_start_element,
        header_end_element,
        header_parse_text, NULL, NULL
    };
    GMarkupParseContext *context;
    XMLParserData *xmldata;
    gint x, xres, xstep, y, yres, ystep, z, zres, zstep, offset, res;
    gdouble xreal, yreal, zreal, xoffset, yoffset, zoffset;
    gdouble zscale = 1.0, wscale = 1.0;
    GwySIUnit *siunitxy = NULL, *siunitz = NULL;
    GwySIUnit *siunitx = NULL, *siunity = NULL, *siunitw = NULL;
    gint power10xy = 1;
    gint power10x = 1, power10y = 1, power10z = 1, power10w = 1;
    gboolean flipz = FALSE;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        goto fail;
    }

    if (size < 13) { /* header too short */
        err_TOO_SHORT(error);
        goto fail;
    }

    p = buffer;
    remaining = size;
    header = g_new0(LIFHeader, 1);
    header->magic = gwy_get_gint32_le(&p);
    gwy_debug("Magic = %d", header->magic);
    header->size = gwy_get_guint32_le(&p);
    gwy_debug("Size = %d", header->size);
    header->testcode = *(p++);
    gwy_debug("Testcode = 0x%x", header->testcode);
    if (header->testcode != TESTCODE) {
        err_FILE_TYPE(error, "Leica LIF");
        goto fail;
    }
    header->xmllen = gwy_get_guint32_le(&p);
    gwy_debug("XML length = %d", header->xmllen);
    if (size < 13 + header->xmllen * 2) {
        err_TOO_SHORT(error);
        goto fail;
    }

    remaining -= 13;

    header->xmlheader = gwy_utf16_to_utf8((const gunichar2*)p, header->xmllen,
                                          GWY_BYTE_ORDER_LITTLE_ENDIAN);
    p += header->xmllen * 2;
    remaining -= header->xmllen * 2;

    // Uncomment to see large XML header
    // gwy_debug("%s", header->xmlheader);

    /* Parse XML header */
    xmldata = g_new0(XMLParserData, 1);
    xmldata->file = g_new0(LIFFile, 1);
    xmldata->file->elements = g_array_new(FALSE, TRUE, sizeof(LIFElement));
    xmldata->elements = g_ptr_array_new();

    context = g_markup_parse_context_new(&parser,
                                         G_MARKUP_TREAT_CDATA_AS_TEXT,
                                         (gpointer)xmldata,
                                         NULL);

    if (!g_markup_parse_context_parse(context, header->xmlheader, -1, &err)
        || !g_markup_parse_context_end_parse(context, &err)) {
        error = &err;
        g_clear_error(&err);
    }
    g_markup_parse_context_free(context);
    file = xmldata->file;
    file->header = header;
    g_ptr_array_free(xmldata->elements, TRUE);
    g_free(xmldata);

    /* Reading memblocks */
    file->memblocks = g_hash_table_new(g_str_hash, g_str_equal);
    while (remaining > 0) {
        memblock = lif_read_memblock(p, &memblock_size, file->version);
        if (!memblock) {
            break;
        }
        remaining -= memblock_size;
        if (remaining >= 0) {
            gwy_debug("remaining = %" G_GUINT64_FORMAT "", remaining);
            p += memblock_size;
            g_hash_table_insert(file->memblocks, memblock->memid,
                                memblock);
        }
    }

    container = gwy_container_new();

    for (i = 0; i < file->elements->len; i++) {
        element = &g_array_index(file->elements, LIFElement, i);

        if ((element->dimensions == NULL)
                                       || (element->channels == NULL)) {
            gwy_debug("Empty element");
            continue;
        }

        gwy_debug("Dimensions = %d channels=%d",
                  element->dimensions->len,
                  element->channels->len);
        gwy_debug("memid=%s", element->memid);

        /* check if we can load this type of data into
         * Gwyddion structures */
        res = 0;
        if ((element->dimensions->len != 2)
                                   && (element->dimensions->len != 3)) {

            /* check for case ndim == 4 && res == 1 */
            for (j = 0; j < element->dimensions->len; j++) {
                dimension = &g_array_index(element->dimensions,
                                           LIFDimension, j);
                xres = dimension->res;
                gwy_debug("dim[%d].res=%d", j, xres);
                if (j == 2) {
                    res = xres;
                }
            }
            if ((element->dimensions->len == 4) && (res == 1)) {
                gwy_debug("4D volume");
            }
            else {
                gwy_debug("not loading");
                continue;
            }
        }

        memblock = (LIFMemBlock *)g_hash_table_lookup(file->memblocks,
                                                      element->memid);
        if (!memblock) {
            gwy_debug("Failed to locate memblock with key %s",
                      element->memid);

            continue;
        }

        p = memblock->data;
        if (element->dimensions->len == 2) { /* Image */
            for (j = 0; j < element->channels->len; j++) {
                dimension = &g_array_index(element->dimensions,
                                           LIFDimension, 0);
                xres = dimension->res;
                xreal = dimension->length;
                xoffset = dimension->origin;
                xstep = dimension->bytesinc;
                siunitxy = gwy_si_unit_new_parse(dimension->unit,
                                                 &power10xy);

                dimension = &g_array_index(element->dimensions,
                                           LIFDimension, 1);
                yres = dimension->res;
                yreal = dimension->length;
                yoffset = dimension->origin;
                ystep = dimension->bytesinc;

                if (xreal <= 0.0)
                    xreal = 1.0;
                if (yreal <= 0.0)
                    yreal = 1.0;

                channel = &g_array_index(element->channels,
                                         LIFChannel, j);
                offset = channel->bytesinc;
                siunitz = gwy_si_unit_new_parse(channel->unit,
                                                &power10z);

                zscale = pow10(power10z);
                if (offset + (xres - 1) * xstep + (yres - 1)* ystep
                                                  > memblock->memsize) {
                    gwy_debug("Memblock too small");
                    gwy_debug("%d %" G_GUINT64_FORMAT "",
                              offset + (xres-1)*xstep + (yres-1)*ystep,
                              memblock->memsize);
                    err_SIZE_MISMATCH(error,
                                      memblock->memsize,
                                      offset+(xres-1)*xstep
                                                        +(yres-1)*ystep,
                                      FALSE);
                    goto fail;
                }

                dfield = gwy_data_field_new(xres, yres,
                                            xreal*pow10(power10xy),
                                            yreal*pow10(power10xy),
                                            TRUE);
                gwy_data_field_set_xoffset(dfield,
                                           xoffset*pow10(power10xy));
                gwy_data_field_set_yoffset(dfield,
                                           yoffset*pow10(power10xy));

                data = gwy_data_field_get_data(dfield);
                if (xstep == 1)
                    for (y = 0; y < yres; y++)
                        for (x = 0; x < xres; x++) {
                            *(data++) = zscale * (gdouble)*(p + offset
                                                   + x*xstep + y*ystep);
                        }
                else if (xstep == 2)
                    for (y = 0; y < yres; y++)
                        for (x = 0; x < xres; x++) {
                            tp = p + offset + x*xstep + y*ystep;
                            *(data++) = zscale * gwy_get_guint16_le(&tp);
                        }

                if (siunitxy) {
                    gwy_data_field_set_si_unit_xy(dfield, siunitxy);
                    g_object_unref(siunitxy);
                }
                if (siunitz) {
                    gwy_data_field_set_si_unit_z(dfield, siunitz);
                    g_object_unref(siunitz);
                }

                strkey = g_strdup_printf("/%d/data", channelno);
                gwy_container_set_object_by_name(container,
                                                 strkey,
                                                 dfield);
                g_object_unref(dfield);
                g_free(strkey);

                if (element->name) {
                    strkey = g_strdup_printf("/%d/data/title",
                                             channelno);
                    gwy_container_set_string_by_name(container, strkey,
                                               g_strdup(element->name));
                    g_free(strkey);
                }

                if (element->metadata) {
                    strkey = g_strdup_printf("/%d/meta",
                                             channelno);
                    gwy_container_set_object_by_name(container,
                                                     strkey,
                                                     element->metadata);
                    g_free(strkey);
                }

                if (channel->lut) {
                    lutname = NULL;
                    if (gwy_strequal(channel->lut, "Red"))
                        lutname = g_strdup_printf("RGB-Red");
                    else if (gwy_strequal(channel->lut, "Green"))
                        lutname = g_strdup_printf("RGB-Green");
                    else if (gwy_strequal(channel->lut, "Blue"))
                        lutname = g_strdup_printf("RGB-Blue");
                    else if (gwy_strequal(channel->lut, "Gray"))
                        lutname = g_strdup_printf("Gray");
                    if (lutname) {
                        strkey = g_strdup_printf("/%u/base/palette",
                                                 channelno);
                        gwy_container_set_string_by_name(container,
                                                         strkey,
                                                         lutname);
                        g_free(strkey);
                    }
                }

                gwy_file_channel_import_log_add(container, channelno,
                                                NULL, filename);

                channelno++;
            }
        }
        else if ((element->dimensions->len == 3)
             || ((element->dimensions->len == 4) && (res == 1))) {
            /* Volume */
            for (j = 0; j < element->channels->len; j++) {
                dimension = &g_array_index(element->dimensions,
                                           LIFDimension, 0);
                xres = dimension->res;
                xreal = dimension->length;
                gwy_debug("xreal = %g", xreal);
                xoffset = dimension->origin;
                xstep = dimension->bytesinc;
                siunitx = gwy_si_unit_new_parse(dimension->unit,
                                                &power10x);

                dimension = &g_array_index(element->dimensions,
                                           LIFDimension, 1);
                yres = dimension->res;
                yreal = dimension->length;
                gwy_debug("yreal = %g", yreal);
                yoffset = dimension->origin;
                ystep = dimension->bytesinc;
                siunity = gwy_si_unit_new_parse(dimension->unit,
                                                &power10y);

                if (element->dimensions->len == 3) {
                    dimension = &g_array_index(element->dimensions,
                                               LIFDimension, 2);
                }
                else {
                    dimension = &g_array_index(element->dimensions,
                                               LIFDimension, 3);
                }
                zres = dimension->res;
                zreal = dimension->length;
                gwy_debug("zreal = %g", zreal);
                zoffset = dimension->origin;
                if (zreal < 0.0) {
                    zreal = -zreal;
                    zoffset -= zreal;
                    flipz = TRUE;
                }
                zstep = dimension->bytesinc;
                siunitz = gwy_si_unit_new_parse(dimension->unit,
                                                &power10z);

                channel = &g_array_index(element->channels,
                                         LIFChannel, j);
                offset = channel->bytesinc;
                siunitw = gwy_si_unit_new_parse(channel->unit,
                                                &power10w);
                wscale = pow10(power10w);

                if (offset
                      + (xres-1)*xstep + (yres-1)*ystep + (zres-1)*zstep
                                                  > memblock->memsize) {
                    gwy_debug("Memblock too small");
                    gwy_debug("%d %" G_GUINT64_FORMAT "",
                              offset + (xres-1)*xstep
                                      + (yres-1)*ystep + (zres-1)*zstep,
                              memblock->memsize);
                    err_SIZE_MISMATCH(error,
                                      memblock->memsize,
                                      offset + (xres-1)*xstep
                                      + (yres-1)*ystep + (zres-1)*zstep,
                                      FALSE);
                    goto fail;
                }
                brick = gwy_brick_new(xres, yres, zres,
                                      xreal*pow10(power10x),
                                      yreal*pow10(power10y),
                                      zreal*pow10(power10z),
                                      TRUE);
                gwy_brick_set_xoffset(brick, xoffset*pow10(power10x));
                gwy_brick_set_yoffset(brick, yoffset*pow10(power10y));
                gwy_brick_set_zoffset(brick, zoffset*pow10(power10z));

                data = gwy_brick_get_data(brick);

                if (!flipz) {
                    if (xstep == 1)
                        for (z = 0; z < zres; z++)
                            for (y = 0; y < yres; y++)
                                for (x = 0; x < xres; x++) {
                                    *(data++) = wscale * (gdouble)*(p
                                         + offset
                                         + x*xstep + y*ystep + z*zstep);
                                }
                    else if (xstep == 2)
                        for (z = 0; z < zres; z++)
                            for (y = 0; y < yres; y++)
                                for (x = 0; x < xres; x++) {
                                    tp = p + offset
                                          + x*xstep + y*ystep + z*zstep;
                                    *(data++)
                                     = wscale * gwy_get_guint16_le(&tp);
                                }
                }
                else {
                    if (xstep == 1)
                        for (z = zres-1; z >= 0; z--)
                            for (y = 0; y < yres; y++)
                                for (x = 0; x < xres; x++) {
                                    *(data++) = wscale * (gdouble)*(p
                                         + offset
                                         + x*xstep + y*ystep + z*zstep);
                                }
                    else if (xstep == 2)
                        for (z = zres-1; z >= 0; z--)
                            for (y = 0; y < yres; y++)
                                for (x = 0; x < xres; x++) {
                                    tp = p + offset
                                          + x*xstep + y*ystep + z*zstep;
                                    *(data++)
                                     = wscale * gwy_get_guint16_le(&tp);
                                }
                }

                if (siunitx) {
                    gwy_brick_set_si_unit_x(brick, siunitx);
                    g_object_unref(siunitx);
                }
                if (siunity) {
                    gwy_brick_set_si_unit_y(brick, siunity);
                    g_object_unref(siunity);
                }
                if (siunitz) {
                    gwy_brick_set_si_unit_z(brick, siunitz);
                    g_object_unref(siunitz);
                }
                if (siunitw) {
                    gwy_brick_set_si_unit_w(brick, siunitw);
                    g_object_unref(siunitw);
                }

                strkey = g_strdup_printf("/brick/%d", volumeno);
                gwy_container_set_object_by_name(container,
                                                 strkey,
                                                 brick);
                g_free(strkey);

                if (element->name) {
                    strkey = g_strdup_printf("/brick/%d/title",
                                             volumeno);
                    gwy_container_set_string_by_name(container, strkey,
                                               g_strdup(element->name));
                    g_free(strkey);
                }

                if (element->metadata) {
                    strkey = g_strdup_printf("/brick/%d/meta",
                                             volumeno);
                    gwy_container_set_object_by_name(container,
                                                     strkey,
                                                     element->metadata);
                    g_free(strkey);
                }

                if (channel->lut) {
                    lutname = NULL;
                    if (gwy_strequal(channel->lut, "Red"))
                        lutname = g_strdup_printf("RGB-Red");
                    else if (gwy_strequal(channel->lut, "Green"))
                        lutname = g_strdup_printf("RGB-Green");
                    else if (gwy_strequal(channel->lut, "Blue"))
                        lutname = g_strdup_printf("RGB-Blue");
                    else if (gwy_strequal(channel->lut, "Gray"))
                        lutname = g_strdup_printf("Gray");
                    if (lutname) {
                        strkey = g_strdup_printf("/brick/%d/preview/palette",
                                                 volumeno);
                        gwy_container_set_string_by_name(container,
                                                         strkey,
                                                         lutname);
                        g_free(strkey);
                    }
                }

                g_object_unref(brick);
                gwy_file_volume_import_log_add(container, volumeno,
                                               NULL, filename);

                volumeno++;
            } /* for (channels) */
        } /* if (volume) */
    }

fail:
    /* freeing all stuff */
    if (file) {
        if (file->memblocks) {
            g_hash_table_foreach_remove(file->memblocks,
                                        lif_remove_memblock,
                                        NULL);
            g_hash_table_unref(file->memblocks);
        }
        if (file->elements) {
            for (i = 0; i < file->elements->len; i++) {
                element = &g_array_index(file->elements, LIFElement, i);
                if (element->dimensions) {
                    for (j = 0; j < element->dimensions->len; j++) {
                        dimension = &g_array_index(element->dimensions,
                                                   LIFDimension, j);
                        if (dimension->unit)
                            g_free(dimension->unit);
                    }
                    g_array_free(element->dimensions, TRUE);
                }
                if (element->channels) {
                    for (j = 0; j < element->channels->len; j++) {
                        channel = &g_array_index(element->channels,
                                                 LIFChannel, j);
                        if (channel->unit)
                            g_free(channel->unit);
                        if (channel->lut)
                            g_free(channel->lut);
                    }
                    g_array_free(element->channels, TRUE);
                }

                if (element->name)
                    g_free(element->name);
                if (element->memid)
                    g_free(element->memid);
                if (element->metadata)
                    g_object_unref(element->metadata);
            }
            g_array_free(file->elements, TRUE);
        }
        g_free(file);
    }
    if (header && header->xmlheader)
        g_free(header->xmlheader);
    if (header) {
        g_free(header);
    }

    return container;
}

static LIFMemBlock*
lif_read_memblock(const guchar *buffer, gsize *size, gint version)
{
    LIFMemBlock *memblock;
    const guchar *p;
    int i;

    p = buffer;
    memblock = g_new0(LIFMemBlock, 1);
    memblock->magic = gwy_get_gint32_le(&p);
    gwy_debug("Magic = %d", memblock->magic);

    if (memcmp(&memblock->magic, MAGIC, MAGIC_SIZE) != 0) {
        gwy_debug("Wrong magic for memblock");
        size = 0;
        g_free(memblock);
        return NULL;
    }
    memblock->size = gwy_get_guint32_le(&p);
    gwy_debug("Size = %d", memblock->size);
    memblock->testcode = *(p++);
    gwy_debug("Testcode = 0x%x", memblock->testcode);
    if (memblock->testcode != TESTCODE) {
        gwy_debug("Wrong testcode for memblock");
        g_free(memblock);
        size = 0;
        return NULL;
    }
    if (version == 1) {
        memblock->memsize = gwy_get_guint32_le(&p);
    }
    else if (version >= 2) {
        memblock->memsize = gwy_get_guint64_le(&p);
    }
    gwy_debug("data length = %" G_GUINT64_FORMAT "", memblock->memsize);

    i = 0;
    while (*(p++) != TESTCODE) {
        i++;
    }
    gwy_debug("skipped %d bytes", i);
    memblock->desclen = gwy_get_guint32_le(&p);
    gwy_debug("description length = %d", memblock->desclen);
    memblock->memid = gwy_utf16_to_utf8((const gunichar2*)p, memblock->desclen,
                                        GWY_BYTE_ORDER_LITTLE_ENDIAN);
    gwy_debug("description = %s", memblock->memid);
    p += memblock->desclen * 2;
    memblock->data = (gpointer)p;

    *size = (gsize)(p - buffer) + memblock->memsize;
    return memblock;
}

static gboolean
lif_remove_memblock(G_GNUC_UNUSED gpointer key,
                    gpointer value,
                    G_GNUC_UNUSED gpointer user_data)
{
    LIFMemBlock *memblock;

    memblock = (LIFMemBlock *)value;
    if (memblock->memid)
        g_free(memblock->memid);

    g_free(memblock);

    return TRUE;
}

static void
header_start_element(G_GNUC_UNUSED GMarkupParseContext *context,
                     const gchar *element_name,
                     const gchar **attribute_names,
                     const gchar **attribute_values,
                     gpointer user_data,
                     GError **error)
{
    const gchar **name_cursor = attribute_names;
    const gchar **value_cursor = attribute_values;
    gchar *name, *value;
    XMLParserData *data;
    LIFElement *element;

    data = (XMLParserData *)user_data;

    // gwy_debug("Name = %s", element_name);
    if (gwy_strequal(element_name, "LMSDataContainerHeader")) {
        while (*name_cursor) {
            if (gwy_strequal(*name_cursor, "Version")) {
                data->file->version = atoi(*value_cursor);
            }

            name_cursor++;
            value_cursor++;
        }
    }
    else if (gwy_strequal(element_name, "Element")) {
        element = g_new0(LIFElement, 1);

        while (*name_cursor) {
            if (gwy_strequal(*name_cursor, "Name")) {
                element->name = g_strdup(*value_cursor);
            }

            name_cursor++;
            value_cursor++;
        }

        g_ptr_array_add(data->elements, (gpointer)element);
    }
    else if (gwy_strequal(element_name, "Memory")) {
        if (!(data->elements->len)) {
            gwy_debug("Wrong XML Memory block");
            err_FILE_TYPE(error, "Leica LIF");
            goto fail_xml;
        }

        element = (LIFElement *)g_ptr_array_index(data->elements,
                                               data->elements->len - 1);
        while (*name_cursor) {
            if (gwy_strequal(*name_cursor, "Size")) {
                element->memsize
                            = g_ascii_strtoull(*value_cursor, NULL, 10);
            }
            else if (gwy_strequal(*name_cursor, "MemoryBlockID")) {
                element->memid = g_strdup(*value_cursor);
            }

            name_cursor++;
            value_cursor++;
        }

        if (!(element->memid)) {
            gwy_debug("Wrong XML: Element has no MemID");
            err_FILE_TYPE(error, "Leica LIF");
        }
    }
    else if (gwy_strequal(element_name, "ChannelDescription")) {
        LIFChannel *channel = NULL;

        if (!(data->elements->len)) {
            gwy_debug("Wrong XML ChannelDescription block");
            err_FILE_TYPE(error, "Leica LIF");
            goto fail_xml;
        }

        element = (LIFElement *)g_ptr_array_index(data->elements,
                                               data->elements->len - 1);
        channel = g_new0(LIFChannel, 1);

        while (*name_cursor) {
            if (gwy_strequal(*name_cursor, "Resolution")) {
                channel->res = atoi(*value_cursor);
            }
            else if (gwy_strequal(*name_cursor, "Min")) {
                channel->min = g_ascii_strtod(*value_cursor, NULL);
            }
            else if (gwy_strequal(*name_cursor, "Max")) {
                channel->max = g_ascii_strtod(*value_cursor, NULL);
            }
            else if (gwy_strequal(*name_cursor, "Unit")) {
                channel->unit = g_strdup(*value_cursor);
            }
            else if (gwy_strequal(*name_cursor, "LUTName")) {
                channel->lut = g_strdup(*value_cursor);
            }
            else if (gwy_strequal(*name_cursor, "BytesInc")) {
                channel->bytesinc = g_ascii_strtoull(*value_cursor,
                                                     NULL, 10);
            }
            name_cursor++;
            value_cursor++;
        }

        if (!(element->channels)) {
            element->channels = g_array_new(FALSE, TRUE,
                                            sizeof(LIFChannel));
        }

        g_array_append_val(element->channels, *channel);
    }
    else if (gwy_strequal(element_name, "DimensionDescription")) {
        LIFDimension *dimension = NULL;

        if (!(data->elements->len)) {
            gwy_debug("Wrong XML DimensionDescription block");
            err_FILE_TYPE(error, "Leica LIF");
            goto fail_xml;
        }

        element = (LIFElement *)g_ptr_array_index(data->elements,
                                               data->elements->len - 1);
        dimension = g_new0(LIFDimension, 1);

        while (*name_cursor) {
            if (gwy_strequal(*name_cursor, "DimID")) {
                dimension->dimid = atoi(*value_cursor);
            }
            else if (gwy_strequal(*name_cursor, "NumberOfElements")) {
                dimension->res = atoi(*value_cursor);
            }
            else if (gwy_strequal(*name_cursor, "Origin")) {
                dimension->origin = g_ascii_strtod(*value_cursor, NULL);
            }
            else if (gwy_strequal(*name_cursor, "Length")) {
                dimension->length = g_ascii_strtod(*value_cursor, NULL);
            }
            else if (gwy_strequal(*name_cursor, "Unit")) {
                dimension->unit = g_strdup(*value_cursor);
            }
            else if (gwy_strequal(*name_cursor, "BytesInc")) {
                dimension->bytesinc = g_ascii_strtoull(*value_cursor,
                                                     NULL, 10);
            }
            name_cursor++;
            value_cursor++;
        }
        if (!(element->dimensions)) {
            element->dimensions
                       = g_array_new(FALSE, TRUE, sizeof(LIFDimension));
        }

        g_array_append_val(element->dimensions, *dimension);
    }
    else if (gwy_strequal(element_name,
                          "ATLConfocalSettingDefinition")) {
        if (!(data->elements->len)) {
            gwy_debug("Wrong XML ATLConfocalSettingDefinition block");
            err_FILE_TYPE(error, "Leica LIF");
            goto fail_xml;
        }

        element = (LIFElement *)g_ptr_array_index(data->elements,
                                                  data->elements->len - 1);

        if (!(element->metadata)) {
            element->metadata = gwy_container_new();
        }

        while (*name_cursor) {
            name = g_strdup(*name_cursor);
            value = g_strdup(*value_cursor);
            gwy_container_set_string_by_name(element->metadata,
                                             name, value);
            g_free(name);

            name_cursor++;
            value_cursor++;
        }

    }

fail_xml:
    return;
}

static void
header_end_element(G_GNUC_UNUSED GMarkupParseContext *context,
                   const gchar *element_name,
                   gpointer user_data,
                   G_GNUC_UNUSED GError **error)
{
    XMLParserData *data;
    LIFElement *element;

    data = (XMLParserData *)user_data;

    // gwy_debug("End name = %s", element_name);
    if (gwy_strequal(element_name, "Element")) {
        element = (LIFElement *)g_ptr_array_index(data->elements,
                                                  data->elements->len - 1);
        if (!(element->memid)) {
            gwy_debug("Wrong XML: Element has no MemID");
            err_FILE_TYPE(error, "Leica LIF");
        }
        else {
            g_array_append_val(data->file->elements, *element);
            g_ptr_array_remove_index(data->elements,
                                     data->elements->len - 1);
        }
    }
}

static void
header_parse_text(G_GNUC_UNUSED GMarkupParseContext *context,
                  G_GNUC_UNUSED const gchar *value,
                  G_GNUC_UNUSED gsize value_len,
                  G_GNUC_UNUSED gpointer user_data,
                  G_GNUC_UNUSED GError **error)
{
    // gwy_debug("Text = %s", value);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
