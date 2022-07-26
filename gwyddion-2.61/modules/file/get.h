/*
 *  $Id: get.h 20677 2017-12-18 18:22:52Z yeti-dn $
 *  Copyright (C) 2004 David Necas (Yeti), Petr Klapetek.
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
#ifndef __GWY_FILE_GET_H__
#define __GWY_FILE_GET_H__

static inline guint64
gwy_get_guint32as64_le(const guchar **ppv)
{
    const guint32 *pv = (const guint32*)(*ppv);
    guint32 v = GUINT32_FROM_LE(*pv);
    *ppv += sizeof(guint32);
    return v;
}

static inline guint64
gwy_get_guint32as64_be(const guchar **ppv)
{
    const guint32 *pv = (const guint32*)(*ppv);
    guint32 v = GUINT32_FROM_BE(*pv);
    *ppv += sizeof(guint32);
    return v;
}

static inline void
get_CHARS(gchar *dest, const guchar **p, guint size)
{
    memcpy(dest, *p, size);
    *p += size;
}

static inline void
get_CHARS0(gchar *dest, const guchar **p, guint size)
{
    memcpy(dest, *p, size);
    *p += size;
    dest[size-1] = '\0';
}

#define get_CHARARRAY(dest, p) get_CHARS(dest, p, sizeof(dest))
#define get_CHARARRAY0(dest, p) get_CHARS0(dest, p, sizeof(dest))

static inline gboolean
get_BBOOLEAN(const guchar **p)
{
    gboolean b;

    b = (**p != 0);
    (*p)++;
    return b;
}

/* Get a non-terminated string preceded by one byte containing the length.
 * Size is the size of buffer pointer by *p.  Returns %NULL if size is too
 * small. */
static inline gchar*
get_PASCAL_STRING(const guchar **p,
                  gsize size)
{
    guint len;
    gchar *s;

    if (!size)
        return NULL;

    len = **p;
    (*p)++;
    if (size < len + 1)
        return NULL;

    s = g_new(gchar, len+1);
    memcpy(s, *p, len);
    s[len] = '\0';
    *p += len;

    return s;
}

/* Get a non-terminated string preceded by one byte containing the length.
 * Size is the maximum size of the string and the number of bytes the pointer
 * will move forward.
 * Dest must be one byte larger to hold the terminating NUL. */
static inline void
get_PASCAL_CHARS0(gchar *dest,
                  const guchar **p,
                  gsize size)
{
    guint len;

    len = MIN(**p, size);
    (*p)++;
    memcpy(dest, *p, len);
    dest[len] = '\0';
    *p += size;
}

#define get_PASCAL_CHARARRAY0(dest, p) \
    get_PASCAL_CHARS0(dest, p, sizeof(dest)-1)

#endif

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
