/*
 *  $Id: lextfile.c 22576 2019-10-18 11:49:52Z yeti-dn $
 *  Copyright (C) 2010-2019 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
 * <mime-type type="application/x-olympus-lext-4000">
 *   <comment>Olympus LEXT 4000</comment>
 *   <magic priority="10">
 *     <match type="string" offset="0" value="II\x2a\x00"/>
 *   </magic>
 *   <glob pattern="*.lext"/>
 *   <glob pattern="*.LEXT"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Olympus LEXT 4000
 * .lext
 * Read
 **/

#include "config.h"
#include <stdlib.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>
#include "err.h"
#include "gwytiff.h"

/* Really.  They use factor 1e-6 and the value is in microns. */
#define Picometer 1e-12

#ifdef HAVE_MEMRCHR
#define strlenrchr(s,c,len) (gchar*)memrchr((s),(c),(len))
#else
#define strlenrchr(s,c,len) strrchr((s),(c))
#endif

#define MAGIC_COMMENT "<TiffTagDescData "

typedef struct {
    GString *path;
    GHashTable *hash;
    const gchar *toplevel;
    GwyContainer *meta;
    gdouble xcal;
    gdouble ycal;
    gdouble zcal;
} LextFile;

static gboolean      module_register   (void);
static gint          lext_detect       (const GwyFileDetectInfo *fileinfo,
                                        gboolean only_name);
static GwyContainer* lext_load         (const gchar *filename,
                                        GwyRunType mode,
                                        GError **error);
static GwyContainer* lext_load_tiff    (const GwyTIFF *tiff,
                                        const gchar *filename,
                                        GError **error);
static const gchar*  guess_image0_title(const GwyTIFF *tiff);
static void          add_info_from_exif(LextFile *lfile,
                                        const GwyTIFF *tiff);
static void          create_metadata   (LextFile *lfile);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    module_register,
    N_("Imports LEXT data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.6",
    "David Nečas (Yeti) & Petr Klapetek",
    "2010",
};

GWY_MODULE_QUERY2(module_info, lextfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("lext",
                           N_("Olympus LEXT OLS4000 (.lext)"),
                           (GwyFileDetectFunc)&lext_detect,
                           (GwyFileLoadFunc)&lext_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
lext_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    GwyTIFF *tiff;
    gint score = 0;
    gchar *comment = NULL;
    guint byteorder = G_LITTLE_ENDIAN;
    GwyTIFFVersion version = GWY_TIFF_CLASSIC;

    if (only_name)
        return score;

    /* Weed out non-TIFFs */
    if (!gwy_tiff_detect(fileinfo->head, fileinfo->buffer_len,
                         &version, &byteorder))
        return 0;

    /* Use GwyTIFF for detection to avoid problems with fragile libtiff. */
    if ((tiff = gwy_tiff_load(fileinfo->name, NULL))
        && gwy_tiff_get_string0(tiff, GWY_TIFFTAG_IMAGE_DESCRIPTION, &comment)
        && strstr(comment, MAGIC_COMMENT))
        score = 100;

    g_free(comment);
    if (tiff)
        gwy_tiff_free(tiff);

    return score;
}

static GwyContainer*
lext_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyTIFF *tiff;
    GwyContainer *container = NULL;

    tiff = gwy_tiff_load(filename, error);
    if (!tiff)
        return NULL;

    container = lext_load_tiff(tiff, filename, error);
    gwy_tiff_free(tiff);

    return container;
}

static void
start_element(G_GNUC_UNUSED GMarkupParseContext *context,
              const gchar *element_name,
              G_GNUC_UNUSED const gchar **attribute_names,
              G_GNUC_UNUSED const gchar **attribute_values,
              gpointer user_data,
              GError **error)
{
    LextFile *lfile = (LextFile*)user_data;

    gwy_debug("<%s>", element_name);
    if (!lfile->path->len && !gwy_strequal(element_name, lfile->toplevel)) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                    _("Top-level element is not ‘%s’."), lfile->toplevel);
        return;
    }

    g_string_append_c(lfile->path, '/');
    g_string_append(lfile->path, element_name);
}

static void
end_element(G_GNUC_UNUSED GMarkupParseContext *context,
            const gchar *element_name,
            gpointer user_data,
            G_GNUC_UNUSED GError **error)
{
    LextFile *lfile = (LextFile*)user_data;
    gchar *pos;

    gwy_debug("</%s>", element_name);
    pos = strlenrchr(lfile->path->str, '/', lfile->path->len);
    /* GMarkupParser should raise a run-time error if this does not hold. */
    g_assert(pos && strcmp(pos + 1, element_name) == 0);
    g_string_truncate(lfile->path, pos - lfile->path->str);
}

static void
text(G_GNUC_UNUSED GMarkupParseContext *context,
     const gchar *value,
     gsize value_len,
     gpointer user_data,
     G_GNUC_UNUSED GError **error)
{
    LextFile *lfile = (LextFile*)user_data;
    const gchar *path = lfile->path->str;
    gchar *val = g_strndup(value, value_len);

    g_strstrip(val);
    if (*val) {
        gwy_debug("%s <%s>", path, val);
        g_hash_table_replace(lfile->hash, g_strdup(path), val);
    }
    else
        g_free(val);
}

static void
titlecase_channel_name(gchar *name)
{
    *name = g_ascii_toupper(*name);
    name++;
    while (*name) {
        *name = g_ascii_tolower(*name);
        name++;
    }
}

static GwyContainer*
lext_load_tiff(const GwyTIFF *tiff, const gchar *filename, GError **error)
{
    const gchar *colour_channels[] = { "Red", "Green", "Blue" };
    const gchar *colour_channel_gradients[] = {
        "RGB-Red", "RGB-Green", "RGB-Blue"
    };

    GwyContainer *container = NULL, *tmpmeta;
    GwyDataField *dfield;
    GwySIUnit *siunit;
    GwyTIFFImageReader *reader = NULL;
    GMarkupParser parser = { start_element, end_element, text, NULL, NULL };
    GHashTable *hash = NULL;
    GMarkupParseContext *context = NULL;
    LextFile lfile;
    gchar *comment = NULL, *title = NULL;
    const gchar *value, *image0title, *keytitle, *channeltitle;
    GError *err = NULL;
    guint dir_num = 0, ch, spp;
    gint id = 0;
    GString *key;

    /* Comment with parameters is common for all data fields */
    if (!gwy_tiff_get_string0(tiff, GWY_TIFFTAG_IMAGE_DESCRIPTION, &comment)
        || !strstr(comment, MAGIC_COMMENT)) {
        g_free(comment);
        err_FILE_TYPE(error, "LEXT");
        return NULL;
    }

    /* Read the comment header. */
    lfile.hash = hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                              g_free, g_free);
    lfile.path = key = g_string_new(NULL);
    lfile.toplevel = "TiffTagDescData";
    lfile.xcal = lfile.ycal = lfile.zcal = 1.0;
    context = g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT,
                                         &lfile, NULL);
    if (!g_markup_parse_context_parse(context, comment, strlen(comment), &err)
        || !g_markup_parse_context_end_parse(context, &err)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("XML parsing failed: %s"), err->message);
        g_clear_error(&err);
        goto fail;
    }

    add_info_from_exif(&lfile, tiff);
    create_metadata(&lfile);
    image0title = guess_image0_title(tiff);
    gwy_debug("guessed image0 type: %s", image0title ? image0title : "UNKNOWN");

    for (dir_num = 0; dir_num < gwy_tiff_get_n_dirs(tiff); dir_num++) {
        double xscale, yscale, zfactor;
        GQuark quark;
        gdouble *data;
        gint i;

        g_free(title);
        title = NULL;

        if (!dir_num) {
            if (image0title)
                title = g_strdup(image0title);
            else
                continue;
        }
        else {
            if (!gwy_tiff_get_string(tiff, dir_num,
                                     GWY_TIFFTAG_IMAGE_DESCRIPTION,
                                     &title)) {
                g_warning("Directory %u has no ImageDescription.", dir_num);
                continue;
            }
        }

        /* Ignore the first directory, thumbnail and anything called INVALID.
         * FIXME: INVALID is probably the mask of invalid pixels and we might
         * want to import it. */
        gwy_debug("Channel <%s>", title);
        titlecase_channel_name(title);
        if (gwy_stramong(title, "Thumbnail", "Invalid", NULL))
            continue;

        if (gwy_strequal(title, "Color"))
            keytitle = "Intensity";
        else
            keytitle = title;

        reader = gwy_tiff_image_reader_free(reader);
        /* Request a reader, this ensures dimensions and stuff are defined. */
        reader = gwy_tiff_get_image_reader(tiff, dir_num, 3, &err);
        if (!reader) {
            g_warning("Ignoring directory %u: %s", dir_num, err->message);
            g_clear_error(&err);
            continue;
        }

        g_string_printf(key, "/TiffTagDescData/%sInfo/%sDataPerPixelX",
                        keytitle, keytitle);
        if (!(value = g_hash_table_lookup(hash, (gpointer)key->str))) {
            g_warning("Cannot find %s", key->str);
        }
        xscale = Picometer * lfile.xcal * g_ascii_strtod(value, NULL);

        g_string_printf(key, "/TiffTagDescData/%sInfo/%sDataPerPixelY",
                        keytitle, keytitle);
        if (!(value = g_hash_table_lookup(hash, (gpointer)key->str))) {
            g_warning("Cannot find %s", key->str);
        }
        yscale = Picometer * lfile.ycal * g_ascii_strtod(value, NULL);

        g_string_printf(key, "/TiffTagDescData/%sInfo/%sDataPerPixelZ",
                        keytitle, keytitle);
        if (!(value = g_hash_table_lookup(hash, (gpointer)key->str))) {
            g_warning("Cannot find %s", key->str);
        }
        zfactor = g_ascii_strtod(value, NULL);

        if (!container)
            container = gwy_container_new();

        spp = reader->samples_per_pixel;
        for (ch = 0; ch < spp; ch++) {
            siunit = gwy_si_unit_new("m");
            dfield = gwy_data_field_new(reader->width, reader->height,
                                        reader->width * xscale,
                                        reader->height * yscale,
                                        FALSE);
            // units
            gwy_data_field_set_si_unit_xy(dfield, siunit);
            g_object_unref(siunit);

            if (gwy_strequal(title, "Height")) {
                siunit = gwy_si_unit_new("m");
                zfactor *= lfile.zcal * Picometer;
            }
            else if (gwy_strequal(title, "Intensity")) {
                siunit = gwy_si_unit_new(NULL);
            }
            else
                siunit = gwy_si_unit_new(NULL);

            gwy_data_field_set_si_unit_z(dfield, siunit);
            g_object_unref(siunit);

            data = gwy_data_field_get_data(dfield);
            for (i = 0; i < reader->height; i++)
                gwy_tiff_read_image_row(tiff, reader, ch, i, zfactor, 0.0,
                                        data + i*reader->width);

            /* add read datafield to container */
            quark = gwy_app_get_data_key_for_id(id);
            gwy_container_set_object(container, quark, dfield);
            g_object_unref(dfield);

            quark = gwy_app_get_data_title_key_for_id(id);
            channeltitle = (spp == 3 ? colour_channels[ch] : title);
            gwy_container_set_const_string(container, quark, channeltitle);

            if (lfile.meta) {
                tmpmeta = gwy_container_duplicate(lfile.meta);
                quark = gwy_app_get_data_meta_key_for_id(id);
                gwy_container_set_object(container, quark, tmpmeta);
                g_object_unref(tmpmeta);
            }

            if (spp == 3) {
                g_string_printf(key, "/%u/base/palette", id);
                gwy_container_set_string_by_name
                                    (container, key->str,
                                     g_strdup(colour_channel_gradients[ch]));
            }

            gwy_file_channel_import_log_add(container, id, NULL,
                                            filename);
            id++;
        }
    }

fail:
    GWY_OBJECT_UNREF(lfile.meta);
    g_free(title);
    g_free(comment);
    if (reader) {
        gwy_tiff_image_reader_free(reader);
        reader = NULL;
    }
    if (hash)
        g_hash_table_destroy(hash);
    if (key)
        g_string_free(key, TRUE);
    if (context)
        g_markup_parse_context_free(context);

    return container;
}

static const gchar*
guess_image0_title(const GwyTIFF *tiff)
{
    enum {
        IMAGE_COLOR,
        IMAGE_THUMBNAIL,
        IMAGE_INTENSITY,
        IMAGE_HEIGHT,
        IMAGE_INVALID,
        N_IMAGES
    };
    guint seen = 0, xres, yres, spp, bpp0, dir_num;
    guint *bpps;

    /* Only read the first value of BitsPerSample if it's a tuple. */
    if (!gwy_tiff_get_uint(tiff, 0, GWY_TIFFTAG_IMAGE_WIDTH, &xres)
        || !gwy_tiff_get_uint(tiff, 0, GWY_TIFFTAG_IMAGE_LENGTH, &yres)
        || !gwy_tiff_get_uint(tiff, 0, GWY_TIFFTAG_SAMPLES_PER_PIXEL, &spp)
        || !spp)
        return NULL;

    bpps = g_new(guint, spp);
    if (!gwy_tiff_get_uints(tiff, 0, GWY_TIFFTAG_BITS_PER_SAMPLE, spp, bpps)) {
        g_free(bpps);
        return NULL;
    }
    bpp0 = bpps[0];
    g_free(bpps);

    for (dir_num = 1; dir_num < gwy_tiff_get_n_dirs(tiff); dir_num++) {
        gchar *title = NULL;
        if (!gwy_tiff_get_string(tiff, dir_num, GWY_TIFFTAG_IMAGE_DESCRIPTION,
                                 &title))
            continue;

        titlecase_channel_name(title);
        if (gwy_strequal(title, "Color"))
            seen |= 1 << IMAGE_COLOR;
        else if (gwy_strequal(title, "Thumbnail"))
            seen |= 1 << IMAGE_THUMBNAIL;
        else if (gwy_strequal(title, "Height"))
            seen |= 1 << IMAGE_HEIGHT;
        else if (gwy_strequal(title, "Intensity"))
            seen |= 1 << IMAGE_INTENSITY;
        else if (gwy_strequal(title, "Invalid"))
            seen |= 1 << IMAGE_INVALID;
        g_free(title);
    }

    if (xres == 128 && yres == 128 && spp == 3 && bpp0 == 8) {
        if (!(seen & (1 << IMAGE_THUMBNAIL)))
            return "Thumbnail";
        return NULL;
    }
    if (spp == 3 && bpp0 == 8) {
        if (!(seen & (1 << IMAGE_COLOR)))
            return "Color";
        return NULL;
    }
    if (spp == 1 && bpp0 == 1) {
        if (!(seen & (1 << IMAGE_INVALID)))
            return "Invalid";
        return NULL;
    }
    if (spp == 1 && bpp0 == 16) {
        if (!(seen & (1 << IMAGE_INTENSITY)))
            return "Intensity";
        if (!(seen & (1 << IMAGE_HEIGHT)))
            return "Intensity";
        return NULL;
    }

    return NULL;
}

static void
add_info_from_exif(LextFile *lfile, const GwyTIFF *tiff)
{
    GMarkupParser parser = { start_element, end_element, text, NULL, NULL };
    guint xmltag = GWY_TIFFTAG_EXIF_DEVICE_SETTING_DESCRIPTION;
    GMarkupParseContext *context = NULL;
    GArray *tags = NULL;
    const GwyTIFFEntry *entry;
    GError *err = NULL;
    gchar *comment = NULL;
    GString *key;
    const gchar *value;
    guint len, exifpos;

    if (!gwy_tiff_get_uint(tiff, 0, GWY_TIFFTAG_EXIF_IFD, &exifpos) || !exifpos)
        return;

    if (!(tags = gwy_tiff_scan_ifd(tiff, exifpos, NULL, NULL))
        || !gwy_tiff_ifd_is_vaild(tiff, tags, NULL))
        goto fail;

    g_array_sort(tags, gwy_tiff_tag_compare);
    if (!(entry = gwy_tiff_find_tag_in_dir(tags, xmltag))
        || !gwy_tiff_get_string_entry(tiff, entry, &comment))
        goto fail;

    lfile->toplevel = "ExifTagDescData";
    context = g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT,
                                         lfile, NULL);
    if (!g_markup_parse_context_parse(context, comment, strlen(comment), &err)
        || !g_markup_parse_context_end_parse(context, &err)) {
        gwy_debug("EXIF xml parsing failed (%s)", err->message);
        g_clear_error(&err);
    }
    g_markup_parse_context_free(context);

    key = lfile->path;
    g_string_assign(key,
                    "/ExifTagDescData/ImageCommonSettingsInfo"
                    "/MakerCalibrationValue");
    len = key->len;

    g_string_truncate(key, len);
    g_string_append_c(key, 'X');
    if ((value = g_hash_table_lookup(lfile->hash, (gpointer)key->str))) {
        lfile->xcal = 1e-6*g_ascii_strtod(value, NULL);
        gwy_debug("xcal %.8g", lfile->xcal);
        if (!((lfile->xcal = fabs(lfile->xcal)) > 0.0))
            lfile->xcal = 1.0;
    }

    g_string_truncate(key, len);
    g_string_append_c(key, 'Y');
    if ((value = g_hash_table_lookup(lfile->hash, (gpointer)key->str))) {
        lfile->ycal = 1e-6*g_ascii_strtod(value, NULL);
        gwy_debug("ycal %.8g", lfile->ycal);
        if (!((lfile->ycal = fabs(lfile->ycal)) > 0.0))
            lfile->ycal = 1.0;
    }

    g_string_truncate(key, len);
    g_string_append_c(key, 'Z');
    if ((value = g_hash_table_lookup(lfile->hash, (gpointer)key->str))) {
        lfile->zcal = 1e-6*g_ascii_strtod(value, NULL);
        gwy_debug("zcal %.8g", lfile->zcal);
    }

fail:
    g_free(comment);
    if (tags)
        g_array_free(tags, TRUE);
}

static void
add_metadata(gpointer hkey,
             gpointer hvalue,
             gpointer user_data)
{
    const gchar *key = (const gchar*)hkey, *value = (const gchar*)hvalue;
    LextFile *lfile = (LextFile*)user_data;
    GString *str = lfile->path;

    g_string_assign(str, key);
    if (g_str_has_prefix(str->str, "/TiffTagDescData/"))
        g_string_erase(str, 0, strlen("/TiffTagDescData/"));
    else if (g_str_has_prefix(str->str, "/ExifTagDescData/"))
        g_string_erase(str, 0, strlen("/ExifTagDescData/"));

    gwy_gstring_replace(str, "/", "::", -1);
    gwy_container_set_const_string_by_name(lfile->meta, str->str, value);
}

static void
create_metadata(LextFile *lfile)
{
    lfile->meta = gwy_container_new();
    g_hash_table_foreach(lfile->hash, add_metadata, lfile);

    if (!gwy_container_get_n_items(lfile->meta))
        GWY_OBJECT_UNREF(lfile->meta);

    /* XXX: We could also extract date & time from EXIT TIFF tags and put
     * them to the metadata... */
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
