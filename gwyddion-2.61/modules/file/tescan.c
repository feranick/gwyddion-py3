/*
 *  $Id: tescan.c 22625 2019-10-30 09:12:55Z yeti-dn $
 *  Copyright (C) 2013-2019 David Necas (Yeti).
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
 * [FILE-MAGIC-USERGUIDE]
 * Tescan MIRA SEM images
 * .tif
 * Read
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Tescan LYRA SEM images
 * .hdr + .png
 * Read
 **/

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-tescan-sem-header">
 *   <comment>Tescan SEM data header</comment>
 *   <glob pattern="*-png.hdr"/>
 *   <glob pattern="*-PNG.HDR"/>
 * </mime-type>
 **/

#include "config.h"
#include <stdlib.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>
#include "err.h"
#include "gwytiff.h"

#define MAGIC_FIELD "PixelSizeX="
#define MAGIC_FIELD_SIZE (sizeof(MAGIC_FIELD)-1)

enum {
    TESCAN_TIFF_TAG = 50431,
};

typedef enum {
    TESCAN_BLOCK_LAST      = 0,
    TESCAN_BLOCK_THUMBNAIL = 1, /* JPEG */
    TESCAN_BLOCK_MAIN      = 2,
    TESCAN_BLOCK_SEM       = 3,
    TESCAN_BLOCK_GAMA      = 4,
    TESCAN_BLOCK_FIB       = 5,
    TESCAN_BLOCK_NTYPES,
} TescanBlockType;

typedef struct {
    TescanBlockType type;
    guint32 size;
    const guchar *data;
} TescanBlock;

typedef struct {
    GHashTable *target;
    const gchar *prefix;
} BlockCopyInfo;

static gboolean            module_register       (void);
static gint                tsctif_detect         (const GwyFileDetectInfo *fileinfo,
                                                  gboolean only_name);
static GwyContainer*       tsctif_load           (const gchar *filename,
                                                  GwyRunType mode,
                                                  GError **error);
static GwyContainer*       tsctif_load_tiff      (GwyTIFF *tiff,
                                                  const GwyTIFFEntry *entry,
                                                  GError **error);
static const GwyTIFFEntry* tsctif_find_header    (GwyTIFF *tiff,
                                                  GError **error);
static GArray*             tsctif_get_blocks     (GwyTIFF *tiff,
                                                  const GwyTIFFEntry *entry,
                                                  GError **error);
static gint                tschdr_detect         (const GwyFileDetectInfo *fileinfo,
                                                  gboolean only_name);
static GwyContainer*       tschdr_load           (const gchar *filename,
                                                  GwyRunType mode,
                                                  GError **error);
static gboolean            tschdr_find_image_file(GString *str);
static void                parse_text_fields     (GHashTable *globalhash,
                                                  const gchar *prefix,
                                                  TescanBlock *block);
static void                copy_with_prefix      (gpointer hkey,
                                                  gpointer hvalue,
                                                  gpointer user_data);
static GwyContainer*       get_meta              (GHashTable *hash);
static void                add_meta              (gpointer hkey,
                                                  gpointer hvalue,
                                                  gpointer user_data);
static GwyDataField*       data_field_from_pixbuf(GdkPixbuf *pixbuf,
                                                  GHashTable *hash);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    module_register,
    N_("Imports Tescan SEM images."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2013",
};

GWY_MODULE_QUERY2(module_info, tescan)

static gboolean
module_register(void)
{
    gwy_file_func_register("tescan-tif",
                           N_("Tescan TIF SEM image (.tif)"),
                           (GwyFileDetectFunc)&tsctif_detect,
                           (GwyFileLoadFunc)&tsctif_load,
                           NULL,
                           NULL);
    gwy_file_func_register("tescan-png",
                           N_("Tescan two-part SEM image (.hdr + .png)"),
                           (GwyFileDetectFunc)&tschdr_detect,
                           (GwyFileLoadFunc)&tschdr_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
tsctif_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    GwyTIFF *tiff;
    guint score = 0;
    GwyTIFFVersion version = GWY_TIFF_CLASSIC;
    guint byteorder = G_LITTLE_ENDIAN;

    if (only_name)
        return score;

    /* Weed out non-TIFFs */
    if (!gwy_tiff_detect(fileinfo->head, fileinfo->buffer_len,
                         &version, &byteorder))
        return 0;

    /* Use GwyTIFF for detection to avoid problems with fragile libtiff.
     * Progressively try more fine tests. */
    if ((tiff = gwy_tiff_load(fileinfo->name, NULL))
         && tsctif_find_header(tiff, NULL))
        score = 100;

    if (tiff)
        gwy_tiff_free(tiff);

    return score;
}

static GwyContainer*
tsctif_load(const gchar *filename,
            G_GNUC_UNUSED GwyRunType mode,
            GError **error)
{
    GwyTIFF *tiff;
    const GwyTIFFEntry *entry;
    GwyContainer *container = NULL;

    tiff = gwy_tiff_load(filename, error);
    if (!tiff)
        return NULL;

    entry = tsctif_find_header(tiff, error);
    if (!entry) {
        gwy_tiff_free(tiff);
        return NULL;
    }

    container = tsctif_load_tiff(tiff, entry, error);
    if (container)
        gwy_file_channel_import_log_add(container, 0, NULL, filename);

    gwy_tiff_free(tiff);

    return container;
}

static GwyContainer*
tsctif_load_tiff(GwyTIFF *tiff, const GwyTIFFEntry *entry, GError **error)
{
    GwyContainer *container = NULL, *meta;
    GwyDataField *dfield;
    GwyTIFFImageReader *reader = NULL;
    GHashTable *hash = NULL;
    GArray *blocks = NULL;
    gint i;
    const gchar *value;
    gdouble *data;
    gdouble xstep, ystep;

    if (!(blocks = tsctif_get_blocks(tiff, entry, error)))
        goto fail;

    hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (i = 0; i < blocks->len; i++) {
        TescanBlock *block = &g_array_index(blocks, TescanBlock, i);
        if (block->type == TESCAN_BLOCK_MAIN)
            parse_text_fields(hash, "Main", block);
        else if (block->type == TESCAN_BLOCK_SEM)
            parse_text_fields(hash, "SEM", block);
        else if (block->type == TESCAN_BLOCK_GAMA)
            parse_text_fields(hash, "GAMA", block);
        else if (block->type == TESCAN_BLOCK_FIB)
            parse_text_fields(hash, "FIB", block);
    }

    if ((value = g_hash_table_lookup(hash, "Main::PixelSizeX"))) {
        gwy_debug("Main::PixelSizeX %s", value);
        xstep = g_strtod(value, NULL);
        if (!((xstep = fabs(xstep)) > 0))
            g_warning("Real pixel width is 0.0, fixing to 1.0");
    }
    else {
        err_MISSING_FIELD(error, "Main::PixelSizeX");
        goto fail;
    }

    if ((value = g_hash_table_lookup(hash, "Main::PixelSizeY"))) {
        gwy_debug("Main::PixelSizeY %s", value);
        ystep = g_strtod(value, NULL);
        if (!((ystep = fabs(ystep)) > 0))
            g_warning("Real pixel height is 0.0, fixing to 1.0");
    }
    else {
        err_MISSING_FIELD(error, "Main::PixelSizeY");
        goto fail;
    }

    /* Request a reader, this ensures dimensions and stuff are defined. */
    if (!(reader = gwy_tiff_get_image_reader(tiff, 0, 1, error)))
        goto fail;

    dfield = gwy_data_field_new(reader->width, reader->height,
                                reader->width * xstep,
                                reader->height * ystep,
                                FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");

    data = gwy_data_field_get_data(dfield);
    for (i = 0; i < reader->height; i++)
        gwy_tiff_read_image_row(tiff, reader, 0, i,
                                1.0/((1 << reader->bits_per_sample) - 1),
                                0.0,
                                data + i*reader->width);

    container = gwy_container_new();

    gwy_container_set_object_by_name(container, "/0/data", dfield);
    g_object_unref(dfield);

    /* FIXME: Just kidding. */
    gwy_container_set_const_string_by_name(container, "/0/data/title",
                                           "Intensity");

    if ((meta = get_meta(hash))) {
        gwy_container_set_object_by_name(container, "/0/meta", meta);
        g_object_unref(meta);
    }

fail:
    if (hash)
        g_hash_table_destroy(hash);
    if (blocks)
        g_array_free(blocks, TRUE);
    if (reader) {
        gwy_tiff_image_reader_free(reader);
        reader = NULL;
    }

    return container;
}

static const GwyTIFFEntry*
tsctif_find_header(GwyTIFF *tiff, GError **error)
{
    const GwyTIFFEntry *entry;
    const guchar *p;

    if (!(entry = gwy_tiff_find_tag(tiff, 0, TESCAN_TIFF_TAG))
        || (entry->type != GWY_TIFF_BYTE
            && entry->type != GWY_TIFF_SBYTE)) {
        err_FILE_TYPE(error, "Tescan MIRA");
        return NULL;
    }

    p = entry->value;
    p = tiff->data + tiff->get_guint32(&p);
    if (!gwy_memmem(p, entry->count, MAGIC_FIELD, MAGIC_FIELD_SIZE)) {
        err_MISSING_FIELD(error, MAGIC_FIELD);
        return NULL;
    }

    return entry;
}

static GArray*
tsctif_get_blocks(GwyTIFF *tiff, const GwyTIFFEntry *entry, GError **error)
{
    const guchar *t = entry->value;
    const guchar *p = tiff->data + tiff->get_guint32(&t);
    const guchar *end = p + entry->count;
    GArray *blocks = g_array_new(FALSE, FALSE, sizeof(TescanBlock));
    gboolean seen_last = FALSE;

    while (p < end) {
        TescanBlock block;

        if (seen_last)
            g_warning("The terminating block is not really last.");

        if ((end - p) < 6) {
            err_TRUNCATED_PART(error, "TescanBlock header");
            goto fail;
        }

        block.size = tiff->get_guint32(&p);
        block.type = tiff->get_guint16(&p);
        gwy_debug("block of type %u and size %u", block.type, block.size);
        /* FIXME: Emit a better message for block.size < 2? */
        if (block.size > (gulong)(end - p) + 2 || block.size < 2) {
            err_TRUNCATED_PART(error, "TescanBlock data");
            goto fail;
        }
        if (block.type >= TESCAN_BLOCK_NTYPES)
            g_warning("Unknown block type %u.", block.type);
        if (block.type == TESCAN_BLOCK_LAST)
            seen_last = TRUE;

        block.data = p;
        g_array_append_val(blocks, block);
        p += block.size - 2;
    }
    if (!seen_last)
        g_warning("Have not seen the terminating block.");

    return blocks;

fail:
    g_array_free(blocks, TRUE);
    return NULL;
}

static gint
tschdr_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    static const gchar fields[]
        = "AccFrames=Device=Magnification=PixelSizeX=PixelSizeY=UserName=";
    guint score = 0;
    const gchar *s, *end;
    GString *str;

    if (only_name)
        return score;

    /* We can't find the image file name if this is not satisfied. */
    if (!g_str_has_suffix(fileinfo->name_lowercase, ".hdr"))
        return 0;

    if (strncmp(fileinfo->head, "[MAIN]", 6))
        return score;

    s = fields;
    while ((end = strchr(s, '='))) {
        end++;
        if (gwy_memmem(fileinfo->head, fileinfo->buffer_len, s, end - s)) {
            gwy_debug("Found %.*s", (gint)(end-s), s);
            score++;
        }
        s = end;
    }

    if (score < 4)
        return 0;

    /* It might be a Tescan header file.  Look for the image file. */
    str = g_string_new(fileinfo->name);
    gwy_debug("Looking for image file for %s", fileinfo->name);
    score = (tschdr_find_image_file(str) ? 100 : 0);
    g_string_free(str, TRUE);

    return score;
}

static GwyContainer*
tschdr_load(const gchar *filename,
            G_GNUC_UNUSED GwyRunType mode,
            GError **error)
{
    GwyContainer *container = NULL, *meta;
    GHashTable *hash = NULL;
    gchar *header = NULL;
    GString *imagefilename = NULL;
    GdkPixbuf *pixbuf = NULL;
    GwyTextHeaderParser parser;
    GwyDataField *dfield;
    GError *err = NULL;
    gsize size;

    if (!g_file_get_contents(filename, &header, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    gwy_clear(&parser, 1);
    parser.key_value_separator = "=";
    parser.section_template = "[\x1a]";
    parser.section_accessor = "::";
    hash = gwy_text_header_parse(header, &parser, NULL, NULL);

    if (!require_keys(hash, error,
                      "MAIN::PixelSizeX", "MAIN::PixelSizeY", NULL))
        goto fail;

    imagefilename = g_string_new(filename);
    if (!tschdr_find_image_file(imagefilename)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("No corresponding data file was found for header file."));
        goto fail;
    }

    if (!(pixbuf = gdk_pixbuf_new_from_file(imagefilename->str, &err))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Pixbuf loader refused data: %s."), err->message);
        g_clear_error(&err);
        goto fail;
    }
    dfield = data_field_from_pixbuf(pixbuf, hash);
    g_object_unref(pixbuf);

    container = gwy_container_new();
    gwy_container_set_object_by_name(container, "/0/data", dfield);
    g_object_unref(dfield);

    /* FIXME: Just kidding. */
    gwy_container_set_const_string_by_name(container, "/0/data/title",
                                           "Intensity");

    if ((meta = get_meta(hash))) {
        gwy_container_set_object_by_name(container, "/0/meta", meta);
        g_object_unref(meta);
    }

    gwy_file_channel_import_log_add(container, 0, NULL, filename);

fail:
    if (hash)
        g_hash_table_destroy(hash);
    if (imagefilename)
        g_string_free(imagefilename, TRUE);
    g_free(header);

    return container;
}

static gboolean
tschdr_find_image_file(GString *str)
{
    static const GFileTest flags = (G_FILE_TEST_IS_REGULAR
                                    | G_FILE_TEST_IS_SYMLINK);
    guint len = str->len;

    if (len < 5)
        return FALSE;

    if (len > 8 && g_ascii_strcasecmp(str->str + len-8, "-png.hdr") == 0)
        g_string_truncate(str, str->len-8);
    else if (len > 4 && g_ascii_strcasecmp(str->str + len-4, ".hdr") == 0)
        g_string_truncate(str, str->len-4);
    else
        return FALSE;

    g_string_append(str, ".png");
    if (g_file_test(str->str, flags)) {
        gwy_debug("Found image %s.", str->str);
        return TRUE;
    }

    g_string_truncate(str, str->len-3);
    g_string_append(str, "PNG");
    if (g_file_test(str->str, flags)) {
        gwy_debug("Found image %s.", str->str);
        return TRUE;
    }

    return FALSE;
}

static void
parse_text_fields(GHashTable *globalhash, const gchar *prefix,
                  TescanBlock *block)
{
    BlockCopyInfo block_copy_info;
    GwyTextHeaderParser parser;
    GHashTable *hash;
    gchar *data;

    data = g_new(gchar, block->size-1);
    memcpy(data, block->data, block->size-2);
    data[block->size-2] = '\0';

    gwy_clear(&parser, 1);
    parser.key_value_separator = "=";
    hash = gwy_text_header_parse(data, &parser, NULL, NULL);

    block_copy_info.target = globalhash;
    block_copy_info.prefix = prefix;
    g_hash_table_foreach(hash, copy_with_prefix, &block_copy_info);

    g_free(data);
    g_hash_table_destroy(hash);
}

static void
copy_with_prefix(gpointer hkey, gpointer hvalue, gpointer user_data)
{
    BlockCopyInfo *block_copy_info = (BlockCopyInfo*)user_data;
    gchar *key = g_strconcat(block_copy_info->prefix, "::", hkey, NULL);
    g_hash_table_insert(block_copy_info->target, key, g_strdup(hvalue));
}

static GwyContainer*
get_meta(GHashTable *hash)
{
    GwyContainer *meta = gwy_container_new();
    g_hash_table_foreach(hash, add_meta, meta);
    if (gwy_container_get_n_items(meta))
        return meta;

    g_object_unref(meta);
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

static GwyDataField*
data_field_from_pixbuf(GdkPixbuf *pixbuf, GHashTable *hash)
{
    GwyDataField *dfield;
    gint width, height, rowstride, i, bpp;
    guchar *pixels;
    gdouble *val;
    gdouble dx, dy;
    const gchar *s;

    s = g_hash_table_lookup(hash, "MAIN::PixelSizeX");
    g_assert(s);
    dx = g_ascii_strtod(s, NULL);

    s = g_hash_table_lookup(hash, "MAIN::PixelSizeY");
    g_assert(s);
    dy = g_ascii_strtod(s, NULL);

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    width = gdk_pixbuf_get_width(pixbuf);
    height = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    bpp = gdk_pixbuf_get_has_alpha(pixbuf) ? 4 : 3;
    dfield = gwy_data_field_new(width, height, dx*width, dy*height, FALSE);
    val = gwy_data_field_get_data(dfield);

    for (i = 0; i < height; i++) {
        guchar *p = pixels + i*rowstride;
        gdouble *r = val + i*width;
        gint j;

        for (j = 0; j < width; j++) {
            guchar red = p[bpp*j], green = p[bpp*j+1], blue = p[bpp*j+2];

            r[j] = (red + green + blue)/(3*255.0);
        }
    }

    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");

    return dfield;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
