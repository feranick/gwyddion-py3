/*
 *  $Id: magellan.c 22626 2019-10-30 09:14:30Z yeti-dn $
 *  Copyright (C) 2013 David Necas (Yeti).
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
 * FEI Magellan SEM images
 * .tif
 * Read
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Indistinguishable from TIFF.  Avoding clash with a standard file format.
 **/

#include "config.h"
#include <stdlib.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>
#include "err.h"
#include "gwytiff.h"

#define MAGIC_COMMENT "[User]\r\n"

enum {
    MAGELLAN_TIFF_TAG = 34682
};

static gboolean      module_register(void);
static gint          mgl_detect     (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name);
static GwyContainer* mgl_load       (const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);
static GwyContainer* mgl_load_tiff  (const GwyTIFF *tiff,
                                     const gchar *filename,
                                     GError **error);
static GwyContainer* get_meta       (GHashTable *hash);
static void          add_meta       (gpointer hkey,
                                     gpointer hvalue,
                                     gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    module_register,
    N_("Imports FEI Magellan SEM images."),
    "Yeti <yeti@gwyddion.net>",
    "1.2",
    "David Nečas (Yeti)",
    "2013",
};

GWY_MODULE_QUERY2(module_info, magellan)

static gboolean
module_register(void)
{
    gwy_file_func_register("magellan",
                           N_("FEI Magellan SEM image (.tif)"),
                           (GwyFileDetectFunc)&mgl_detect,
                           (GwyFileLoadFunc)&mgl_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
mgl_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    GwyTIFF *tiff;
    gint score = 0;
    gchar *comment = NULL;
    GwyTIFFVersion version = GWY_TIFF_CLASSIC;
    guint byteorder = G_LITTLE_ENDIAN;

    if (only_name)
        return score;

    /* Weed out non-TIFFs */
    if (!gwy_tiff_detect(fileinfo->head, fileinfo->buffer_len,
                         &version, &byteorder))
        return 0;

    /* Use GwyTIFF for detection to avoid problems with fragile libtiff. */
    if ((tiff = gwy_tiff_load(fileinfo->name, NULL))
        && gwy_tiff_get_string0(tiff, MAGELLAN_TIFF_TAG, &comment)
        && strstr(comment, MAGIC_COMMENT))
        score = 100;

    g_free(comment);
    if (tiff)
        gwy_tiff_free(tiff);

    return score;
}

static GwyContainer*
mgl_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyTIFF *tiff;
    GwyContainer *container = NULL;

    tiff = gwy_tiff_load(filename, error);
    if (!tiff)
        return NULL;

    container = mgl_load_tiff(tiff, filename, error);
    gwy_tiff_free(tiff);

    return container;
}

static GwyContainer*
mgl_load_tiff(const GwyTIFF *tiff, const gchar *filename, GError **error)
{
    GwyContainer *container = NULL, *meta;
    GwyDataField *dfield;
    GwyTIFFImageReader *reader = NULL;
    GwyTextHeaderParser parser;
    GHashTable *hash;
    gint i;
    gchar *comment = NULL;
    const gchar *value;
    GError *err = NULL;
    guint dir_num = 0;
    gdouble *data;
    gdouble xstep, ystep, q;
    GQuark quark;
    GString *key = NULL;

    /* Comment with parameters is common for all data fields */
    if (!gwy_tiff_get_string0(tiff, MAGELLAN_TIFF_TAG, &comment)
        || !strstr(comment, MAGIC_COMMENT)) {
        g_free(comment);
        err_FILE_TYPE(error, "FEI Magellan");
        return NULL;
    }

    /* Read the comment header. */
    gwy_clear(&parser, 1);
    parser.key_value_separator = "=";
    parser.section_template = "[\x1a]";
    parser.section_accessor = "::";
    hash = gwy_text_header_parse(comment, &parser, NULL, NULL);

    if ((value = g_hash_table_lookup(hash, "EScan::PixelWidth"))
        || (value = g_hash_table_lookup(hash, "Scan::PixelWidth"))) {
        gwy_debug("PixelWidth %s", value);
        xstep = g_strtod(value, NULL);
        if (!((xstep = fabs(xstep)) > 0))
            g_warning("Real pixel width is 0.0, fixing to 1.0");
    }
    else {
        err_MISSING_FIELD(error, "PixelWidth");
        goto fail;
    }

    if ((value = g_hash_table_lookup(hash, "EScan::PixelHeight"))
        || (value = g_hash_table_lookup(hash, "Scan::PixelHeight"))) {
        gwy_debug("PixelHeight %s", value);
        ystep = g_strtod(value, NULL);
        if (!((ystep = fabs(ystep)) > 0))
            g_warning("Real pixel height is 0.0, fixing to 1.0");
    }
    else {
        err_MISSING_FIELD(error, "PixelHeight");
        goto fail;
    }

    key = g_string_new(NULL);
    for (dir_num = 0; dir_num < gwy_tiff_get_n_dirs(tiff); dir_num++) {
        const gchar *name, *mode;

        reader = gwy_tiff_image_reader_free(reader);
        /* Request a reader, this ensures dimensions and stuff are defined. */
        reader = gwy_tiff_get_image_reader(tiff, dir_num, 3, &err);
        if (!reader) {
            g_warning("Ignoring directory %u: %s", dir_num, err->message);
            g_clear_error(&err);
            continue;
        }
        q = 1.0/((1 << reader->bits_per_sample) - 1);
        name = g_hash_table_lookup(hash, "Detectors::Name");
        mode = g_hash_table_lookup(hash, "Detectors::Mode");

        dfield = gwy_data_field_new(reader->width, reader->height,
                                    reader->width * xstep,
                                    reader->height * ystep,
                                    FALSE);
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");

        data = gwy_data_field_get_data(dfield);
        for (i = 0; i < reader->height; i++)
            gwy_tiff_read_image_row_averaged(tiff, reader, i, q, 0.0,
                                             data + i*reader->width);

        if (!container)
            container = gwy_container_new();

        quark = gwy_app_get_data_key_for_id(dir_num);
        gwy_container_set_object(container, quark, dfield);
        g_object_unref(dfield);

        if (name && mode) {
            g_string_printf(key, "%s/title", g_quark_to_string(quark));
            gwy_container_set_string_by_name(container, key->str,
                                             g_strconcat(name, " ", mode,
                                                         NULL));
        }

        if ((meta = get_meta(hash))) {
            g_string_printf(key, "/%d/meta", dir_num);
            gwy_container_set_object_by_name(container, key->str, meta);
            g_object_unref(meta);
        }
        gwy_file_channel_import_log_add(container, dir_num, NULL,
                                        filename);
    }

    if (!container)
        err_NO_DATA(error);

fail:
    g_hash_table_destroy(hash);
    g_free(comment);
    if (key)
        g_string_free(key, TRUE);
    if (reader) {
        gwy_tiff_image_reader_free(reader);
        reader = NULL;
    }

    return container;
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
    gchar *value = hvalue, *skey = hkey;

    if (!strlen(value))
        return;

    if (gwy_strequal(skey, "User::UserTextUnicode")
        || g_str_has_prefix(skey, "PrivateFei::"))
        return;

    value = g_convert(value, -1, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
    if (value)
        gwy_container_set_string_by_name(GWY_CONTAINER(user_data), skey, value);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
