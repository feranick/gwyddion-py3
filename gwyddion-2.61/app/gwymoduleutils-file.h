/*
 *  $Id: gwymoduleutils-file.h 23978 2021-08-13 14:48:02Z yeti-dn $
 *  Copyright (C) 2007-2019 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_MODULEUTILS_FILE_H__
#define __GWY_MODULEUTILS_FILE_H__

#include <string.h>
#include <glib.h>
#include <libgwyddion/gwycontainer.h>
#include <libprocess/datafield.h>

G_BEGIN_DECLS

/* This is necessary to fool gtk-doc that ignores static inline functions */
#define _GWY_STATIC_INLINE static inline

_GWY_STATIC_INLINE gboolean gwy_get_gboolean8     (const guchar **ppv);
_GWY_STATIC_INLINE gint16   gwy_get_gint16_le     (const guchar **ppv);
_GWY_STATIC_INLINE gint16   gwy_get_gint16_be     (const guchar **ppv);
_GWY_STATIC_INLINE guint16  gwy_get_guint16_le    (const guchar **ppv);
_GWY_STATIC_INLINE guint16  gwy_get_guint16_be    (const guchar **ppv);
_GWY_STATIC_INLINE gint32   gwy_get_gint32_le     (const guchar **ppv);
_GWY_STATIC_INLINE gint32   gwy_get_gint32_be     (const guchar **ppv);
_GWY_STATIC_INLINE guint32  gwy_get_guint32_le    (const guchar **ppv);
_GWY_STATIC_INLINE guint32  gwy_get_guint32_be    (const guchar **ppv);
_GWY_STATIC_INLINE gint64   gwy_get_gint64_le     (const guchar **ppv);
_GWY_STATIC_INLINE gint64   gwy_get_gint64_be     (const guchar **ppv);
_GWY_STATIC_INLINE guint64  gwy_get_guint64_le    (const guchar **ppv);
_GWY_STATIC_INLINE guint64  gwy_get_guint64_be    (const guchar **ppv);
_GWY_STATIC_INLINE gfloat   gwy_get_gfloat_le     (const guchar **ppv);
_GWY_STATIC_INLINE gfloat   gwy_get_gfloat_be     (const guchar **ppv);
_GWY_STATIC_INLINE gdouble  gwy_get_gdouble_le    (const guchar **ppv);
_GWY_STATIC_INLINE gdouble  gwy_get_gdouble_be    (const guchar **ppv);
_GWY_STATIC_INLINE gdouble  gwy_get_pascal_real_le(const guchar **ppv);
_GWY_STATIC_INLINE gdouble  gwy_get_pascal_real_be(const guchar **ppv);

#undef _GWY_STATIC_INLINE

static inline gboolean
gwy_get_gboolean8(const guchar **ppv)
{
    const guint8 *pv = (const guint8*)(*ppv);
    guint8 v = *pv;
    *ppv += sizeof(guint8);
    return !!v;
}

static inline gint16
gwy_get_gint16_le(const guchar **ppv)
{
    const gint16 *pv = (const gint16*)(*ppv);
    gint16 v = *pv;
    *ppv += sizeof(gint16);
    return GINT16_FROM_LE(v);
}

static inline gint16
gwy_get_gint16_be(const guchar **ppv)
{
    const gint16 *pv = (const gint16*)(*ppv);
    gint16 v = *pv;
    *ppv += sizeof(gint16);
    return GINT16_FROM_BE(v);
}

static inline guint16
gwy_get_guint16_le(const guchar **ppv)
{
    const guint16 *pv = (const guint16*)(*ppv);
    guint16 v = *pv;
    *ppv += sizeof(guint16);
    return GUINT16_FROM_LE(v);
}

static inline guint16
gwy_get_guint16_be(const guchar **ppv)
{
    const guint16 *pv = (const guint16*)(*ppv);
    guint16 v = *pv;
    *ppv += sizeof(guint16);
    return GUINT16_FROM_BE(v);
}

static inline gint32
gwy_get_gint32_le(const guchar **ppv)
{
    const gint32 *pv = (const gint32*)(*ppv);
    gint32 v = *pv;
    *ppv += sizeof(gint32);
    return GINT32_FROM_LE(v);
}

static inline gint32
gwy_get_gint32_be(const guchar **ppv)
{
    const gint32 *pv = (const gint32*)(*ppv);
    gint32 v = *pv;
    *ppv += sizeof(gint32);
    return GINT32_FROM_BE(v);
}

static inline guint32
gwy_get_guint32_le(const guchar **ppv)
{
    const guint32 *pv = (const guint32*)(*ppv);
    guint32 v = *pv;
    *ppv += sizeof(guint32);
    return GUINT32_FROM_LE(v);
}

static inline guint32
gwy_get_guint32_be(const guchar **ppv)
{
    const guint32 *pv = (const guint32*)(*ppv);
    guint32 v = *pv;
    *ppv += sizeof(guint32);
    return GUINT32_FROM_BE(v);
}

static inline gint64
gwy_get_gint64_le(const guchar **ppv)
{
    const gint64 *pv = (const gint64*)(*ppv);
    gint64 v = *pv;
    *ppv += sizeof(gint64);
    return GINT64_FROM_LE(v);
}

static inline gint64
gwy_get_gint64_be(const guchar **ppv)
{
    const gint64 *pv = (const gint64*)(*ppv);
    gint64 v = *pv;
    *ppv += sizeof(gint64);
    return GINT64_FROM_BE(v);
}

static inline guint64
gwy_get_guint64_le(const guchar **ppv)
{
    const guint64 *pv = (const guint64*)(*ppv);
    guint64 v = *pv;
    *ppv += sizeof(guint64);
    return GUINT64_FROM_LE(v);
}

static inline guint64
gwy_get_guint64_be(const guchar **ppv)
{
    const guint64 *pv = (const guint64*)(*ppv);
    guint64 v = *pv;
    *ppv += sizeof(guint64);
    return GUINT64_FROM_BE(v);
}

static inline gfloat
gwy_get_gfloat_le(const guchar **p)
{
    union { guchar pp[4]; gfloat f; } z;

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
    memcpy(z.pp, *p, sizeof(gfloat));
#else
    z.pp[0] = (*p)[3];
    z.pp[1] = (*p)[2];
    z.pp[2] = (*p)[1];
    z.pp[3] = (*p)[0];
#endif
    *p += sizeof(gfloat);
    return z.f;
}

static inline gfloat
gwy_get_gfloat_be(const guchar **p)
{
    union { guchar pp[4]; gfloat f; } z;

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    memcpy(z.pp, *p, sizeof(gfloat));
#else
    z.pp[0] = (*p)[3];
    z.pp[1] = (*p)[2];
    z.pp[2] = (*p)[1];
    z.pp[3] = (*p)[0];
#endif
    *p += sizeof(gfloat);
    return z.f;
}

static inline gdouble
gwy_get_gdouble_le(const guchar **p)
{
    union { guchar pp[8]; gdouble d; } z;

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
    memcpy(z.pp, *p, sizeof(gdouble));
#else
    z.pp[0] = (*p)[7];
    z.pp[1] = (*p)[6];
    z.pp[2] = (*p)[5];
    z.pp[3] = (*p)[4];
    z.pp[4] = (*p)[3];
    z.pp[5] = (*p)[2];
    z.pp[6] = (*p)[1];
    z.pp[7] = (*p)[0];
#endif
    *p += sizeof(gdouble);
    return z.d;
}

static inline gdouble
gwy_get_gdouble_be(const guchar **p)
{
    union { guchar pp[8]; gdouble d; } z;

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    memcpy(z.pp, *p, sizeof(gdouble));
#else
    z.pp[0] = (*p)[7];
    z.pp[1] = (*p)[6];
    z.pp[2] = (*p)[5];
    z.pp[3] = (*p)[4];
    z.pp[4] = (*p)[3];
    z.pp[5] = (*p)[2];
    z.pp[6] = (*p)[1];
    z.pp[7] = (*p)[0];
#endif
    *p += sizeof(gdouble);
    return z.d;
}

static inline gdouble
gwy_get_pascal_real_le(const guchar **ppv)
{
    gint power;
    gdouble x;

    if (!(*ppv)[0]) {
        *ppv += 6;
        return 0.0;
    }
    x = 1.0 + (((((*ppv)[1]/256.0 + (*ppv)[2])/256.0 + (*ppv)[3])/256.0
                + (*ppv)[4])/256.0 + ((*ppv)[5] & 0x7f))/128.0;
    if ((*ppv)[5] & 0x80)
        x = -x;

    power = (gint)(*ppv)[0] - 129;
    while (power > 0) {
        x *= 2.0;
        power--;
    }
    while (power < 0) {
        x /= 2.0;
        power++;
    }

    *ppv += 6;

    return x;
}

static inline gdouble
gwy_get_pascal_real_be(const guchar **ppv)
{
    gint power;
    gdouble x;

    if (!(*ppv)[5]) {
        *ppv += 6;
        return 0.0;
    }
    x = 1.0 + (((((*ppv)[4]/256.0 + (*ppv)[3])/256.0 + (*ppv)[2])/256.0
                + (*ppv)[1])/256.0 + ((*ppv)[0] & 0x7f))/128.0;
    if ((*ppv)[0] & 0x80)
        x = -x;

    power = (gint)(*ppv)[5] - 129;
    while (power > 0) {
        x *= 2.0;
        power--;
    }
    while (power < 0) {
        x /= 2.0;
        power++;
    }

    *ppv += 6;

    return x;
}

gboolean      gwy_app_channel_check_nonsquare(GwyContainer *data,
                                              gint id);
gboolean      gwy_app_channel_title_fall_back(GwyContainer *data,
                                              gint id);
gboolean      gwy_app_xyz_title_fall_back    (GwyContainer *data,
                                              gint id);
guint         gwy_app_channel_remove_bad_data(GwyDataField *dfield,
                                              GwyDataField *mfield);
GwyDataField* gwy_app_channel_mask_of_nans   (GwyDataField *dfield,
                                              gboolean removebad);

#define GWY_TEXT_HEADER_ERROR gwy_text_header_error_quark()

typedef enum {
    GWY_TEXT_HEADER_ERROR_SECTION_NAME,
    GWY_TEXT_HEADER_ERROR_SECTION_END,
    GWY_TEXT_HEADER_ERROR_SECTION_START,
    GWY_TEXT_HEADER_ERROR_PREFIX,
    GWY_TEXT_HEADER_ERROR_GARBAGE,
    GWY_TEXT_HEADER_ERROR_KEY,
    GWY_TEXT_HEADER_ERROR_VALUE,
    GWY_TEXT_HEADER_ERROR_TERMINATOR,
} GwyTextHeaderError;

typedef struct _GwyTextHeaderContext GwyTextHeaderContext;

typedef struct {
    const gchar *comment_prefix;
    const gchar *section_template;
    const gchar *endsection_template;
    const gchar *section_accessor;
    const gchar *line_prefix;
    const gchar *key_value_separator;
    const gchar *terminator;
    gboolean (*item)(const GwyTextHeaderContext *context,
                     GHashTable *hash,
                     gchar *key,
                     gchar *value,
                     gpointer user_data,
                     GError **error);
    gboolean (*section)(const GwyTextHeaderContext *context,
                        const gchar *name,
                        gpointer user_data,
                        GError **error);
    gboolean (*endsection)(const GwyTextHeaderContext *context,
                           const gchar *name,
                           gpointer user_data,
                           GError **error);
    void (*end)(const GwyTextHeaderContext *context,
                gsize length,
                gpointer user_data);
    gboolean (*error)(const GwyTextHeaderContext *context,
                      GError *error,
                      gpointer user_data);
    GDestroyNotify destroy_key;
    GDestroyNotify destroy_value;
} GwyTextHeaderParser;

GQuark      gwy_text_header_error_quark(void);
GHashTable* gwy_text_header_parse      (gchar *header,
                                        const GwyTextHeaderParser *parser,
                                        gpointer user_data,
                                        GError **error);

const gchar* gwy_text_header_context_get_section(const GwyTextHeaderContext *context);
guint gwy_text_header_context_get_lineno(const GwyTextHeaderContext *context);

void gwy_file_channel_import_log_add  (GwyContainer *data,
                                       gint id,
                                       const gchar *filetype,
                                       const gchar *filename);
void gwy_file_volume_import_log_add   (GwyContainer *data,
                                       gint id,
                                       const gchar *filetype,
                                       const gchar *filename);
void gwy_file_xyz_import_log_add      (GwyContainer *data,
                                       gint id,
                                       const gchar *filetype,
                                       const gchar *filename);
void gwy_file_curve_map_import_log_add(GwyContainer *data,
                                       gint id,
                                       const gchar *filetype,
                                       const gchar *filename);

G_END_DECLS

#endif /* __GWY_MODULEUTILS_FILE_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
