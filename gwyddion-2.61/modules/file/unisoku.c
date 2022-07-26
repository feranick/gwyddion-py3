/*
 *  $Id: unisoku.c 23866 2021-06-16 19:17:41Z yeti-dn $
 *  Copyright (C) 2005 David Necas (Yeti), Petr Klapetek.
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
 * <mime-type type="application/x-unisoku-spm">
 *   <comment>Unisoku SPM data header</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value=":STM data\r\n"/>
 *   </magic>
 *   <glob pattern="*.hdr"/>
 *   <glob pattern="*.HDR"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Unisoku
 * # Usually accompanied with an unidentifiable data file.
 * 0 string :STM\ data\x0d\x0a Unisoku STM text header
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Unisoku
 * .hdr + .dat
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

#define MAGIC ":STM data\r\n"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define EXTENSION_HEADER ".hdr"
#define EXTENSION_DATA ".dat"

typedef enum {
    UNISOKU_UINT8  = 2,
    UNISOKU_SINT8  = 3,
    UNISOKU_UINT16 = 4,
    UNISOKU_SINT16 = 5,
    UNISOKU_FLOAT  = 8
} UnisokuDataType;

typedef enum {
    UNISOKU_DIM_LENGTH = 1,
    UNISOKU_DIM_TIME = 2,
    UNISOKU_DIM_CURRENT = 3,
    UNISOKU_DIM_VOLTAGE = 4,
    UNISOKU_DIM_TEMPERATURE = 5,
    UNISOKU_DIM_INVERSE_LENGTH = 6,
    UNISOKU_DIM_INVERSE_TIME = 7,
    UNISOKU_DIM_OTHER = 8
} UnisokuDimType;

typedef struct {
    gint format_version;
    gchar *date;
    gchar *time;
    gchar *sample_name;
    gchar *remark;
    gboolean ascii_flag;
    UnisokuDataType data_type;
    gint xres;
    gint yres;
    UnisokuDimType dim_x;
    UnisokuDimType dim_y;
    gchar *unit_x;
    gdouble start_x;
    gdouble end_x;
    gboolean log_flag_x;
    gchar *unit_y;
    gdouble start_y;
    gdouble end_y;
    gboolean log_flag_y;
    gboolean ineq_flag;
    gchar *unit_z;
    gdouble min_raw_z;
    gdouble max_raw_z;
    gdouble min_z;
    gdouble max_z;
    gboolean log_flag_z;
    gdouble stm_voltage;
    gdouble stm_current;
    gdouble scan_time;
    gint accum;
    gchar *stm_voltage_unit;
    gchar *stm_current_unit;
    gchar *ad_name;
} UnisokuFile;

static gboolean      module_register        (void);
static gint          unisoku_detect         (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static GwyContainer* unisoku_load           (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static gboolean      unisoku_read_header    (gchar *buffer,
                                             UnisokuFile *ufile,
                                             GError **error);
static gint          unisoku_sscanf         (const gchar *str,
                                             const gchar *format,
                                             ...);
static GwyDataField* unisoku_read_data_field(const guchar *buffer,
                                             gsize size,
                                             UnisokuFile *ufile,
                                             GError **error);
static GwyContainer* unisoku_get_metadata   (UnisokuFile *ufile);
static gchar*        unisoku_find_data_name (const gchar *header_name);
static void          unisoku_file_free      (UnisokuFile *ufile);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Unisoku data files (two-part .hdr + .dat)."),
    "Yeti <yeti@gwyddion.net>",
    "0.10",
    "David Nečas (Yeti) & Petr Klapetek",
    "2005",
};

static const guint type_sizes[] = { 0, 0, 1, 1, 2, 2, 0, 0, 4 };

GWY_MODULE_QUERY2(module_info, unisoku)

static gboolean
module_register(void)
{
    gwy_file_func_register("unisoku",
                           N_("Unisoku files (.hdr + .dat)"),
                           (GwyFileDetectFunc)&unisoku_detect,
                           (GwyFileLoadFunc)&unisoku_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
unisoku_detect(const GwyFileDetectInfo *fileinfo,
               gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION_HEADER)
               ? 10 : 0;

    if (fileinfo->buffer_len > MAGIC_SIZE
        && memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) == 0
        && g_str_has_suffix(fileinfo->name_lowercase, EXTENSION_HEADER)) {
        gchar *data_name;

        if ((data_name = unisoku_find_data_name(fileinfo->name))) {
            score = 100;
            g_free(data_name);
        }
    }

    return score;
}

static GwyContainer*
unisoku_load(const gchar *filename,
             G_GNUC_UNUSED GwyRunType mode,
             GError **error)
{
    UnisokuFile ufile;
    GwyContainer *meta, *container = NULL;
    guchar *buffer = NULL;
    gchar *text = NULL;
    gsize i, size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    gchar *data_name;

    if (!g_file_get_contents(filename, &text, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    for (i = 0; i < size; i++) {
        if (!text[i])
            text[i] = ' ';
    }

    gwy_clear(&ufile, 1);
    if (!unisoku_read_header(text, &ufile, error)) {
        unisoku_file_free(&ufile);
        g_free(text);
        return NULL;
    }
    g_free(text);

    if (ufile.data_type < UNISOKU_UINT8
        || ufile.data_type > UNISOKU_FLOAT
        || type_sizes[ufile.data_type] == 0) {
        err_DATA_TYPE(error, ufile.data_type);
        unisoku_file_free(&ufile);
        return NULL;
    }

    if (!(data_name = unisoku_find_data_name(filename))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("No corresponding data file was found for header file."));
        unisoku_file_free(&ufile);
        return NULL;
    }

    if (!gwy_file_get_contents(data_name, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        unisoku_file_free(&ufile);
        g_free(data_name);
        return NULL;
    }

    dfield = unisoku_read_data_field(buffer, size, &ufile, error);
    gwy_file_abandon_contents(buffer, size, NULL);

    if (!dfield) {
        unisoku_file_free(&ufile);
        g_free(data_name);
        return NULL;
    }

    container = gwy_container_new();
    gwy_container_set_object_by_name(container, "/0/data", dfield);
    g_object_unref(dfield);
    gwy_app_channel_title_fall_back(container, 0);

    meta = unisoku_get_metadata(&ufile);
    gwy_container_set_object_by_name(container, "/0/meta", meta);
    g_object_unref(meta);

    gwy_file_channel_import_log_add(container, 0, NULL, data_name);

    unisoku_file_free(&ufile);
    g_free(data_name);

    return container;
}

#define NEXT(buffer, line, err) \
    do { \
        if (!(line = gwy_str_next_line(&buffer))) { \
            g_set_error(error, GWY_MODULE_FILE_ERROR, \
                        GWY_MODULE_FILE_ERROR_DATA, \
                        _("File header ended unexpectedly.")); \
            return FALSE; \
        } \
    } while (g_str_has_prefix(line, "\t:")); \
    g_strstrip(line)

static gboolean
unisoku_read_header(gchar *buffer,
                    UnisokuFile *ufile,
                    GError **error)
{
    gchar *line;
    gint type1, type2;

    line = gwy_str_next_line(&buffer);
    if (!line)
        return FALSE;

    NEXT(buffer, line, error);
    /* garbage */

    NEXT(buffer, line, error);
    if (unisoku_sscanf(line, "i", &ufile->format_version) != 1) {
        err_UNSUPPORTED(error, _("format version"));
        return FALSE;
    }

    NEXT(buffer, line, error);
    ufile->date = g_strdup(line);
    NEXT(buffer, line, error);
    ufile->time = g_strdup(line);
    NEXT(buffer, line, error);
    ufile->sample_name = g_strdup(line);
    NEXT(buffer, line, error);
    ufile->remark = g_strdup(line);

    NEXT(buffer, line, error);
    if (unisoku_sscanf(line, "ii", &ufile->ascii_flag, &type1) != 2) {
        err_INVALID(error, _("format flags"));
        return FALSE;
    }
    ufile->data_type = type1;

    NEXT(buffer, line, error);
    if (unisoku_sscanf(line, "ii", &ufile->xres, &ufile->yres) != 2) {
        err_INVALID(error, _("resolution"));
        return FALSE;
    }

    if (err_DIMENSION(error, ufile->xres) || err_DIMENSION(error, ufile->yres))
        return FALSE;

    NEXT(buffer, line, error);
    if (unisoku_sscanf(line, "ii", &type1, &type2) != 2) {
        /* FIXME */
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Missing or invalid some integers heaven knows what "
                      "they mean but that should be here."));
        return FALSE;
    }
    ufile->dim_x = type1;
    ufile->dim_y = type2;

    NEXT(buffer, line, error);
    ufile->unit_x = g_strdup(line);

    NEXT(buffer, line, error);
    /* The log flags seem to be missing occasionally.  Do not abort when it
     * happens. */
    if (unisoku_sscanf(line, "ddi",
                       &ufile->start_x, &ufile->end_x,
                       &ufile->log_flag_x) < 2) {
        err_INVALID(error, _("x scale parameters"));
        return FALSE;
    }

    NEXT(buffer, line, error);
    ufile->unit_y = g_strdup(line);

    NEXT(buffer, line, error);
    if (unisoku_sscanf(line, "ddii",
                       &ufile->start_y, &ufile->end_y,
                       &ufile->ineq_flag, &ufile->log_flag_y) < 3) {
        err_INVALID(error, _("y scale parameters"));
        return FALSE;
    }

    /* Use negated positive conditions to catch NaNs */
    if (!(ufile->end_x - ufile->start_x > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        ufile->start_x = 0.0;
        ufile->end_x = 1.0;
    }
    if (!(ufile->end_y - ufile->start_y > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        ufile->start_y = 0.0;
        ufile->end_y = 1.0;
    }

    NEXT(buffer, line, error);
    ufile->unit_z = g_strdup(line);

    NEXT(buffer, line, error);
    if (unisoku_sscanf(line, "ddddi",
                       &ufile->max_raw_z, &ufile->min_raw_z,
                       &ufile->max_z, &ufile->min_z,
                       &ufile->log_flag_z) < 4) {
        err_INVALID(error, _("z scale parameters"));
        return FALSE;
    }

    NEXT(buffer, line, error);
    if (unisoku_sscanf(line, "dddi",
                       &ufile->stm_voltage, &ufile->stm_current,
                       &ufile->scan_time, &ufile->accum) != 4) {
        err_INVALID(error, _("data type parameters"));
        return FALSE;
    }

    NEXT(buffer, line, error);
    /* reserved */

    NEXT(buffer, line, error);
    ufile->stm_voltage_unit = g_strdup(line);

    NEXT(buffer, line, error);
    ufile->stm_current_unit = g_strdup(line);

    NEXT(buffer, line, error);
    ufile->ad_name = g_strdup(line);

    /* There is more stuff after that, but heaven knows what it means... */

    return TRUE;
}

static gint
unisoku_sscanf(const gchar *str,
               const gchar *format,
               ...)
{
    va_list ap;
    gchar *endptr;
    gint *pi;
    gdouble *pd;
    gint count = 0;

    va_start(ap, format);
    while (*format) {
        switch (*format++) {
            case 'i':
            pi = va_arg(ap, gint*);
            g_assert(pi);
            *pi = strtol(str, &endptr, 10);
            break;

            case 'd':
            pd = va_arg(ap, gdouble*);
            g_assert(pd);
            *pd = g_ascii_strtod(str, &endptr);
            break;

            default:
            g_return_val_if_reached(0);
            break;
        }
        if ((gchar*)str == endptr)
            break;

        count++;
        str = endptr;
    }
    va_end(ap);

    return count;
}

static GwyDataField*
unisoku_read_data_field(const guchar *buffer,
                        gsize size,
                        UnisokuFile *ufile,
                        GError **error)
{
    gint i, n, power10;
    const gchar *unit;
    GwyDataField *dfield;
    GwySIUnit *siunit;
    gdouble q, pmin, pmax, rmin, rmax;
    gdouble *data;

    n = ufile->xres * ufile->yres;
    if (err_SIZE_MISMATCH(error, n*type_sizes[ufile->data_type], size, FALSE))
        return NULL;

    dfield = gwy_data_field_new(ufile->xres, ufile->yres,
                                fabs((ufile->end_x - ufile->start_x)),
                                fabs((ufile->end_y - ufile->start_y)),
                                FALSE);
    data = gwy_data_field_get_data(dfield);

    /* FIXME: what to do when ascii_flag is set? */
    switch (ufile->data_type) {
        case UNISOKU_UINT8:
        for (i = 0; i < n; i++)
            data[i] = buffer[i];
        break;

        case UNISOKU_SINT8:
        for (i = 0; i < n; i++)
            data[i] = (signed char)buffer[i];
        break;

        case UNISOKU_UINT16:
        {
            const guint16 *pdata = (const guint16*)buffer;

            for (i = 0; i < n; i++)
                data[i] = GUINT16_FROM_LE(pdata[i]);
        }
        break;

        case UNISOKU_SINT16:
        {
            const gint16 *pdata = (const gint16*)buffer;

            for (i = 0; i < n; i++)
                data[i] = GINT16_FROM_LE(pdata[i]);
        }
        break;

        case UNISOKU_FLOAT:
        for (i = 0; i < n; i++)
            data[i] = gwy_get_gfloat_le(&buffer);
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }

    unit = ufile->unit_x;
    if (!*unit)
        unit = "nm";
    siunit = gwy_si_unit_new_parse(unit, &power10);
    gwy_data_field_set_si_unit_xy(dfield, siunit);
    q = pow10((gdouble)power10);
    gwy_data_field_set_xreal(dfield, q*gwy_data_field_get_xreal(dfield));
    gwy_data_field_set_yreal(dfield, q*gwy_data_field_get_yreal(dfield));
    g_object_unref(siunit);

    unit = ufile->unit_z;
    /* XXX: No fallback yet, just make z unitless */
    siunit = gwy_si_unit_new_parse(unit, &power10);
    gwy_data_field_set_si_unit_z(dfield, siunit);
    q = pow10((gdouble)power10);
    pmin = q*ufile->min_z;
    pmax = q*ufile->max_z;
    rmin = ufile->min_raw_z;
    rmax = ufile->max_raw_z;
    gwy_data_field_multiply(dfield, (pmax - pmin)/(rmax - rmin));
    gwy_data_field_add(dfield, (pmin*rmax - pmax*rmin)/(rmax - rmin));
    g_object_unref(siunit);

    return dfield;
}

static GwyContainer*
unisoku_get_metadata(UnisokuFile *ufile)
{
    GwyContainer *meta;

    meta = gwy_container_new();

    gwy_container_set_string_by_name(meta, "Date",
                                     g_strconcat(ufile->date, " ",
                                                 ufile->time, NULL));
    if (*ufile->remark)
        gwy_container_set_string_by_name(meta, "Remark",
                                         g_strdup(ufile->remark));
    if (*ufile->sample_name)
        gwy_container_set_string_by_name(meta, "Sample name",
                                         g_strdup(ufile->sample_name));
    if (*ufile->ad_name)
        gwy_container_set_string_by_name(meta, "AD name",
                                         g_strdup(ufile->ad_name));

    return meta;
}

static gchar*
unisoku_find_data_name(const gchar *header_name)
{
    GString *data_name;
    gchar *retval;
    gboolean ok = FALSE;

    data_name = g_string_new(header_name);
    g_string_truncate(data_name,
                      data_name->len - (sizeof(EXTENSION_HEADER) - 1));
    g_string_append(data_name, EXTENSION_DATA);
    if (g_file_test(data_name->str, G_FILE_TEST_IS_REGULAR))
        ok = TRUE;
    else {
        guint i;
        for (i = 0; i < sizeof(EXTENSION_DATA); i++)
            data_name->str[data_name->len-1 - i]
                = g_ascii_toupper(data_name->str[data_name->len-1 - i]);
        if (g_file_test(data_name->str, G_FILE_TEST_IS_REGULAR))
            ok = TRUE;
    }
    retval = data_name->str;
    g_string_free(data_name, !ok);

    return ok ? retval : NULL;
}

static void
unisoku_file_free(UnisokuFile *ufile)
{
    g_free(ufile->date);
    g_free(ufile->time);
    g_free(ufile->sample_name);
    g_free(ufile->remark);
    g_free(ufile->unit_x);
    g_free(ufile->unit_y);
    g_free(ufile->unit_z);
    g_free(ufile->stm_voltage_unit);
    g_free(ufile->stm_current_unit);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
