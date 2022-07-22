/*
 *  $Id: oirfile.c 22968 2020-11-26 13:53:48Z yeti-dn $
 *  Copyright (C) 2019-2020 David Necas (Yeti).
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

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-olympus-oir">
 *   <comment>Olympus OIR data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="OLYMPUSRAWFORMAT"/>
 *   </magic>
 *   <glob pattern="*.oir"/>
 *   <glob pattern="*.OIR"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-olympus-poir">
 *   <comment>Olympus packed OIR data</comment>
 *   <glob pattern="*.poir"/>
 *   <glob pattern="*.POIR"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Olympus OIR
 * .oir
 * Read
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Olympus packed OIR
 * .poir
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymd5.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>
#include "err.h"
#include "get.h"
#include "gwyzip.h"

#define OIR_MAGIC "OLYMPUSRAWFORMAT"
#define OIR_MAGIC_SIZE (sizeof(OIR_MAGIC)-1)

#define POIR_MAGIC "PK\x03\x04"
#define POIR_MAGIC_SIZE (sizeof(POIR_MAGIC)-1)

#define XML_MAGIC "<?xml version=\"1.0\" encoding=\"ASCII\"?>"
#define XML_MAGIC_SIZE (sizeof(XML_MAGIC)-1)

#define EXTENSION ".oir"

enum {
    HEADER_LENGTH = 96,
};

typedef enum {
    OIR_CHUNK_XML        = 1,
    OIR_CHUNK_BMP        = 2,
    OIR_CHUNK_WTF        = 3,
    OIR_CHUNK_TERMINATOR = 96,
} OIRChunkType;

typedef struct {
    guchar magic[16];         /* = text OLYMPUSRAWFORMAT */
    guint32 unknown1;         /* = 12 */
    guint32 unknown2;         /* = 0 */
    guint32 unknown3;         /* = 1 */
    guint32 unknown4;         /* = 2 */
    guint32 file_size;        /* total file length in bytes */
    guint32 unknown5;         /* = 2 */
    guint32 some_size;        /* file length minus 140 bytes - why? */
    guint32 unknown6;         /* = 0 */
    guint32 unknown7;         /* = 17 */
    guint32 unknown8;         /* = 0 */
    guint32 unknown9;         /* = 1 */
    guint32 unknown10;        /* = 0 */
    guint32 unknown11;        /* = 96 (colour), ~ file size (LSM) */
    guint32 unknown12;        /* = 0 */
    guchar unknown_str[8];    /* = text UNKNOWN + 0x00 */
    guint32 unknown13;        /* = 1 */
    guint32 unknown14;        /* = 1 */
    guint32 unknown15;        /* = 0xffffffff */
    guint32 unknown16;        /* = 0xffffffff */
} OIRFileHeader;

typedef struct {
    guint32 remainder_size;   /* without the four first integers */
    OIRChunkType chunktype;   /* = 1, 2, or 3 */
    guint32 unknown2;         /* = 0 */
    guint32 image_size;       /* = 935*1024 */
    guint32 uuid_size;        /* = 50 or so, length of uuID text */
    const guchar *uuid;       /* UUID fragment */
    guint32 image_size_again; /* = 935*1024 */
    guint32 unknown3;         /* = 4 */
    const guchar *data;       /* image data */
} OIRImageDataBlock;

/* Bare XML fragment.  They are accompanied/surrounded by various binary data,
 * but their structres vary. */
typedef struct {
    guint32 size;             /* length of the following XML text fragment */
    const guchar *xml;        /* XML fragment */
    guint32 root_size;        /* length of the following root tag name */
    const guchar *root_name;  /* name of XML fragment root tag */
    guchar md5[16];
} OIRXMLFragment;

typedef struct {
    /* These two are a part of a common header. */
    guint32 content_size;     /* total size of the contained blocks? */
    guint32 unknown1;         /* = 0 or 1, but normally 1? */

    guint32 id;               /* = sequential fragment number, from 1 */
    guint32 unknown3;         /* = 2 */
    guint32 unknown4;         /* = 1 */
    guint32 unknown5;         /* = 4 */
    guint32 xml_dxx;          /* = 0xd48, 0xdc6 or a similar number for pure
                               * XML, smaller when there is an UUID */
    guint32 unknown7;         /* = 1 */
    guint32 unknown8;         /* = 1 */
    guint32 unknown9;         /* = 1 */
    guint32 unknown10;        /* = 1 */
    OIRXMLFragment xml;
} OIRImageXMLFragment;

typedef struct {
    /* These two are a part of a common header. */
    guint32 content_size;     /* total size of the contained blocks? */
    guint nfragments;
    OIRXMLFragment *fragments;
} OIRMetaData;

typedef struct {
    GString *path;
    GHashTable *hash;
    GArray *scales;
} OIRXMLParserData;

typedef struct {
    OIRFileHeader header;
    OIRImageDataBlock wtf1;
    OIRImageDataBlock wtf2;
    OIRImageDataBlock wtf3;
    OIRImageXMLFragment imgmeta;
    OIRImageDataBlock red;
    OIRImageDataBlock green;
    OIRImageDataBlock blue;
    OIRMetaData moremeta;
    OIRXMLParserData xmldata;
} OIRFile;

static gboolean      module_register         (void);
static gint          oirfile_detect          (const GwyFileDetectInfo *fileinfo,
                                              gboolean only_name);
static GwyContainer* oirfile_load            (const gchar *filename,
                                              GwyRunType mode,
                                              GError **error);
#ifdef HAVE_GWYZIP
static GwyContainer* poirfile_load           (const gchar *filename,
                                              GwyRunType mode,
                                              GError **error);
static gint          poirfile_detect         (const GwyFileDetectInfo *fileinfo,
                                              gboolean only_name);
#endif
static gint          oirfile_load_from_memory(GwyContainer *container,
                                              gint channelno,
                                              const guchar *buffer,
                                              gsize size,
                                              const gchar *filename,
                                              GError **error);
static void          oirfile_free            (OIRFile *oirfile);
static gboolean      read_file_header        (const guchar **p,
                                              const guchar *end,
                                              OIRFileHeader *header,
                                              GError **error);
static gboolean      read_image_data_block   (const guchar **p,
                                              const guchar *end,
                                              OIRImageDataBlock *image,
                                              const gchar *id,
                                              GError **error);
static gboolean      read_image_meta_data    (const guchar **p,
                                              const guchar *end,
                                              OIRImageXMLFragment *fragment,
                                              GError **error);
static gboolean      read_more_meta_data     (const guchar **p,
                                              const guchar *end,
                                              OIRMetaData *metadata,
                                              GError **error);
static void          parse_xml_to_hash       (OIRXMLFragment *xml,
                                              OIRXMLParserData *data);
static gboolean      chunk_size_and_type     (const guchar *p,
                                              const guchar *end,
                                              guint32 *chunksize,
                                              OIRChunkType *chunktype,
                                              GError **error);
static gboolean      skip_thumbnail          (const guchar **p,
                                              const guchar *end,
                                              GError **error);
static gboolean      create_datafield        (GwyContainer *data,
                                              GHashTable *hash,
                                              guint imgid,
                                              gint i,
                                              const OIRImageDataBlock *image,
                                              const gchar *filename,
                                              GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Olympus OIR data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.5",
    "David Nečas (Yeti)",
    "2019",
};

#ifdef DEBUG
static const guchar *global_buffer = NULL;
#endif

GWY_MODULE_QUERY2(module_info, oirfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("oirfile",
                           N_("Olympus OIR data files (.oir)"),
                           (GwyFileDetectFunc)&oirfile_detect,
                           (GwyFileLoadFunc)&oirfile_load,
                           NULL,
                           NULL);
#ifdef HAVE_GWYZIP
    gwy_file_func_register("poirfile",
                           N_("Olympus packed OIR data files (.poir)"),
                           (GwyFileDetectFunc)&poirfile_detect,
                           (GwyFileLoadFunc)&poirfile_load,
                           NULL,
                           NULL);
#endif

    return TRUE;
}

static gint
oirfile_detect(const GwyFileDetectInfo *fileinfo,
               gboolean only_name)
{
    const gchar *head = fileinfo->head;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;

    if (fileinfo->buffer_len <= OIR_MAGIC_SIZE
        || memcmp(head, OIR_MAGIC, OIR_MAGIC_SIZE) != 0)
        return 0;

    return 100;
}

static GwyContainer*
oirfile_load(const gchar *filename,
             G_GNUC_UNUSED GwyRunType mode,
             GError **error)
{
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    GError *err = NULL;
    gsize size = 0;
    gint ndata;

    if (!gwy_file_get_contents(filename, &buffer, &size, error)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    container = gwy_container_new();
    ndata = oirfile_load_from_memory(container, 0, buffer, size, filename,
                                     error);
    gwy_file_abandon_contents(buffer, size, NULL);
    if (!ndata)
        GWY_OBJECT_UNREF(container);

    return container;
}

#ifdef HAVE_GWYZIP
static gint
poirfile_detect(const GwyFileDetectInfo *fileinfo,
                gboolean only_name)
{
    const gchar *head = fileinfo->head;
    guint mlen, count = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;

    if (fileinfo->buffer_len <= POIR_MAGIC_SIZE
        || memcmp(head, POIR_MAGIC, POIR_MAGIC_SIZE) != 0)
        return 0;

    /* Try to find some typical file name fragments near the beginning of the
     * ZIP file. */
    mlen = MIN(fileinfo->buffer_len, 256);
    if (!gwy_memmem(fileinfo->head, mlen, ".oir", 4))
        return 0;

    count += !!gwy_memmem(fileinfo->head, mlen, "^3D_LSM", 7);
    count += !!gwy_memmem(fileinfo->head, mlen, "_LSM3D^", 7);
    count += !!gwy_memmem(fileinfo->head, mlen, "_COLOR3D^", 9);
    count += !!gwy_memmem(fileinfo->head, mlen, "^XY_Camera", 10);
    gwy_debug("count %u", count);
    if (count >= 2)
        return 100;

    if (count)
        return 60;

    return 0;
}

static GwyContainer*
poirfile_load(const gchar *filename,
              G_GNUC_UNUSED GwyRunType mode,
              GError **error)
{
    GwyContainer *container = NULL;
    GwyZipFile zipfile;
    gchar *currname = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    gint ndata = 0, prevndata;

    if (!(zipfile = gwyzip_open(filename, error)))
        return NULL;

    if (!gwyzip_first_file(zipfile, error))
        goto fail;

    container = gwy_container_new();
    do {
        if (!gwyzip_get_current_filename(zipfile, &currname, error)) {
            GWY_OBJECT_UNREF(container);
            goto fail;
        }

        gwy_debug("found file in ZIP: %s", currname);
        if (!g_str_has_suffix(currname, ".oir")) {
            GWY_FREE(currname);
            continue;
        }

        if (!(buffer = gwyzip_get_file_content(zipfile, &size, error))) {
            GWY_OBJECT_UNREF(container);
            goto fail;
        }

        prevndata = ndata;
        ndata = oirfile_load_from_memory(container, ndata, buffer, size,
                                         filename, error);
        if (ndata <= prevndata) {
            GWY_OBJECT_UNREF(container);
            goto fail;
        }

        GWY_FREE(buffer);
        GWY_FREE(currname);
    } while (gwyzip_next_file(zipfile, NULL));

fail:
    gwyzip_close(zipfile);
    g_free(buffer);
    g_free(currname);

    return container;
}
#endif

static gint
oirfile_load_from_memory(GwyContainer *container,
                         gint channelno,
                         const guchar *buffer,
                         gsize size,
                         const gchar *filename,
                         GError **error)
{
    OIRFile oirfile;
    const guchar *p, *end;
    guint32 chunksize;
    OIRChunkType chunktype;
    gboolean seen_wtf = FALSE, seen_images = FALSE;
    guint i;

#ifdef DEBUG
    global_buffer = buffer;
#endif
    p = buffer;
    end = buffer + size;
    gwy_clear(&oirfile, 1);

    /* File header */
    if (!read_file_header(&p, end, &oirfile.header, error))
        goto fail;
    gwy_debug("pos after header: %d", (gint)(p - buffer));

    while (p < end &&
           chunk_size_and_type(p, end, &chunksize, &chunktype, error)) {
        if (chunktype == OIR_CHUNK_BMP) {
            gwy_debug("skipping thumbnail chunk starting at %d",
                      (gint)(p - buffer));
            if (!skip_thumbnail(&p, end, error))
                goto fail;
        }
        else if (chunktype == OIR_CHUNK_WTF) {
            if (seen_wtf) {
                g_set_error(error, GWY_MODULE_FILE_ERROR,
                            GWY_MODULE_FILE_ERROR_DATA,
                            _("Chunk type %u occured multiple times."),
                            chunktype);
                goto fail;
            }
            gwy_debug("reading triplet of strange images starting at %d",
                      (gint)(p - buffer));
            if (!read_image_data_block(&p, end, &oirfile.wtf1, "wtf1", error)
                || !read_image_data_block(&p, end, &oirfile.wtf2, "wtf2", error)
                || !read_image_data_block(&p, end, &oirfile.wtf3, "wtf3", error))
                goto fail;
            seen_wtf = TRUE;
        }
        else if (chunktype == OIR_CHUNK_XML) {
            if (seen_images) {
                g_set_error(error, GWY_MODULE_FILE_ERROR,
                            GWY_MODULE_FILE_ERROR_DATA,
                            _("Chunk type %u occured multiple times."),
                            chunktype);
                goto fail;
            }
            /* Before we get to image data, there is a short binary block with
             * a bit of XML inside.  This one is important as it defines image
             * type, dimensions, depth, etc. */
            gwy_debug("reading first part of metadata starting at %d",
                      (gint)(p - buffer));
            if (!read_image_meta_data(&p, end, &oirfile.imgmeta, error))
                goto fail;

            gwy_debug("reading images starting at %d (finally!)",
                      (gint)(p - buffer));
            if (!read_image_data_block(&p, end, &oirfile.red, "red", error)
                || !read_image_data_block(&p, end, &oirfile.green, "green", error)
                || !read_image_data_block(&p, end, &oirfile.blue, "blue", error))
                goto fail;

            gwy_debug("reading second part of metadata starting at %d",
                      (gint)(p - buffer));
            if (!read_more_meta_data(&p, end, &oirfile.moremeta, error))
                goto fail;
            seen_images = TRUE;
        }
        else if (chunktype == OIR_CHUNK_TERMINATOR) {
            /* The last 140 bytes is some kind of nonsense apparently.  But
             * it is nonsense we expect to see.  */
            gwy_debug("found known terminator chunk or whatever it is");
            gwy_debug("first 4 bytes are %02x %02x %02x %02x (expecting ffs)",
                      p[0], p[1], p[2], p[3]);
            break;
        }
        else {
            gwy_debug("unknown chunk, aborting");
            break;
        }
        gwy_debug("remaining data: %d", (gint)(end - p));
    }

    if (!seen_images) {
        err_NO_DATA(error);
        goto fail;
    }

    parse_xml_to_hash(&oirfile.imgmeta.xml, &oirfile.xmldata);
    for (i = 0; i < oirfile.moremeta.nfragments; i++) {
        OIRXMLFragment *xml = oirfile.moremeta.fragments + i;
        guint j;

        if (memcmp(xml->root_name, "lut:LUT", strlen("lut:LUT")) == 0)
            continue;

        /* Some XML fragments are present twice.  Skip the second copy. */
        for (j = 0; j < i; j++) {
            OIRXMLFragment *seenxml = oirfile.moremeta.fragments + j;
            if (memcmp(xml->md5, seenxml->md5, 16) == 0)
                break;
        }

        if (j == i)
            parse_xml_to_hash(xml, &oirfile.xmldata);
    }

    if (!create_datafield(container, oirfile.xmldata.hash, 0, channelno + 0,
                          &oirfile.red, filename, error)
        || !create_datafield(container, oirfile.xmldata.hash, 1, channelno + 1,
                             &oirfile.green, filename, error)
        || !create_datafield(container, oirfile.xmldata.hash, 2, channelno + 2,
                             &oirfile.blue, filename, error))
        goto fail;

    channelno += 3;

fail:
    oirfile_free(&oirfile);

    return channelno;
}

static void
oirfile_free(OIRFile *oirfile)
{
    OIRXMLParserData *xmldata = &oirfile->xmldata;

    g_free(oirfile->moremeta.fragments);

    if (xmldata->scales)
        g_array_free(xmldata->scales, TRUE);
    if (xmldata->path)
        g_string_free(xmldata->path, TRUE);
    if (xmldata->hash)
        g_hash_table_destroy(xmldata->hash);
}

static gboolean
read_file_header(const guchar **p, const guchar *end,
                 OIRFileHeader *header,
                 GError **error)
{
    if (end - *p < HEADER_LENGTH) {
        err_TRUNCATED_HEADER(error);
        return FALSE;
    }

    get_CHARARRAY(header->magic, p);
    if (memcmp(header->magic, OIR_MAGIC, OIR_MAGIC_SIZE) != 0) {
        err_FILE_TYPE(error, "Olympus OIR");
        return FALSE;
    }

    header->unknown1 = gwy_get_guint32_le(p);
    header->unknown2 = gwy_get_guint32_le(p);
    header->unknown3 = gwy_get_guint32_le(p);
    header->unknown4 = gwy_get_guint32_le(p);
    gwy_debug("unknown[1..4] %u %u %u %u",
              header->unknown1, header->unknown2,
              header->unknown3, header->unknown4);
    header->file_size = gwy_get_guint32_le(p);
    gwy_debug("file_size %u", header->file_size);
    header->unknown5 = gwy_get_guint32_le(p);
    gwy_debug("unknown5 %u", header->unknown5);
    header->some_size = gwy_get_guint32_le(p);
    gwy_debug("some_size %u", header->some_size);
    header->unknown6 = gwy_get_guint32_le(p);
    header->unknown7 = gwy_get_guint32_le(p);
    header->unknown8 = gwy_get_guint32_le(p);
    header->unknown9 = gwy_get_guint32_le(p);
    header->unknown10 = gwy_get_guint32_le(p);
    header->unknown11 = gwy_get_guint32_le(p);
    header->unknown12 = gwy_get_guint32_le(p);
    gwy_debug("unknown[5..12] %u %u %u %u :: %u %u %u %u",
              header->unknown5, header->unknown6,
              header->unknown7, header->unknown8,
              header->unknown9, header->unknown10,
              header->unknown11, header->unknown12);
    get_CHARARRAY(header->unknown_str, p);
    gwy_debug("unknown_str %.8s", header->unknown_str);
    header->unknown13 = gwy_get_guint32_le(p);
    header->unknown14 = gwy_get_guint32_le(p);
    header->unknown15 = gwy_get_guint32_le(p);
    header->unknown16 = gwy_get_guint32_le(p);
    gwy_debug("unknown[13..16] %u %u 0x%x 0x%x",
              header->unknown13, header->unknown14,
              header->unknown15, header->unknown16);

    gwy_debug("successfully read file header");

    return TRUE;
}

static gboolean
read_image_data_block(const guchar **p, const guchar *end,
                      OIRImageDataBlock *image,
                      G_GNUC_UNUSED const gchar *id,
                      GError **error)
{
    if (end - *p < 4*sizeof(guchar)) {
        err_TRUNCATED_PART(error, "Image header");
        return FALSE;
    }

    image->remainder_size = gwy_get_guint32_le(p);
    gwy_debug("[%s]remainder_size %u", id, image->remainder_size);
    image->chunktype = gwy_get_guint32_le(p);
    gwy_debug("[%s]chunktype %u", id, image->chunktype);
    image->unknown2 = gwy_get_guint32_le(p);
    gwy_debug("[%s]unknown2 %u", id, image->unknown2);
    image->image_size = gwy_get_guint32_le(p);
    gwy_debug("[%s]image_size %u", id, image->image_size);
    if (end - *p < image->remainder_size || image->remainder_size < 4) {
        err_TRUNCATED_PART(error, "Image header");
        return FALSE;
    }

    image->uuid_size = gwy_get_guint32_le(p);
    gwy_debug("[%s]uuid_size %u", id, image->uuid_size);
    if (end - *p < image->uuid_size) {
        err_TRUNCATED_PART(error, "Image uuid");
        return FALSE;
    }
    image->uuid = *p;
    gwy_debug("[%s]uuid %.*s", id, image->uuid_size, image->uuid);
    *p += image->uuid_size;

    if (end - *p < 2*sizeof(guint32)) {
        err_TRUNCATED_PART(error, "Image header");
        return FALSE;
    }

    image->image_size_again = gwy_get_guint32_le(p);
    gwy_debug("[%s]image_size_again %u", id, image->image_size_again);
    image->unknown3 = gwy_get_guint32_le(p);
    gwy_debug("[%s]unknown3 %u", id, image->unknown3);
    if (end - *p < image->image_size) {
        err_TRUNCATED_PART(error, "Image data");
        return FALSE;
    }
    image->data = *p;
    *p += image->image_size;

    gwy_debug("[%s]successfully read image data", id);

    return TRUE;
}

static void
identify_xml_fragment(OIRXMLFragment *fragment)
{
    const guchar *p, *end;

    gwy_md5_get_digest(fragment->xml, fragment->size, fragment->md5);

    if (!gwy_memmem(fragment->xml, fragment->size, XML_MAGIC, XML_MAGIC_SIZE)) {
        fragment->root_size = 0;
        fragment->root_name = fragment->xml;
        return;
    }

    end = fragment->xml + fragment->size;
    for (p = fragment->xml + XML_MAGIC_SIZE; p < end && *p != '<'; p++)
        ;
    if (p < end)
        p++;
    while (p < end && g_ascii_isspace(*p))
        p++;
    fragment->root_name = p;
    while (p < end && (*p == ':' || g_ascii_isalpha(*p)))
        p++;
    fragment->root_size = p - fragment->root_name;

    gwy_debug("XML fragment type is %.*s",
              fragment->root_size, fragment->root_name);
}

static gboolean
read_image_meta_data(const guchar **p, const guchar *end,
                     OIRImageXMLFragment *fragment,
                     GError **error)
{
    if (end - *p < 11*sizeof(guint32)) {
        err_TRUNCATED_PART(error, "ImageMetadata header");
        return FALSE;
    }

    fragment->content_size = gwy_get_guint32_le(p);
    gwy_debug("content_size %u", fragment->content_size);
    fragment->unknown1 = gwy_get_guint32_le(p);
    gwy_debug("unknown[1] %u", fragment->unknown1);

    if (end - *p < fragment->content_size || fragment->content_size < 4) {
        err_TRUNCATED_PART(error, "ImageMetadata");
        return FALSE;
    }

    end = *p + fragment->content_size;
    fragment->id = gwy_get_guint32_le(p);
    gwy_debug("id %u", fragment->id);

    if (end - *p < 10*sizeof(guint32)) {
        err_TRUNCATED_PART(error, "XML fragment header");
        return FALSE;
    }

    fragment->unknown3 = gwy_get_guint32_le(p);
    fragment->unknown4 = gwy_get_guint32_le(p);
    gwy_debug("unknown[3..4] %u %u", fragment->unknown3, fragment->unknown4);
    fragment->unknown5 = gwy_get_guint32_le(p);
    gwy_debug("unknown[5] %u", fragment->unknown5);
    fragment->xml_dxx = gwy_get_guint32_le(p);
    gwy_debug("xml_dxx 0x%04x", fragment->xml_dxx);
    fragment->unknown7 = gwy_get_guint32_le(p);
    fragment->unknown8 = gwy_get_guint32_le(p);
    gwy_debug("unknown[7..8] %u %u", fragment->unknown7, fragment->unknown8);
    fragment->unknown9 = gwy_get_guint32_le(p);
    fragment->unknown10 = gwy_get_guint32_le(p);
    gwy_debug("unknown[9..10] %u %u",
              fragment->unknown9, fragment->unknown10);

    if (end - *p < sizeof(guint32)) {
        err_TRUNCATED_PART(error, "XML fragment header ");
        return FALSE;
    }

    fragment->xml.size = gwy_get_guint32_le(p);
    gwy_debug("xml_size %u", fragment->xml.size);
    if (end - *p < fragment->xml.size) {
        err_TRUNCATED_PART(error, "XML fragment");
        return FALSE;
    }
    fragment->xml.xml = *p;
    gwy_debug("xml %.*s", fragment->xml.size, fragment->xml.xml);
    *p += fragment->xml.size;
    identify_xml_fragment(&fragment->xml);

    gwy_debug("successfully read image metadata");

    return TRUE;
}

static void
append_xml_fragment(GArray *fragments, const guchar *begin, const guchar *end)
{
    OIRXMLFragment fragment;

    fragment.xml = begin;
    fragment.size = end - begin;
    g_array_append_val(fragments, fragment);
    if (fragment.size <= 4096) {
        gwy_debug("xml[%u] %.*s",
                  (guint)fragments->len, fragment.size, fragment.xml);
    }
    else {
        gwy_debug("xml[%u] %.4096s... (total length %u)",
                  (guint)fragments->len, fragment.xml, fragment.size);
    }
}

static gboolean
read_more_meta_data(const guchar **p, const guchar *end,
                    OIRMetaData *metadata,
                    GError **error)
{
    GArray *fragments;

    if (end - *p < sizeof(guint32)) {
        err_TRUNCATED_PART(error, "Metadata header");
        return FALSE;
    }

    metadata->content_size = gwy_get_guint32_le(p);
    gwy_debug("content_size %u (but we ignore that)", metadata->content_size);
    if (end - *p < metadata->content_size) {
        err_TRUNCATED_PART(error, "Metadata");
        return FALSE;
    }

    /* metadata->content_size does not seem to be the actual content size
     * end = *p + metadata->content_size; */
    fragments = g_array_new(FALSE, FALSE, sizeof(OIRXMLFragment));
    while (end - *p > XML_MAGIC_SIZE) {
        const guchar *xml = gwy_memmem(*p, end - *p, XML_MAGIC, XML_MAGIC_SIZE);
        const guchar *q;
        guint depth = 0, tagcntr = 0;
        gboolean failed = FALSE, closing = FALSE, last_was_slash = FALSE;

        if (!xml)
            break;

#ifdef DEBUG
        gwy_debug("XML fragment at %u", (guint)(xml - global_buffer));
#endif
        for (q = xml + XML_MAGIC_SIZE; q < end; q++) {
            if (g_ascii_isspace(*q))
                continue;
            if (G_UNLIKELY(*q < 0x20)) {
                gwy_debug("Binary data encounteded while scanning XML");
                failed = TRUE;
                break;
            }
            if (*q == '<') {
                if (G_UNLIKELY(tagcntr)) {
                    gwy_debug("Malformed XML: extra <");
                    failed = TRUE;
                    break;
                }
                tagcntr = 1;
                depth++;
            }
            else if (*q == '>') {
                if (G_UNLIKELY(!tagcntr)) {
                    gwy_debug("Malformed XML: extra >");
                    failed = TRUE;
                    break;
                }
                if (G_UNLIKELY(closing && last_was_slash)) {
                    gwy_debug("Malformed XML: double-closed tag");
                    failed = TRUE;
                    break;
                }
                if (closing || last_was_slash) {
                    if (depth == 0) {
                        gwy_debug("Malformed XML: too many closings");
                        failed = TRUE;
                        break;
                    }
                    depth--;
                    closing = FALSE;
                    if (!depth)
                        break;
                }
                tagcntr = 0;
            }
            else {
                if (tagcntr)
                    tagcntr++;
            }

            if (*q == '/') {
                if (tagcntr == 2) {
                    closing = TRUE;
                    depth--;
                }
                last_was_slash = TRUE;
                continue;
            }
            else
                last_was_slash = FALSE;
        }

        if (failed)
            break;

        append_xml_fragment(fragments, xml, q+1);
        identify_xml_fragment(&g_array_index(fragments,
                                             OIRXMLFragment, fragments->len-1));
        *p = q + 1;
    }

    gwy_debug("remaining data: %u", (guint)(end - *p));

    metadata->nfragments = fragments->len;
    metadata->fragments = (OIRXMLFragment*)g_array_free(fragments, FALSE);

    gwy_debug("read %u items of metadata", metadata->nfragments);

    return TRUE;
}

static void
oir_xml_start_element(G_GNUC_UNUSED GMarkupParseContext *context,
                      const gchar *element_name,
                      G_GNUC_UNUSED const gchar **attribute_names,
                      G_GNUC_UNUSED const gchar **attribute_values,
                      gpointer user_data,
                      G_GNUC_UNUSED GError **error)
{
    OIRXMLParserData *data = (OIRXMLParserData*)user_data;
    const gchar *p;

    if (data->path->len)
        g_string_append(data->path, "::");
    if ((p = strchr(element_name, ':')))
        g_string_append(data->path, p+1);
    else
        g_string_append(data->path, element_name);
}

static void
oir_xml_end_element(G_GNUC_UNUSED GMarkupParseContext *context,
                    G_GNUC_UNUSED const gchar *element_name,
                    gpointer user_data,
                    G_GNUC_UNUSED GError **error)
{
    OIRXMLParserData *data = (OIRXMLParserData*)user_data;
    const gchar *p;

    if ((p = g_strrstr(data->path->str, "::")))
        g_string_truncate(data->path, p - data->path->str);
    else
        g_string_truncate(data->path, 0);
}

static void
oir_xml_text(G_GNUC_UNUSED GMarkupParseContext *context,
             const gchar *text,
             gsize text_len,
             gpointer user_data,
             G_GNUC_UNUSED GError **error)
{
    static const gchar *channel_arrays[] = {
        "frameProperties::channelImageDefinition::",
        "cameraChannel::elementChannel::",
        /* These can occur six times.  But the last three belong to strange
         * images.  We can use the first three. */
        "imageProperties::imageInfo::phase::group::channel::",
        "imageProperties::acquisition::phase::group::channel::",
        "imageProperties::acquisition::imagingParam::productData::scale::",
        "imageProperties::acquisition::imagingParam::productData::range::",
    };
    static const gchar scales[] = "frameProperties::additionalData::scales";

    OIRXMLParserData *data = (OIRXMLParserData*)user_data;
    guint i, j, l;
    GString *path;
    gchar *s;

    for (i = 0; i < text_len; i++) {
        if (!g_ascii_isspace(text[i]))
            break;
    }
    if (i == text_len)
        return;

    path = data->path;
    s = g_strstrip(g_strndup(text + i, text_len - i));

    /* Handle the scales array.  There should be just one in LSM data. */
    if (gwy_strequal(path->str, scales)) {
        gdouble d;

        if (!data->scales)
            data->scales = g_array_new(FALSE, FALSE, sizeof(gdouble));
        d = g_ascii_strtod(s, NULL);
        g_array_append_val(data->scales, d);
        g_free(s);
        return;
    }

    l = path->len;
    for (i = 0; i < G_N_ELEMENTS(channel_arrays); i++) {
        if (g_str_has_prefix(path->str, channel_arrays[i])) {
            j = 0;
            while (TRUE) {
                g_string_append_printf(path, "[%u]", j);
                if (!g_hash_table_lookup(data->hash, path->str)) {
                    gwy_debug("%s = %s", path->str, s);
                    g_hash_table_replace(data->hash,
                                         g_strdup(path->str), g_strdup(s));
                    g_string_truncate(path, l);
                    return;
                }
                g_string_truncate(path, l);
                j++;
                g_return_if_fail(j < G_MAXUINT);
            }
        }
    }

    gwy_debug("%s = %s", path->str, s);
    g_hash_table_replace(data->hash, g_strdup(path->str), s);
}

static void
parse_xml_to_hash(OIRXMLFragment *xml, OIRXMLParserData *data)
{
    GMarkupParser parser = {
        oir_xml_start_element, oir_xml_end_element, oir_xml_text, NULL, NULL,
    };
    GMarkupParseContext *context;

    if (!data->path)
        data->path = g_string_new(NULL);
    else
        g_string_truncate(data->path, 0);

    if (!data->hash) {
        data->hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                           g_free, g_free);
    }

    context = g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT,
                                         data, NULL);
    g_markup_parse_context_parse(context, xml->xml, xml->size, NULL);
    g_markup_parse_context_free(context);
}

/* We do NOT advance p here! */
static gboolean
chunk_size_and_type(const guchar *p, const guchar *end,
                    guint32 *chunksize, OIRChunkType *chunktype,
                    GError **error)
{
    if (end - p < 2*sizeof(guint32)) {
        err_TRUNCATED_PART(error, "Chunk header");
        return FALSE;
    }

    *chunksize = gwy_get_guint32_le(&p);
    *chunktype = gwy_get_guint32_le(&p);

    gwy_debug("chunk type %u, size %u", *chunktype, *chunksize);
    return TRUE;
}

static gboolean
skip_thumbnail(const guchar **p, const guchar *end, GError **error)
{
    guint32 thumbsize;
    OIRChunkType chunktype;

    if (end - *p < 2*sizeof(guint32)) {
        err_TRUNCATED_PART(error, "Thumbnail header ");
        return FALSE;
    }
    thumbsize = gwy_get_guint32_le(p);
    gwy_debug("thumbsize %u", thumbsize);
    chunktype = gwy_get_guint32_le(p);
    gwy_debug("chunktype %u", chunktype);
    g_assert(chunktype == OIR_CHUNK_BMP);

    if (thumbsize > end - *p) {
        err_TRUNCATED_PART(error, "thumbnail");
        return FALSE;
    }
    gwy_debug("skipping %u bytes of thumbnail", thumbsize);
    *p += thumbsize;
    return TRUE;
}

static const gchar*
get_meta_or_fail(GHashTable *hash,
                 const gchar *prefix, const gchar *key, gint imgid,
                 GError **error)
{
    const guchar *s;
    gchar *strkey;

    if (imgid == -1)
        strkey = g_strconcat(prefix, "::", key, NULL);
    else
        strkey = g_strdup_printf("%s::%s[%d]", prefix, key, imgid);

    gwy_debug("looking for %s", strkey);
    if ((s = g_hash_table_lookup(hash, strkey))) {
        g_free(strkey);
        return s;
    }

    err_MISSING_FIELD(error, strkey);
    g_free(strkey);
    return NULL;
}

static void
add_meta(gpointer hkey, gpointer hvalue, gpointer user_data)
{
    GwyContainer *meta = (GwyContainer*)user_data;
    gchar *value = hvalue, *skey = hkey;

    if (!strlen(value))
        return;

    gwy_container_set_const_string_by_name(meta, skey, value);
}

static GwyContainer*
make_metadata(GHashTable *hash)
{
    GwyContainer *meta = gwy_container_new();

    g_hash_table_foreach(hash, add_meta, meta);
    if (gwy_container_get_n_items(meta))
        return meta;

    g_object_unref(meta);
    return NULL;
}

/* XXX: This works for nice images, not strange images. */
static gboolean
create_datafield(GwyContainer *data, GHashTable *hash,
                 guint imgid, gint i,
                 const OIRImageDataBlock *image,
                 const gchar *filename,
                 GError **error)
{
    static const gchar frameprops[] = "frameProperties::imageDefinition";
    static const gchar imgpprops[]
        = "imageProperties::imageInfo::phase::group::channel";

    const gchar *s, *device, *name, *gradient = NULL;
    gchar *title;
    gint xres, yres, depth;
    gdouble xreal, yreal, zscale = 0.0;
    GwyRawDataType rawdatatype;
    GwyDataField *dfield;
    GwyContainer *meta;
    GQuark quark;

    if (!(s = get_meta_or_fail(hash, frameprops, "width", -1, error)))
        return FALSE;
    xres = atoi(s);

    if (!(s = get_meta_or_fail(hash, frameprops, "height", -1, error)))
        return FALSE;
    yres = atoi(s);

    if (!(s = get_meta_or_fail(hash, frameprops, "depth", -1, error)))
        return FALSE;
    depth = atoi(s);

    if (depth == 1)
        rawdatatype = GWY_RAW_DATA_UINT8;
    else if (depth == 2)
        rawdatatype = GWY_RAW_DATA_UINT16;
    else {
        err_BPP(error, depth);
        return FALSE;
    }

    if (err_SIZE_MISMATCH(error, xres*yres*depth, image->image_size, TRUE))
        return FALSE;

    /* Values for imdid != 0 are not present in Camera (COLOR3D) files. */
    if (!(s = get_meta_or_fail(hash, imgpprops, "length::x", 0, error)))
        return FALSE;
    xreal = 1e-6*xres*g_ascii_strtod(s, NULL);

    if (!(s = get_meta_or_fail(hash, imgpprops, "length::y", 0, error)))
        return FALSE;
    yreal = 1e-6*yres*g_ascii_strtod(s, NULL);

    device = get_meta_or_fail(hash,
                              "imageProperties::imageInfo", "acquireDevice",
                              -1, NULL);
    gwy_debug("name %s", device);

    if (device && gwy_strequal(device, "LSM")) {
        /* Only one channel is actually height.  The other two are intensity
         * and ???.  Furthermore, the names are shifted.  It's a mess. */
        imgid = (imgid + 1) % 3;
        if ((s = get_meta_or_fail(hash, imgpprops, "length::z", imgid, NULL))) {
            zscale = 1e-6*g_ascii_strtod(s, NULL);
            gwy_debug("zscale %g", zscale);
        }
    }

    name = get_meta_or_fail(hash, imgpprops, "imageDefinition::imageType",
                            imgid, NULL);
    if (!name || !gwy_strequal(name, "HEIGHT"))
        zscale = 0.0;

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
    if (zscale != 0.0) {
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "m");
    }

    gwy_convert_raw_data(image->data, xres*yres, 1,
                         rawdatatype, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield),
                         zscale != 0.0 ? zscale : 1.0, 0.0);

    quark = gwy_app_get_data_key_for_id(i);
    gwy_container_set_object(data, quark, dfield);
    gwy_file_channel_import_log_add(data, i, NULL, filename);
    g_object_unref(dfield);

    if (device && gwy_stramong(device, "Camera", "CAMERA", NULL)) {
        if (imgid == 0) {
            gradient = "RGB-Red";
            if (!name)
                name = "Red";
        }
        else if (imgid == 1) {
            gradient = "RGB-Green";
            if (!name)
                name = "Green";
        }
        else if (imgid == 2) {
            gradient = "RGB-Blue";
            if (!name)
                name = "Blue";
        }
    }

    quark = gwy_app_get_data_title_key_for_id(i);
    if (name) {
        if (device) {
            title = g_strconcat(device, " ", name, NULL);
            gwy_container_set_string(data, quark, title);
        }
        else
            gwy_container_set_const_string(data, quark, name);
    }
    else if (device)
        gwy_container_set_const_string(data, quark, device);

    if (gradient) {
        quark = gwy_app_get_data_palette_key_for_id(i);
        gwy_container_set_const_string(data, quark, gradient);
    }

    meta = make_metadata(hash);
    quark = gwy_app_get_data_meta_key_for_id(i);
    gwy_container_set_object(data, quark, meta);
    g_object_unref(meta);

    return TRUE;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
