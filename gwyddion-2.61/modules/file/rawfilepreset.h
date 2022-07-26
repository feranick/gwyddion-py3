/*
 *  $Id: rawfilepreset.h 21712 2018-12-04 14:52:16Z yeti-dn $
 *  Copyright (C) 2003,2004 David Necas (Yeti), Petr Klapetek.
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

#define GWY_TYPE_RAW_FILE_PRESET             (gwy_raw_file_preset_get_type())
#define GWY_RAW_FILE_PRESET(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_RAW_FILE_PRESET, GwyRawFilePreset))
#define GWY_RAW_FILE_PRESET_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_RAW_FILE_PRESET, GwyRawFilePresetClass))
#define GWY_IS_RAW_FILE_PRESET(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_RAW_FILE_PRESET))
#define GWY_IS_RAW_FILE_PRESET_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_RAW_FILE_PRESET))
#define GWY_RAW_FILE_PRESET_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_RAW_FILE_PRESET, GwyRawFilePresetClass))

#define DEFAULT_MISSINGVALUE "-32768.0"

/* Predefined common binary formats */
typedef enum {
    RAW_NONE = 0,
    RAW_SIGNED_BYTE,
    RAW_UNSIGNED_BYTE,
    RAW_SIGNED_WORD16,
    RAW_UNSIGNED_WORD16,
    RAW_SIGNED_WORD32,
    RAW_UNSIGNED_WORD32,
    RAW_IEEE_FLOAT,
    RAW_IEEE_DOUBLE,
    RAW_SIGNED_WORD64,
    RAW_UNSIGNED_WORD64,
    RAW_IEEE_HALF,
    RAW_PASCAL_REAL,
    RAW_LAST
} RawFileBuiltin;

/* Text or binary data? */
typedef enum {
    RAW_BINARY,
    RAW_TEXT
} RawFileFormat;

typedef struct _GwyRawFilePreset      GwyRawFilePreset;
typedef struct _GwyRawFilePresetClass GwyRawFilePresetClass;

/* note: size, skip, and rowskip are in bits */
typedef struct {
    RawFileFormat format;  /* binary, text */

    /* Information */
    guint32 xres;
    guint32 yres;
    gdouble xreal;
    gdouble yreal;
    gint xyexponent;
    gdouble zscale;
    gint zexponent;
    gchar *xyunit;
    gchar *zunit;

    /* Missing values. */
    gboolean havemissing;
    gchar *missingvalue;

    /* Binary */
    RawFileBuiltin builtin;
    guint32 offset;  /* offset from file start, in bytes */
    guint32 size;  /* data sample size (auto if builtin) */
    guint32 skip;  /* skip after each sample (multiple of 8 if builtin) */
    guint32 rowskip;  /* extra skip after each sample row (multiple of 8 if
                         builtin) */
    gboolean sign;  /* take the number as signed? (unused if not integer) */
    gboolean revsample;  /* reverse bit order in samples? */
    gboolean revbyte;  /* reverse bit order in bytes as we read them? */
    guint32 byteswap;  /* swap bytes (relative to HOST order), bit set means
                          swap blocks of this size (only for builtin) */

    /* Text */
    guint32 lineoffset;  /* start reading from this line (ASCII) */
    guchar *delimiter;  /* field delimiter (ASCII) */
    guint32 skipfields;  /* skip this number of fields at line start (ASCII) */
    gboolean decomma;  /* decimal separator is comma */
} GwyRawFilePresetData;

struct _GwyRawFilePreset {
    GwyResource parent_instance;
    GwyRawFilePresetData data;
};

struct _GwyRawFilePresetClass {
    GwyResourceClass parent_class;
};

static GType             gwy_raw_file_preset_get_type(void)                             G_GNUC_CONST;
static void              gwy_raw_file_preset_finalize(GObject *object);
static GwyRawFilePreset* gwy_raw_file_preset_new     (const gchar *name,
                                                      const GwyRawFilePresetData *data,
                                                      gboolean is_const);
static void              gwy_raw_file_preset_dump    (GwyResource *resource,
                                                      GString *str);
static GwyResource*      gwy_raw_file_preset_parse   (const gchar *text,
                                                      gboolean is_const);


static const GwyRawFilePresetData rawfilepresetdata_default = {
    RAW_BINARY,                     /* format */
    500, 500,                       /* xres, yres */
    100.0, 100.0, -6,               /* physical dimensions */
    1.0, -6,                        /* z-scale */
    NULL, NULL,                     /* units */
    FALSE, NULL,                    /* missing values */
    RAW_UNSIGNED_BYTE, 0, 8, 0, 0,  /* binary parameters */
    FALSE, FALSE, FALSE, 0,         /* binary options */
    0, NULL, 0, FALSE,              /* text parameters */
};

/* sizes of RawFile built-in types */
static const guint BUILTIN_SIZE[RAW_LAST] = {
    0, 8, 8, 16, 16, 32, 32, 32, 64, 64, 64, 16, 48,
};

G_DEFINE_TYPE(GwyRawFilePreset, gwy_raw_file_preset, GWY_TYPE_RESOURCE)

static void
gwy_raw_file_preset_class_init(GwyRawFilePresetClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GwyResourceClass *parent_class, *res_class = GWY_RESOURCE_CLASS(klass);

    gobject_class->finalize = gwy_raw_file_preset_finalize;

    parent_class = GWY_RESOURCE_CLASS(gwy_raw_file_preset_parent_class);
    res_class->item_type = *gwy_resource_class_get_item_type(parent_class);

    res_class->item_type.type = G_TYPE_FROM_CLASS(klass);

    res_class->name = "rawfile";
    res_class->inventory = gwy_inventory_new(&res_class->item_type);
    res_class->dump = gwy_raw_file_preset_dump;
    res_class->parse = gwy_raw_file_preset_parse;
}

static void
gwy_raw_file_preset_init(GwyRawFilePreset *preset)
{
    preset->data = rawfilepresetdata_default;
}

static void
gwy_raw_file_preset_finalize(GObject *object)
{
    GwyRawFilePreset *preset;

    preset = GWY_RAW_FILE_PRESET(object);
    g_free(preset->data.delimiter);
    g_free(preset->data.xyunit);
    g_free(preset->data.zunit);
    g_free(preset->data.missingvalue);

    G_OBJECT_CLASS(gwy_raw_file_preset_parent_class)->finalize(object);
}

static void
gwy_raw_file_preset_data_sanitize(GwyRawFilePresetData *data)
{
    data->xres = MAX(1, data->xres);
    data->yres = MAX(1, data->yres);
    if (data->xreal <= 0.0)
        data->xreal = rawfilepresetdata_default.xreal;
    if (data->yreal <= 0.0)
        data->yreal = rawfilepresetdata_default.yreal;
    if (data->zscale <= 0.0)
        data->zscale = rawfilepresetdata_default.zscale;
    data->xyexponent = CLAMP(data->xyexponent, -12, 3);
    data->zexponent = CLAMP(data->zexponent, -12, 3);

    data->havemissing = !!data->havemissing;

    if (!data->delimiter)
        data->delimiter = g_strdup("");
    if (!data->xyunit)
        data->xyunit = g_strdup("");
    if (!data->zunit)
        data->zunit = g_strdup("");
    if (!data->missingvalue)
        data->missingvalue = g_strdup(DEFAULT_MISSINGVALUE);

    data->decomma = !!data->decomma;
    data->builtin = MIN(data->builtin, RAW_LAST-1);
    if (data->builtin) {
        data->size = BUILTIN_SIZE[data->builtin];
        data->sign = (data->builtin == RAW_SIGNED_BYTE)
                      || (data->builtin == RAW_SIGNED_WORD16)
                      || (data->builtin == RAW_SIGNED_WORD32);
        data->skip = ((data->skip + 7)/8)*8;
        data->rowskip = ((data->rowskip + 7)/8)*8;
        data->byteswap = MIN(data->byteswap, data->size/8-1);
        data->revsample = FALSE;
    }
    else {
        data->size = MIN(data->size, 24);
        data->byteswap = 0;
    }
}

static void
gwy_raw_file_preset_data_copy(const GwyRawFilePresetData *src,
                              GwyRawFilePresetData *dest)
{
    g_return_if_fail(src != (const GwyRawFilePresetData*)dest);
    g_free(dest->delimiter);
    g_free(dest->xyunit);
    g_free(dest->zunit);
    g_free(dest->missingvalue);
    *dest = *src;
    dest->delimiter = g_strdup(dest->delimiter
                               ? dest->delimiter
                               : (const guchar*)"");
    dest->xyunit = g_strdup(dest->xyunit ? dest->xyunit : "");
    dest->zunit = g_strdup(dest->zunit ? dest->zunit : "");
    dest->missingvalue = g_strdup(dest->missingvalue
                                  ? dest->missingvalue
                                  : DEFAULT_MISSINGVALUE);
}

static GwyRawFilePreset*
gwy_raw_file_preset_new(const gchar *name,
                        const GwyRawFilePresetData *data,
                        gboolean is_const)
{
    GwyRawFilePreset *preset;

    preset = g_object_new(GWY_TYPE_RAW_FILE_PRESET,
                          "is-const", is_const,
                          NULL);
    gwy_raw_file_preset_data_copy(data, &preset->data);
    g_string_assign(GWY_RESOURCE(preset)->name, name);
    /* New non-const resources start as modified */
    GWY_RESOURCE(preset)->is_modified = !is_const;

    return preset;
}

static void
gwy_raw_file_preset_dump(GwyResource *resource,
                         GString *str)
{
    gchar xreal[G_ASCII_DTOSTR_BUF_SIZE],
          yreal[G_ASCII_DTOSTR_BUF_SIZE],
          zscale[G_ASCII_DTOSTR_BUF_SIZE];
    GwyRawFilePreset *preset;
    GwyRawFilePresetData *data;
    gchar *s;

    g_return_if_fail(GWY_IS_RAW_FILE_PRESET(resource));
    preset = GWY_RAW_FILE_PRESET(resource);
    data = &preset->data;

    /* Information */
    g_ascii_dtostr(xreal, sizeof(xreal), data->xreal);
    g_ascii_dtostr(yreal, sizeof(yreal), data->yreal);
    g_ascii_dtostr(zscale, sizeof(zscale), data->zscale);
    g_string_append_printf(str,
                           "format %u\n"
                           "xres %d\n"
                           "yres %d\n"
                           "xreal %s\n"
                           "yreal %s\n"
                           "xyexponent %d\n"
                           "zscale %s\n"
                           "zexponent %d\n",
                           data->format,
                           data->xres,
                           data->yres,
                           xreal, yreal,
                           data->xyexponent,
                           zscale,
                           data->zexponent);
    if (data->xyunit && *data->xyunit) {
        s = g_strescape(data->xyunit, NULL);
        g_string_append_printf(str, "xyunit \"%s\"\n", s);
        g_free(s);
    }
    if (data->zunit && *data->zunit) {
        s = g_strescape(data->zunit, NULL);
        g_string_append_printf(str, "zunit \"%s\"\n", s);
        g_free(s);
    }

    s = g_strescape(data->missingvalue, NULL);
    g_string_append_printf(str,
                           "havemissing %d\n"
                           "missingvalue \"%s\"\n",
                           data->havemissing,
                           s);
    g_free(s);

    /* Binary */
    g_string_append_printf(str,
                           "builtin %u\n"
                           "offset %u\n"
                           "size %u\n"
                           "skip %u\n"
                           "rowskip %u\n"
                           "sign %d\n"
                           "revsample %d\n"
                           "revbyte %d\n"
                           "byteswap %u\n",
                           data->builtin,
                           data->offset,
                           data->size,
                           data->skip,
                           data->rowskip,
                           data->sign,
                           data->revsample,
                           data->revbyte,
                           data->byteswap);

    /* Text */
    g_string_append_printf(str,
                           "lineoffset %u\n"
                           "skipfields %u\n"
                           "decomma %d\n",
                           data->lineoffset,
                           data->skipfields,
                           data->decomma);
    if (data->delimiter && *data->delimiter) {
        s = g_strescape(data->delimiter, NULL);
        g_string_append_printf(str, "delimiter \"%s\"\n", s);
        g_free(s);
    }
}

static void
unquote_string(gchar *quoted, gchar **s)
{
    guint len;

    len = strlen(quoted);
    if (len < 2 || quoted[0] != '"' || quoted[len-1] != '"')
        return;

    quoted[len-1] = '\0';
    quoted++;
    g_free(*s);
    *s = g_strcompress(quoted);
}

static GwyResource*
gwy_raw_file_preset_parse(const gchar *text,
                          gboolean is_const)
{
    GwyRawFilePresetData data;
    GwyRawFilePreset *preset = NULL;
    GwyRawFilePresetClass *klass;
    gchar *str, *p, *line, *key, *value;

    g_return_val_if_fail(text, NULL);
    klass = g_type_class_peek(GWY_TYPE_RAW_FILE_PRESET);
    g_return_val_if_fail(klass, NULL);

    data = rawfilepresetdata_default;
    p = str = g_strdup(text);
    while ((line = gwy_str_next_line(&p))) {
        g_strstrip(line);
        key = line;
        if (!*key)
            continue;
        value = strchr(key, ' ');
        if (value) {
            *value = '\0';
            value++;
            g_strstrip(value);
        }
        if (!value || !*value) {
            g_warning("Missing value for `%s'.", key);
            continue;
        }

        /* Information */
        if (gwy_strequal(key, "format"))
            data.format = atoi(value);
        else if (gwy_strequal(key, "xres"))
            data.xres = atoi(value);
        else if (gwy_strequal(key, "yres"))
            data.yres = atoi(value);
        else if (gwy_strequal(key, "xyexponent"))
            data.xyexponent = atoi(value);
        else if (gwy_strequal(key, "zexponent"))
            data.zexponent = atoi(value);
        else if (gwy_strequal(key, "xreal"))
            data.xreal = g_ascii_strtod(value, NULL);
        else if (gwy_strequal(key, "yreal"))
            data.yreal = g_ascii_strtod(value, NULL);
        else if (gwy_strequal(key, "zscale"))
            data.zscale = g_ascii_strtod(value, NULL);
        else if (gwy_strequal(key, "xyunit"))
            unquote_string(value, &data.xyunit);
        else if (gwy_strequal(key, "zunit"))
            unquote_string(value, &data.zunit);
        /* Missing values. */
        else if (gwy_strequal(key, "havemissing"))
            data.havemissing = atoi(value);
        else if (gwy_strequal(key, "missingvalue"))
            unquote_string(value, &data.missingvalue);
        /* Binary */
        else if (gwy_strequal(key, "builtin"))
            data.builtin = atoi(value);
        else if (gwy_strequal(key, "offset"))
            data.offset = atoi(value);
        else if (gwy_strequal(key, "size"))
            data.size = atoi(value);
        else if (gwy_strequal(key, "skip"))
            data.skip = atoi(value);
        else if (gwy_strequal(key, "rowskip"))
            data.rowskip = atoi(value);
        else if (gwy_strequal(key, "sign"))
            data.sign = atoi(value);
        else if (gwy_strequal(key, "revsample"))
            data.revsample = atoi(value);
        else if (gwy_strequal(key, "revbyte"))
            data.revbyte = atoi(value);
        else if (gwy_strequal(key, "byteswap"))
            data.byteswap = atoi(value);
        /* Text */
        else if (gwy_strequal(key, "lineoffset"))
            data.lineoffset = atoi(value);
        else if (gwy_strequal(key, "skipfields"))
            data.skipfields = atoi(value);
        else if (gwy_strequal(key, "decomma"))
            data.decomma = atoi(value);
        else if (gwy_strequal(key, "delimiter"))
            unquote_string(value, (gchar**)&data.delimiter);
        else
            g_warning("Unknown field `%s'.", key);
    }

    preset = gwy_raw_file_preset_new("", &data, is_const);
    GWY_RESOURCE(preset)->is_modified = FALSE;
    gwy_raw_file_preset_data_sanitize(&preset->data);
    g_free(str);
    g_free(data.delimiter);
    g_free(data.xyunit);
    g_free(data.zunit);

    return (GwyResource*)preset;
}

static GwyInventory*
gwy_raw_file_presets(void)
{
    return GWY_RESOURCE_CLASS(g_type_class_peek
                                        (GWY_TYPE_RAW_FILE_PRESET))->inventory;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
