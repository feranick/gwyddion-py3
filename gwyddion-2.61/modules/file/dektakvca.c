/*
 *  $Id: dektakvca.c 21923 2019-02-26 11:57:06Z yeti-dn $
 *  Copyright (C) 2017-2018 David Necas (Yeti).
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
 * <mime-type type="application/x-dektak-opdx">
 *   <comment>Dektak OPDx profilometry data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="VCA DATA\x01\x00\x00\x55"/>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Dektak (binary serialisation)
 * 0 string VCA\ DATA\x01\x00\x00\x55 Dektak OPDx profilometry data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Dektak OPDx profilometry data
 * .OPDx
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "get.h"

#define MAGIC "VCA DATA\x01\x00\x00\x55"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define EXTENSION ".OPDx"

#define MEAS_SETTINGS "/MetaData/MeasurementSettings"

enum {
    TIMESTAMP_SIZE = 9,
    UNIT_EXTRA = 12,
    DOUBLE_ARRAY_EXTRA = 5,
};

/* Int16s are probably either 0x08, 0x09 or 0x04, 0x05 */
typedef enum {
    DEKTAK_MATRIX       = 0x00, /* Too lazy to assign an actual type id? */
    DEKTAK_BOOLEAN      = 0x01, /* Takes value 0 and 1 */
    DEKTAK_SINT32       = 0x06,
    DEKTAK_UINT32       = 0x07,
    DEKTAK_SINT64       = 0x0a,
    DEKTAK_UINT64       = 0x0b,
    DEKTAK_FLOAT        = 0x0c, /* Single precision float */
    DEKTAK_DOUBLE       = 0x0d, /* Double precision float */
    DEKTAK_TYPE_ID      = 0x0e, /* Compound type holding some kind of type id */
    DEKTAK_STRING       = 0x12, /* Free-form string value */
    DEKTAK_QUANTITY     = 0x13, /* Value with units (compound type) */
    DEKTAK_TIME_STAMP   = 0x15, /* Datetime (string/9-byte binary) */
    DEKTAK_UNITS        = 0x18, /* Units (compound type) */
    DEKTAK_DOUBLE_ARRAY = 0x40, /* Raw data array, in XML Base64-encoded */
    DEKTAK_STRING_LIST  = 0x42, /* List of Str */
    DEKTAK_ANON_MATRIX  = 0x45, /* Like DEKTAK_MATRIX, but with no name. */
    DEKTAK_RAW_DATA     = 0x46, /* Parent/wrapper tag of raw data */
    DEKTAK_RAW_DATA_2D  = 0x47, /* Parent/wrapper tag of raw data */
    DEKTAK_POS_RAW_DATA = 0x7c, /* Base64-encoded positions, not sure how
                                   it differs from 64 */
    DEKTAK_CONTAINER    = 0x7d, /* General nested data structure */
    DEKTAK_TERMINATOR   = 0x7f, /* Always the last item.
                                   Usually a couple of 0xff bytes inside. */
} DektakTypeID;

/* Points directly to the memory buffer.  We use it for all kind of blocks
 * in the file: containers, raw data regions, strings and also the file itself.
 */
typedef struct {
    const guchar *p;
    guint32 len;
} DektakBuf;

/* Quantities have name, symbol and value (in that units presumably).
 * Units have name, symbol, conversion factor *to* (not from) the unit and
 * then some stuff.  Possibly boolean flags? */
typedef struct {
    gdouble value;
    DektakBuf name;
    DektakBuf symbol;
    guchar extra[UNIT_EXTRA];
} DektakQuantUnit;

typedef struct {
    DektakQuantUnit unit;
    gdouble divisor;
    guint64 count;
    DektakBuf buf;
} DektakRawPos1D;

typedef struct {
    DektakQuantUnit unitx;
    DektakQuantUnit unity;
    gdouble divisorx;
    gdouble divisory;
} DektakRawPos2D;

typedef struct {
    DektakBuf another_name;   /* Optional, not present in DEKTAK_ANON_MATRIX */
    guint32 some_int;
    guint32 xres;
    guint32 yres;
    DektakBuf buf;
} DektakMatrix;

typedef union {
    gboolean b;
    guint32 ui;
    gint32 si;
    guint64 uq;
    gint64 sq;
    gdouble d;
    guint8 timestamp[TIMESTAMP_SIZE];  /* Format unknown. */
    DektakBuf buf;
    DektakQuantUnit qun;
    DektakRawPos1D rawpos1d;
    DektakRawPos2D rawpos2d;
    DektakMatrix matrix;
    GArray *strlist;                   /* Contains heap-allocated data. */
} DektakItemData;

typedef struct {
    DektakBuf typename;
    guint typeid;
    DektakItemData data;
} DektakItem;

static gboolean          module_register (void);
static gint              dektakvca_detect(const GwyFileDetectInfo *fileinfo,
                                          gboolean only_name);
static GwyContainer*     dektakvca_load  (const gchar *filename,
                                          GwyRunType mode,
                                          GError **error);
static gboolean          find_1d_data    (GHashTable *hash,
                                          GwyContainer *container,
                                          GString *str,
                                          GError **error);
static gboolean          find_2d_data    (GHashTable *hash,
                                          GwyContainer *container,
                                          GString *str,
                                          GError **error);
static GwyContainer*     create_meta     (GHashTable *hash);
static GwySIUnit*        find_quantity   (GHashTable *hash,
                                          const gchar *key,
                                          GString *str,
                                          gdouble *value,
                                          GError **error);
static const DektakItem* find_item       (GHashTable *hash,
                                          const gchar *path,
                                          guint expected_type,
                                          gboolean fail_if_not_found,
                                          GError **error);
static gboolean          read_item       (const DektakBuf *buf,
                                          guint32 *pos,
                                          GHashTable *hash,
                                          GString *path,
                                          GError **error);
static void              dektak_item_free(gpointer p);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Dektak OPDx data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.2",
    "David Nečas (Yeti)",
    "2017",
};

GWY_MODULE_QUERY2(module_info, dektakvca)

static gboolean
module_register(void)
{
    gwy_file_func_register("dektakvca",
                           N_("Dektak OPDx data files (.OPDx)"),
                           (GwyFileDetectFunc)&dektakvca_detect,
                           (GwyFileLoadFunc)&dektakvca_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
dektakvca_detect(const GwyFileDetectInfo *fileinfo,
                 gboolean only_name)
{
    const gchar *head = fileinfo->head;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;

    if (fileinfo->buffer_len <= MAGIC_SIZE
        || memcmp(head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    return 100;
}

static GwyContainer*
dektakvca_load(const gchar *filename,
               G_GNUC_UNUSED GwyRunType mode,
               GError **error)
{
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;
    GHashTable *hash;
    GString *str;
    DektakBuf buf;
    guint pos;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (size < MAGIC_SIZE || memcmp(buffer, MAGIC, MAGIC_SIZE) != 0) {
        gwy_file_abandon_contents(buffer, size, NULL);
        err_FILE_TYPE(error, "Dektak OPDx");
        return NULL;
    }

    hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                 g_free, dektak_item_free);
    str = g_string_new(NULL);
    buf.len = size;
    buf.p = buffer;

    pos = MAGIC_SIZE;
    while (pos < size) {
       if (!read_item(&buf, &pos, hash, str, error))
           goto fail;
    }

    /* Many things have two values, one in measurement settings and the other
     * in the data.  The value in data seems to be the actual one, the value
     * in settings is nominal? */
    container = gwy_container_new();
    if (!find_1d_data(hash, container, str, error)) {
        GWY_OBJECT_UNREF(container);
        goto fail;
    }

    if (!find_2d_data(hash, container, str, error)) {
        GWY_OBJECT_UNREF(container);
        goto fail;
    }

    if (!gwy_container_get_n_items(container)) {
        err_NO_DATA(error);
        GWY_OBJECT_UNREF(container);
    }


fail:
    g_hash_table_destroy(hash);
    g_string_free(str, TRUE);
    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

/* Only returns FALSE when @error is set.  When it does not seem there are
 * 1D data it returns TRUE; must check if @container has anyhthing in it
 * later. */
static gboolean
find_1d_data(GHashTable *hash, GwyContainer *container, GString *str,
             GError **error)
{
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    const DektakItem *item, *arrayitem;
    GwySIUnit *xunit = NULL, *yunit = NULL;
    const guchar *rawydata, *rawxdata = NULL;
    gdouble *xdata, *ydata;
    guint i, res;
    gdouble real, qy, qx;
    gboolean ok = FALSE;

    /* For 1D I have only ever see Raw arrays. */
    if (!(arrayitem = find_item(hash, "/1D_Data/Raw/Array",
                                DEKTAK_DOUBLE_ARRAY, FALSE, NULL)))
        return TRUE;

    if (!(item = find_item(hash, MEAS_SETTINGS "/SamplesToLog",
                           DEKTAK_UINT64, TRUE, error)))
        goto fail;

    res = item->data.uq;
    gwy_debug("res %u", res);

    if (!(xunit = find_quantity(hash, MEAS_SETTINGS "/ScanLength", str,
                                &real, error)))
        goto fail;
    gwy_debug("real %g", real);

    if (!(yunit = find_quantity(hash, "/1D_Data/Raw/DataScale", str,
                                &qy, error)))
        goto fail;
    gwy_debug("qy %g", qy);

    if (err_SIZE_MISMATCH(error,
                          DOUBLE_ARRAY_EXTRA + res*sizeof(gdouble),
                          arrayitem->data.buf.len,
                          TRUE))
        goto fail;
    rawydata = arrayitem->data.buf.p + DOUBLE_ARRAY_EXTRA;

    /* Positions -- optional. */
    if ((item = find_item(hash, "/1D_Data/Raw/PositionFunction",
                          DEKTAK_POS_RAW_DATA, TRUE, NULL))) {
        if (err_SIZE_MISMATCH(error,
                              res*sizeof(gdouble), item->data.rawpos1d.buf.len,
                              TRUE))
            goto fail;
        rawxdata = item->data.rawpos1d.buf.p;
        qx = 1.0/item->data.rawpos1d.unit.value;
    }

    /* Use DataKind as the title. */
    g_string_truncate(str, 0);
    if ((item = find_item(hash, "/MetaData/DataKind",
                          DEKTAK_STRING, FALSE, NULL)))
        g_string_append_len(str, item->data.buf.p, item->data.buf.len);
    else
        g_string_append(str, "Curve");

    /* Create the graph. */
    xdata = g_new(gdouble, res);
    if (rawxdata) {
        gwy_convert_raw_data(rawxdata, res, 1,
                             GWY_RAW_DATA_DOUBLE, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                             xdata, qx, 0.0);
    }
    else {
        for (i = 0; i < res; i++)
            xdata[i] = real*i/(res - 1.0);
    }

    ydata = g_new(gdouble, res);
    gwy_convert_raw_data(rawydata, res, 1,
                         GWY_RAW_DATA_DOUBLE, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         ydata, qy, 0.0);

    gmodel = gwy_graph_model_new();
    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(0),
                 "description", str->str,
                 NULL);
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, res);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);
    g_free(xdata);
    g_free(ydata);

    g_object_set(gmodel,
                 "si-unit-x", xunit,
                 "si-unit-y", yunit,
                 "title", str->str,
                 NULL);
    gwy_container_set_object(container,
                             gwy_app_get_graph_key_for_id(0), gmodel);
    g_object_unref(gmodel);

    ok = TRUE;

fail:
    GWY_OBJECT_UNREF(xunit);
    GWY_OBJECT_UNREF(yunit);

    return ok;
}

static void
find_2d_data_matrix(gpointer key, gpointer value, gpointer user_data)
{
    const gchar *name = (const gchar*)key;
    const DektakItem *item = (const DektakItem*)value;
    GPtrArray *channels = (GPtrArray*)user_data;
    const gchar *s;

    if ((item->typeid != DEKTAK_MATRIX && item->typeid != DEKTAK_ANON_MATRIX))
        return;
    if (strncmp(name, "/2D_Data/", 9) != 0)
        return;
    if (!(s = strchr(name + 9, '/')))
        return;
    if (!gwy_strequal(s + 1, "Matrix"))
        return;

    g_ptr_array_add(channels, g_strndup(name + 9, s - name - 9));
    gwy_debug("found 2D channel %s",
              (gchar*)g_ptr_array_index(channels, channels->len-1));
}

static gboolean
find_2d_data(GHashTable *hash, GwyContainer *container, GString *str,
             GError **error)
{
    GwySIUnit *xunit = NULL, *yunit = NULL, *zunit = NULL;
    GwyContainer *meta;
    GPtrArray *channels;
    GwyDataField *dfield, *mask;
    const DektakItem *item;
    gboolean ok = FALSE;
    const guchar *rawdata;
    guint i, len, xres, yres;
    gdouble xreal, yreal, q = 1.0;
    GString *s;
    GQuark quark;

    channels = g_ptr_array_new();
    g_hash_table_foreach(hash, find_2d_data_matrix, channels);
    if (!channels->len) {
        g_ptr_array_free(channels, TRUE);
        return TRUE;
    }

    s = g_string_new(NULL);
    for (i = 0; i < channels->len; i++) {
        g_string_assign(str, "/2D_Data/");
        g_string_append(str, (gchar*)g_ptr_array_index(channels, i));
        len = str->len;

        g_string_append(str, "/Matrix");
        item = find_item(hash, str->str, DEKTAK_MATRIX, TRUE, NULL);
        /* Must correspond to what find_2d_data_matrix() found. */
        g_return_val_if_fail(item, FALSE);

        rawdata = item->data.matrix.buf.p;
        xres = item->data.matrix.xres;
        yres = item->data.matrix.yres;
        if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
            goto fail;

        g_string_truncate(str, len);
        g_string_append(str, "/Dimension1Extent");
        if (!(yunit = find_quantity(hash, str->str, s, &yreal, error)))
            goto fail;
        gwy_debug("yreal %g", yreal);

        g_string_truncate(str, len);
        g_string_append(str, "/Dimension2Extent");
        if (!(xunit = find_quantity(hash, str->str, s, &xreal, error)))
            goto fail;
        gwy_debug("xreal %g", xreal);

        g_string_truncate(str, len);
        g_string_append(str, "/DataScale");
        if (!(zunit = find_quantity(hash, str->str, s, &q, error)))
            goto fail;
        gwy_debug("q %g", q);

        if (!gwy_si_unit_equal(xunit, yunit)) {
            g_warning("Different x and y units are not representable, "
                      "ignoring y.");
        }

        dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
        gwy_data_field_set_si_unit_xy(dfield, xunit);
        gwy_data_field_set_si_unit_z(dfield, zunit);

        gwy_convert_raw_data(rawdata, xres*yres, 1,
                             GWY_RAW_DATA_FLOAT, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                             gwy_data_field_get_data(dfield), q, 0.0);

        quark = gwy_app_get_data_key_for_id(i);
        gwy_container_set_object(container, quark, dfield);
        if ((mask = gwy_app_channel_mask_of_nans(dfield, TRUE))) {
            quark = gwy_app_get_mask_key_for_id(i);
            gwy_container_set_object(container, quark, mask);
            g_object_unref(mask);
        }
        g_object_unref(dfield);

        quark = gwy_app_get_data_title_key_for_id(i);
        gwy_container_set_const_string(container, quark,
                                       g_ptr_array_index(channels, i));
        gwy_app_channel_check_nonsquare(container, i);

        GWY_OBJECT_UNREF(xunit);
        GWY_OBJECT_UNREF(yunit);
        GWY_OBJECT_UNREF(zunit);

        if ((meta = create_meta(hash))) {
            quark = gwy_app_get_data_meta_key_for_id(i);
            gwy_container_set_object(container, quark, meta);
            g_object_unref(meta);
        }
    }
    ok = TRUE;

fail:
    GWY_OBJECT_UNREF(xunit);
    GWY_OBJECT_UNREF(yunit);
    GWY_OBJECT_UNREF(zunit);
    g_string_free(s, TRUE);
    for (i = 0; i < channels->len; i++)
        g_free(g_ptr_array_index(channels, i));
    g_ptr_array_free(channels, TRUE);
    return ok;
}

static void
create_meta_item(gpointer key, gpointer value, gpointer user_data)
{
    const gchar *name = (const gchar*)key;
    const DektakItem *item = (const DektakItem*)value;
    GwyContainer *meta = (GwyContainer*)user_data;
    gchar *metakey, *metavalue;

    if (!g_str_has_prefix(name, "/MetaData/"))
        return;

    if (item->typeid == DEKTAK_BOOLEAN)
        metavalue = g_strdup(item->data.b ? "True" : "False");
    else if (item->typeid == DEKTAK_SINT32)
        metavalue = g_strdup_printf("%d", item->data.si);
    else if (item->typeid == DEKTAK_UINT32)
        metavalue = g_strdup_printf("%u", item->data.ui);
    else if (item->typeid == DEKTAK_SINT64)
        metavalue = g_strdup_printf("%" G_GINT64_FORMAT, item->data.sq);
    else if (item->typeid == DEKTAK_UINT64)
        metavalue = g_strdup_printf("%" G_GUINT64_FORMAT, item->data.uq);
    else if (item->typeid == DEKTAK_DOUBLE || item->typeid == DEKTAK_FLOAT)
        metavalue = g_strdup_printf("%g", item->data.d);
    else if (item->typeid == DEKTAK_STRING)
        metavalue = g_strndup(item->data.buf.p, item->data.buf.len);
    else if (item->typeid == DEKTAK_QUANTITY) {
        metavalue = g_strdup_printf("%g %.*s", item->data.qun.value,
                                    item->data.qun.symbol.len,
                                    item->data.qun.symbol.p);
    }
    else if (item->typeid == DEKTAK_STRING_LIST) {
        GArray *strlist = item->data.strlist;
        const DektakBuf *str;
        guint i, len = 0;

        for (i = 0; i < strlist->len; i++) {
            str = &g_array_index(strlist, DektakBuf, i);
            len += str->len + 1;
        }

        if (len) {
            metavalue = g_new(gchar, len);
            len = 0;
            for (i = 0; i < strlist->len; i++) {
                str = &g_array_index(strlist, DektakBuf, i);
                memcpy(metavalue + len, str->p, str->len);
                len += str->len;
                metavalue[len] = ' ';
                len++;
            }
            metavalue[len-1] = '\0';
        }
        else
            metavalue = g_strdup("");
    }
    else {
        gwy_debug("unhandled meta %02x %s", item->typeid, name);
        return;
    }

    metakey = gwy_strreplace(name + 10, "/", "::", (gsize)-1);
    gwy_container_set_string_by_name(meta, metakey, (const guchar*)metavalue);
    g_free(metakey);
}

static GwyContainer*
create_meta(GHashTable *hash)
{
    GwyContainer *meta = gwy_container_new();

    g_hash_table_foreach(hash, create_meta_item, meta);
    if (gwy_container_get_n_items(meta))
        return meta;

    g_object_unref(meta);
    return NULL;
}

static GwySIUnit*
find_quantity(GHashTable *hash, const gchar *key, GString *str,
              gdouble *value, GError **error)
{
    const DektakItem *item;
    GwySIUnit *unit;
    gint power10;

    if (!(item = find_item(hash, key, DEKTAK_QUANTITY, TRUE, error)))
        return NULL;
    g_string_truncate(str, 0);
    g_string_append_len(str,
                        item->data.qun.symbol.p, item->data.qun.symbol.len);
    unit = gwy_si_unit_new_parse(str->str, &power10);
    *value = item->data.qun.value * pow10(power10);

    return unit;
}

static const DektakItem*
find_item(GHashTable *hash, const gchar *path, guint expected_type,
          gboolean fail_if_not_found, GError **error)
{
    const DektakItem *item = g_hash_table_lookup(hash, path);

    if (!item) {
        if (fail_if_not_found)
            err_MISSING_FIELD(error, path);
        return NULL;
    }
    if (expected_type && item->typeid != expected_type) {
        if (fail_if_not_found) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Item `%s' has unexpected type %u instead of %u."),
                        path, item->typeid, expected_type);
        }
        return NULL;
    }
    return item;
}

static gboolean
read_with_check(const DektakBuf *buf, guint32 *pos, guint32 nbytes, void *out)
{
#if 0
    gwy_debug("attempting to read %u bytes of buffer of size %u at pos %u",
              nbytes, buf->len, *pos);
#endif
    if (buf->len < nbytes || *pos > buf->len - nbytes)
        return FALSE;
    memcpy(out, buf->p + *pos, nbytes);
    *pos += nbytes;
    return TRUE;
}

static gboolean
read_int16(const DektakBuf *buf, guint32 *pos, guint16 *i)
{
    if (read_with_check(buf, pos, sizeof(guint16), i)) {
        *i = GUINT16_FROM_LE(*i);
        return TRUE;
    }
    return FALSE;
}

static gboolean
read_int32(const DektakBuf *buf, guint32 *pos, guint32 *i)
{
    if (read_with_check(buf, pos, sizeof(guint32), i)) {
        *i = GUINT32_FROM_LE(*i);
        return TRUE;
    }
    return FALSE;
}

static gboolean
read_int64(const DektakBuf *buf, guint32 *pos, guint64 *i)
{
    if (read_with_check(buf, pos, sizeof(guint64), i)) {
        *i = GUINT64_FROM_LE(*i);
        return TRUE;
    }
    return FALSE;
}

static gboolean
read_float(const DektakBuf *buf, guint32 *pos, gdouble *f)
{
    union { guint32 i; gfloat f; } u;
    if (read_with_check(buf, pos, sizeof(guint32), &u)) {
        u.i = GUINT32_FROM_LE(u.i);
        *f = u.f;
        return TRUE;
    }
    return FALSE;
}

static gboolean
read_double(const DektakBuf *buf, guint32 *pos, gdouble *f)
{
    union { guint64 i; gdouble f; } u;
    if (read_with_check(buf, pos, sizeof(guint64), &u)) {
        u.i = GUINT64_FROM_LE(u.i);
        *f = u.f;
        return TRUE;
    }
    return FALSE;
}

static gboolean
read_varlen(const DektakBuf *buf, guint32 *pos, guint32 *l)
{
    guint8 lenlen;
    if (!read_with_check(buf, pos, 1, &lenlen))
        return FALSE;

    if (lenlen == 1) {
        guint8 len;
        if (!read_with_check(buf, pos, 1, &len))
            return FALSE;
        *l = len;
    }
    else if (lenlen == 2) {
        guint16 len;
        if (!read_int16(buf, pos, &len))
            return FALSE;
        *l = len;
    }
    else if (lenlen == 4) {
        guint32 len;
        if (!read_int32(buf, pos, &len))
            return FALSE;
        *l = len;
    }
    else {
        /* XXX: We should to report a different error type here, like
         * "Unsupported length length".  */
        return FALSE;
    }
    return TRUE;
}

/* Name has always 4byte size, unlike a string which has varlength. */
static gboolean
read_name(const DektakBuf *buf, guint32 *pos, DektakBuf *str)
{
    if (!read_int32(buf, pos, &str->len))
        return FALSE;
    if (buf->len < str->len || *pos > buf->len - str->len)
        return FALSE;
    str->p = buf->p + *pos;
    *pos += str->len;
    return TRUE;
}

/* NB: it moves @pos to the end of the structure because @p becomes the inner
 * content buffer. */
static gboolean
read_structured(const DektakBuf *buf, guint32 *pos, DektakBuf *content)
{
    if (!read_varlen(buf, pos, &content->len))
        return FALSE;
    if (buf->len < content->len || *pos > buf->len - content->len)
        return FALSE;
    content->p = buf->p + *pos;
    *pos += content->len;
    return TRUE;
}

/* NB: it moves @pos to the end of the structure because @content becomes the
 * inner content buffer. */
static gboolean
read_named_struct(const DektakBuf *buf, guint32 *pos,
                  DektakBuf *typename, DektakBuf *content)
{
    if (!read_name(buf, pos, typename))
        return FALSE;
    if (!read_structured(buf, pos, content))
        return FALSE;
    return TRUE;
}

/* Helper function; the structure seems to be repeated in various places. */
static gboolean
read_quantunit_content(const DektakBuf *buf, guint32 *pos, gboolean is_unit,
                       DektakQuantUnit *unit)
{
    if (!is_unit) {
        if (!read_double(buf, pos, &unit->value))
            return FALSE;
    }
    if (!read_name(buf, pos, &unit->name))
        return FALSE;
    if (!read_name(buf, pos, &unit->symbol))
        return FALSE;
    if (is_unit) {
        if (!read_double(buf, pos, &unit->value))
            return FALSE;
        if (!read_with_check(buf, pos, UNIT_EXTRA, &unit->extra[0]))
            return FALSE;
    }
    return TRUE;
}

static gboolean
read_dimension2d_content(const DektakBuf *buf, guint32 *pos,
                         DektakQuantUnit *unit, gdouble *divisor)
{
    if (!read_double(buf, pos, &unit->value))
        return FALSE;
    if (!read_name(buf, pos, &unit->name))
        return FALSE;
    if (!read_name(buf, pos, &unit->symbol))
        return FALSE;
    if (!read_double(buf, pos, divisor))
        return FALSE;
    if (!read_with_check(buf, pos, UNIT_EXTRA, &unit->extra[0]))
        return FALSE;
    return TRUE;
}

static gboolean
read_item(const DektakBuf *buf, guint32 *pos, GHashTable *hash, GString *path,
          GError **error)
{
    DektakItem item;
    DektakBuf name, content, s;
    guint8 typeid, b8;
    guint orig_path_len = path->len;
    guint32 itempos = 0;

    if (!read_name(buf, pos, &name))
        goto fail;

    g_string_append_c(path, '/');
    g_string_append_len(path, (gchar*)name.p, name.len);

    if (!read_with_check(buf, pos, 1, &typeid))
        goto fail;

    gwy_clear(&item, 1);
    item.typeid = typeid;

    /* Simple types. */
    if (typeid == DEKTAK_BOOLEAN) {
        if (!read_with_check(buf, pos, 1, &b8))
            goto fail;
        item.data.b = b8;
    }
    else if (typeid == DEKTAK_SINT32 || typeid == DEKTAK_UINT32) {
        if (!read_int32(buf, pos, &item.data.ui))
            goto fail;
    }
    else if (typeid == DEKTAK_SINT64 || typeid == DEKTAK_UINT64) {
        if (!read_int64(buf, pos, &item.data.uq))
            goto fail;
    }
    else if (typeid == DEKTAK_FLOAT) {
        if (!read_float(buf, pos, &item.data.d))
            goto fail;
    }
    else if (typeid == DEKTAK_DOUBLE) {
        if (!read_double(buf, pos, &item.data.d))
            goto fail;
    }
    else if (typeid == DEKTAK_TIME_STAMP) {
        if (!read_with_check(buf, pos, TIMESTAMP_SIZE, &item.data.timestamp[0]))
            goto fail;
    }
    else if (typeid == DEKTAK_STRING) {
        if (!read_structured(buf, pos, &item.data.buf))
            goto fail;
    }
    else if (typeid == DEKTAK_QUANTITY) {
        if (!read_structured(buf, pos, &content))
            goto fail;
        if (!read_quantunit_content(&content, &itempos, FALSE, &item.data.qun))
            goto fail;
    }
    else if (typeid == DEKTAK_UNITS) {
        if (!read_structured(buf, pos, &content))
            goto fail;
        if (!read_quantunit_content(&content, &itempos, TRUE, &item.data.qun))
            goto fail;
    }
    else if (typeid == DEKTAK_TERMINATOR) {
        /* There are usually some 0xff bytes.  Not sure what to think about
         * them. */
        *pos = buf->len;
    }
    /* Container types.  Cannot tell any difference between these two.  Raw
     * data purpose seems to be wrapping actual raw data in something
     * container-like. */
    else if (typeid == DEKTAK_CONTAINER
             || typeid == DEKTAK_RAW_DATA
             || typeid == DEKTAK_RAW_DATA_2D) {
        if (!read_structured(buf, pos, &content))
            goto fail;
        while (itempos < content.len) {
            if (!read_item(&content, &itempos, hash, path, error))
                return FALSE;
        }
    }
    /* Types with string type name (i.e. untyped serialised junk we have to
     * know how to read). */
    else if (typeid == DEKTAK_DOUBLE_ARRAY) {
        /* Must check if array size is 8*something + 5.   But later. */
        if (!read_named_struct(buf, pos, &item.typename, &item.data.buf))
            goto fail;
    }
    else if (typeid == DEKTAK_STRING_LIST) {
        if (!read_named_struct(buf, pos, &item.typename, &content))
            goto fail;
        item.data.strlist = g_array_new(FALSE, FALSE, sizeof(DektakBuf));
        while (itempos < content.len) {
           if (!read_name(&content, &itempos, &s)) {
               g_array_free(item.data.strlist, TRUE);
               goto fail;
           }
           g_array_append_val(item.data.strlist, s);
        }
    }
    else if (typeid == DEKTAK_TYPE_ID) {
        /* The type id is presumably an integral type because in XML it is
         * represented by an integer literal.  But it is represented as a
         * byte buffer.  */
        if (!read_named_struct(buf, pos, &item.typename, &item.data.buf))
            goto fail;
    }
    else if (typeid == DEKTAK_POS_RAW_DATA) {
        /* Unfortunately, we have to know if we are readin 1D or 2D at this
         * point because the structs differ.  Bummer. */
        if (g_str_has_prefix(path->str, "/2D_Data")) {
            gwy_debug("assuming 2D for pos raw data");
            if (!read_named_struct(buf, pos, &item.typename, &content))
                goto fail;

            if (!read_dimension2d_content(&content, &itempos,
                                          &item.data.rawpos2d.unitx,
                                          &item.data.rawpos2d.divisorx))
                goto fail;
            if (!read_dimension2d_content(&content, &itempos,
                                          &item.data.rawpos2d.unity,
                                          &item.data.rawpos2d.divisory))
                goto fail;
        }
        else {
            if (!g_str_has_prefix(path->str, "/1D_Data")) {
                g_warning("Cannot tell if we have 1D or 2D data, assuming 1D.");
            }
            gwy_debug("assuming 1D for pos raw data");
            if (!read_named_struct(buf, pos, &item.typename, &content))
                goto fail;

            /* This is exactly as a UNIT record, but it does not carry the type
             * byte.  Whatever. */
            if (!read_quantunit_content(&content, &itempos, TRUE,
                                        &item.data.rawpos1d.unit))
                goto fail;
            if (!read_int64(&content, &itempos, &item.data.rawpos1d.count))
                goto fail;
            /* Must check if item count matches the buffer size.  But later. */
            item.data.rawpos1d.buf = content;
            item.data.rawpos1d.buf.p += itempos;
            item.data.rawpos1d.buf.len -= itempos;
        }
    }
    else if (typeid == DEKTAK_MATRIX || typeid == DEKTAK_ANON_MATRIX) {
        if (!read_name(buf, pos, &item.typename))
            goto fail;
        if (typeid == DEKTAK_MATRIX) {
            /* This is usually zero... */
            if (!read_int32(buf, pos, &item.data.matrix.some_int))
                goto fail;
            if (!read_name(buf, pos, &item.data.matrix.another_name))
                goto fail;
        }
        else {
            /* Item is zero-cleared, which should be OK. */
        }
        /* The length includes the following xres and yres. */
        if (!read_varlen(buf, pos, &item.data.matrix.buf.len))
            goto fail;
        if (!read_int32(buf, pos, &item.data.matrix.yres))
            goto fail;
        if (!read_int32(buf, pos, &item.data.matrix.xres))
            goto fail;
        if (item.data.matrix.buf.len < 2*sizeof(guint32))
            goto fail;
        item.data.matrix.buf.len -= 2*sizeof(guint32);
        item.data.matrix.buf.p = buf->p + *pos;
        if (buf->len - *pos < item.data.matrix.buf.len)
            goto fail;
        *pos += item.data.matrix.buf.len;
    }
    else {
        err_DATA_TYPE(error, typeid);
        return FALSE;
    }

    gwy_debug("%s (typeid %02x)", path->str, typeid);
    g_hash_table_insert(hash,
                        g_strndup(path->str, path->len),
                        g_slice_dup(DektakItem, &item));
    g_string_truncate(path, orig_path_len);
    return TRUE;

fail:
    err_TRUNCATED_PART(error, path->str);
    return FALSE;
}

static void
dektak_item_free(gpointer p)
{
    DektakItem *item = (DektakItem*)p;

    /* Currently the only data type with heap-allocated data. */
    if (item->typeid == DEKTAK_STRING_LIST)
        g_array_free(item->data.strlist, TRUE);
    g_slice_free(DektakItem, item);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
