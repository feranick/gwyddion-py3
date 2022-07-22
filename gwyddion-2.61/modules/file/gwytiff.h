/*
 *  $Id: gwytiff.h 24567 2021-12-20 12:11:34Z yeti-dn $
 *  Copyright (C) 2007-2021 David Necas (Yeti).
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

#include <glib.h>
#include <libgwyddion/gwymacros.h>
#include "get.h"

/*
 * This is a rudimentary built-in TIFF reader.
 *
 * It is required to read some TIFF-based files because the software that writes them is very creative with regard to
 * the specification.  In other words, we need to read some incorrect TIFFs too.  In particular, we do not expect
 * directories to be sorted and we accept bogus (nul) entries.
 *
 * Names starting GWY_TIFF, GwyTIFF and gwy_tiff are reserved.
 */

/* Search in all directories */
#define GWY_TIFF_ANY_DIR ((guint)-1)

/* Convenience functions for the 0th directory */
#define gwy_tiff_get_sint0(T, t, r) gwy_tiff_get_sint((T), 0, (t), (r))
#define gwy_tiff_get_uint0(T, t, r) gwy_tiff_get_uint((T), 0, (t), (r))
#define gwy_tiff_get_float0(T, t, r) gwy_tiff_get_float((T), 0, (t), (r))
#define gwy_tiff_get_string0(T, t, r) gwy_tiff_get_string((T), 0, (t), (r))

/* File header size, real files must be actually larger. */
#define GWY_TIFF_HEADER_SIZE     8
#define GWY_TIFF_HEADER_SIZE_BIG 16

/* TIFF format versions */
typedef enum {
    GWY_TIFF_CLASSIC   = 42,
    GWY_TIFF_BIG       = 43,    /* The proposed BigTIFF format */
} GwyTIFFVersion;

/* TIFF data types */
typedef enum {
    GWY_TIFF_NOTYPE    = 0,
    GWY_TIFF_BYTE      = 1,
    GWY_TIFF_ASCII     = 2,
    GWY_TIFF_SHORT     = 3,
    GWY_TIFF_LONG      = 4,
    GWY_TIFF_RATIONAL  = 5,
    GWY_TIFF_SBYTE     = 6,
    GWY_TIFF_UNDEFINED = 7,
    GWY_TIFF_SSHORT    = 8,
    GWY_TIFF_SLONG     = 9,
    GWY_TIFF_SRATIONAL = 10,
    GWY_TIFF_FLOAT     = 11,
    GWY_TIFF_DOUBLE    = 12,
    GWY_TIFF_IFD       = 13,
    /* Not sure what they are but they are assigned. */
    GWY_TIFF_UNICODE   = 14,
    GWY_TIFF_COMPLEX   = 15,
    /* BigTIFF */
    GWY_TIFF_LONG8     = 16,
    GWY_TIFF_SLONG8    = 17,
    GWY_TIFF_IFD8      = 18,
} GwyTIFFDataType;

/* Standard TIFF tags */
typedef enum {
    GWY_TIFFTAG_SUB_FILE_TYPE     = 254,
    GWY_TIFFTAG_IMAGE_WIDTH       = 256,
    GWY_TIFFTAG_IMAGE_LENGTH      = 257,
    GWY_TIFFTAG_BITS_PER_SAMPLE   = 258,
    GWY_TIFFTAG_COMPRESSION       = 259,
    GWY_TIFFTAG_PHOTOMETRIC       = 262,
    GWY_TIFFTAG_FILL_ORDER        = 266,
    GWY_TIFFTAG_DOCUMENT_NAME     = 269,
    GWY_TIFFTAG_IMAGE_DESCRIPTION = 270,
    GWY_TIFFTAG_MAKE              = 271,
    GWY_TIFFTAG_MODEL             = 272,
    GWY_TIFFTAG_STRIP_OFFSETS     = 273,
    GWY_TIFFTAG_ORIENTATION       = 274,
    GWY_TIFFTAG_SAMPLES_PER_PIXEL = 277,
    GWY_TIFFTAG_ROWS_PER_STRIP    = 278,
    GWY_TIFFTAG_STRIP_BYTE_COUNTS = 279,
    GWY_TIFFTAG_X_RESOLUTION      = 282,
    GWY_TIFFTAG_Y_RESOLUTION      = 283,
    GWY_TIFFTAG_PLANAR_CONFIG     = 284,
    GWY_TIFFTAG_RESOLUTION_UNIT   = 296,
    GWY_TIFFTAG_SOFTWARE          = 305,
    GWY_TIFFTAG_DATE_TIME         = 306,
    GWY_TIFFTAG_ARTIST            = 315,
    GWY_TIFFTAG_PREDICTOR         = 317,
    GWY_TIFFTAG_COLORMAP          = 320,
    GWY_TIFFTAG_TILE_WIDTH        = 322,
    GWY_TIFFTAG_TILE_LENGTH       = 323,
    GWY_TIFFTAG_TILE_OFFSETS      = 324,
    GWY_TIFFTAG_TILE_BYTE_COUNTS  = 325,
    GWY_TIFFTAG_SAMPLE_FORMAT     = 339,
    /* EXIF tags, used in LEXT. */
    GWY_TIFFTAG_EXIF_IFD                        = 34665,
    GWY_TIFFTAG_EXIF_VERSION                    = 36864,
    GWY_TIFFTAG_EXIF_DATETIME_ORIGINAL          = 36867,
    GWY_TIFFTAG_EXIF_DATETIME_DIGITIZED         = 36868,
    GWY_TIFFTAG_EXIF_USER_COMMENT               = 37510,
    GWY_TIFFTAG_EXIF_DATETIME_SUBSEC            = 37520,
    GWY_TIFFTAG_EXIF_DATETIME_ORIGINAL_SUBSEC   = 37521,
    GWY_TIFFTAG_EXIF_DATETIME_DIGITIZED_SUBSEC  = 37522,
    GWY_TIFFTAG_EXIF_DEVICE_SETTING_DESCRIPTION = 41995,
} GwyTIFFTag;

/* Values of some standard tags.
 * Note only values interesting for us are enumerated.  Add more from the standard if needed.  */

/* Baseline readers are required to implement NONE, HUFFMAN and PACKBITS.
 * PACKBITS seems to be used in the wild occasionally.
 * HUFFMAN is only for bilevel images and can be probably ignored. */
typedef enum {
    GWY_TIFF_COMPRESSION_NONE     = 1,
    GWY_TIFF_COMPRESSION_HUFFMAN  = 2,
    GWY_TIFF_COMPRESSION_LZW      = 5,
    GWY_TIFF_COMPRESSION_PACKBITS = 32773,
} GwyTIFFCompression;

typedef enum {
    GWY_TIFF_ORIENTATION_TOPLEFT  = 1,
    GWY_TIFF_ORIENTATION_TOPRIGHT = 2,
    GWY_TIFF_ORIENTATION_BOTRIGHT = 3,
    GWY_TIFF_ORIENTATION_BOTLEFT  = 4,
    GWY_TIFF_ORIENTATION_LEFTTOP  = 5,
    GWY_TIFF_ORIENTATION_RIGHTTOP = 6,
    GWY_TIFF_ORIENTATION_RIGHTBOT = 7,
    GWY_TIFF_ORIENTATION_LEFTBOT  = 8,
} GwyTIFFOrientation;

typedef enum {
    GWY_TIFF_PHOTOMETRIC_MIN_IS_WHITE = 0,
    GWY_TIFF_PHOTOMETRIC_MIN_IS_BLACK = 1,
    GWY_TIFF_PHOTOMETRIC_RGB          = 2,
} GwyTIFFPhotometric;

typedef enum {
    GWY_TIFF_SUBFILE_FULL_IMAGE_DATA    = 1,
    GWY_TIFF_SUBFILE_REDUCED_IMAGE_DATA = 2,
    GWY_TIFF_SUBFILE_SINGLE_PAGE        = 3,
} GwyTIFFSubFileType;

typedef enum {
    GWY_TIFF_PLANAR_CONFIG_CONTIGNUOUS = 1,
    GWY_TIFF_PLANAR_CONFIG_SEPARATE    = 2,
} GwyTIFFPlanarConfig;

typedef enum {
    GWY_TIFF_RESOLUTION_UNIT_NONE       = 1,
    GWY_TIFF_RESOLUTION_UNIT_INCH       = 2,
    GWY_TIFF_RESOLUTION_UNIT_CENTIMETER = 3,
} GwyTIFFResolutionUnit;

typedef enum {
    GWY_TIFF_SAMPLE_FORMAT_UNSIGNED_INTEGER = 1,
    GWY_TIFF_SAMPLE_FORMAT_SIGNED_INTEGER   = 2,
    GWY_TIFF_SAMPLE_FORMAT_FLOAT            = 3,
    GWY_TIFF_SAMPLE_FORMAT_UNDEFINED        = 4
} GwyTIFFSampleFormat;

typedef guint (*GwyTIFFUnpackFunc)(const guchar *packed,
                                   guint packedsize,
                                   guchar *unpacked,
                                   guint tounpack);

/* TIFF structure representation */
typedef struct {
    guint tag;
    GwyTIFFDataType type;
    guint64 count;
    guchar value[8];   /* The actual length is only 4 bytes in classic TIFF */
} GwyTIFFEntry;

typedef struct {
    guchar *data;
    guint64 size;
    GPtrArray *dirs;  /* Array of GwyTIFFEntry GArray*. */
    guint16 (*get_guint16)(const guchar **p);
    gint16 (*get_gint16)(const guchar **p);
    guint32 (*get_guint32)(const guchar **p);
    gint32 (*get_gint32)(const guchar **p);
    guint64 (*get_guint64)(const guchar **p);
    gint64 (*get_gint64)(const guchar **p);
    gfloat (*get_gfloat)(const guchar **p);
    gdouble (*get_gdouble)(const guchar **p);
    guint64 (*get_length)(const guchar **p);    /* 32bit, 64bit for BigTIFF */
    GwyTIFFVersion version;
    guint tagvaluesize;
    guint tagsize;
    guint ifdsize;
    gboolean allow_compressed;
} GwyTIFF;

/* State-object for image data reading */
typedef struct {
    /* public for reading */
    guint dirno;
    guint64 width;
    guint64 height;
    guint bits_per_sample;
    guint samples_per_pixel;
    /* private */
    guint64 strip_rows;
    guint64 tile_width;
    guint64 tile_height;
    guint64 rowstride;    /* For a single tile if image is tiled. */
    guint64 *offsets;     /* Either for strips or tiles. */
    guint64 *bytecounts;  /* Either for strips or tiles. */
    gdouble *rowbuf;
    guint sample_format;
    guint compression;
    /* Decompression (keeping track of current state). */
    GwyTIFFUnpackFunc unpack_func;
    guchar *unpacked;       /* Buffer for unpacking, large enough to hold one * strip or tile. */
    guint64 which_unpacked; /* Which strip or tile we have in unpacked[]; G_MAXUINT64 means none. */
} GwyTIFFImageReader;

/* Parameters version and byteorder are inout.  If they are non-zero, the file must match the specified value to be
 * accepted.  In any case, they are set to the true values on success. */
G_GNUC_UNUSED
static const guchar*
gwy_tiff_detect(const guchar *buffer,
                gsize size,
                GwyTIFFVersion *version,
                guint *byteorder)
{
    guint bom, vm;

    if (size < GWY_TIFF_HEADER_SIZE)
        return NULL;

    bom = gwy_get_guint16_le(&buffer);
    if (bom == 0x4949) {
        bom = G_LITTLE_ENDIAN;
        vm = gwy_get_guint16_le(&buffer);
    }
    else if (bom == 0x4d4d) {
        bom = G_BIG_ENDIAN;
        vm = gwy_get_guint16_be(&buffer);
    }
    else
        return NULL;

    if (vm != GWY_TIFF_CLASSIC && vm != GWY_TIFF_BIG)
        return NULL;

    if (vm == GWY_TIFF_BIG && size < GWY_TIFF_HEADER_SIZE_BIG)
        return NULL;

    // Typecast because of bloddy C++.
    if (version) {
        if (*version && *version != (GwyTIFFVersion)vm)
            return NULL;
        *version = (GwyTIFFVersion)vm;
    }

    if (byteorder) {
        if (*byteorder && *byteorder != bom)
            return NULL;
        *byteorder = bom;
    }

    return buffer;
}

/* By default compressed files are not allowed because no one saves SPM data this way.
 *
 * When files are not compressed gwy_tiff_read_image_row() can never fail because we can easily check the sizes of
 * everything before attempting to read data.  Consequently, most GwyTIFF users do not need any error handling after
 * successfully creating a GwyTIFFImageReader. */
G_GNUC_UNUSED static inline void
gwy_tiff_allow_compressed(GwyTIFF *tiff,
                          gboolean setting)
{
    tiff->allow_compressed = setting;
}

static gpointer
err_TIFF_REQUIRED_TAG(GError **error, GwyTIFFTag tag)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Required tag %u was not found."), tag);
    return NULL;
}

static inline gboolean
gwy_tiff_data_fits(const GwyTIFF *tiff,
                   guint64 offset,
                   guint64 item_size,
                   guint64 nitems)
{
    guint64 bytesize;

    /* Overflow in total size */
    if (nitems > G_GUINT64_CONSTANT(0xffffffffffffffff)/item_size)
        return FALSE;

    bytesize = nitems*item_size;
    /* Overflow in addition */
    if (offset + bytesize < offset)
        return FALSE;

    return offset + bytesize <= tiff->size;
}

static guint
gwy_tiff_data_type_size(GwyTIFFDataType type)
{
    switch (type) {
        case GWY_TIFF_BYTE:
        case GWY_TIFF_SBYTE:
        case GWY_TIFF_ASCII:
        return 1;
        break;

        case GWY_TIFF_SHORT:
        case GWY_TIFF_SSHORT:
        return 2;
        break;

        case GWY_TIFF_LONG:
        case GWY_TIFF_SLONG:
        case GWY_TIFF_FLOAT:
        return 4;
        break;

        case GWY_TIFF_RATIONAL:
        case GWY_TIFF_SRATIONAL:
        case GWY_TIFF_DOUBLE:
        case GWY_TIFF_LONG8:
        case GWY_TIFF_SLONG8:
        return 8;
        break;

        default:
        return 0;
        break;
    }
}

static GArray*
gwy_tiff_scan_ifd(const GwyTIFF *tiff, guint64 offset,
                  const guchar **pafter, GError **error)
{
    guint16 (*get_guint16)(const guchar **p) = tiff->get_guint16;
    guint64 (*get_length)(const guchar **p) = tiff->get_length;
    guint ifdsize = tiff->ifdsize;
    guint tagsize = tiff->tagsize;
    guint valuesize = tiff->tagvaluesize;
    guint64 nentries, i;
    const guchar *p;
    GArray *tags;

    if (!gwy_tiff_data_fits(tiff, offset, ifdsize, 1)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("TIFF directory %lu ended unexpectedly."), (gulong)tiff->dirs->len);
        return NULL;
    }

    p = tiff->data + offset;
    if (tiff->version == GWY_TIFF_CLASSIC)
        nentries = get_guint16(&p);
    else if (tiff->version == GWY_TIFF_BIG)
        nentries = tiff->get_guint64(&p);
    else {
        g_assert_not_reached();
    }

    if (!gwy_tiff_data_fits(tiff, offset + ifdsize, tagsize, nentries)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("TIFF directory %lu ended unexpectedly."), (gulong)tiff->dirs->len);
        return NULL;
    }

    tags = g_array_sized_new(FALSE, FALSE, sizeof(GwyTIFFEntry), nentries);
    for (i = 0; i < nentries; i++) {
        GwyTIFFEntry entry;

        entry.tag = get_guint16(&p);
        entry.type = (GwyTIFFDataType)get_guint16(&p);
        entry.count = get_length(&p);
        memcpy(entry.value, p, valuesize);
        p += tiff->tagvaluesize;
        g_array_append_val(tags, entry);
    }
    if (pafter)
        *pafter = p;

    return tags;
}

static gboolean
gwy_tiff_ifd_is_vaild(const GwyTIFF *tiff, const GArray *tags, GError **error)
{
    const guchar *p;
    guint j;

    for (j = 0; j < tags->len; j++) {
        const GwyTIFFEntry *entry;
        guint64 item_size, offset;

        entry = &g_array_index(tags, GwyTIFFEntry, j);
        if (tiff->version == GWY_TIFF_CLASSIC
            && (entry->type == GWY_TIFF_LONG8 || entry->type == GWY_TIFF_SLONG8 || entry->type == GWY_TIFF_IFD8)) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("BigTIFF data type %u was found in a classic TIFF."), entry->type);
            return FALSE;
        }
        p = entry->value;
        offset = tiff->get_length(&p);
        item_size = gwy_tiff_data_type_size(entry->type);
        /* Uknown types are implicitly OK.  If we cannot read it we never read it by definition, so let the hell take
         * what it refers to. This also means readers of custom types have to check the size themselves. */
        if (item_size
            && entry->count > tiff->tagvaluesize/item_size
            && !gwy_tiff_data_fits(tiff, offset, item_size, entry->count)) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Invalid tag data positions were found."));
            return FALSE;
        }
    }

    return TRUE;
}

/* Does not need to free tags on failure, the caller takes care of it. */
static gboolean
gwy_tiff_load_impl(GwyTIFF *tiff,
                   const gchar *filename,
                   GError **error)
{
    GError *err = NULL;
    GArray *tags;
    const guchar *p;
    guint64 offset;
    gsize size;
    guint byteorder = 0;

    if (!gwy_file_get_contents(filename, &tiff->data, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return FALSE;
    }
    tiff->size = size;

    p = tiff->data;
    if (!(p = gwy_tiff_detect(p, tiff->size, &tiff->version, &byteorder))) {
        err_FILE_TYPE(error, "TIFF");
        return FALSE;
    }

    if (byteorder == G_LITTLE_ENDIAN) {
        tiff->get_guint16 = gwy_get_guint16_le;
        tiff->get_gint16 = gwy_get_gint16_le;
        tiff->get_guint32 = gwy_get_guint32_le;
        tiff->get_gint32 = gwy_get_gint32_le;
        tiff->get_guint64 = gwy_get_guint64_le;
        tiff->get_gint64 = gwy_get_gint64_le;
        tiff->get_gfloat = gwy_get_gfloat_le;
        tiff->get_gdouble = gwy_get_gdouble_le;
        if (tiff->version == GWY_TIFF_BIG)
            tiff->get_length = gwy_get_guint64_le;
        else
            tiff->get_length = gwy_get_guint32as64_le;
    }
    else if (byteorder == G_BIG_ENDIAN) {
        tiff->get_guint16 = gwy_get_guint16_be;
        tiff->get_gint16 = gwy_get_gint16_be;
        tiff->get_guint32 = gwy_get_guint32_be;
        tiff->get_gint32 = gwy_get_gint32_be;
        tiff->get_guint64 = gwy_get_guint64_be;
        tiff->get_gint64 = gwy_get_gint64_be;
        tiff->get_gfloat = gwy_get_gfloat_be;
        tiff->get_gdouble = gwy_get_gdouble_be;
        if (tiff->version == GWY_TIFF_BIG)
            tiff->get_length = gwy_get_guint64_be;
        else
            tiff->get_length = gwy_get_guint32as64_be;
    }
    else {
        g_assert_not_reached();
    }

    if (tiff->version == GWY_TIFF_CLASSIC) {
        tiff->ifdsize = 2 + 4;
        tiff->tagsize = 12;
        tiff->tagvaluesize = 4;
    }
    else if (tiff->version == GWY_TIFF_BIG) {
        if (tiff->size < GWY_TIFF_HEADER_SIZE_BIG) {
            err_TOO_SHORT(error);
            return FALSE;
        }
        tiff->ifdsize = 8 + 8;
        tiff->tagsize = 20;
        tiff->tagvaluesize = 8;
    }
    else {
        g_assert_not_reached();
    }

    if (tiff->version == GWY_TIFF_BIG) {
        guint bytesize = tiff->get_guint16(&p);
        guint reserved0 = tiff->get_guint16(&p);

        if (bytesize != 8 || reserved0 != 0) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("BigTIFF reserved fields are %u and %u instead of 8 and 0."), bytesize, reserved0);
            return FALSE;
        }
    }

    tiff->dirs = g_ptr_array_new();
    while ((offset = tiff->get_length(&p))) {
        if (!(tags = gwy_tiff_scan_ifd(tiff, offset, &p, error)))
            return FALSE;
        g_ptr_array_add(tiff->dirs, tags);
    }

    return TRUE;
}

static inline void
gwy_tiff_free(GwyTIFF *tiff)
{
    if (tiff->dirs) {
        guint i;

        for (i = 0; i < tiff->dirs->len; i++) {
            GArray *dir = (GArray*)g_ptr_array_index(tiff->dirs, i);
            if (dir)
                g_array_free(dir, TRUE);
        }

        g_ptr_array_free(tiff->dirs, TRUE);
    }

    if (tiff->data)
        gwy_file_abandon_contents(tiff->data, tiff->size, NULL);

    g_free(tiff);
}

static gboolean
gwy_tiff_tags_valid(const GwyTIFF *tiff, GError **error)
{
    gsize i;

    for (i = 0; i < tiff->dirs->len; i++) {
        const GArray *tags = (const GArray*)g_ptr_array_index(tiff->dirs, i);

        if (!gwy_tiff_ifd_is_vaild(tiff, tags, error))
            return FALSE;
    }

    return TRUE;
}

static gint
gwy_tiff_tag_compare(gconstpointer a, gconstpointer b)
{
    const GwyTIFFEntry *ta = (const GwyTIFFEntry*)a;
    const GwyTIFFEntry *tb = (const GwyTIFFEntry*)b;

    if (ta->tag < tb->tag)
        return -1;
    if (ta->tag > tb->tag)
        return 1;
    return 0;
}

static inline void
gwy_tiff_sort_tags(GwyTIFF *tiff)
{
    gsize i;

    for (i = 0; i < tiff->dirs->len; i++)
        g_array_sort((GArray*)g_ptr_array_index(tiff->dirs, i), gwy_tiff_tag_compare);
}

static const GwyTIFFEntry*
gwy_tiff_find_tag_in_dir(const GArray *tags, guint tag)
{
    const GwyTIFFEntry *entry;
    gsize lo, hi, m;

    lo = 0;
    hi = tags->len-1;
    while (hi - lo > 1) {
        m = (lo + hi)/2;
        entry = &g_array_index(tags, GwyTIFFEntry, m);
        if (entry->tag > tag)
            hi = m;
        else
            lo = m;
    }

    entry = &g_array_index(tags, GwyTIFFEntry, lo);
    if (entry->tag == tag)
        return entry;

    entry = &g_array_index(tags, GwyTIFFEntry, hi);
    if (entry->tag == tag)
        return entry;

    return NULL;
}

static const GwyTIFFEntry*
gwy_tiff_find_tag(const GwyTIFF *tiff, guint dirno, guint tag)
{
    const GwyTIFFEntry *entry;
    const GArray *tags;

    if (!tiff->dirs)
        return NULL;

    /* If dirno is GWY_TIFF_ANY_DIR, search in all directories. */
    if (dirno == GWY_TIFF_ANY_DIR) {
        for (dirno = 0; dirno < tiff->dirs->len; dirno++) {
            tags = (const GArray*)g_ptr_array_index(tiff->dirs, dirno);
            if ((entry = gwy_tiff_find_tag_in_dir(tags, tag)))
                return entry;
        }
        return NULL;
    }

    if (dirno >= tiff->dirs->len)
        return NULL;

    tags = (const GArray*)g_ptr_array_index(tiff->dirs, dirno);
    return gwy_tiff_find_tag_in_dir(tags, tag);
}


G_GNUC_UNUSED static gboolean
gwy_tiff_get_uint_entry(const GwyTIFF *tiff,
                        const GwyTIFFEntry *entry,
                        guint *retval)
{
    const guchar *p;

    if (!entry || entry->count != 1)
        return FALSE;

    p = entry->value;
    switch (entry->type) {
        case GWY_TIFF_BYTE:
        *retval = p[0];
        break;

        case GWY_TIFF_SHORT:
        *retval = tiff->get_guint16(&p);
        break;

        case GWY_TIFF_LONG:
        *retval = tiff->get_guint32(&p);
        break;

        default:
        return FALSE;
        break;
    }

    return TRUE;
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_uint(const GwyTIFF *tiff,
                  guint dirno,
                  guint tag,
                  guint *retval)
{
    return gwy_tiff_get_uint_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_size_entry(const GwyTIFF *tiff,
                        const GwyTIFFEntry *entry,
                        guint64 *retval)
{
    const guchar *p;

    if (!entry || entry->count != 1)
        return FALSE;

    p = entry->value;
    switch (entry->type) {
        case GWY_TIFF_BYTE:
        *retval = p[0];
        break;

        case GWY_TIFF_SHORT:
        *retval = tiff->get_guint16(&p);
        break;

        case GWY_TIFF_LONG:
        *retval = tiff->get_guint32(&p);
        break;

        case GWY_TIFF_LONG8:
        *retval = tiff->get_guint64(&p);
        break;

        default:
        return FALSE;
        break;
    }

    return TRUE;
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_size(const GwyTIFF *tiff,
                  guint dirno,
                  guint tag,
                  guint64 *retval)
{
    return gwy_tiff_get_size_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_uints_entry(const GwyTIFF *tiff,
                         const GwyTIFFEntry *entry,
                         guint64 expected_count,
                         guint *retval)
{
    const guchar *p;
    guint64 i, offset, size = 0;

    if (!entry || entry->count != expected_count)
        return FALSE;

    p = entry->value;
    if (entry->type == GWY_TIFF_BYTE)
        size = expected_count;
    else if (entry->type == GWY_TIFF_SHORT)
        size = 2*expected_count;
    else if (entry->type == GWY_TIFF_LONG)
        size = 4*expected_count;
    else
        return FALSE;

    if (size > tiff->tagvaluesize) {
        offset = tiff->get_guint32(&p);
        p = tiff->data + offset;
    }

    for (i = 0; i < expected_count; i++) {
        switch (entry->type) {
            case GWY_TIFF_BYTE:
            *(retval++) = *(p++);
            break;

            case GWY_TIFF_SHORT:
            *(retval++) = tiff->get_guint16(&p);
            break;

            case GWY_TIFF_LONG:
            *(retval++) = tiff->get_guint32(&p);
            break;

            default:
            return FALSE;
            break;
        }
    }

    return TRUE;
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_uints(const GwyTIFF *tiff,
                   guint dirno,
                   guint tag,
                   guint64 expected_count,
                   guint *retval)
{
    return gwy_tiff_get_uints_entry(tiff,
                                    gwy_tiff_find_tag(tiff, dirno, tag),
                                    expected_count, retval);
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_sint_entry(const GwyTIFF *tiff,
                        const GwyTIFFEntry *entry,
                        gint *retval)
{
    const guchar *p;
    const gchar *q;

    if (!entry || entry->count != 1)
        return FALSE;

    p = entry->value;
    switch (entry->type) {
        case GWY_TIFF_SBYTE:
        q = (const gchar*)p;
        *retval = q[0];
        break;

        case GWY_TIFF_BYTE:
        *retval = p[0];
        break;

        case GWY_TIFF_SHORT:
        *retval = tiff->get_guint16(&p);
        break;

        case GWY_TIFF_SSHORT:
        *retval = tiff->get_gint16(&p);
        break;

        /* XXX: If the value does not fit, this is wrong no matter what. */
        case GWY_TIFF_LONG:
        *retval = tiff->get_guint32(&p);
        break;

        case GWY_TIFF_SLONG:
        *retval = tiff->get_gint32(&p);
        break;

        default:
        return FALSE;
        break;
    }

    return TRUE;
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_sint(const GwyTIFF *tiff,
                  guint dirno,
                  guint tag,
                  gint *retval)
{
    return gwy_tiff_get_sint_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_bool_entry(const GwyTIFF *tiff,
                        const GwyTIFFEntry *entry,
                        gboolean *retval)
{
    const guchar *p;
    const gchar *q;

    if (!entry || entry->count != 1)
        return FALSE;

    p = entry->value;
    switch (entry->type) {
        case GWY_TIFF_BYTE:
        case GWY_TIFF_SBYTE:
        q = (const gchar*)p;
        *retval = !!q[0];
        break;

        case GWY_TIFF_SHORT:
        case GWY_TIFF_SSHORT:
        *retval = !!tiff->get_gint16(&p);
        break;

        default:
        return FALSE;
        break;
    }

    return TRUE;
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_bool(const GwyTIFF *tiff,
                  guint dirno,
                  guint tag,
                  gboolean *retval)
{
    return gwy_tiff_get_bool_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_float_entry(const GwyTIFF *tiff,
                         const GwyTIFFEntry *entry,
                         gdouble *retval)
{
    const guchar *p;
    guint64 offset;

    if (!entry || entry->count != 1)
        return FALSE;

    p = entry->value;
    switch (entry->type) {
        case GWY_TIFF_FLOAT:
        *retval = tiff->get_gfloat(&p);
        break;

        case GWY_TIFF_DOUBLE:
        offset = tiff->get_guint32(&p);
        p = tiff->data + offset;
        *retval = tiff->get_gdouble(&p);
        break;

        default:
        return FALSE;
        break;
    }

    return TRUE;
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_float(const GwyTIFF *tiff,
                   guint dirno,
                   guint tag,
                   gdouble *retval)
{
    return gwy_tiff_get_float_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_string_entry(const GwyTIFF *tiff,
                          const GwyTIFFEntry *entry,
                          gchar **retval)
{
    const guchar *p;
    guint64 offset;

    if (!entry || entry->type != GWY_TIFF_ASCII)
        return FALSE;

    p = entry->value;
    if (entry->count <= tiff->tagvaluesize) {
        *retval = g_new0(gchar, MAX(entry->count, 1) + 1);
        memcpy(*retval, entry->value, entry->count);
    }
    else {
        offset = tiff->get_guint32(&p);
        p = tiff->data + offset;
        *retval = g_new(gchar, entry->count);
        memcpy(*retval, p, entry->count);
        (*retval)[entry->count-1] = '\0';
    }

    return TRUE;
}

G_GNUC_UNUSED static gboolean
gwy_tiff_get_string(const GwyTIFF *tiff,
                    guint dirno,
                    guint tag,
                    gchar **retval)
{
    return gwy_tiff_get_string_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

/* Unpack a data segment compressed using the PackBits algorithm.
 *
 * Returns the number of bytes consumed, except on failure when zero is returned.
 *
 * The caller must provide output buffer which can hold the entire segment. Since TIFF forbids packing across row
 * boundaries, we consider an error when we do not stop exactly at the requested number of bytes. */
G_GNUC_UNUSED static inline guint
gwy_tiff_unpack_packbits(const guchar *packed,
                         guint packedsize,
                         guchar *unpacked,
                         guint tounpack)
{
    guint x, b, i = 0;

    while (tounpack) {
        if (i == packedsize)
            return 0;

        x = packed[i++];
        if (x <= 127) {
            /* Copy next x+1 bytes literally. */
            x++;
            if (x > packedsize - i || x > tounpack)
                return 0;
            memcpy(unpacked, packed + i, x);
            unpacked += x;
            tounpack -= x;
            i += x;
        }
        else if (x > 128) {
            /* Take the number as negative and copy the next byte x+1 times. */
            x = 257 - x;
            if (i == packedsize || x > tounpack)
                return 0;
            b = packed[i++];
            tounpack -= x;
            while (x--)
                *(unpacked++) = b;
        }
        else {
            /* And this is apparently also a thing (x = 128 AKA -128). */
        }
    }

    return i;
}

G_GNUC_UNUSED static inline guint
gwy_tiff_lzw_get_code(const guchar *packed, guint packedsize,
                      guint *bitpos, guint nbits)
{
    const guchar *p = packed + *bitpos/8;
    guint bi = *bitpos % 8, x = 0;

    if (*bitpos + nbits > 8*packedsize)
        return G_MAXUINT;

    *bitpos += nbits;

    /* All our codes are larger than one byte so we always consume everything from the first byte. */
    x = ((0xff >> bi) & p[0]) << (nbits + bi - 8);
    if (nbits + bi <= 16)
        /* Another byte is enough. */
        return x | (p[1] >> (16 - nbits - bi));

    /* Another byte is not enough, so consume it all. */
    x |= (p[1] << (nbits + bi - 16));

    /* And with the next byte it is definitely enough because we can get at least 17 bits this way, but TIFF LZW needs
     * at most 12. */
    return x | (p[2] >> (24 - nbits - bi));
}

G_GNUC_UNUSED
static inline gboolean
gwy_tiff_lzw_append(const guchar *bytes,
                    guint nbytes,
                    guchar *unpacked,
                    guint tounpack,
                    guint *outpos)
{
    if (nbytes >= tounpack - *outpos) {
        memcpy(unpacked + *outpos, bytes, tounpack - *outpos);
        *outpos = tounpack;
        return TRUE;
    }

    memcpy(unpacked + *outpos, bytes, nbytes);
    *outpos += nbytes;
    return FALSE;
}

G_GNUC_UNUSED
static inline gboolean
gwy_tiff_lzw_append1(guint code,
                     guchar *unpacked,
                     guint tounpack,
                     guint *outpos)
{
    unpacked[*outpos] = code;
    (*outpos)++;
    return *outpos == tounpack;
}

G_GNUC_UNUSED static inline guint
gwy_tiff_unpack_lzw(const guchar *packed,
                    guint packedsize,
                    guchar *unpacked,
                    guint tounpack)
{
    enum {
        GWY_TIFF_NLZW = 4096,
        GWY_TIFF_LZW_CLEAR = 0x100,
        GWY_TIFF_LZW_END = 0x101,
        GWY_TIFF_LZW_FIRST = 0x102,
    };
    typedef struct {
        guint pos;
        guint which : 1;
        guint len : 31;
    } GwyTIFF_LZWCode;
    guint code, prev, i, bitpos, table_pos, nbits, outpos, len, retval = 0;
    GByteArray *buffer;
    GwyTIFF_LZWCode *table;
    const guchar *t;

    table = g_new(GwyTIFF_LZWCode, GWY_TIFF_NLZW);
    buffer = g_byte_array_sized_new(8192);
    g_byte_array_set_size(buffer, 0x100);
    /* We should never need the pos fields for the first 0x100 entries. */
    for (i = 0; i < 0x100; i++) {
        table[i].len = 1;
        table[i].which = 1;
        table[i].pos = i;
        buffer->data[i] = i;
    }

    table_pos = GWY_TIFF_LZW_FIRST;
    nbits = 9;
    bitpos = 0;
    outpos = 0;
    prev = sizeof("Silly, silly GCC");
    while (TRUE) {
        code = gwy_tiff_lzw_get_code(packed, packedsize, &bitpos, nbits);
        if (!i && code != GWY_TIFF_LZW_CLEAR) {
            gwy_debug("first code is not CLEAR");
            goto finalise;
        }
        if (code == GWY_TIFF_LZW_END) {
end:
            if (outpos == tounpack)
                retval = bitpos/8;
            else {
                gwy_debug("stream is shorter than requested");
            }
            goto finalise;
        }
        else if (code == GWY_TIFF_LZW_CLEAR) {
            nbits = 9;
            code = gwy_tiff_lzw_get_code(packed, packedsize, &bitpos, nbits);
            if (code > 0x100) {
                gwy_debug("first code after CLEAR is > 0x100");
                goto finalise;
            }
            if (code == GWY_TIFF_LZW_END)
                goto end;
            if (gwy_tiff_lzw_append1(code, unpacked, tounpack, &outpos)) {
                retval = bitpos/8;
                goto finalise;
            }
            table_pos = GWY_TIFF_LZW_FIRST;
            g_byte_array_set_size(buffer, 0x100);
        }
        else if (code < table_pos) {
            table[table_pos].which = 1;
            table[table_pos].pos = buffer->len;
            table[table_pos].len = table[prev].len + 1;
            t = (table[prev].which ? buffer->data : unpacked) + table[prev].pos;
            g_byte_array_append(buffer, (const guint8*)t, table[prev].len);
            t = (table[code].which ? buffer->data : unpacked) + table[code].pos;
            len = table[code].len;
            g_byte_array_append(buffer, (const guint8*)t, 1);
            if (gwy_tiff_lzw_append(t, len, unpacked, tounpack, &outpos)) {
                retval = bitpos/8;
                goto finalise;
            }
            table_pos++;
        }
        else if (code == table_pos) {
            table[table_pos].which = 0;
            table[table_pos].pos = outpos;
            table[table_pos].len = table[prev].len + 1;
            t = (table[prev].which ? buffer->data : unpacked) + table[prev].pos;
            len = table[prev].len;
            if (gwy_tiff_lzw_append(t, len, unpacked, tounpack, &outpos)
                || gwy_tiff_lzw_append1(t[0], unpacked, tounpack, &outpos)) {
                retval = bitpos/8;
                goto finalise;
            }
            table_pos++;
        }
        else {
            /* Any unseen code must be the next available.  Getting some other large number means things went awry.
             * This also covers getting G_MAXUINT from get-code. */
            gwy_debug("random unseen large code %u (expecting %u)", code, table_pos);
            goto finalise;
        }

        if (table_pos == 511 || table_pos == 1023 || table_pos == 2047)
            nbits++;
        if (table_pos == 4095) {
            gwy_debug("reached table pos 4095, so the next code would be 13bit, even if it was CLEAR");
            goto finalise;
        }
        prev = code;
    }

finalise:
    g_free(table);
    g_byte_array_free(buffer, TRUE);

    return retval;
}

/* Used for strip/tile offsets and byte counts. */
G_GNUC_UNUSED static inline gboolean
gwy_tiff_read_image_reader_sizes(const GwyTIFF *tiff,
                                 GwyTIFFImageReader *reader,
                                 GwyTIFFTag tag,
                                 guint64 *values,
                                 guint nvalues,
                                 GError **error)
{
    const GwyTIFFEntry *entry;
    const guchar *p;
    guint64 l;
    guint i;

    if (nvalues == 1) {
        if (!gwy_tiff_get_size(tiff, reader->dirno, tag, values))
            return !!err_TIFF_REQUIRED_TAG(error, tag);
        return TRUE;
    }

    if (!(entry = gwy_tiff_find_tag(tiff, reader->dirno, tag))
        || (entry->type != GWY_TIFF_SHORT && entry->type != GWY_TIFF_LONG && entry->type != GWY_TIFF_LONG8)
        || entry->count != nvalues) {
        return !!err_TIFF_REQUIRED_TAG(error, tag);
    }

    /* Matching type ensured the tag data is at a valid position in the file. */
    p = entry->value;
    i = tiff->get_guint32(&p);
    p = tiff->data + i;
    if (entry->type == GWY_TIFF_LONG) {
        for (l = 0; l < nvalues; l++)
            values[l] = tiff->get_guint32(&p);
    }
    else if (entry->type == GWY_TIFF_LONG8) {
        for (l = 0; l < nvalues; l++)
            values[l] = tiff->get_guint64(&p);
    }
    else if (entry->type == GWY_TIFF_SHORT) {
        for (l = 0; l < nvalues; l++)
            values[l] = tiff->get_guint16(&p);
    }
    else {
        g_return_val_if_reached(FALSE);
    }

    return TRUE;
}

G_GNUC_UNUSED static inline gboolean
gwy_tiff_init_image_reader_striped(const GwyTIFF *tiff,
                                   GwyTIFFImageReader *reader,
                                   GError **error)
{
    guint64 nstrips, i, ssize;

    if (reader->strip_rows == 0) {
        err_INVALID(error, "RowsPerStrip");
        return FALSE;
    }

    if (reader->compression == GWY_TIFF_COMPRESSION_PACKBITS)
        reader->unpack_func = gwy_tiff_unpack_packbits;
    else if (reader->compression == GWY_TIFF_COMPRESSION_LZW)
        reader->unpack_func = gwy_tiff_unpack_lzw;
    else if (reader->compression != GWY_TIFF_COMPRESSION_NONE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Compression type %u is not supported."), reader->compression);
        return FALSE;
    }

    nstrips = (reader->height + reader->strip_rows-1)/reader->strip_rows;
    reader->offsets = g_new(guint64, nstrips);
    reader->bytecounts = g_new(guint64, nstrips);
    if (!gwy_tiff_read_image_reader_sizes(tiff, reader, GWY_TIFFTAG_STRIP_OFFSETS,
                                          reader->offsets, nstrips, error))
        goto fail;
    if (!gwy_tiff_read_image_reader_sizes(tiff, reader, GWY_TIFFTAG_STRIP_BYTE_COUNTS,
                                          reader->bytecounts, nstrips, error))
        goto fail;

    /* Validate strip offsets and sizes.  Strips are not padded so the last strip can be shorter.*/
    reader->rowstride = (reader->bits_per_sample/8 * reader->samples_per_pixel * reader->width);
    ssize = reader->rowstride * reader->strip_rows;
    for (i = 0; i < nstrips; i++) {
        if (i == nstrips-1 && reader->height % reader->strip_rows)
            ssize = reader->rowstride * (reader->height % reader->strip_rows);
        if ((reader->compression == GWY_TIFF_COMPRESSION_NONE && ssize != reader->bytecounts[i])
            || reader->offsets[i] + reader->bytecounts[i] > tiff->size) {
            err_INVALID(error, "StripOffsets");
            goto fail;
        }
    }

    if (reader->compression != GWY_TIFF_COMPRESSION_NONE) {
        ssize = reader->rowstride * reader->strip_rows;
        reader->unpacked = g_new(guchar, ssize);
    }

    return TRUE;

fail:
    GWY_FREE(reader->offsets);
    GWY_FREE(reader->bytecounts);
    return FALSE;
}

G_GNUC_UNUSED static inline gboolean
gwy_tiff_init_image_reader_tiled(const GwyTIFF *tiff,
                                 GwyTIFFImageReader *reader,
                                 GError **error)
{
    guint64 nhtiles, nvtiles, ntiles, i, tsize;

    if (reader->tile_width == 0 || tiff->size/reader->tile_width == 0) {
        err_INVALID(error, "TileWidth");
        return FALSE;
    }
    if (reader->tile_height == 0 || tiff->size/reader->tile_height == 0) {
        err_INVALID(error, "TileLength");   /* The specs calls it length. */
        return FALSE;
    }

    if (reader->compression != GWY_TIFF_COMPRESSION_NONE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Compression type %u is not supported."), reader->compression);
        return FALSE;
    }

    nhtiles = (reader->width + reader->tile_width-1)/reader->tile_width;
    nvtiles = (reader->height + reader->tile_height-1)/reader->tile_height;
    ntiles = nhtiles*nvtiles;
    reader->offsets = g_new(guint64, ntiles);
    reader->bytecounts = g_new(guint64, ntiles);
    if (!gwy_tiff_read_image_reader_sizes(tiff, reader, GWY_TIFFTAG_TILE_OFFSETS,
                                          reader->offsets, ntiles, error))
        goto fail;
    if (!gwy_tiff_read_image_reader_sizes(tiff, reader, GWY_TIFFTAG_TILE_BYTE_COUNTS,
                                          reader->bytecounts, ntiles, error))
        goto fail;

    /* Validate tile offsets and sizes.  Tiles are padded so size must be reserved for entire tiles.  The standard
     * says the tile width must be a multiple of 16 so we can ignore alignment as only invalid files would need row
     * padding. */
    reader->rowstride = (reader->bits_per_sample/8 * reader->samples_per_pixel * reader->tile_width);
    tsize = reader->rowstride * reader->tile_height;
    for (i = 0; i < ntiles; i++) {
        if ((reader->compression == GWY_TIFF_COMPRESSION_NONE && tsize != reader->bytecounts[i])
            || reader->offsets[i] + reader->bytecounts[i] > tiff->size) {
            err_INVALID(error, "TileOffsets");
            goto fail;
        }
    }

    if (reader->compression != GWY_TIFF_COMPRESSION_NONE)
        reader->unpacked = g_new(guchar, tsize);

    return TRUE;

fail:
    GWY_FREE(reader->offsets);
    GWY_FREE(reader->bytecounts);
    return FALSE;
}

G_GNUC_UNUSED static GwyTIFFImageReader*
gwy_tiff_get_image_reader(const GwyTIFF *tiff,
                          guint dirno,
                          guint max_samples,
                          GError **error)
{
    GwyTIFFImageReader reader;
    guint i;
    guint *bps;

    gwy_clear(&reader, 1);
    reader.dirno = dirno;
    reader.which_unpacked = G_MAXUINT64;

    /* Required integer fields */
    if (!gwy_tiff_get_size(tiff, dirno, GWY_TIFFTAG_IMAGE_WIDTH, &reader.width))
        return (GwyTIFFImageReader*)err_TIFF_REQUIRED_TAG(error, GWY_TIFFTAG_IMAGE_WIDTH);
    if (!gwy_tiff_get_size(tiff, dirno, GWY_TIFFTAG_IMAGE_LENGTH, &reader.height))
        return (GwyTIFFImageReader*)err_TIFF_REQUIRED_TAG(error, GWY_TIFFTAG_IMAGE_LENGTH);

    /* The TIFF specs say this is required, but it seems to default to 1. */
    if (!gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_SAMPLES_PER_PIXEL, &reader.samples_per_pixel))
        reader.samples_per_pixel = 1;
    if (reader.samples_per_pixel == 0 || reader.samples_per_pixel > max_samples) {
        err_UNSUPPORTED(error, "SamplesPerPixel");
        return NULL;
    }

    /* The TIFF specs say this is required, but it seems to default to 1. */
    bps = g_new(guint, reader.samples_per_pixel);
    if (!gwy_tiff_get_uints(tiff, dirno, GWY_TIFFTAG_BITS_PER_SAMPLE, reader.samples_per_pixel, bps))
        reader.bits_per_sample = 1;
    else {
        for (i = 1; i < reader.samples_per_pixel; i++) {
            if (bps[i] != bps[i-1]) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                            _("Non-uniform bits per sample are unsupported."));
                g_free(bps);
                return NULL;
            }
        }
        reader.bits_per_sample = bps[0];
    }
    g_free(bps);

    /* The TIFF specs say this is required, but it seems to default to MAXINT.  Setting more reasonably
     * RowsPerStrip = ImageLength achieves the same end.  Also it is not required for tiled images. */
    if (!gwy_tiff_get_size(tiff, dirno, GWY_TIFFTAG_ROWS_PER_STRIP, &reader.strip_rows))
        reader.strip_rows = reader.height;

    /* The data sample type (default is unsigned integer). */
    if (!gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_SAMPLE_FORMAT, &reader.sample_format))
        reader.sample_format = GWY_TIFF_SAMPLE_FORMAT_UNSIGNED_INTEGER;

    /* Integer fields specifying data in a format we do not support */
    if (!gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_COMPRESSION, &reader.compression))
        reader.compression = GWY_TIFF_COMPRESSION_NONE;

    if (!tiff->allow_compressed && reader.compression != GWY_TIFF_COMPRESSION_NONE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Compression type %u is not supported."), reader.compression);
        return NULL;
    }

    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_PLANAR_CONFIG, &i) && i != GWY_TIFF_PLANAR_CONFIG_CONTIGNUOUS) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Planar configuration %u is not supported."), i);
        return NULL;
    }

    /* Sample format and bits per sample combinations. */
    if (reader.sample_format == GWY_TIFF_SAMPLE_FORMAT_UNSIGNED_INTEGER
        || reader.sample_format == GWY_TIFF_SAMPLE_FORMAT_SIGNED_INTEGER) {
        if (reader.bits_per_sample != 8
            && reader.bits_per_sample != 16
            && reader.bits_per_sample != 32
            && reader.bits_per_sample != 64) {
            err_BPP(error, reader.bits_per_sample);
            return NULL;
        }
    }
    else if (reader.sample_format == GWY_TIFF_SAMPLE_FORMAT_FLOAT) {
        if (reader.bits_per_sample != 32 && reader.bits_per_sample != 64) {
            err_BPP(error, reader.bits_per_sample);
            return NULL;
        }
    }
    else {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Unsupported sample format"));
        return NULL;
    }

    /* Apparently in Zeiss SEM files, RowsPerStrip can be *anything* larger than the imager height. */
    if (reader.strip_rows > reader.height)
        reader.strip_rows = reader.height;

    if (err_DIMENSION(error, reader.width) || err_DIMENSION(error, reader.height))
        return NULL;

    /* If we can read the tile dimensions assume it is a tiled image and report possible errors as for a tiled image.
     * If the image contains just one of them ignore it (and report errors as for a non-tiled image). */
    if (gwy_tiff_get_size(tiff, dirno, GWY_TIFFTAG_TILE_WIDTH, &reader.tile_width)
        && gwy_tiff_get_size(tiff, dirno, GWY_TIFFTAG_TILE_LENGTH, &reader.tile_height)) {
        reader.strip_rows = 0;
        if (!gwy_tiff_init_image_reader_tiled(tiff, &reader, error))
            return NULL;
    }
    else {
        reader.tile_width = reader.tile_height = 0;
        if (!gwy_tiff_init_image_reader_striped(tiff, &reader, error))
            return NULL;
    }

    /* If we got here we are convinced we can read the image data. */
    return (GwyTIFFImageReader*)g_memdup(&reader, sizeof(GwyTIFFImageReader));
}

G_GNUC_UNUSED static inline void
gwy_tiff_reader_read_segment(const GwyTIFF *tiff,
                             GwyTIFFSampleFormat sformat,
                             guint bits_per_sample,
                             const guchar *p,
                             guint len,
                             guint skip,
                             gdouble q,
                             gdouble z0,
                             gdouble *dest)
{
    guint i;

    switch (bits_per_sample) {
        case 8:
        skip++;
        if (sformat == GWY_TIFF_SAMPLE_FORMAT_UNSIGNED_INTEGER) {
            for (i = 0; i < len; i++, p += skip)
                dest[i] = z0 + q*(*p);
        }
        else if (sformat == GWY_TIFF_SAMPLE_FORMAT_SIGNED_INTEGER) {
            const gchar *s = (const gchar*)p;
            for (i = 0; i < len; i++, s += skip)
                dest[i] = z0 + q*(*s);
        }
        break;

        case 16:
        if (sformat == GWY_TIFF_SAMPLE_FORMAT_UNSIGNED_INTEGER) {
            for (i = 0; i < len; i++, p += skip)
                dest[i] = z0 + q*tiff->get_guint16(&p);
        }
        else if (sformat == GWY_TIFF_SAMPLE_FORMAT_SIGNED_INTEGER) {
            for (i = 0; i < len; i++, p += skip)
                dest[i] = z0 + q*tiff->get_gint16(&p);
        }
        break;

        case 32:
        if (sformat == GWY_TIFF_SAMPLE_FORMAT_UNSIGNED_INTEGER) {
            for (i = 0; i < len; i++, p += skip)
                dest[i] = z0 + q*tiff->get_guint32(&p);
        }
        else if (sformat == GWY_TIFF_SAMPLE_FORMAT_SIGNED_INTEGER) {
            for (i = 0; i < len; i++, p += skip)
                dest[i] = z0 + q*tiff->get_gint32(&p);
        }
        else if (sformat == GWY_TIFF_SAMPLE_FORMAT_FLOAT) {
            for (i = 0; i < len; i++, p += skip)
                dest[i] = z0 + q*tiff->get_gfloat(&p);
        }
        break;

        case 64:
        if (sformat == GWY_TIFF_SAMPLE_FORMAT_UNSIGNED_INTEGER) {
            for (i = 0; i < len; i++, p += skip)
                dest[i] = z0 + q*tiff->get_guint64(&p);
        }
        else if (sformat == GWY_TIFF_SAMPLE_FORMAT_SIGNED_INTEGER) {
            for (i = 0; i < len; i++, p += skip)
                dest[i] = z0 + q*tiff->get_gint64(&p);
        }
        else if (sformat == GWY_TIFF_SAMPLE_FORMAT_FLOAT) {
            for (i = 0; i < len; i++, p += skip)
                dest[i] = z0 + q*tiff->get_gdouble(&p);
        }
        break;

        default:
        g_return_if_reached();
        break;
    }
}

G_GNUC_UNUSED static inline gboolean
gwy_tiff_read_image_row_striped(const GwyTIFF *tiff,
                                GwyTIFFImageReader *reader,
                                guint channelno,
                                guint rowno,
                                gdouble q,
                                gdouble z0,
                                gdouble *dest)
{
    GwyTIFFSampleFormat sformat = (GwyTIFFSampleFormat)reader->sample_format;
    guint bps = reader->bits_per_sample;
    guint stripno, stripindex, skip, nstrips;
    gsize nrows, rowstride;
    const guchar *p;

    rowstride = reader->rowstride;
    nrows = reader->strip_rows;
    stripno = rowno/nrows;
    stripindex = rowno % nrows;
    p = tiff->data + reader->offsets[stripno];
    if (reader->unpack_func) {
        g_assert(reader->unpacked);
        /* If we want a row from different stripe than current we unpack the stripe. */
        if (stripno != reader->which_unpacked) {
            nstrips = (reader->height + nrows-1)/nrows;
            if (stripno == nstrips-1 && reader->height % nrows)
                nrows = reader->height % nrows;
            if (!reader->unpack_func(p, reader->bytecounts[stripno], reader->unpacked, rowstride * nrows))
                return FALSE;
            reader->which_unpacked = stripno;
        }
        /* Read from the unpacked buffer instead of the file data. */
        p = reader->unpacked;
    }
    p += stripindex*rowstride + (bps/8)*channelno;
    skip = (reader->samples_per_pixel - 1)*bps/8;
    gwy_tiff_reader_read_segment(tiff, sformat, bps, p, reader->width, skip, q, z0, dest);

    return TRUE;
}

G_GNUC_UNUSED static inline void
gwy_tiff_read_image_row_tiled(const GwyTIFF *tiff,
                              GwyTIFFImageReader *reader,
                              guint channelno,
                              guint rowno,
                              gdouble q,
                              gdouble z0,
                              gdouble *dest)
{
    GwyTIFFSampleFormat sformat = (GwyTIFFSampleFormat)reader->sample_format;
    guint bps = reader->bits_per_sample;
    guint nhtiles, vtileno, vtileindex, i, l, skip, len;
    const guchar *p;

    nhtiles = (reader->width + reader->tile_width-1)/reader->tile_width;
    vtileno = rowno/reader->tile_height;
    vtileindex = rowno % reader->tile_height;
    skip = (reader->samples_per_pixel - 1)*bps/8;
    len = reader->tile_width;
    for (i = 0; i < nhtiles; i++) {
        l = vtileno*nhtiles + i;
        p = tiff->data + (reader->offsets[l] + vtileindex*reader->rowstride + (bps/8)*channelno);
        if (i == reader->tile_width-1 && reader->width % reader->tile_width)
            len = reader->width % reader->tile_width;
        gwy_tiff_reader_read_segment(tiff, sformat, bps, p, len, skip, q, z0, dest);
        dest += len;
    }
}

/* If the file may be compressed (which needs to be explicitly allowed using gwy_tiff_allow_compressed()) this
 * function needs to be called with rowno in a mononotonically increasing sequence.  Anything else can result in
 * repeated unpacking data from the beginning and quadratic time complexity. */
G_GNUC_UNUSED static inline gboolean
gwy_tiff_read_image_row(const GwyTIFF *tiff,
                        GwyTIFFImageReader *reader,
                        guint channelno,
                        guint rowno,
                        gdouble q,
                        gdouble z0,
                        gdouble *dest)
{
    g_return_val_if_fail(tiff, FALSE);
    g_return_val_if_fail(reader, FALSE);
    g_return_val_if_fail(reader->dirno < tiff->dirs->len, FALSE);
    g_return_val_if_fail(rowno < reader->height, FALSE);
    g_return_val_if_fail(channelno < reader->samples_per_pixel, FALSE);
    if (reader->strip_rows) {
        g_return_val_if_fail(!reader->tile_width, FALSE);
        gwy_tiff_read_image_row_striped(tiff, reader, channelno, rowno, q, z0, dest);
    }
    else {
        g_return_val_if_fail(reader->tile_width, FALSE);
        g_return_val_if_fail(!reader->unpack_func, FALSE);
        gwy_tiff_read_image_row_tiled(tiff, reader, channelno, rowno, q, z0, dest);
    }

    return TRUE;
}

G_GNUC_UNUSED static inline void
gwy_tiff_read_image_row_averaged(const GwyTIFF *tiff,
                                 GwyTIFFImageReader *reader,
                                 guint rowno,
                                 gdouble q,
                                 gdouble z0,
                                 gdouble *dest)
{
    gint ch, j, width, spp = reader->samples_per_pixel;
    gdouble *rowbuf;

    g_return_if_fail(spp >= 1);

    q /= spp;
    gwy_tiff_read_image_row(tiff, reader, 0, rowno, q, z0, dest);
    if (spp == 1)
        return;

    width = reader->width;
    if (!reader->rowbuf)
        reader->rowbuf = g_new(gdouble, width);

    rowbuf = reader->rowbuf;
    for (ch = 1; ch < spp; ch++) {
        gwy_tiff_read_image_row(tiff, reader, ch, rowno, q, 0.0, rowbuf);
        for (j = 0; j < width; j++)
            dest[j] += rowbuf[j];
    }
}

/* Idempotent, use: reader = gwy_tiff_image_reader_free(reader); */
G_GNUC_UNUSED static inline GwyTIFFImageReader*
gwy_tiff_image_reader_free(GwyTIFFImageReader *reader)
{
    if (reader) {
        g_free(reader->offsets);
        g_free(reader->bytecounts);
        g_free(reader->unpacked);
        g_free(reader->rowbuf);
        g_free(reader);
    }
    return NULL;
}

G_GNUC_UNUSED static inline guint
gwy_tiff_get_n_dirs(const GwyTIFF *tiff)
{
    if (!tiff->dirs)
        return 0;

    return tiff->dirs->len;
}

G_GNUC_UNUSED static GwyTIFF*
gwy_tiff_load(const gchar *filename,
              GError **error)
{
    GwyTIFF *tiff;

    tiff = g_new0(GwyTIFF, 1);
    if (gwy_tiff_load_impl(tiff, filename, error) && gwy_tiff_tags_valid(tiff, error)) {
        gwy_tiff_sort_tags(tiff);
        return tiff;
    }

    gwy_tiff_free(tiff);
    return NULL;
}

/* vim: set cin et columns=120 tw=118 ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
