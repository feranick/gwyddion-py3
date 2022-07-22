/*
 *  $Id: data-browser-aux.c 24417 2021-10-24 08:36:28Z yeti-dn $
 *  Copyright (C) 2006-2021 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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

/* This file contains helpers that used to be in data-browser.c because they did not belong anywhere else and are
 * somehow related to data management.  However, they do not need internal knowledge of the data browser and do not
 * work with GwyAppDataBrowser, GwyAppDataProxy and similar structs. */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwycontainer.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libdraw/gwypixfield.h>
#include <libgwydgets/gwydgets.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "app/gwyappinternal.h"

enum {
    BITS_PER_SAMPLE = 8,
    CACHED_IDS = 24,
};

/* TODO: These probably should be public along with the corresponding sync functions. */
typedef enum {
    GWY_BRICK_ITEM_PREVIEW,
    GWY_BRICK_ITEM_GRADIENT,
    GWY_BRICK_ITEM_META,
    GWY_BRICK_ITEM_TITLE,
} GwyBrickItem;

typedef enum {
    GWY_SURFACE_ITEM_PREVIEW,
    GWY_SURFACE_ITEM_GRADIENT,
    GWY_SURFACE_ITEM_META,
    GWY_SURFACE_ITEM_TITLE,
} GwySurfaceItem;

typedef enum {
    GWY_LAWN_ITEM_PREVIEW,
    GWY_LAWN_ITEM_GRADIENT,
    GWY_LAWN_ITEM_META,
    GWY_LAWN_ITEM_TITLE,
} GwyLawnItem;

typedef GQuark (*GetKeyFunc)(gint id);

typedef struct {
    GwyAppKeyType keytype;
    GType gtype;
    GArray *ids;
} GwyAppFindIdsData;

typedef struct {
    GwyDataItem item;
    GetKeyFunc getkey;
} KeyFuncForItem;

static const struct {
    GwyAppKeyType type;
    GwyAppPage page;
} page_data_keys[] = {
    { KEY_IS_DATA,    GWY_PAGE_CHANNELS,   },
    { KEY_IS_GRAPH,   GWY_PAGE_GRAPHS,     },
    { KEY_IS_SPECTRA, GWY_PAGE_SPECTRA,    },
    { KEY_IS_BRICK,   GWY_PAGE_VOLUMES,    },
    { KEY_IS_SURFACE, GWY_PAGE_XYZS,       },
    { KEY_IS_LAWN,    GWY_PAGE_CURVE_MAPS, },
};

static const KeyFuncForItem brick_keyfuncs[] = {
    { GWY_DATA_ITEM_GRADIENT, gwy_app_get_brick_palette_key_for_id, },
    { GWY_DATA_ITEM_TITLE,    gwy_app_get_brick_title_key_for_id,   },
    { GWY_DATA_ITEM_META,     gwy_app_get_brick_meta_key_for_id,    },
    { GWY_DATA_ITEM_PREVIEW,  gwy_app_get_brick_preview_key_for_id, },
};

static const KeyFuncForItem surface_keyfuncs[] = {
    { GWY_DATA_ITEM_GRADIENT, gwy_app_get_surface_palette_key_for_id, },
    { GWY_DATA_ITEM_TITLE,    gwy_app_get_surface_title_key_for_id,   },
    { GWY_DATA_ITEM_META,     gwy_app_get_surface_meta_key_for_id,    },
    { GWY_DATA_ITEM_PREVIEW,  gwy_app_get_surface_preview_key_for_id, },
};

static const KeyFuncForItem lawn_keyfuncs[] = {
    { GWY_DATA_ITEM_GRADIENT,    gwy_app_get_lawn_palette_key_for_id,     },
    { GWY_DATA_ITEM_TITLE,       gwy_app_get_lawn_title_key_for_id,       },
    { GWY_DATA_ITEM_META,        gwy_app_get_lawn_meta_key_for_id,        },
    { GWY_DATA_ITEM_PREVIEW,     gwy_app_get_lawn_preview_key_for_id,     },
    { GWY_DATA_ITEM_REAL_SQUARE, gwy_app_get_lawn_real_square_key_for_id, },
};

/* Do not use strtol directly.  It allows weird stuff like spaces. */
static inline gint
skip_digits(const gchar *s, gint i)
{
    while (g_ascii_isdigit(s[i]))
        i++;
    return i;
}

static inline gboolean
identify_key_by_suffix(const gchar *s, GwyAppKeyType *type, ...)
{
    const gchar *suffix;
    va_list ap;

    va_start(ap, type);
    while ((suffix = va_arg(ap, const gchar*))) {
        GwyAppKeyType sufftype = va_arg(ap, gint);
        if (gwy_strequal(s, suffix)) {
            va_end(ap);
            *type = sufftype;
            return TRUE;
        }
    }
    va_end(ap);
    return FALSE;
}

/**
 * _gwy_app_analyse_data_key:
 * @strkey: String container key.
 * @type: Location to store data type to.
 * @len: Location to store the length of common prefix or %NULL. Usually this is up to the last digit of data number,
 *       however selections have also "/select" skipped, titles have "/data" skipped.  Note the remaining part of the
 *       key still includes the leading "/" (if non-empty).
 *
 * Infers expected data type from container key.
 *
 * When key is not recognized, @type is set to %KEY_IS_NONE and value of @len is unchanged.
 *
 * Returns: Data number (id), -1 when key does not correspond to any data object.  Note -1 is also returned for
 *          %KEY_IS_FILENAME type.
 **/
gint
_gwy_app_analyse_data_key(const gchar *strkey,
                          GwyAppKeyType *type,
                          guint *len)
{
    const gchar *s;
    gint i, ii;
    guint n;

    *type = KEY_IS_NONE;

    if (strkey[0] != GWY_CONTAINER_PATHSEP)
        return -1;

    /* Graph */
    if (g_str_has_prefix(strkey, GRAPH_PREFIX GWY_CONTAINER_PATHSEP_STR)) {
        s = strkey + sizeof(GRAPH_PREFIX);
        i = skip_digits(s, 0);
        if (!i || (s[i] && s[i] != GWY_CONTAINER_PATHSEP))
            return -1;
        if (!identify_key_by_suffix(s + i, type, "", KEY_IS_GRAPH,
                                    "/visible", KEY_IS_GRAPH_VISIBLE,
                                    "/view/relative-size", KEY_IS_GRAPH_VIEW_SCALE,
                                    "/view/width", KEY_IS_GRAPH_VIEW_SIZE,
                                    "/view/height", KEY_IS_GRAPH_VIEW_SIZE,
                                    NULL))
            return -1;

        if (len)
            *len = (s + i) - strkey;

        return atoi(s);
    }

    /* Spectra */
    if (g_str_has_prefix(strkey, SPECTRA_PREFIX GWY_CONTAINER_PATHSEP_STR)) {
        s = strkey + sizeof(SPECTRA_PREFIX);
        i = skip_digits(s, 0);
        if (!i || (s[i] && s[i] != GWY_CONTAINER_PATHSEP))
            return -1;
        if (!identify_key_by_suffix(s + i, type, "", KEY_IS_SPECTRA,
                                    "/visible", KEY_IS_SPECTRA_VISIBLE,
                                    NULL))
            return -1;

        if (len)
            *len = (s + i) - strkey;

        return atoi(s);
    }

    /* Brick */
    if (g_str_has_prefix(strkey, BRICK_PREFIX GWY_CONTAINER_PATHSEP_STR)) {
        s = strkey + sizeof(BRICK_PREFIX);
        i = skip_digits(s, 0);
        if (!i || (s[i] && s[i] != GWY_CONTAINER_PATHSEP))
            return -1;
        if (!identify_key_by_suffix(s + i, type, "", KEY_IS_BRICK,
                                    "/visible", KEY_IS_BRICK_VISIBLE,
                                    "/preview", KEY_IS_BRICK_PREVIEW,
                                    "/preview/palette", KEY_IS_BRICK_PREVIEW_PALETTE,
                                    "/preview/view/scale", KEY_IS_BRICK_VIEW_SCALE,
                                    "/preview/view/relative-size", KEY_IS_BRICK_VIEW_SCALE,
                                    "/title", KEY_IS_BRICK_TITLE,
                                    "/meta", KEY_IS_BRICK_META,
                                    "/log", KEY_IS_BRICK_LOG,
                                    NULL))
            return -1;

        if (len)
            *len = (s + i) - strkey;

        return atoi(s);
    }

    /* Surface */
    if (g_str_has_prefix(strkey, SURFACE_PREFIX GWY_CONTAINER_PATHSEP_STR)) {
        s = strkey + sizeof(SURFACE_PREFIX);
        i = skip_digits(s, 0);
        if (!i || (s[i] && s[i] != GWY_CONTAINER_PATHSEP))
            return -1;
        if (!identify_key_by_suffix(s + i, type, "", KEY_IS_SURFACE,
                                    "/visible", KEY_IS_SURFACE_VISIBLE,
                                    "/preview", KEY_IS_SURFACE_PREVIEW,
                                    "/preview/palette", KEY_IS_SURFACE_PREVIEW_PALETTE,
                                    "/preview/view/width", KEY_IS_SURFACE_VIEW_SIZE,
                                    "/preview/view/height", KEY_IS_SURFACE_VIEW_SIZE,
                                    "/preview/view/relative-size", KEY_IS_SURFACE_VIEW_SCALE,
                                    "/title", KEY_IS_SURFACE_TITLE,
                                    "/meta", KEY_IS_SURFACE_META,
                                    "/log", KEY_IS_SURFACE_LOG,
                                    NULL))

        if (len)
            *len = (s + i) - strkey;

        return atoi(s);
    }

    /* Lawn */
    if (g_str_has_prefix(strkey, LAWN_PREFIX GWY_CONTAINER_PATHSEP_STR)) {
        s = strkey + sizeof(LAWN_PREFIX);
        i = skip_digits(s, 0);
        if (!i || (s[i] && s[i] != GWY_CONTAINER_PATHSEP))
            return -1;
        if (!identify_key_by_suffix(s + i, type, "", KEY_IS_LAWN,
                                    "/visible", KEY_IS_LAWN_VISIBLE,
                                    "/preview", KEY_IS_LAWN_PREVIEW,
                                    "/preview/palette", KEY_IS_LAWN_PREVIEW_PALETTE,
                                    "/preview/realsquare", KEY_IS_LAWN_REAL_SQUARE,
                                    "/preview/view/scale", KEY_IS_LAWN_VIEW_SCALE,
                                    "/preview/view/relative-size", KEY_IS_LAWN_VIEW_SCALE,
                                    "/title", KEY_IS_LAWN_TITLE,
                                    "/meta", KEY_IS_LAWN_META,
                                    "/log", KEY_IS_LAWN_LOG,
                                    NULL))
            return -1;

        if (len)
            *len = (s + i) - strkey;

        return atoi(s);
    }

    /* Non-id */
    if (gwy_strequal(strkey, "/filename")) {
        if (len)
            *len = 0;
        *type = KEY_IS_FILENAME;
        return -1;
    }
    if (gwy_strequal(strkey, "/0/graph/lastid")) {
        if (len)
            *len = 0;
        *type = KEY_IS_GRAPH_LASTID;
        return -1;
    }

    /* Other data */
    s = strkey + 1;
    i = skip_digits(s, 0);
    if (!i || s[i] != GWY_CONTAINER_PATHSEP)
        return -1;

    n = i + 1;
    i = atoi(s);
    s = strkey + n + 1;

    if (identify_key_by_suffix(s, type,
                               "data", KEY_IS_DATA,
                               "mask", KEY_IS_MASK,
                               "show", KEY_IS_SHOW,
                               "data/visible", KEY_IS_DATA_VISIBLE,
                               "data/log", KEY_IS_CHANNEL_LOG,
                               "base/palette", KEY_IS_PALETTE,
                               "base/range-type", KEY_IS_RANGE_TYPE,
                               "meta", KEY_IS_CHANNEL_META,
                               "data/realsquare", KEY_IS_REAL_SQUARE,
                               "sps-id", KEY_IS_SPS_REF,
                               NULL)) {
        /* Nothing more to do. */
    }
    else if (g_str_has_prefix(s, "select/") && !strchr(s + sizeof("select/")-1, '/')) {
        *type = KEY_IS_SELECT;
        n += strlen("select/");
    }
    else if (gwy_strequal(s, "data/title") || gwy_strequal(s, "data/untitled")) {
        *type = KEY_IS_TITLE;
        n += strlen("data/");
    }
    else if (gwy_strequal(s, "base/min") || gwy_strequal(s, "base/max")) {
        *type = KEY_IS_RANGE;
        n += strlen("base/");
    }
    else if (gwy_strequal(s, "data/view/scale") || gwy_strequal(s, "data/view/relative-size")) {
        *type = KEY_IS_DATA_VIEW_SCALE;
        n += strlen("data/");
    }
    else if (gwy_stramong(s, "mask/red", "mask/blue", "mask/green", "mask/alpha", NULL)) {
        *type = KEY_IS_MASK_COLOR;
        n += strlen("mask/");
    }
    else if (gwy_stramong(s,
                          "data/cal_xunc", "data/cal_yunc", "data/cal_zunc",
                          "data/cal_xerr", "data/cal_yerr", "data/cal_zerr", NULL)) {
        *type = KEY_IS_CALDATA;
        n += strlen("data/");
    }
    else if (g_str_has_prefix(s, "3d/")) {
        ii = strlen("3d/");
        if (gwy_stramong(s + ii, "x", "y", "min", "max", NULL)) {
            *type = KEY_IS_3D_LABEL;
            n += strlen("3d/");
        }
        else if (!identify_key_by_suffix(s + ii, type,
                                         "setup", KEY_IS_3D_SETUP,
                                         "palette", KEY_IS_3D_PALETTE,
                                         "material", KEY_IS_3D_MATERIAL,
                                         "view/relative-size", KEY_IS_3D_VIEW_SCALE,
                                         "view/width", KEY_IS_3D_VIEW_SIZE,
                                         "view/height", KEY_IS_3D_VIEW_SIZE,
                                         NULL))
            return -1;
    }
    else
        return -1;

    if (len && i > -1)
        *len = n;

    return i;
}

void
_gwy_app_data_merge_gather(gpointer key,
                           G_GNUC_UNUSED gpointer value,
                           gpointer user_data)
{
    GQuark quark = GPOINTER_TO_UINT(key);
    GList **ids = (GList**)user_data;
    GwyAppKeyType type;
    gint id, pageno;
    guint i;

    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    for (i = 0; i < G_N_ELEMENTS(page_data_keys); i++) {
        if (type == page_data_keys[i].type) {
            pageno = page_data_keys[i].page;
            gwy_debug("adding %d to page %d", id, pageno);
            ids[pageno] = g_list_prepend(ids[pageno], GINT_TO_POINTER(id));
            return;
        }
    }
}

void
_gwy_app_data_merge_copy_1(gpointer key,
                           gpointer value,
                           gpointer user_data)
{
    GQuark quark = GPOINTER_TO_UINT(key);
    GValue *gvalue = (GValue*)value;
    GHashTable **map = (GHashTable**)user_data;
    GwyAppKeyType type;
    gpointer idp, id2p;
    gint id, pageno;
    guint i;

    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &type, NULL);
    idp = GINT_TO_POINTER(id);
    for (i = 0; i < G_N_ELEMENTS(page_data_keys); i++) {
        if (type == page_data_keys[i].type) {
            pageno = page_data_keys[i].page;
            if (!g_hash_table_lookup_extended(map[pageno], idp, NULL, &id2p))
                goto fail;
            quark = _gwy_app_get_page_data_key_for_id(GPOINTER_TO_INT(id2p), pageno);
            gwy_container_set_object((GwyContainer*)map[GWY_NPAGES], quark, g_value_get_object(gvalue));
            return;
        }
    }
    /* Handle these in gwy_app_data_merge_copy_2() */
    return;

fail:
    g_warning("%s does not map to any new location", g_quark_to_string(quark));
}

void
_gwy_app_data_merge_copy_2(gpointer key,
                           gpointer value,
                           gpointer user_data)
{
    GQuark quark = GPOINTER_TO_UINT(key);
    GValue *gvalue = (GValue*)value;
    GHashTable **map = (GHashTable**)user_data;
    GwyContainer *dest;
    const gchar *strkey;
    GwyAppKeyType type;
    gpointer idp, id2p;
    gint id, id2, pageno;
    guint len, i;
    gboolean visibility = FALSE;
    gchar buf[80];

    strkey = g_quark_to_string(quark);
    if (gwy_strequal(strkey, "/0/graph/lastid"))
        return;

    /* Handle visibility by stripping "/visible" from the key before analysis */
    if (g_str_has_suffix(strkey, "/visible")) {
        gchar *vstrkey;

        vstrkey = g_strndup(strkey, strlen(strkey) - strlen("/visible"));
        id = _gwy_app_analyse_data_key(vstrkey, &type, &len);
        g_free(vstrkey);
        visibility = TRUE;
    }
    else
        id = _gwy_app_analyse_data_key(strkey, &type, &len);

    if (type == KEY_IS_FILENAME)
        return;
    if (id < 0)
        goto fail;

    idp = GINT_TO_POINTER(id);
    dest = (GwyContainer*)map[GWY_NPAGES];

    /* Visibilty */
    for (i = 0; i < G_N_ELEMENTS(page_data_keys); i++) {
        if (type == page_data_keys[i].type) {
            pageno = page_data_keys[i].page;
            if (visibility) {
                if (!g_hash_table_lookup_extended(map[pageno], idp, NULL, &id2p))
                    goto fail;
                quark = _gwy_app_get_page_data_key_for_id(GPOINTER_TO_INT(id2p), pageno);
                g_snprintf(buf, sizeof(buf), "%s/visible", g_quark_to_string(quark));
                if (g_value_get_boolean(gvalue))
                    gwy_container_set_boolean_by_name(dest, buf, TRUE);
            }
            return;
        }
    }

    switch (type) {
        case KEY_IS_MASK:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        quark = gwy_app_get_mask_key_for_id(id2);
        gwy_container_set_object(dest, quark, g_value_get_object(gvalue));
        break;

        case KEY_IS_SHOW:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        quark = gwy_app_get_show_key_for_id(id2);
        gwy_container_set_object(dest, quark, g_value_get_object(gvalue));
        break;

        case KEY_IS_SPS_REF:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        id = g_value_get_int(gvalue);
        idp = GINT_TO_POINTER(id);
        /* Ignore references to nonexistent sps ids silently */
        if (g_hash_table_lookup_extended(map[GWY_PAGE_SPECTRA], idp, NULL, &id2p)) {
            g_snprintf(buf, sizeof(buf), "/%d/data/sps-is", id2);
            id2 = GPOINTER_TO_INT(id2p);
            gwy_container_set_int32_by_name(dest, buf, id2);
        }
        break;

        case KEY_IS_TITLE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_string(dest, gwy_app_get_data_title_key_for_id(id2), g_value_dup_string(gvalue));
        break;

        case KEY_IS_PALETTE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_string(dest, gwy_app_get_data_palette_key_for_id(id2), g_value_dup_string(gvalue));
        break;

        case KEY_IS_MASK_COLOR:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/mask%s", id2, strkey + len);
        gwy_container_set_double_by_name(dest, buf, g_value_get_double(gvalue));
        break;

        case KEY_IS_SELECT:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/data/select%s", id2, strkey + len);
        gwy_container_set_object_by_name(dest, buf, g_value_get_object(gvalue));
        break;

        case KEY_IS_RANGE_TYPE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_enum(dest, gwy_app_get_data_range_type_key_for_id(id2), g_value_get_int(gvalue));
        break;

        case KEY_IS_RANGE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/base%s", id2, strkey + len);
        gwy_container_set_double_by_name(dest, buf, g_value_get_double(gvalue));
        break;

        case KEY_IS_REAL_SQUARE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/data/realsquare", id2);
        gwy_container_set_boolean_by_name(dest, buf, g_value_get_boolean(gvalue));
        break;

        case KEY_IS_CHANNEL_META:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_object(dest, gwy_app_get_data_meta_key_for_id(id2), g_value_get_object(gvalue));
        break;

        case KEY_IS_CHANNEL_LOG:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/data/log", id2);
        gwy_container_set_object_by_name(dest, buf, g_value_get_object(gvalue));
        break;

        case KEY_IS_DATA_VIEW_SCALE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/data%s", id2, strkey + len);
        gwy_container_set_double_by_name(dest, buf, g_value_get_double(gvalue));
        break;

        case KEY_IS_3D_SETUP:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/3d/setup", id2);
        gwy_container_set_object_by_name(dest, buf, g_value_get_object(gvalue));
        break;

        case KEY_IS_3D_LABEL:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/3d%s", id2, strkey + len);
        gwy_container_set_object_by_name(dest, buf, g_value_get_object(gvalue));
        break;

        case KEY_IS_3D_PALETTE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/3d/palette", id2);
        gwy_container_set_string_by_name(dest, buf, g_value_dup_string(gvalue));
        break;

        case KEY_IS_3D_MATERIAL:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/3d/material", id2);
        gwy_container_set_string_by_name(dest, buf, g_value_dup_string(gvalue));
        break;

        case KEY_IS_3D_VIEW_SCALE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/%s", id2, strkey + len);
        gwy_container_set_double_by_name(dest, buf, g_value_get_double(gvalue));
        break;

        case KEY_IS_3D_VIEW_SIZE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CHANNELS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), "/%d/%s", id2, strkey + len);
        gwy_container_set_int32_by_name(dest, buf, g_value_get_int(gvalue));
        break;

        case KEY_IS_GRAPH_VIEW_SCALE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_GRAPHS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), GRAPH_PREFIX "/%d%s", id2, strkey + len);
        gwy_container_set_double_by_name(dest, buf, g_value_get_double(gvalue));
        break;

        case KEY_IS_GRAPH_VIEW_SIZE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_GRAPHS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), GRAPH_PREFIX "/%d%s", id2, strkey + len);
        gwy_container_set_int32_by_name(dest, buf, g_value_get_int(gvalue));
        break;

        case KEY_IS_BRICK_TITLE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_VOLUMES], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_string(dest, gwy_app_get_brick_title_key_for_id(id2), g_value_dup_string(gvalue));
        break;

        case KEY_IS_BRICK_PREVIEW:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_VOLUMES], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_object(dest, gwy_app_get_brick_preview_key_for_id(id2), g_value_get_object(gvalue));
        break;

        case KEY_IS_BRICK_PREVIEW_PALETTE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_VOLUMES], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_string(dest, gwy_app_get_brick_palette_key_for_id(id2), g_value_dup_string(gvalue));
        break;

        case KEY_IS_BRICK_META:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_VOLUMES], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_object(dest, gwy_app_get_brick_meta_key_for_id(id2), g_value_get_object(gvalue));
        break;

        case KEY_IS_BRICK_LOG:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_VOLUMES], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), BRICK_PREFIX "/%d/log", id2);
        gwy_container_set_object_by_name(dest, buf, g_value_get_object(gvalue));
        break;

        case KEY_IS_BRICK_VIEW_SCALE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_VOLUMES], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), BRICK_PREFIX "/%d%s", id2, strkey + len);
        gwy_container_set_double_by_name(dest, buf, g_value_get_double(gvalue));
        break;

        case KEY_IS_SURFACE_TITLE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_XYZS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_string(dest, gwy_app_get_surface_title_key_for_id(id2), g_value_dup_string(gvalue));
        break;

        case KEY_IS_SURFACE_PREVIEW:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_XYZS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_object(dest, gwy_app_get_surface_preview_key_for_id(id2), g_value_get_object(gvalue));
        break;

        case KEY_IS_SURFACE_PREVIEW_PALETTE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_XYZS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_string(dest, gwy_app_get_surface_palette_key_for_id(id2), g_value_dup_string(gvalue));
        break;

        case KEY_IS_SURFACE_META:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_XYZS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_object(dest, gwy_app_get_surface_meta_key_for_id(id2), g_value_get_object(gvalue));
        break;

        case KEY_IS_SURFACE_LOG:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_XYZS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), SURFACE_PREFIX "/%d/log", id2);
        gwy_container_set_object_by_name(dest, buf, g_value_get_object(gvalue));
        break;

        case KEY_IS_SURFACE_VIEW_SCALE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_XYZS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), SURFACE_PREFIX "/%d%s", id2, strkey + len);
        gwy_container_set_double_by_name(dest, buf, g_value_get_double(gvalue));
        break;

        case KEY_IS_SURFACE_VIEW_SIZE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_XYZS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), SURFACE_PREFIX "/%d%s", id2, strkey + len);
        gwy_container_set_int32_by_name(dest, buf, g_value_get_int(gvalue));
        break;

        case KEY_IS_LAWN_TITLE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CURVE_MAPS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_string(dest, gwy_app_get_lawn_title_key_for_id(id2), g_value_dup_string(gvalue));
        break;

        case KEY_IS_LAWN_PREVIEW:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CURVE_MAPS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_object(dest, gwy_app_get_lawn_preview_key_for_id(id2), g_value_get_object(gvalue));
        break;

        case KEY_IS_LAWN_PREVIEW_PALETTE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CURVE_MAPS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_string(dest, gwy_app_get_lawn_palette_key_for_id(id2), g_value_dup_string(gvalue));
        break;

        case KEY_IS_LAWN_REAL_SQUARE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CURVE_MAPS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), LAWN_PREFIX "/%d/preview/realsquare", id2);
        gwy_container_set_boolean_by_name(dest, buf, g_value_get_boolean(gvalue));
        break;

        case KEY_IS_LAWN_META:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CURVE_MAPS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        gwy_container_set_object(dest, gwy_app_get_lawn_meta_key_for_id(id2), g_value_get_object(gvalue));
        break;

        case KEY_IS_LAWN_LOG:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CURVE_MAPS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), LAWN_PREFIX "/%d/log", id2);
        gwy_container_set_object_by_name(dest, buf, g_value_get_object(gvalue));
        break;

        case KEY_IS_LAWN_VIEW_SCALE:
        if (!g_hash_table_lookup_extended(map[GWY_PAGE_CURVE_MAPS], idp, NULL, &id2p))
            goto fail;
        id2 = GPOINTER_TO_INT(id2p);
        g_snprintf(buf, sizeof(buf), LAWN_PREFIX "/%d%s", id2, strkey + len);
        gwy_container_set_double_by_name(dest, buf, g_value_get_double(gvalue));
        break;

        default:
        goto fail;
        break;
    }
    return;

fail:
    g_warning("%s (%u) does not map to any new location, cannot map it generically because the current key "
              "organization is a mess",
              strkey, type);
}

static GQuark
gwy_app_get_any_key_for_id(gint id,
                           const gchar *format,
                           guint nquarks,
                           GQuark *quarks)
{
    gchar key[48];
    GQuark q;

    g_return_val_if_fail(id >= 0, 0);
    if (id < nquarks && quarks[id])
        return quarks[id];

    g_snprintf(key, sizeof(key), format, id);
    q = g_quark_from_string(key);
    if (id < nquarks)
        quarks[id] = q;

    return q;
}

/**
 * gwy_app_get_data_key_for_id:
 * @id: Numerical id of a channel in file (container).
 *
 * Calculates data field quark identifier from its id.
 *
 * Returns: The quark key identifying channel data field with number @id.
 **/
GQuark
gwy_app_get_data_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/data", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_mask_key_for_id:
 * @id: Numerical id of a channel in file (container).
 *
 * Calculates mask field quark identifier from its id.
 *
 * Returns: The quark key identifying mask data field with number @id.
 **/
GQuark
gwy_app_get_mask_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/mask", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_show_key_for_id:
 * @id: Numerical id of a channel in file (container).
 *
 * Calculates presentation field quark identifier from its id.
 *
 * Returns: The quark key identifying presentation data field with number @id.
 **/
GQuark
gwy_app_get_show_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/show", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_graph_key_for_id:
 * @id: Numerical id of a graph in file (container).
 *
 * Calculates graph model quark identifier from its id.
 *
 * Returns: The quark key identifying graph model with number @id.
 *
 * Since: 2.7
 **/
GQuark
gwy_app_get_graph_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, GRAPH_PREFIX "/%d", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_spectra_key_for_id:
 * @id: Numerical id of a spectra set in file (container).
 *
 * Calculates spectra quark identifier from its id.
 *
 * Returns: The quark key identifying spectra with number @id.
 *
 * Since: 2.7
 **/
GQuark
gwy_app_get_spectra_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, SPECTRA_PREFIX "/%d", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_brick_key_for_id:
 * @id: Numerical id of a data brick in file (container).
 *
 * Calculates data brick quark identifier from its id.
 *
 * Returns: The quark key identifying data brick with number @id.
 *
 * Since: 2.32
 **/
GQuark
gwy_app_get_brick_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, BRICK_PREFIX "/%d", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_surface_key_for_id:
 * @id: Numerical id of an XYZ surface in file (container).
 *
 * Calculates XYZ surface quark identifier from its id.
 *
 * Returns: The quark key identifying XYZ surface with number @id.
 *
 * Since: 2.45
 **/
GQuark
gwy_app_get_surface_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, SURFACE_PREFIX "/%d", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_lawn_key_for_id:
 * @id: Numerical id of a #GwyLawn curve map in file (container).
 *
 * Calculates #GwyLawn curve map quark identifier from its id.
 *
 * Returns: The quark key identifying #GwyLawn curve map with number @id.
 *
 * Since: 2.60
 **/
GQuark
gwy_app_get_lawn_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, LAWN_PREFIX "/%d", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_data_title_key_for_id:
 * @id: Numerical id of a image in file (container).
 *
 * Calculates data field title quark identifier from its id.
 *
 * Returns: The quark key identifying string title of image with number @id.
 *
 * Since: 2.43
 **/
GQuark
gwy_app_get_data_title_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/data/title", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_data_base_key_for_id:
 * @id: Numerical id of a image in file (container).
 *
 * Calculates data field base visualisation quark identifier from its id.
 *
 * This is the common prefix for range and palette key.  It is not useful alone because it is only prefix.
 *
 * Returns: The quark key identifying base string prefix of image with number @id.
 *
 * Since: 2.59
 **/
GQuark
gwy_app_get_data_base_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/base", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_data_range_type_key_for_id:
 * @id: Numerical id of a image in file (container).
 *
 * Calculates data field range type quark identifier from its id.
 *
 * Returns: The quark key identifying #GwyLayerBasicRangeType false colour mapping type of image with number @id.
 *
 * Since: 2.43
 **/
GQuark
gwy_app_get_data_range_type_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/base/range-type", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_data_range_min_key_for_id:
 * @id: Numerical id of a image in file (container).
 *
 * Calculates data field fixed range minimum quark identifier from its id.
 *
 * Returns: The quark key identifying floating fixed false colour range minimum of image with number @id.
 *
 * Since: 2.43
 **/
GQuark
gwy_app_get_data_range_min_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/base/min", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_data_range_max_key_for_id:
 * @id: Numerical id of a image in file (container).
 *
 * Calculates data field fixed range maximum quark identifier from its id.
 *
 * Returns: The quark key identifying floating fixed false colour range maximum of image with number @id.
 *
 * Since: 2.43
 **/
GQuark
gwy_app_get_data_range_max_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/base/max", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_data_meta_key_for_id:
 * @id: Numerical id of a image in file (container).
 *
 * Calculates data field metadata quark identifier from its id.
 *
 * Returns: The quark key identifying metadata container of image with number @id.
 *
 * Since: 2.43
 **/
GQuark
gwy_app_get_data_meta_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/meta", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_data_palette_key_for_id:
 * @id: Numerical id of a image in file (container).
 *
 * Calculates data field palette quark identifier from its id.
 *
 * Returns: The quark key identifying string name palette of image with number @id.
 *
 * Since: 2.43
 **/
GQuark
gwy_app_get_data_palette_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/base/palette", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_data_real_square_key_for_id:
 * @id: Numerical id of a image in file (container).
 *
 * Calculates data field real-square quark identifier from its id.
 *
 * Returns: The quark key identifying boolean controlling real-square setting of image with number @id.
 *
 * Since: 2.60
 **/
GQuark
gwy_app_get_data_real_square_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, "/%d/data/realsquare", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_brick_title_key_for_id:
 * @id: Numerical id of a data brick in file (container).
 *
 * Calculates data brick title quark identifier from its id.
 *
 * Returns: The quark key identifying string title of data brick with number @id.
 *
 * Since: 2.43
 **/
GQuark
gwy_app_get_brick_title_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, BRICK_PREFIX "/%d/title", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_brick_preview_key_for_id:
 * @id: Numerical id of a data brick in file (container).
 *
 * Calculates data brick preview quark identifier from its id.
 *
 * Returns: The quark key identifying preview data field of data brick with number @id.
 *
 * Since: 2.43
 **/
GQuark
gwy_app_get_brick_preview_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, BRICK_PREFIX "/%d/preview", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_brick_palette_key_for_id:
 * @id: Numerical id of a data brick in file (container).
 *
 * Calculates data brick palette quark identifier from its id.
 *
 * Returns: The quark key identifying string name palette of data brick with number @id.
 *
 * Since: 2.43
 **/
GQuark
gwy_app_get_brick_palette_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, BRICK_PREFIX "/%d/preview/palette", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_brick_meta_key_for_id:
 * @id: Numerical id of a data brick in file (container).
 *
 * Calculates data brick title quark identifier from its id.
 *
 * Returns: The quark key identifying metadata container of data brick with number @id.
 *
 * Since: 2.43
 **/
GQuark
gwy_app_get_brick_meta_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, BRICK_PREFIX "/%d/meta", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_surface_title_key_for_id:
 * @id: Numerical id of a data surface in file (container).
 *
 * Calculates data surface title quark identifier from its id.
 *
 * Returns: The quark key identifying string title of data surface with number @id.
 *
 * Since: 2.45
 **/
GQuark
gwy_app_get_surface_title_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, SURFACE_PREFIX "/%d/title", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_surface_palette_key_for_id:
 * @id: Numerical id of an XYZ surface in file (container).
 *
 * Calculates XYZ surface palette quark identifier from its id.
 *
 * Returns: The quark key identifying string name palette of XYZ surface with number @id.
 *
 * Since: 2.45
 **/
GQuark
gwy_app_get_surface_palette_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, SURFACE_PREFIX "/%d/preview/palette", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_surface_meta_key_for_id:
 * @id: Numerical id of an XYZ surface in file (container).
 *
 * Calculates XYZ surface title quark identifier from its id.
 *
 * Returns: The quark key identifying metadata container of XYZ surface with number @id.
 *
 * Since: 2.45
 **/
GQuark
gwy_app_get_surface_meta_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, SURFACE_PREFIX "/%d/meta", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_surface_preview_key_for_id:
 * @id: Numerical id of an XYZ surface in file (container).
 *
 * Calculates XYZ surface preview quark identifier from its id.
 *
 * Returns: The quark key identifying preview data field of XYZ surface with number @id.
 *
 * Since: 2.46
 **/
GQuark
gwy_app_get_surface_preview_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, SURFACE_PREFIX "/%d/preview", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_lawn_title_key_for_id:
 * @id: Numerical id of a #GwyLawn curve map in file (container).
 *
 * Calculates #GwyLawn curve map title quark identifier from its id.
 *
 * Returns: The quark key identifying string title of #GwyLawn curve map with number @id.
 *
 * Since: 2.60
 **/
GQuark
gwy_app_get_lawn_title_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, LAWN_PREFIX "/%d/title", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_lawn_palette_key_for_id:
 * @id: Numerical id of a #GwyLawn curve map in file (container).
 *
 * Calculates #GwyLawn curve map palette quark identifier from its id.
 *
 * Returns: The quark key identifying string name palette of #GwyLawn curve map with number @id.
 *
 * Since: 2.60
 **/
GQuark
gwy_app_get_lawn_palette_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, LAWN_PREFIX "/%d/preview/palette", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_lawn_meta_key_for_id:
 * @id: Numerical id of a #GwyLawn curve map in file (container).
 *
 * Calculates #GwyLawn curve map title quark identifier from its id.
 *
 * Returns: The quark key identifying metadata container of #GwyLawn curve map with number @id.
 *
 * Since: 2.60
 **/
GQuark
gwy_app_get_lawn_meta_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, LAWN_PREFIX "/%d/meta", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_lawn_preview_key_for_id:
 * @id: Numerical id of a #GwyLawn curve map in file (container).
 *
 * Calculates #GwyLawn curve map preview quark identifier from its id.
 *
 * Returns: The quark key identifying preview data field of #GwyLawn curve map with number @id.
 *
 * Since: 2.60
 **/
GQuark
gwy_app_get_lawn_preview_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, LAWN_PREFIX "/%d/preview", G_N_ELEMENTS(quarks), quarks);
}

/**
 * gwy_app_get_lawn_real_square_key_for_id:
 * @id: Numerical id of a #GwyLawn curve map in file (container).
 *
 * Calculates #GwyLawn curve map real-square quark identifier from its id.
 *
 * Returns: The quark key identifying boolean controlling real-square setting of #GwyLawn curve map with number @id.
 *
 * Since: 2.60
 **/
GQuark
gwy_app_get_lawn_real_square_key_for_id(gint id)
{
    static GQuark quarks[CACHED_IDS] = { 0, };
    return gwy_app_get_any_key_for_id(id, LAWN_PREFIX "/%d/preview/realsquare", G_N_ELEMENTS(quarks), quarks);
}

GQuark
_gwy_app_get_page_data_key_for_id(gint id, GwyAppPage pageno)
{
    static GetKeyFunc getkey[] = {
        gwy_app_get_data_key_for_id,
        gwy_app_get_graph_key_for_id,
        gwy_app_get_spectra_key_for_id,
        gwy_app_get_brick_key_for_id,
        gwy_app_get_surface_key_for_id,
        gwy_app_get_lawn_key_for_id,
    };
    g_return_val_if_fail(pageno < G_N_ELEMENTS(getkey), 0);
    return getkey[pageno](id);
}

static const gchar*
find_position_for_number(const gchar *s)
{
    guint len = strlen(s);
    const gchar *p = s + len;

    while (p > s && g_ascii_isdigit(*(p-1)))
        p--;
    if (p == s)
        return s;
    if (g_ascii_isspace(*(p-1)))
        return p-1;
    return s + len;
}

/**
 * gwy_app_set_data_field_title:
 * @data: A data container.
 * @id: The data channel id.
 * @name: The title to set.  It can be %NULL to use somthing like "Untitled". The id will be appended to it or
 *        (replaced in it if it already ends with digits).
 *
 * Sets channel title.
 **/
void
gwy_app_set_data_field_title(GwyContainer *data,
                             gint id,
                             const gchar *name)
{
    const gchar *p;
    gchar *title;

    if (!name) {
        name = _("Untitled");
        p = name + strlen(name);
    }
    else
        p = find_position_for_number(name);
    title = g_strdup_printf("%.*s %d", (gint)(p - name), name, id);
    gwy_container_set_string(data, gwy_app_get_data_title_key_for_id(id), title);
}

gchar*
_gwy_app_figure_out_channel_title(GwyContainer *data, gint channel)
{
    const guchar *title = NULL;
    gchar buf[32];

    g_return_val_if_fail(GWY_IS_CONTAINER(data), NULL);
    g_return_val_if_fail(channel >= 0, NULL);

    gwy_container_gis_string(data, gwy_app_get_data_title_key_for_id(channel), &title);
    if (!title) {
        g_snprintf(buf, sizeof(buf), "/%d/data/untitled", channel);
        gwy_container_gis_string_by_name(data, buf, &title);
    }
    /* Support 1.x titles */
    if (!title)
        gwy_container_gis_string_by_name(data, "/filename/title", &title);

    if (title)
        return g_strdup(title);

    return g_strdup_printf(_("Unknown channel %d"), channel + 1);
}

/**
 * gwy_app_get_data_field_title:
 * @data: A data container.
 * @id: Data channel id.
 *
 * Gets a data channel title.
 *
 * This function should return a reasonable title for untitled channels, channels with old titles, channels with and
 * without a file, etc.
 *
 * Returns: The channel title as a newly allocated string.
 **/
gchar*
gwy_app_get_data_field_title(GwyContainer *data,
                             gint id)
{
    return _gwy_app_figure_out_channel_title(data, id);
}

/**
 * gwy_app_set_brick_title:
 * @data: A data container.
 * @id: The volume data brick id.
 * @name: The title to set.  It can be %NULL to use somthing like "Untitled". The id will be appended to it or
 *        (replaced in it if it already ends with digits).
 *
 * Sets volume data title.
 *
 * Since: 2.32
 **/
void
gwy_app_set_brick_title(GwyContainer *data,
                        gint id,
                        const gchar *name)
{
    gchar *title;
    const gchar *p;

    if (!name) {
        name = _("Untitled");
        p = name + strlen(name);
    }
    else
        p = find_position_for_number(name);
    title = g_strdup_printf("%.*s %d", (gint)(p - name), name, id);
    gwy_container_set_string(data, gwy_app_get_brick_title_key_for_id(id), title);
}

/**
 * gwy_app_get_brick_title:
 * @data: A data container.
 * @id: Volume data brick id.
 *
 * Gets a volume data brick title.
 *
 * Returns: The brick title as a newly allocated string.
 *
 * Since: 2.32
 **/
gchar*
gwy_app_get_brick_title(GwyContainer *data,
                        gint id)
{
    const guchar *title = NULL;

    g_return_val_if_fail(GWY_IS_CONTAINER(data), NULL);
    g_return_val_if_fail(id >= 0, NULL);
    gwy_container_gis_string(data, gwy_app_get_brick_title_key_for_id(id), &title);

    if (title)
        return g_strdup(title);

    return g_strdup_printf(_("Unknown volume %d"), id + 1);
}

/**
 * gwy_app_set_surface_title:
 * @data: A data container.
 * @id: The XYZ surface data channel id.
 * @name: The title to set.  It can be %NULL to use somthing like "Untitled". The id will be appended to it or
 *        (replaced in it if it already ends with digits).
 *
 * Sets XYZ surface data title.
 *
 * Since: 2.45
 **/
void
gwy_app_set_surface_title(GwyContainer *data,
                          gint id,
                          const gchar *name)
{
    const gchar *p;
    gchar *title;

    if (!name) {
        name = _("Untitled");
        p = name + strlen(name);
    }
    else
        p = find_position_for_number(name);
    title = g_strdup_printf("%.*s %d", (gint)(p - name), name, id);
    gwy_container_set_string(data, gwy_app_get_surface_title_key_for_id(id), title);
}

/**
 * gwy_app_get_surface_title:
 * @data: A data container.
 * @id: XYZ data surface id.
 *
 * Gets an XYZ surface data title.
 *
 * Returns: The surface title as a newly allocated string.
 *
 * Since: 2.45
 **/
gchar*
gwy_app_get_surface_title(GwyContainer *data,
                          gint id)
{
    const guchar *title = NULL;

    g_return_val_if_fail(GWY_IS_CONTAINER(data), NULL);
    g_return_val_if_fail(id >= 0, NULL);
    gwy_container_gis_string(data, gwy_app_get_surface_title_key_for_id(id), &title);

    if (title)
        return g_strdup(title);

    return g_strdup_printf(_("Unknown XYZ %d"), id + 1);
}

/**
 * gwy_app_set_lawn_title:
 * @data: A data container.
 * @id: The #GwyLawn curve map data channel id.
 * @name: The title to set.  It can be %NULL to use somthing like "Untitled". The id will be appended to it or
 *        (replaced in it if it already ends with digits).
 *
 * Sets #GwyLawn curve map data title.
 *
 * Since: 2.60
 **/
void
gwy_app_set_lawn_title(GwyContainer *data,
                       gint id,
                       const gchar *name)
{
    const gchar *p;
    gchar *title;

    if (!name) {
        name = _("Untitled");
        p = name + strlen(name);
    }
    else
        p = find_position_for_number(name);
    title = g_strdup_printf("%.*s %d", (gint)(p - name), name, id);
    gwy_container_set_string(data, gwy_app_get_lawn_title_key_for_id(id), title);
}

/**
 * gwy_app_get_lawn_title:
 * @data: A data container.
 * @id: XYZ data lawn id.
 *
 * Gets a #GwyLawn curve map data title.
 *
 * Returns: The lawn title as a newly allocated string.
 *
 * Since: 2.60
 **/
gchar*
gwy_app_get_lawn_title(GwyContainer *data,
                       gint id)
{
    const guchar *title = NULL;

    g_return_val_if_fail(GWY_IS_CONTAINER(data), NULL);
    g_return_val_if_fail(id >= 0, NULL);
    gwy_container_gis_string(data, gwy_app_get_lawn_title_key_for_id(id), &title);

    if (title)
        return g_strdup(title);

    return g_strdup_printf(_("Unknown curve map %d"), id + 1);
}

static void
gather_ids_for_unmanaged(gpointer key,
                         gpointer value,
                         gpointer user_data)
{
    GQuark quark = GPOINTER_TO_UINT(key);
    GValue *gvalue = (GValue*)value;
    GwyAppFindIdsData *fidata = (GwyAppFindIdsData*)user_data;
    GwyAppKeyType keytype;
    GObject *object;
    gint id;

    if (!G_VALUE_HOLDS_OBJECT(value))
        return;

    object = g_value_get_object(gvalue);
    if (!g_type_is_a(G_OBJECT_TYPE(object), fidata->gtype))
        return;

    id = _gwy_app_analyse_data_key(g_quark_to_string(quark), &keytype, NULL);
    if (keytype != fidata->keytype)
        return;

    g_array_append_val(fidata->ids, id);
}

gint*
_gwy_app_find_ids_unmanaged(GwyContainer *data,
                            GwyAppKeyType keytype, GType gtype)
{
    GwyAppFindIdsData fidata;
    gint none = -1;

    fidata.keytype = keytype;
    fidata.gtype = gtype;
    fidata.ids = g_array_new(FALSE, FALSE, sizeof(gint));
    gwy_container_foreach(data, NULL, &gather_ids_for_unmanaged, &fidata);
    /* The array can only contain positive values. */
    gwy_guint_sort(fidata.ids->len, (guint*)fidata.ids->data);
    g_array_append_val(fidata.ids, none);

    return (gint*)g_array_free(fidata.ids, FALSE);
}

static void
sync_one_generic_item(GwyContainer *source, GwyContainer *dest,
                      gint from_id, gint to_id,
                      GwyDataItem what, gboolean delete_too,
                      const KeyFuncForItem *keyfuncs, guint nfuncs)
{
    GQuark qfrom, qto;
    GObject *obj;
    GType type;
    guint i;

    for (i = 0; i < nfuncs; i++) {
        if (keyfuncs[i].item == what) {
            qfrom = keyfuncs[i].getkey(from_id);
            qto = keyfuncs[i].getkey(to_id);
            type = gwy_container_value_type(source, qfrom);
            if (!type) {
                if (delete_too)
                    gwy_container_remove(dest, qto);
            }
            else if (type == G_TYPE_BOOLEAN)
                gwy_container_set_boolean(dest, qto, gwy_container_get_boolean(source, qfrom));
            else if (type == G_TYPE_STRING)
                gwy_container_set_const_string(dest, qto, gwy_container_get_string(source, qfrom));
            else if (type == G_TYPE_OBJECT) {
                obj = gwy_serializable_duplicate(gwy_container_get_object(source, qfrom));
                gwy_container_set_object(dest, qto, obj);
                g_object_unref(obj);
            }
            else {
                g_assert_not_reached();
            }
        }
    }
}

static void
sync_one_data_item(GwyContainer *source, GwyContainer *dest,
                   gint from_id, gint to_id,
                   GwyDataItem what, gboolean delete_too)
{
    static const KeyFuncForItem keyfuncs[] = {
        { GWY_DATA_ITEM_GRADIENT,    gwy_app_get_data_palette_key_for_id,     },
        { GWY_DATA_ITEM_TITLE,       gwy_app_get_data_title_key_for_id,       },
        { GWY_DATA_ITEM_META,        gwy_app_get_data_meta_key_for_id,        },
        { GWY_DATA_ITEM_REAL_SQUARE, gwy_app_get_data_real_square_key_for_id, },
    };

    static const gchar *cal_keys[] = {
        "cal_xerr", "cal_yerr", "cal_zerr", "cal_xunc", "cal_yunc", "cal_zunc",
    };

    gchar key_from[40];
    gchar key_to[40];
    const guchar *name;
    GwyRGBA rgba;
    guint enumval, flen, tlen;
    GQuark *keys;
    GObject *obj;
    gdouble dbl;
    guint i;

    switch (what) {
        case GWY_DATA_ITEM_GRADIENT:
        case GWY_DATA_ITEM_TITLE:
        case GWY_DATA_ITEM_META:
        case GWY_DATA_ITEM_REAL_SQUARE:
        sync_one_generic_item(source, dest, from_id, to_id, what, delete_too, keyfuncs, G_N_ELEMENTS(keyfuncs));
        break;

        case GWY_DATA_ITEM_MASK_COLOR:
        g_snprintf(key_from, sizeof(key_from), "/%d/mask", from_id);
        g_snprintf(key_to, sizeof(key_to), "/%d/mask", to_id);
        if (gwy_rgba_get_from_container(&rgba, source, key_from))
            gwy_rgba_store_to_container(&rgba, dest, key_to);
        else if (delete_too)
            gwy_rgba_remove_from_container(dest, key_to);
        break;

        case GWY_DATA_ITEM_RANGE:
        if (gwy_container_gis_double(source, gwy_app_get_data_range_min_key_for_id(from_id), &dbl))
            gwy_container_set_double(dest, gwy_app_get_data_range_min_key_for_id(to_id), dbl);
        else if (delete_too)
            gwy_container_remove(dest, gwy_app_get_data_range_min_key_for_id(to_id));

        if (gwy_container_gis_double(source, gwy_app_get_data_range_max_key_for_id(from_id), &dbl)) {
            gwy_container_set_double(dest, gwy_app_get_data_range_max_key_for_id(to_id), dbl);
        }
        else if (delete_too)
            gwy_container_remove(dest, gwy_app_get_data_range_max_key_for_id(to_id));

        case GWY_DATA_ITEM_RANGE_TYPE:
        if (gwy_container_gis_enum(source, gwy_app_get_data_range_type_key_for_id(from_id), &enumval))
            gwy_container_set_enum(dest, gwy_app_get_data_range_type_key_for_id(to_id), enumval);
        else if (delete_too)
            gwy_container_remove(dest, gwy_app_get_data_range_type_key_for_id(to_id));
        break;

        case GWY_DATA_ITEM_CALDATA:
        for (i = 0; i < G_N_ELEMENTS(cal_keys); i++) {
            g_snprintf(key_from, sizeof(key_from), "/%d/data/%s", from_id, cal_keys[i]);
            g_snprintf(key_to, sizeof(key_to), "/%d/data/%s", to_id, cal_keys[i]);
            if (gwy_container_gis_object_by_name(source, key_from, &obj)) {
                obj = gwy_serializable_duplicate(obj);
                gwy_container_set_object_by_name(dest, key_to, obj);
                g_object_unref(obj);
            }
            else if (delete_too)
                gwy_container_remove_by_name(dest, key_to);
        }
        break;

        case GWY_DATA_ITEM_SELECTIONS:
        g_snprintf(key_from, sizeof(key_from), "/%d/select/", from_id);
        g_snprintf(key_to, sizeof(key_to), "/%d/select/", to_id);
        if (delete_too)
            gwy_container_remove_by_prefix(dest, key_to);
        flen = strlen(key_from);
        tlen = strlen(key_to);
        keys = gwy_container_keys_with_prefix(source, key_from, NULL);
        for (i = 0; i < keys[i]; i++) {
            if (gwy_container_value_type(source, keys[i]) != G_TYPE_OBJECT)
                continue;
            obj = gwy_container_get_object(source, keys[i]);
            if (!GWY_IS_SELECTION(obj))
                continue;
            name = g_quark_to_string(keys[i]) + flen;
            if (strlen(name) >= sizeof(key_to)-tlen)
                continue;

            memcpy(key_to + tlen, name, strlen(name)+1);
            obj = gwy_serializable_duplicate(obj);
            gwy_container_set_object_by_name(dest, key_to, obj);
            g_object_unref(obj);
        }
        g_free(keys);
        break;

        default:
        g_assert_not_reached();
        break;
    }
}

/**
 * gwy_app_sync_data_items:
 * @source: Source container.
 * @dest: Target container (may be identical to source).
 * @from_id: Data number to copy items from.
 * @to_id: Data number to copy items to.
 * @delete_too: %TRUE to delete items in target if source does not contain
 *              them, %FALSE to copy only.
 * @...: 0-terminated list of #GwyDataItem values defining the items to copy.
 *
 * Synchronizes auxiliary image data items between data containers.
 **/
void
gwy_app_sync_data_items(GwyContainer *source,
                        GwyContainer *dest,
                        gint from_id,
                        gint to_id,
                        gboolean delete_too,
                        ...)
{
    GwyDataItem what;
    va_list ap;

    g_return_if_fail(GWY_IS_CONTAINER(source));
    g_return_if_fail(GWY_IS_CONTAINER(dest));
    g_return_if_fail(from_id >= 0 && to_id >= 0);
    if (source == dest && from_id == to_id)
        return;

    va_start(ap, delete_too);
    while ((what = va_arg(ap, GwyDataItem)))
        sync_one_data_item(source, dest, from_id, to_id, what, delete_too);
    va_end(ap);
}

/**
 * gwy_app_sync_data_itemsv:
 * @source: Source container.
 * @dest: Target container (may be identical to source).
 * @from_id: Data number to copy items from.
 * @to_id: Data number to copy items to.
 * @delete_too: %TRUE to delete items in target if source does not contain
 *              them, %FALSE to copy only.
 * @items: List of #GwyDataItem values defining the items to copy.
 * @nitems: Number of items in @items.
 *
 * Synchronizes auxiliary image data items between data containers.
 *
 * Since: 2.48
 **/
void
gwy_app_sync_data_itemsv(GwyContainer *source,
                         GwyContainer *dest,
                         gint from_id,
                         gint to_id,
                         gboolean delete_too,
                         const GwyDataItem *items,
                         guint nitems)
{
    guint i;

    g_return_if_fail(GWY_IS_CONTAINER(source));
    g_return_if_fail(GWY_IS_CONTAINER(dest));
    g_return_if_fail(from_id >= 0 && to_id >= 0);
    if (source == dest && from_id == to_id)
        return;

    for (i = 0; i < nitems; i++)
        sync_one_data_item(source, dest, from_id, to_id, items[i], delete_too);
}

/**
 * gwy_app_sync_volume_items:
 * @source: Source container.
 * @dest: Target container (may be identical to source).
 * @from_id: Data number to copy items from.
 * @to_id: Data number to copy items to.
 * @delete_too: %TRUE to delete items in target if source does not contain them, %FALSE to copy only.
 * @...: 0-terminated list of #GwyDataItem values defining the items to copy.
 *
 * Synchronizes auxiliary volume data items between data containers.
 *
 * Only %GWY_DATA_ITEM_GRADIENT, %GWY_DATA_ITEM_TITLE, %GWY_DATA_ITEM_META and %GWY_DATA_ITEM_PREVIEW are valid items
 * for volume data.
 *
 * Since: 2.51
 **/
void
gwy_app_sync_volume_items(GwyContainer *source,
                          GwyContainer *dest,
                          gint from_id,
                          gint to_id,
                          gboolean delete_too,
                          ...)
{
    GwyDataItem what;
    va_list ap;

    g_return_if_fail(GWY_IS_CONTAINER(source));
    g_return_if_fail(GWY_IS_CONTAINER(dest));
    g_return_if_fail(from_id >= 0 && to_id >= 0);
    if (source == dest && from_id == to_id)
        return;

    va_start(ap, delete_too);
    while ((what = va_arg(ap, GwyDataItem))) {
        sync_one_generic_item(source, dest, from_id, to_id, what, delete_too,
                              brick_keyfuncs, G_N_ELEMENTS(brick_keyfuncs));
    }
    va_end(ap);
}

/**
 * gwy_app_sync_volume_itemsv:
 * @source: Source container.
 * @dest: Target container (may be identical to source).
 * @from_id: Data number to copy items from.
 * @to_id: Data number to copy items to.
 * @delete_too: %TRUE to delete items in target if source does not contain them, %FALSE to copy only.
 * @items: List of #GwyDataItem values defining the items to copy.
 * @nitems: Number of items in @items.
 *
 * Synchronizes auxiliary volume data items between data containers.
 *
 * Only %GWY_DATA_ITEM_GRADIENT, %GWY_DATA_ITEM_TITLE, %GWY_DATA_ITEM_META and %GWY_DATA_ITEM_PREVIEW are valid items
 * for volume data.
 *
 * Since: 2.51
 **/
void
gwy_app_sync_volume_itemsv(GwyContainer *source,
                           GwyContainer *dest,
                           gint from_id,
                           gint to_id,
                           gboolean delete_too,
                           const GwyDataItem *items,
                           guint nitems)
{
    guint i;

    g_return_if_fail(GWY_IS_CONTAINER(source));
    g_return_if_fail(GWY_IS_CONTAINER(dest));
    g_return_if_fail(from_id >= 0 && to_id >= 0);
    if (source == dest && from_id == to_id)
        return;

    for (i = 0; i < nitems; i++) {
        sync_one_generic_item(source, dest, from_id, to_id, items[i], delete_too,
                              brick_keyfuncs, G_N_ELEMENTS(brick_keyfuncs));
    }
}

/**
 * gwy_app_data_browser_copy_channel:
 * @source: Source container.
 * @id: Data channel id.
 * @dest: Target container (may be identical to source).
 *
 * Copies a channel including all auxiliary data.
 *
 * Returns: The id of the copy.
 **/
gint
gwy_app_data_browser_copy_channel(GwyContainer *source,
                                  gint id,
                                  GwyContainer *dest)
{
    GwyDataField *dfield;
    GwyStringList *slog;
    gchar buf[32];
    GQuark key;
    gint newid;

    g_return_val_if_fail(GWY_IS_CONTAINER(source), -1);
    g_return_val_if_fail(GWY_IS_CONTAINER(dest), -1);
    key = gwy_app_get_data_key_for_id(id);
    dfield = gwy_container_get_object(source, key);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(dfield), -1);

    dfield = gwy_data_field_duplicate(dfield);
    newid = gwy_app_data_browser_add_data_field(dfield, dest, TRUE);
    g_object_unref(dfield);

    key = gwy_app_get_mask_key_for_id(id);
    if (gwy_container_gis_object(source, key, &dfield)) {
        dfield = gwy_data_field_duplicate(dfield);
        key = gwy_app_get_mask_key_for_id(newid);
        gwy_container_set_object(dest, key, dfield);
        g_object_unref(dfield);
    }

    key = gwy_app_get_show_key_for_id(id);
    if (gwy_container_gis_object(source, key, &dfield)) {
        dfield = gwy_data_field_duplicate(dfield);
        key = gwy_app_get_show_key_for_id(newid);
        gwy_container_set_object(dest, key, dfield);
        g_object_unref(dfield);
    }

    gwy_app_sync_data_items(source, dest, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            GWY_DATA_ITEM_META,
                            GWY_DATA_ITEM_TITLE,
                            GWY_DATA_ITEM_SELECTIONS,
                            GWY_DATA_ITEM_CALDATA,
                            0);

    g_snprintf(buf, sizeof(buf), "/%d/data/log", id);
    if (gwy_container_gis_object_by_name(source, buf, &slog)
        && gwy_string_list_get_length(slog)) {
        slog = gwy_string_list_duplicate(slog);
        g_snprintf(buf, sizeof(buf), "/%d/data/log", newid);
        gwy_container_set_object_by_name(dest, buf, slog);
        g_object_unref(slog);
        gwy_app_channel_log_add(dest, newid, newid, "builtin::duplicate", NULL);
    }

    return newid;
}

/**
 * gwy_app_data_browser_copy_volume:
 * @source: Source container.
 * @id: Volume data brick id.
 * @dest: Target container (may be identical to source).
 *
 * Copies volume brick data including all auxiliary data.
 *
 * Returns: The id of the copy.
 *
 * Since: 2.32
 **/
gint
gwy_app_data_browser_copy_volume(GwyContainer *source,
                                 gint id,
                                 GwyContainer *dest)
{
    GwyBrick *brick;
    GwyStringList *slog;
    GwyDataField *preview = NULL;
    GQuark key;
    gchar *strkey;
    gchar buf[32];
    gint newid;

    g_return_val_if_fail(GWY_IS_CONTAINER(source), -1);
    g_return_val_if_fail(GWY_IS_CONTAINER(dest), -1);
    key = gwy_app_get_brick_key_for_id(id);
    brick = gwy_container_get_object(source, key);
    g_return_val_if_fail(GWY_IS_BRICK(brick), -1);

    /* Do this explicitly to prevent calculation of auto preview field. */
    strkey = g_strconcat(g_quark_to_string(key), "/preview", NULL);
    if (gwy_container_gis_object_by_name(source, strkey, (GObject**)&preview))
        preview = gwy_data_field_duplicate(preview);
    g_free(strkey);

    brick = gwy_brick_duplicate(brick);
    newid = gwy_app_data_browser_add_brick(brick, preview, dest, TRUE);
    g_object_unref(brick);
    GWY_OBJECT_UNREF(preview);

    gwy_app_sync_volume_items(source, dest, id, newid, FALSE,
                              GWY_DATA_ITEM_PREVIEW,
                              GWY_DATA_ITEM_GRADIENT,
                              GWY_DATA_ITEM_META,
                              GWY_DATA_ITEM_TITLE,
                              0);

    g_snprintf(buf, sizeof(buf), BRICK_PREFIX "/%d/log", id);
    if (gwy_container_gis_object_by_name(source, buf, &slog)
        && gwy_string_list_get_length(slog)) {
        slog = gwy_string_list_duplicate(slog);
        g_snprintf(buf, sizeof(buf), BRICK_PREFIX "/%d/log", newid);
        gwy_container_set_object_by_name(dest, buf, slog);
        g_object_unref(slog);
        gwy_app_volume_log_add(dest, newid, newid, "builtin::duplicate", NULL);
    }

    return newid;
}

/**
 * gwy_app_sync_xyz_items:
 * @source: Source container.
 * @dest: Target container (may be identical to source).
 * @from_id: Data number to copy items from.
 * @to_id: Data number to copy items to.
 * @delete_too: %TRUE to delete items in target if source does not contain them, %FALSE to copy only.
 * @...: 0-terminated list of #GwyDataItem values defining the items to copy.
 *
 * Synchronizes auxiliary XYZ data items between data containers.
 *
 * Only %GWY_DATA_ITEM_GRADIENT, %GWY_DATA_ITEM_TITLE, %GWY_DATA_ITEM_META and %GWY_DATA_ITEM_PREVIEW are valid items
 * for XYZ data.
 *
 * Since: 2.60
 **/
void
gwy_app_sync_xyz_items(GwyContainer *source,
                       GwyContainer *dest,
                       gint from_id,
                       gint to_id,
                       gboolean delete_too,
                       ...)
{
    GwyDataItem what;
    va_list ap;

    g_return_if_fail(GWY_IS_CONTAINER(source));
    g_return_if_fail(GWY_IS_CONTAINER(dest));
    g_return_if_fail(from_id >= 0 && to_id >= 0);
    if (source == dest && from_id == to_id)
        return;

    va_start(ap, delete_too);
    while ((what = va_arg(ap, GwyDataItem))) {
        sync_one_generic_item(source, dest, from_id, to_id, what, delete_too,
                              surface_keyfuncs, G_N_ELEMENTS(surface_keyfuncs));
    }
    va_end(ap);
}

/**
 * gwy_app_sync_xyz_itemsv:
 * @source: Source container.
 * @dest: Target container (may be identical to source).
 * @from_id: Data number to copy items from.
 * @to_id: Data number to copy items to.
 * @delete_too: %TRUE to delete items in target if source does not contain them, %FALSE to copy only.
 * @items: List of #GwyDataItem values defining the items to copy.
 * @nitems: Number of items in @items.
 *
 * Synchronizes auxiliary XYZ data items between data containers.
 *
 * Only %GWY_DATA_ITEM_GRADIENT, %GWY_DATA_ITEM_TITLE, %GWY_DATA_ITEM_META and %GWY_DATA_ITEM_PREVIEW are valid items
 * for XYZ data.
 *
 * Since: 2.60
 **/
void
gwy_app_sync_xyz_itemsv(GwyContainer *source,
                        GwyContainer *dest,
                        gint from_id,
                        gint to_id,
                        gboolean delete_too,
                        const GwyDataItem *items,
                        guint nitems)
{
    guint i;

    g_return_if_fail(GWY_IS_CONTAINER(source));
    g_return_if_fail(GWY_IS_CONTAINER(dest));
    g_return_if_fail(from_id >= 0 && to_id >= 0);
    if (source == dest && from_id == to_id)
        return;

    for (i = 0; i < nitems; i++) {
        sync_one_generic_item(source, dest, from_id, to_id, items[i], delete_too,
                              surface_keyfuncs, G_N_ELEMENTS(surface_keyfuncs));
    }
}

/**
 * gwy_app_data_browser_copy_xyz:
 * @source: Source container.
 * @id: XYZ surface data id.
 * @dest: Target container (may be identical to source).
 *
 * Copies XYZ surface data including all auxiliary data.
 *
 * Returns: The id of the copy.
 *
 * Since: 2.45
 **/
gint
gwy_app_data_browser_copy_xyz(GwyContainer *source,
                              gint id,
                              GwyContainer *dest)
{
    GwySurface *surface;
    GwyStringList *slog;
    GQuark key;
    gchar buf[32];
    gint newid;

    g_return_val_if_fail(GWY_IS_CONTAINER(source), -1);
    g_return_val_if_fail(GWY_IS_CONTAINER(dest), -1);
    key = gwy_app_get_surface_key_for_id(id);
    surface = gwy_container_get_object(source, key);
    g_return_val_if_fail(GWY_IS_SURFACE(surface), -1);

    surface = gwy_surface_duplicate(surface);
    newid = gwy_app_data_browser_add_surface(surface, dest, TRUE);
    g_object_unref(surface);

    gwy_app_sync_xyz_items(source, dest, id, newid, FALSE,
                           GWY_DATA_ITEM_GRADIENT,
                           GWY_DATA_ITEM_META,
                           GWY_DATA_ITEM_TITLE,
                           0);

    g_snprintf(buf, sizeof(buf), SURFACE_PREFIX "/%d/log", id);
    if (gwy_container_gis_object_by_name(source, buf, &slog)
        && gwy_string_list_get_length(slog)) {
        slog = gwy_string_list_duplicate(slog);
        g_snprintf(buf, sizeof(buf), SURFACE_PREFIX "/%d/log", newid);
        gwy_container_set_object_by_name(dest, buf, slog);
        g_object_unref(slog);
        gwy_app_xyz_log_add(dest, newid, newid, "builtin::duplicate", NULL);
    }

    return newid;
}

/**
 * gwy_app_sync_curve_map_items:
 * @source: Source container.
 * @dest: Target container (may be identical to source).
 * @from_id: Data number to copy items from.
 * @to_id: Data number to copy items to.
 * @delete_too: %TRUE to delete items in target if source does not contain them, %FALSE to copy only.
 * @...: 0-terminated list of #GwyDataItem values defining the items to copy.
 *
 * Synchronizes auxiliary curve map data items between data containers.
 *
 * Only %GWY_DATA_ITEM_GRADIENT, %GWY_DATA_ITEM_TITLE, %GWY_DATA_ITEM_META, %GWY_DATA_ITEM_REAL_SQUARE and
 * %GWY_DATA_ITEM_PREVIEW are valid items for curve map data.
 *
 * Since: 2.60
 **/
void
gwy_app_sync_curve_map_items(GwyContainer *source,
                             GwyContainer *dest,
                             gint from_id,
                             gint to_id,
                             gboolean delete_too,
                             ...)
{
    GwyDataItem what;
    va_list ap;

    g_return_if_fail(GWY_IS_CONTAINER(source));
    g_return_if_fail(GWY_IS_CONTAINER(dest));
    g_return_if_fail(from_id >= 0 && to_id >= 0);
    if (source == dest && from_id == to_id)
        return;

    va_start(ap, delete_too);
    while ((what = va_arg(ap, GwyDataItem))) {
        sync_one_generic_item(source, dest, from_id, to_id, what, delete_too,
                              lawn_keyfuncs, G_N_ELEMENTS(lawn_keyfuncs));
    }
    va_end(ap);
}

/**
 * gwy_app_sync_curve_map_itemsv:
 * @source: Source container.
 * @dest: Target container (may be identical to source).
 * @from_id: Data number to copy items from.
 * @to_id: Data number to copy items to.
 * @delete_too: %TRUE to delete items in target if source does not contain them, %FALSE to copy only.
 * @items: List of #GwyDataItem values defining the items to copy.
 * @nitems: Number of items in @items.
 *
 * Synchronizes auxiliary curve map data items between data containers.
 *
 * Only %GWY_DATA_ITEM_GRADIENT, %GWY_DATA_ITEM_TITLE, %GWY_DATA_ITEM_META, %GWY_DATA_ITEM_REAL_SQUARE and
 * %GWY_DATA_ITEM_PREVIEW are valid items for curve map data.
 *
 * Since: 2.60
 **/
void
gwy_app_sync_curve_map_itemsv(GwyContainer *source,
                              GwyContainer *dest,
                              gint from_id,
                              gint to_id,
                              gboolean delete_too,
                              const GwyDataItem *items,
                              guint nitems)
{
    guint i;

    g_return_if_fail(GWY_IS_CONTAINER(source));
    g_return_if_fail(GWY_IS_CONTAINER(dest));
    g_return_if_fail(from_id >= 0 && to_id >= 0);
    if (source == dest && from_id == to_id)
        return;

    for (i = 0; i < nitems; i++) {
        sync_one_generic_item(source, dest, from_id, to_id, items[i], delete_too,
                              lawn_keyfuncs, G_N_ELEMENTS(lawn_keyfuncs));
    }
}

/**
 * gwy_app_data_browser_copy_curve_map:
 * @source: Source container.
 * @id: #GwyLawn curve map data id.
 * @dest: Target container (may be identical to source).
 *
 * Copies #GwyLawn curve map data including all auxiliary data.
 *
 * Returns: The id of the copy.
 *
 * Since: 2.60
 **/
gint
gwy_app_data_browser_copy_curve_map(GwyContainer *source,
                                    gint id,
                                    GwyContainer *dest)
{
    GwyLawn *lawn;
    GwyStringList *slog;
    GwyDataField *preview = NULL;
    GQuark key;
    gchar buf[32];
    gchar *strkey;
    gint newid;

    g_return_val_if_fail(GWY_IS_CONTAINER(source), -1);
    g_return_val_if_fail(GWY_IS_CONTAINER(dest), -1);
    key = gwy_app_get_lawn_key_for_id(id);
    lawn = gwy_container_get_object(source, key);
    g_return_val_if_fail(GWY_IS_LAWN(lawn), -1);

    /* Do this explicitly to prevent calculation of auto preview field. */
    strkey = g_strconcat(g_quark_to_string(key), "/preview", NULL);
    if (gwy_container_gis_object_by_name(source, strkey, (GObject**)&preview))
        preview = gwy_data_field_duplicate(preview);
    g_free(strkey);

    lawn = gwy_lawn_duplicate(lawn);
    newid = gwy_app_data_browser_add_lawn(lawn, preview, dest, TRUE);
    g_object_unref(lawn);
    GWY_OBJECT_UNREF(preview);

    gwy_app_sync_curve_map_items(source, dest, id, newid, FALSE,
                                 GWY_DATA_ITEM_PREVIEW,
                                 GWY_DATA_ITEM_GRADIENT,
                                 GWY_DATA_ITEM_META,
                                 GWY_DATA_ITEM_TITLE,
                                 0);

    g_snprintf(buf, sizeof(buf), LAWN_PREFIX "/%d/log", id);
    if (gwy_container_gis_object_by_name(source, buf, &slog)
        && gwy_string_list_get_length(slog)) {
        slog = gwy_string_list_duplicate(slog);
        g_snprintf(buf, sizeof(buf), LAWN_PREFIX "/%d/log", newid);
        gwy_container_set_object_by_name(dest, buf, slog);
        g_object_unref(slog);
        gwy_app_curve_map_log_add(dest, newid, newid, "builtin::duplicate", NULL);
    }

    return newid;
}

GwyDataField*
_gwy_app_create_brick_preview_field(GwyBrick *brick)
{
    gint xres = gwy_brick_get_xres(brick);
    gint yres = gwy_brick_get_yres(brick);
    gdouble xreal = gwy_brick_get_xreal(brick);
    gdouble yreal = gwy_brick_get_yreal(brick);
    GwyDataField *preview = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);

    gwy_brick_mean_xy_plane(brick, preview);
    return preview;
}

static gdouble
lawn_reduce_avg(gint ncurves, gint curvelength, const gdouble *curvedata, gpointer user_data)
{
    guint i, idx = GPOINTER_TO_UINT(user_data);
    gdouble s = 0.0;

    gwy_debug("nc %d, clen %d, data %p, idx %d", ncurves, curvelength, curvedata, idx);
    g_return_val_if_fail(idx < ncurves, 0.0);
    if (!curvelength)
        return s;

    curvedata += idx*curvelength;
    for (i = 0; i < curvelength; i++)
        s += curvedata[i];
    return s/curvelength;
}

GwyDataField*
_gwy_app_create_lawn_preview_field(GwyLawn *lawn)
{
    gint xres = gwy_lawn_get_xres(lawn);
    gint yres = gwy_lawn_get_yres(lawn);
    gdouble xreal = gwy_lawn_get_xreal(lawn);
    gdouble yreal = gwy_lawn_get_yreal(lawn);
    GwyDataField *preview = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
    gwy_lawn_reduce_to_plane(lawn, preview, lawn_reduce_avg, GUINT_TO_POINTER(0));
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(preview), gwy_lawn_get_si_unit_curve(lawn, 0));
    return preview;
}

static GwyDataField*
make_thumbnail_field(GwyDataField *dfield,
                     gint *width,
                     gint *height)
{
    gint xres, yres;
    gdouble scale;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    scale = MAX(xres/(gdouble)*width, yres/(gdouble)*height);
    if (scale > 1.0) {
        xres = xres/scale;
        yres = yres/scale;
        xres = CLAMP(xres, 2, *width);
        yres = CLAMP(yres, 2, *height);
        dfield = gwy_data_field_new_resampled(dfield, xres, yres, GWY_INTERPOLATION_NNA);
    }
    else
        g_object_ref(dfield);

    *width = xres;
    *height = yres;

    return dfield;
}

static GdkPixbuf*
render_data_thumbnail(GwyDataField *dfield,
                      const gchar *gradname,
                      GwyLayerBasicRangeType range_type,
                      gint width,
                      gint height,
                      gdouble *pmin,
                      gdouble *pmax)
{
    GwyDataField *render_field;
    GdkPixbuf *pixbuf;
    GwyGradient *gradient;
    gdouble min, max;

    gradient = gwy_gradients_get_gradient(gradname);
    gwy_resource_use(GWY_RESOURCE(gradient));

    render_field = make_thumbnail_field(dfield, &width, &height);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, BITS_PER_SAMPLE,
                            width, height);

    switch (range_type) {
        case GWY_LAYER_BASIC_RANGE_FULL:
        gwy_pixbuf_draw_data_field(pixbuf, render_field, gradient);
        break;

        case GWY_LAYER_BASIC_RANGE_FIXED:
        min = pmin ? *pmin : gwy_data_field_get_min(render_field);
        max = pmax ? *pmax : gwy_data_field_get_max(render_field);
        gwy_pixbuf_draw_data_field_with_range(pixbuf, render_field, gradient, min, max);
        break;

        case GWY_LAYER_BASIC_RANGE_AUTO:
        gwy_data_field_get_autorange(render_field, &min, &max);
        gwy_pixbuf_draw_data_field_with_range(pixbuf, render_field, gradient, min, max);
        break;

        case GWY_LAYER_BASIC_RANGE_ADAPT:
        gwy_pixbuf_draw_data_field_adaptive(pixbuf, render_field, gradient);
        break;

        default:
        g_warning("Bad range type: %d", range_type);
        gwy_pixbuf_draw_data_field(pixbuf, render_field, gradient);
        break;
    }
    g_object_unref(render_field);

    gwy_resource_release(GWY_RESOURCE(gradient));

    return pixbuf;
}

static GdkPixbuf*
render_mask_thumbnail(GwyDataField *dfield,
                      const GwyRGBA *color,
                      gint width,
                      gint height)
{
    GwyDataField *render_field;
    GdkPixbuf *pixbuf;

    render_field = make_thumbnail_field(dfield, &width, &height);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, BITS_PER_SAMPLE, width, height);
    gwy_pixbuf_draw_data_field_as_mask(pixbuf, render_field, color);
    g_object_unref(render_field);

    return pixbuf;
}

/**
 * gwy_app_get_channel_thumbnail:
 * @data: A data container.
 * @id: Data channel id.
 * @max_width: Maximum width of the created pixbuf, it must be at least 2.
 * @max_height: Maximum height of the created pixbuf, it must be at least 2.
 *
 * Creates a channel thumbnail.
 *
 * Returns: A newly created pixbuf with channel thumbnail.  It keeps the aspect
 *          ratio of the data field while not exceeding @max_width and
 *          @max_height.
 **/
GdkPixbuf*
gwy_app_get_channel_thumbnail(GwyContainer *data,
                              gint id,
                              gint max_width,
                              gint max_height)
{
    GwyDataField *dfield, *mfield = NULL, *sfield = NULL;
    GwyLayerBasicRangeType range_type = GWY_LAYER_BASIC_RANGE_FULL;
    const guchar *gradient = NULL;
    GdkPixbuf *pixbuf, *mask;
    gdouble min, max;
    gboolean min_set = FALSE, max_set = FALSE;
    GwyRGBA color;
    GQuark quark;

    g_return_val_if_fail(GWY_IS_CONTAINER(data), NULL);
    g_return_val_if_fail(id >= 0, NULL);
    g_return_val_if_fail(max_width > 1 && max_height > 1, NULL);

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &dfield))
        return NULL;

    gwy_container_gis_object(data, gwy_app_get_mask_key_for_id(id), &mfield);
    gwy_container_gis_object(data, gwy_app_get_show_key_for_id(id), &sfield);
    gwy_container_gis_string(data, gwy_app_get_data_palette_key_for_id(id), &gradient);

    if (sfield)
        pixbuf = render_data_thumbnail(sfield, gradient, GWY_LAYER_BASIC_RANGE_FULL,
                                       max_width, max_height, NULL, NULL);
    else {
        gwy_container_gis_enum(data, gwy_app_get_data_range_type_key_for_id(id), &range_type);
        if (range_type == GWY_LAYER_BASIC_RANGE_FIXED) {
            quark = gwy_app_get_data_range_min_key_for_id(id);
            min_set = gwy_container_gis_double(data, quark, &min);
            quark = gwy_app_get_data_range_max_key_for_id(id);
            max_set = gwy_container_gis_double(data, quark, &max);
        }
        /* Make thumbnails of images with defects nicer */
        if (range_type == GWY_LAYER_BASIC_RANGE_FULL)
            range_type = GWY_LAYER_BASIC_RANGE_AUTO;

        pixbuf = render_data_thumbnail(dfield, gradient, range_type, max_width, max_height,
                                       min_set ? &min : NULL,
                                       max_set ? &max : NULL);
    }

    if (mfield) {
        quark = gwy_app_get_mask_key_for_id(id);
        if (!gwy_rgba_get_from_container(&color, data, g_quark_to_string(quark)))
            gwy_rgba_get_from_container(&color, gwy_app_settings_get(), "/mask");
        mask = render_mask_thumbnail(mfield, &color, max_width, max_height);
        gdk_pixbuf_composite(mask, pixbuf,
                             0, 0, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), 0, 0,
                             1.0, 1.0, GDK_INTERP_NEAREST, 255);
        g_object_unref(mask);
    }

    return pixbuf;
}

/**
 * gwy_app_get_volume_thumbnail:
 * @data: A data container.
 * @id: Volume data id.
 * @max_width: Maximum width of the created pixbuf, it must be at least 2.
 * @max_height: Maximum height of the created pixbuf, it must be at least 2.
 *
 * Creates a volume thumbnail.
 *
 * Returns: A newly created pixbuf with volume data thumbnail.  It keeps the aspect ratio of the brick preview while
 *          not exceeding @max_width and @max_height.
 *
 * Since: 2.33
 **/
GdkPixbuf*
gwy_app_get_volume_thumbnail(GwyContainer *data,
                             gint id,
                             gint max_width,
                             gint max_height)
{
    GwyBrick *brick;
    GwyDataField *dfield = NULL;
    const guchar *gradient = NULL;
    GdkPixbuf *pixbuf;

    g_return_val_if_fail(GWY_IS_CONTAINER(data), NULL);
    g_return_val_if_fail(id >= 0, NULL);
    g_return_val_if_fail(max_width > 1 && max_height > 1, NULL);

    if (!gwy_container_gis_object(data, gwy_app_get_brick_key_for_id(id), &brick))
        return NULL;

    if (!gwy_container_gis_object(data, gwy_app_get_brick_preview_key_for_id(id), &dfield)) {
        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, BITS_PER_SAMPLE, max_width, max_height);
        gdk_pixbuf_fill(pixbuf, 0);
        return pixbuf;
    }

    gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient);
    pixbuf = render_data_thumbnail(dfield, gradient, GWY_LAYER_BASIC_RANGE_FULL,
                                   max_width, max_height, NULL, NULL);

    return pixbuf;
}

/**
 * gwy_app_get_xyz_thumbnail:
 * @data: A data container.
 * @id: XYZ surface data id.
 * @max_width: Maximum width of the created pixbuf, it must be at least 2.
 * @max_height: Maximum height of the created pixbuf, it must be at least 2.
 *
 * Creates an XYZ data thumbnail.
 *
 * Returns: A newly created pixbuf with XYZ data thumbnail.  It keeps the aspect ratio of the brick preview while not
 *          exceeding @max_width and @max_height.
 *
 * Since: 2.45
 **/
GdkPixbuf*
gwy_app_get_xyz_thumbnail(GwyContainer *data,
                          gint id,
                          gint max_width,
                          gint max_height)
{
    GwySurface *surface;
    GwyDataField *raster = NULL;
    const guchar *gradient = NULL;
    GdkPixbuf *pixbuf;

    g_return_val_if_fail(GWY_IS_CONTAINER(data), NULL);
    g_return_val_if_fail(id >= 0, NULL);
    g_return_val_if_fail(max_width > 1 && max_height > 1, NULL);

    if (!gwy_container_gis_object(data, gwy_app_get_surface_key_for_id(id), &surface))
        return NULL;

    gwy_container_gis_string(data, gwy_app_get_surface_palette_key_for_id(id), &gradient);

    raster = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    gwy_preview_surface_to_datafield(surface, raster, max_width, max_height, 0);
    pixbuf = render_data_thumbnail(raster, gradient, GWY_LAYER_BASIC_RANGE_FULL,
                                   max_width, max_height, NULL, NULL);
    g_object_unref(raster);

    return pixbuf;
}

/**
 * gwy_app_get_graph_thumbnail:
 * @data: A data container.
 * @id: Graph model data id.
 * @max_width: Maximum width of the created pixbuf, it must be at least 2.
 * @max_height: Maximum height of the created pixbuf, it must be at least 2.
 *
 * Creates a graph thumbnail.
 *
 * Note this function needs the GUI running (unlike the other thumbnail functions).  It cannot be used in a console
 * program.
 *
 * Returns: A newly created pixbuf with graph thumbnail.  Since graphs do not have natural width and height, its size
 *          will normally be exactly @max_width and @max_height.
 *
 * Since: 2.45
 **/
GdkPixbuf*
gwy_app_get_graph_thumbnail(GwyContainer *data,
                            gint id,
                            gint max_width,
                            gint max_height)
{
    static GwyGraph *graph = NULL;
    GdkColor color = { 0, 65535, 65535, 65535 };
    gint width = 160, height = 120;
    GdkPixbuf *big_pixbuf, *pixbuf;
    GdkColormap *cmap;
    GdkGC *gc;
    GdkVisual *visual;
    GdkPixmap *pixmap;
    GwyGraphModel *gmodel;
    GwyGraphArea *area;
    gdouble min, max, d;
    gboolean is_logscale = FALSE;

    g_return_val_if_fail(GWY_IS_CONTAINER(data), NULL);
    g_return_val_if_fail(id >= 0, NULL);
    g_return_val_if_fail(max_width > 1 && max_height > 1, NULL);

    if (!gwy_container_gis_object(data, gwy_app_get_graph_key_for_id(id), &gmodel))
        return NULL;

    if (!gtk_main_level())
        return NULL;

    visual = gdk_visual_get_system();
    g_return_val_if_fail(visual, NULL);
    cmap = gdk_colormap_get_system();
    g_return_val_if_fail(cmap, NULL);

    width = MAX(width, max_width);
    height = MAX(height, max_height);
    pixmap = gdk_pixmap_new(NULL, width, height, visual->depth);
    gdk_drawable_set_colormap(pixmap, cmap);

    gc = gdk_gc_new(pixmap);
    g_return_val_if_fail(gc, NULL);
    gdk_gc_set_colormap(gc, cmap);

    gdk_gc_set_rgb_fg_color(gc, &color);
    gdk_draw_rectangle(pixmap, gc, TRUE, 0, 0, width, height);

    if (!graph)
        graph = GWY_GRAPH(gwy_graph_new(gmodel));
    else
        gwy_graph_set_model(GWY_GRAPH(graph), gmodel);

    area = GWY_GRAPH_AREA(gwy_graph_get_area(graph));

    gwy_graph_model_get_x_range(gmodel, &min, &max);
    gwy_graph_area_set_x_range(area, min, max);

    g_object_get(gmodel, "y-logarithmic", &is_logscale, NULL);
    gwy_graph_model_get_y_range(gmodel, &min, &max);
    if (is_logscale) {
        if (max > min) {
            d = max/min;
            d = pow(d, 0.07);
            min /= d;
            max *= d;
        }
        else if (max) {
            min = 0.5*max;
            max = 2.0*max;
        }
        else {
            min = 0.1;
            max = 10.0;
        }
    }
    else {
        if (max > min) {
            d = max - min;
            min -= 0.07*d;
            max += 0.07*d;
        }
        else if (max) {
            min = 0.5*max;
            max = 1.5*max;
        }
        else {
            min = -1.0;
            max = 1.0;
        }
    }
    gwy_graph_area_set_y_range(area, min, max);

    gwy_graph_area_draw_on_drawable(area, pixmap, gc, 0, 0, width, height);
    big_pixbuf = gdk_pixbuf_get_from_drawable(NULL, pixmap, cmap, 0, 0, 0, 0, -1, -1);
    g_object_unref(pixmap);
    g_object_unref(gc);
    /* This crashes.  Not sure how colourmap references work.  Better leak it than crash...
     * g_object_unref(cmap);
     */

    if (width == max_width && height == max_height)
        pixbuf = big_pixbuf;
    else {
        pixbuf = gdk_pixbuf_scale_simple(big_pixbuf, max_width, max_height, GDK_INTERP_BILINEAR);
        g_object_unref(big_pixbuf);
    }

    return pixbuf;
}

/**
 * gwy_app_get_curve_map_thumbnail:
 * @data: A data container.
 * @id: Volume data id.
 * @max_width: Maximum width of the created pixbuf, it must be at least 2.
 * @max_height: Maximum height of the created pixbuf, it must be at least 2.
 *
 * Creates a curve map thumbnail.
 *
 * Returns: A newly created pixbuf with curve map data thumbnail.  It keeps the aspect ratio of the lawn preview while
 *          not exceeding @max_width and @max_height.
 *
 * Since: 2.60
 **/
GdkPixbuf*
gwy_app_get_curve_map_thumbnail(GwyContainer *data,
                                gint id,
                                gint max_width,
                                gint max_height)
{
    GwyLawn *lawn;
    GwyDataField *dfield = NULL;
    const guchar *gradient = NULL;
    GdkPixbuf *pixbuf;

    g_return_val_if_fail(GWY_IS_CONTAINER(data), NULL);
    g_return_val_if_fail(id >= 0, NULL);
    g_return_val_if_fail(max_width > 1 && max_height > 1, NULL);

    if (!gwy_container_gis_object(data, gwy_app_get_lawn_key_for_id(id), &lawn))
        return NULL;

    if (!gwy_container_gis_object(data, gwy_app_get_lawn_preview_key_for_id(id), &dfield)) {
        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, BITS_PER_SAMPLE, max_width, max_height);
        gdk_pixbuf_fill(pixbuf, 0);
        return pixbuf;
    }

    gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), &gradient);
    pixbuf = render_data_thumbnail(dfield, gradient, GWY_LAYER_BASIC_RANGE_FULL,
                                   max_width, max_height, NULL, NULL);

    return pixbuf;
}

void
_gwy_app_sync_show(GwyContainer *data,
                   GQuark quark,
                   G_GNUC_UNUSED GwyDataView *data_view)
{
    GwyContainer *current_data;
    gboolean has_show;

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &current_data, 0);
    if (data != current_data)
        return;

    has_show = gwy_container_contains(data, quark);
    gwy_debug("Syncing show sens flags");
    gwy_app_sensitivity_set_state(GWY_MENU_FLAG_DATA_SHOW, has_show ? GWY_MENU_FLAG_DATA_SHOW: 0);
}

static void
gwy_app_data_proxy_setup_mask(GwyContainer *data,
                              gint i)
{
    static const gchar *keys[] = {
        "/%d/mask/red", "/%d/mask/green", "/%d/mask/blue", "/%d/mask/alpha"
    };

    GwyContainer *settings;
    gchar key[32];
    const gchar *gkey;
    gdouble x;
    guint j;

    settings = gwy_app_settings_get();
    for (j = 0; j < G_N_ELEMENTS(keys); j++) {
        g_snprintf(key, sizeof(key), keys[j], i);
        if (gwy_container_contains_by_name(data, key))
            continue;
        /* XXX: This is a dirty trick stripping the first 3 chars of key */
        gkey = keys[j] + 3;
        if (!gwy_container_gis_double_by_name(data, gkey, &x))
            /* be noisy when we don't have default mask color */
            x = gwy_container_get_double_by_name(settings, gkey);
        gwy_container_set_double_by_name(data, key, x);
    }
}

void
_gwy_app_sync_mask(GwyContainer *data,
                   GQuark quark,
                   GwyDataView *data_view)
{
    GwyContainer *current_data;
    gboolean has_dfield, has_layer;
    const gchar *strkey;
    GwyPixmapLayer *layer;
    GwyAppKeyType type;
    gint i;

    has_dfield = gwy_container_contains(data, quark);
    has_layer = gwy_data_view_get_alpha_layer(data_view) != NULL;
    gwy_debug("has_dfield: %d, has_layer: %d", has_dfield, has_layer);

    if (has_dfield && !has_layer) {
        strkey = g_quark_to_string(quark);
        i = _gwy_app_analyse_data_key(strkey, &type, NULL);
        g_return_if_fail(i >= 0 && type == KEY_IS_MASK);
        gwy_app_data_proxy_setup_mask(data, i);
        layer = gwy_layer_mask_new();
        gwy_pixmap_layer_set_data_key(layer, strkey);
        gwy_layer_mask_set_color_key(GWY_LAYER_MASK(layer), strkey);
        gwy_data_view_set_alpha_layer(data_view, layer);
    }
    else if (!has_dfield && has_layer)
        gwy_data_view_set_alpha_layer(data_view, NULL);

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &current_data, 0);
    if (has_dfield != has_layer
        && data == current_data) {
        gwy_debug("Syncing mask sens flags");
        gwy_app_sensitivity_set_state(GWY_MENU_FLAG_DATA_MASK, has_dfield ? GWY_MENU_FLAG_DATA_MASK : 0);
    }
}

static void
adaptive_color_axis_map_func(G_GNUC_UNUSED GwyColorAxis *axis,
                             const gdouble *z,
                             gdouble *mapped,
                             guint n,
                             gpointer user_data)
{
    GwyDataWindow *data_window = GWY_DATA_WINDOW(user_data);
    GwyDataView *data_view;
    GwyPixmapLayer *layer;
    GObject *dfield;
    GwyContainer *data;
    const gchar *key;

    data_view = gwy_data_window_get_data_view(data_window);
    data = gwy_data_view_get_data(data_view);
    layer = gwy_data_view_get_base_layer(data_view);
    key = gwy_pixmap_layer_get_data_key(layer);
    if (!gwy_container_gis_object_by_name(data, key, &dfield)) {
        gwy_clear(mapped, n);
        return;
    }

    gwy_draw_data_field_map_adaptive(GWY_DATA_FIELD(dfield), z, mapped, n);
}

void
_gwy_app_update_data_range_type(GwyDataView *data_view,
                                gint id)
{
    GtkWidget *data_window, *widget;
    GwyPixmapLayer *layer;
    GwyColorAxis *color_axis;
    GwyColorAxisMapFunc map_func = NULL;
    gpointer map_func_data = NULL;
    GwyContainer *data;
    GwyTicksStyle ticks_style;
    gboolean show_labels;

    data_window = gtk_widget_get_ancestor(GTK_WIDGET(data_view), GWY_TYPE_DATA_WINDOW);
    if (!data_window) {
        g_warning("GwyDataView has no GwyDataWindow ancestor");
        return;
    }

    widget = gwy_data_window_get_color_axis(GWY_DATA_WINDOW(data_window));
    color_axis = GWY_COLOR_AXIS(widget);
    data = gwy_data_view_get_data(data_view);

    if (gwy_container_contains(data, gwy_app_get_show_key_for_id(id))) {
        ticks_style = GWY_TICKS_STYLE_CENTER;
        show_labels = FALSE;
    }
    else {
        layer = gwy_data_view_get_base_layer(data_view);
        switch (gwy_layer_basic_get_range_type(GWY_LAYER_BASIC(layer))) {
            case GWY_LAYER_BASIC_RANGE_FULL:
            case GWY_LAYER_BASIC_RANGE_FIXED:
            case GWY_LAYER_BASIC_RANGE_AUTO:
            ticks_style = GWY_TICKS_STYLE_AUTO;
            show_labels = TRUE;
            break;

            case GWY_LAYER_BASIC_RANGE_ADAPT:
            ticks_style = GWY_TICKS_STYLE_UNLABELLED;
            map_func = &adaptive_color_axis_map_func;
            map_func_data = data_window;
            show_labels = TRUE;
            break;

            default:
            g_warning("Unknown range type");
            ticks_style = GWY_TICKS_STYLE_NONE;
            show_labels = FALSE;
            break;
        }
    }

    gwy_color_axis_set_ticks_style(color_axis, ticks_style);
    gwy_color_axis_set_labels_visible(color_axis, show_labels);
    gwy_color_axis_set_tick_map_func(color_axis, map_func, map_func_data);
}

void
_gwy_app_update_channel_sens(void)
{
    GwyMenuSensFlags mask = (GWY_MENU_FLAG_DATA
                             | GWY_MENU_FLAG_UNDO
                             | GWY_MENU_FLAG_REDO
                             | GWY_MENU_FLAG_DATA_MASK
                             | GWY_MENU_FLAG_DATA_SHOW);
    GwyMenuSensFlags flags = 0;
    GwyContainer *data;
    GwyDataView *dataview;
    GwyDataField *maskfield, *presentation;
    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data,
                                     GWY_APP_DATA_VIEW, &dataview,
                                     GWY_APP_MASK_FIELD, &maskfield,
                                     GWY_APP_SHOW_FIELD, &presentation,
                                     0);

    if (!data || !dataview) {
        gwy_app_sensitivity_set_state(mask, flags);
        _gwy_app_data_view_set_current(NULL);
        return;
    }

    flags |= GWY_MENU_FLAG_DATA;
    if (gwy_undo_container_has_undo(data))
        flags |= GWY_MENU_FLAG_UNDO;
    if (gwy_undo_container_has_redo(data))
        flags |= GWY_MENU_FLAG_REDO;
    if (maskfield)
        flags |= GWY_MENU_FLAG_DATA_MASK;
    if (presentation)
        flags |= GWY_MENU_FLAG_DATA_SHOW;

    gwy_app_sensitivity_set_state(mask, flags);
}

void
_gwy_app_update_graph_sens(void)
{
    GwyMenuSensFlags mask = (GWY_MENU_FLAG_GRAPH | GWY_MENU_FLAG_GRAPH_CURVE);
    GwyMenuSensFlags flags = 0;
    GwyGraph *graph;
    GwyGraphModel *gmodel;

    gwy_app_data_browser_get_current(GWY_APP_GRAPH, &graph, GWY_APP_GRAPH_MODEL, &gmodel, 0);
    if (graph && gmodel)
        flags |= GWY_MENU_FLAG_GRAPH;
    if (gmodel && gwy_graph_model_get_n_curves(gmodel))
        flags |= GWY_MENU_FLAG_GRAPH_CURVE;
    gwy_app_sensitivity_set_state(mask, flags);
}

void
_gwy_app_update_brick_sens(void)
{
    GwyMenuSensFlags flags = GWY_MENU_FLAG_VOLUME;
    GwyDataView *dataview;
    gwy_app_data_browser_get_current(GWY_APP_VOLUME_VIEW, &dataview, 0);
    gwy_app_sensitivity_set_state(flags, dataview ? flags : 0);
}

void
_gwy_app_update_surface_sens(void)
{
    GwyMenuSensFlags flags = GWY_MENU_FLAG_XYZ;
    GwyDataView *dataview;
    gwy_app_data_browser_get_current(GWY_APP_XYZ_VIEW, &dataview, 0);
    gwy_app_sensitivity_set_state(flags, dataview ? flags : 0);
}

void
_gwy_app_update_lawn_sens(void)
{
    GwyMenuSensFlags flags = GWY_MENU_FLAG_CURVE_MAP;
    GwyDataView *dataview;
    gwy_app_data_browser_get_current(GWY_APP_CURVE_MAP_VIEW, &dataview, 0);
    gwy_app_sensitivity_set_state(flags, dataview ? flags : 0);
}

void
_gwy_app_update_3d_window_title(Gwy3DWindow *window3d, gint id)
{
    GtkWidget *view3d;
    GwyContainer *data;
    gchar *title, *ctitle;

    view3d = gwy_3d_window_get_3d_view(window3d);
    data = gwy_3d_view_get_data(GWY_3D_VIEW(view3d));
    ctitle = _gwy_app_figure_out_channel_title(data, id);
    title = g_strconcat("3D ", ctitle, NULL);
    gtk_window_set_title(GTK_WINDOW(window3d), title);
    g_free(title);
    g_free(ctitle);
}

void
_gwy_app_update_brick_info(GwyContainer *data,
                           gint id,
                           GwyDataView *data_view)
{
    GtkWidget *infolabel, *window;
    GwyBrick *brick = NULL;
    gchar *info, *unit;

    window = gtk_widget_get_toplevel(GTK_WIDGET(data_view));
    g_return_if_fail(GWY_IS_DATA_WINDOW(window));
    infolabel = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "gwy-brick-info"));
    if (!infolabel)
        return;
    if (!gwy_container_gis_object(data, gwy_app_get_brick_key_for_id(id), (GObject**)&brick))
        return;
    if (!GWY_IS_BRICK(brick))
        return;

    unit = gwy_si_unit_get_string(gwy_brick_get_si_unit_z(brick), GWY_SI_UNIT_FORMAT_MARKUP);
    info = g_strdup_printf(_("Z levels: %d, Z unit: %s"), gwy_brick_get_zres(brick), unit);
    g_free(unit);
    gtk_label_set_text(GTK_LABEL(infolabel), info);
    g_free(info);
}

void
_gwy_app_update_surface_info(GwyContainer *data,
                             gint id,
                             GwyDataView *data_view)
{
    GtkWidget *infolabel, *window;
    GwySurface *surface = NULL;
    gchar *info;

    window = gtk_widget_get_toplevel(GTK_WIDGET(data_view));
    g_return_if_fail(GWY_IS_DATA_WINDOW(window));
    infolabel = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "gwy-surface-info"));
    if (!infolabel)
        return;
    if (!gwy_container_gis_object(data, gwy_app_get_surface_key_for_id(id), (GObject**)&surface))
        return;
    if (!GWY_IS_SURFACE(surface))
        return;

    info = g_strdup_printf(_("Points: %d"), surface->n);
    gtk_label_set_text(GTK_LABEL(infolabel), info);
    g_free(info);
}

void
_gwy_app_update_lawn_info(GwyContainer *data,
                          gint id,
                          GwyDataView *data_view)
{
    GtkWidget *infolabel, *window;
    GwyLawn *lawn = NULL;
    GString *str;
    const gchar *name;
    guint i, n;

    window = gtk_widget_get_toplevel(GTK_WIDGET(data_view));
    g_return_if_fail(GWY_IS_DATA_WINDOW(window));
    infolabel = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "gwy-lawn-info"));
    if (!infolabel)
        return;
    if (!gwy_container_gis_object(data, gwy_app_get_lawn_key_for_id(id), (GObject**)&lawn))
        return;
    if (!GWY_IS_LAWN(lawn))
        return;

    str = g_string_new(_("Curves:"));

    n = gwy_lawn_get_n_curves(lawn);
    for (i = 0; i < n; i++) {
        if (i)
            g_string_append_c(str, ',');
        g_string_append_c(str, ' ');
        name = gwy_lawn_get_curve_label(lawn, i);
        g_string_append(str, name ? name : _("Untitled"));
    }

    if ((n = gwy_lawn_get_n_segments(lawn))) {
        g_string_append(str, "   ");
        g_string_append(str, _("Segments:"));
        for (i = 0; i < n; i++) {
            if (i)
                g_string_append_c(str, ',');
            g_string_append_c(str, ' ');
            name = gwy_lawn_get_segment_label(lawn, i);
            g_string_append(str, name ? name : _("Untitled"));
        }
    }

    gtk_label_set_text(GTK_LABEL(infolabel), str->str);
    g_string_free(str, TRUE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
