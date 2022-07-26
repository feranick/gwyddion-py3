/*
 *  $Id: mulfile.c 20676 2017-12-18 18:19:09Z yeti-dn $
 *  Copyright (C) 2011 David Necas (Yeti).
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
 * <mime-type type="application/x-mul-spm">
 *   <comment>Aarhus MUL SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="\x01\x00\x03\x00\x00\x00"/>
 *   </magic>
 *   <glob pattern="*.mul"/>
 *   <glob pattern="*.MUL"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Aarhus MUL
 * # This is actually the number and address of the first data as there is no
 * # real magic header.  It should be constant though.
 * # Conflicts with the TARGA rule.
 * 0 string \x01\x00\x03\x00\x00\x00 Aarhus MUL SPM data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Aarhus MUL
 * .mul
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "get.h"
#include "err.h"

#define Angstrom (1e-10)
#define Nano (1e-9)

/* This is actually the number and address of the first data as there is no
 * real identifier.  It should be constant, though. */
#define MAGIC "\x01\x00\x03\x00\x00\x00"
#define MAGIC_SIZE (sizeof(MAGIC)-1)
#define EXTENSION ".mul"

enum {
    MUL_BLOCK_SIZE = 128,
    MUL_INDEX_LENGTH = 64,
    MUL_INDEX_SIZE = 3*MUL_BLOCK_SIZE,
    MUL_STRING_SIZE = 20
};

typedef enum {
    MUL_MODE_HEIGHT    = 0,
    MUL_MODE_CURRENT   = 1,
    MUL_MODE_V_X_I_Y   = 2,
    MUL_MODE_DI_DZ     = 3,
    MUL_MODE_Z_I_TIME  = 4,
    MUL_MODE_V_Y       = 5,
    MUL_MODE_I_Y       = 6,
    MUL_MODE_DIFFERENT = 7,
    MUL_MODE_VOLTAGE   = 8,
    MUL_MODE_NMODES
} MulModeType;

typedef struct {
    gint id;
    gsize addr;   /* measured in blocks! */
} MulIndexEntry;

typedef struct {
    gint id;
    gint size;   /* In blocks */
    gint xres, yres, zres;
    gint year, month, day, hour, minute, second;
    gint xdim, ydim;    /* In Angström */
    gint xoff, yoff;    /* In Angström */
    gint zscale;   /* In Volts */
    gint tilt;
    gint speed;
    gint bias;
    gint current;
    gchar sample[MUL_STRING_SIZE+1];
    gchar title[MUL_STRING_SIZE+1];
    gint postpr, postd1;   /* ??? */
    MulModeType mode;
    gint curr_factor;
    gint n_point_scans;
    gint unitnr;
    gint version;
    /* They bear some information, sometimes. */
    gint spare_48;
    gint spare_49;
    gint spare_50;
    gint spare_51;
    gint spare_52;
    gint spare_53;
    gint spare_54;
    gint spare_55;
    gint spare_56;
    gint spare_57;
    gint spare_58;
    gint spare_59;
    gint spare_60;
    gint spare_61;
    gint spare_62;
    gint spare_63;
} MulImageLabel;

static gboolean      module_register     (void);
static gint          mul_detect          (const GwyFileDetectInfo *fileinfo,
                                          gboolean only_name);
static GwyContainer* mul_load            (const gchar *filename,
                                          GwyRunType mode,
                                          GError **error);
static gint          mul_read_index      (const guchar *p,
                                          gsize size,
                                          MulIndexEntry *image_index);
static gboolean      mul_read_image_label(const guchar *buffer,
                                          gsize size,
                                          const MulIndexEntry *entry,
                                          MulImageLabel *label,
                                          GError **error);
static void          mul_read_image      (GwyContainer *container,
                                          const guchar *buffer,
                                          const MulIndexEntry *entry,
                                          const MulImageLabel *label,
                                          const gchar *filename);
static GwyContainer* mul_get_meta        (const MulImageLabel *label);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Aarhus MUL files."),
    "Yeti <yeti@gwyddion.net>",
    "0.2",
    "David Nečas (Yeti)",
    "2011",
};

GWY_MODULE_QUERY2(module_info, mulfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("mulfile",
                           N_("Aarhus MUL files (.mul)"),
                           (GwyFileDetectFunc)&mul_detect,
                           (GwyFileLoadFunc)&mul_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
mul_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 10 : 0;

    if (fileinfo->file_size > 3*MUL_INDEX_SIZE
        && fileinfo->file_size % MUL_BLOCK_SIZE == 0
        && memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) == 0)
        return 100;

    return 0;
}

static GwyContainer*
mul_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    gsize size;
    GError *err = NULL;
    gint i, n;
    MulIndexEntry image_index[MUL_INDEX_LENGTH];

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        goto fail;
    }

    if (size % MUL_BLOCK_SIZE != 0 || size < 3*MUL_INDEX_SIZE) {
        err_FILE_TYPE(error, "Aarhus MUL");
        goto fail;
    }

    n = mul_read_index(buffer, size, image_index);
    if (!n) {
        err_NO_DATA(error);
        goto fail;
    }

    container = gwy_container_new();

    for (i = 0; i < n; i++) {
        MulImageLabel label;
        if (!mul_read_image_label(buffer, size, image_index + i, &label, NULL))
            continue;

        mul_read_image(container, buffer, image_index + i, &label, filename);
    }

    if (!gwy_container_get_n_items(container)) {
        err_NO_DATA(error);
        GWY_OBJECT_UNREF(container);
    }

fail:
    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

static gint
mul_read_index(const guchar *p,
               gsize size,
               MulIndexEntry *image_index)
{
    gboolean first_zero = TRUE;
    gint i, n = 0;

    for (i = 0; i < MUL_INDEX_LENGTH; i++) {
        image_index[n].id = gwy_get_gint16_le(&p);
        image_index[n].addr = gwy_get_gint32_le(&p);
        gwy_debug("%u 0x%08lx", image_index[n].id, (gulong)image_index[n].addr);
        if (image_index[n].id) {
            if (image_index[n].addr >= size/MUL_BLOCK_SIZE
                || image_index[n].addr < 3) {
                g_warning("Address of block %u is invalid.",
                          image_index[n].id);
            }
            else
                n++;
        }
        else if (first_zero) {
            /* The first zero-id entry contains the file size, apparently. */
            if (image_index[n].addr
                && MUL_BLOCK_SIZE * image_index[n].addr != size)
                g_warning("The sentinel zero-id address is 0x%08lx "
                          "but we would expect 0x%08lx.",
                          (gulong)image_index[n].addr,
                          (gulong)(size/MUL_BLOCK_SIZE));
            first_zero = FALSE;
        }
    }

    return n;
}

static gboolean
mul_read_image_label(const guchar *buffer,
                     gsize size,
                     const MulIndexEntry *entry,
                     MulImageLabel *label,
                     GError **error)
{
    const guchar *p = buffer + entry->addr * MUL_BLOCK_SIZE;
    gint len;

    label->id = gwy_get_gint16_le(&p);
    if (label->id != entry->id) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Image number in the label %u does not match "
                      "the number %u in the index."),
                    label->id, entry->id);
        return FALSE;
    }
    label->size = gwy_get_gint16_le(&p);
    gwy_debug("[%u] size: %u", label->id, label->size);
    if (label->size < 2 || entry->addr + label->size > size/MUL_BLOCK_SIZE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Image data are outside the file."));
        return FALSE;
    }
    label->xres = gwy_get_gint16_le(&p);
    label->yres = gwy_get_gint16_le(&p);
    label->zres = gwy_get_gint16_le(&p);
    gwy_debug("[%u] xres: %u, yres: %u, zres: %u",
              label->id, label->xres, label->yres, label->zres);
    if (err_DIMENSION(error, label->xres) || err_DIMENSION(error, label->yres))
        return FALSE;
    /* The extra MUL_BLOCK_SIZE is for the label itself. */
    if (err_SIZE_MISMATCH(error, 2*label->xres * label->yres + MUL_BLOCK_SIZE,
                          label->size*MUL_BLOCK_SIZE, FALSE))
        return FALSE;
    label->year = gwy_get_gint16_le(&p);
    label->month = gwy_get_gint16_le(&p);
    label->day = gwy_get_gint16_le(&p);
    label->hour = gwy_get_gint16_le(&p);
    label->minute = gwy_get_gint16_le(&p);
    label->second = gwy_get_gint16_le(&p);
    gwy_debug("[%u] %u-%u-%u %u:%u:%u",
              label->id,
              label->year, label->month, label->day,
              label->hour, label->minute, label->second);
    label->xdim = gwy_get_gint16_le(&p);
    label->ydim = gwy_get_gint16_le(&p);
    gwy_debug("[%u] (%u, %u)", label->id, label->xdim, label->ydim);
    /* Use negated positive conditions to catch NaNs */
    if (!label->xdim) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        label->xdim = 1;
    }
    if (!label->ydim) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        label->ydim = 1;
    }
    label->xoff = gwy_get_gint16_le(&p);
    label->yoff = gwy_get_gint16_le(&p);
    label->zscale = gwy_get_gint16_le(&p);
    gwy_debug("[%u] zscale: %u", label->id, label->zscale);
    label->tilt = gwy_get_gint16_le(&p);
    label->speed = gwy_get_gint16_le(&p);
    label->bias = gwy_get_gint16_le(&p);
    label->current = gwy_get_gint16_le(&p);
    gwy_debug("[%u] tilt: %u, speed: %u, bias: %d, current: %d",
              label->id, label->tilt, label->speed, label->bias, label->current);

    len = *(p++);
    if (len > MUL_STRING_SIZE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Label string length %u is larger than 20."), len);
        return FALSE;
    }
    get_CHARS(label->sample, &p, MUL_STRING_SIZE);
    label->sample[MUL_STRING_SIZE] = '\0';

    len = *(p++);
    if (len > MUL_STRING_SIZE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Label string length %u is larger than 20."), len);
        return FALSE;
    }
    get_CHARS(label->title, &p, MUL_STRING_SIZE);
    label->title[MUL_STRING_SIZE] = '\0';

    gwy_debug("[%u] sample: <%s>, title: <%s>",
              label->id, label->sample, label->title);

    label->postpr = gwy_get_gint16_le(&p);
    label->postd1 = gwy_get_gint16_le(&p);
    label->mode = gwy_get_gint16_le(&p);
    gwy_debug("[%u] mode: %u", label->id, label->mode);
    label->curr_factor = gwy_get_gint16_le(&p);
    label->n_point_scans = gwy_get_gint16_le(&p);
    gwy_debug("[%u] n_point_scans: %u", label->id, label->n_point_scans);
    if (label->n_point_scans)
        g_warning("FIXME: n_point_scans > 0, so there's more data somewhere.");
    label->unitnr = gwy_get_gint16_le(&p);
    label->version = gwy_get_gint16_le(&p);

    label->spare_48 = gwy_get_gint16_le(&p);
    label->spare_49 = gwy_get_gint16_le(&p);
    label->spare_50 = gwy_get_gint16_le(&p);
    label->spare_51 = gwy_get_gint16_le(&p);
    label->spare_52 = gwy_get_gint16_le(&p);
    label->spare_53 = gwy_get_gint16_le(&p);
    label->spare_54 = gwy_get_gint16_le(&p);
    label->spare_55 = gwy_get_gint16_le(&p);
    label->spare_56 = gwy_get_gint16_le(&p);
    label->spare_57 = gwy_get_gint16_le(&p);
    label->spare_58 = gwy_get_gint16_le(&p);
    label->spare_59 = gwy_get_gint16_le(&p);
    label->spare_60 = gwy_get_gint16_le(&p);
    label->spare_61 = gwy_get_gint16_le(&p);
    label->spare_62 = gwy_get_gint16_le(&p);
    label->spare_63 = gwy_get_gint16_le(&p);

    return TRUE;
}

static void
mul_read_image(GwyContainer *container,
               const guchar *buffer,
               const MulIndexEntry *entry,
               const MulImageLabel *label,
               const gchar *filename)
{
    const gint16 *d16 = (const gint16*)(buffer
                                        + (entry->addr + 1)* MUL_BLOCK_SIZE);
    const guint16 *u16 = (const guint16*)(buffer
                                          + (entry->addr + 1)* MUL_BLOCK_SIZE);
    GwyDataField *field;
    GwyContainer *meta;
    gdouble *data;
    gdouble q_height, q_current, q_voltage, q = 1.0/32768;
    guint i, j;
    guchar key[64], *title;

    /* XXX: The specs say lenght unit is 0.1 Å but that does not seem right. */
    field = gwy_data_field_new(label->xres, label->yres,
                               Angstrom*label->xdim,
                               Angstrom*label->ydim,
                               FALSE);
    /*
    gwy_data_field_set_xoffset(field, Angstrom*label->xoff);
    gwy_data_field_set_yoffset(field, Angstrom*label->yoff);
    */

    //q_height = -0.5 * 0.136*Angstrom * label->zscale/200.0;
    q_height = -0.1/1.36*Angstrom * label->zscale/200.0;
    q_current = 1.0/32768 * label->curr_factor * 10*Nano;
    /* q_voltage = -1.0/32768 * ((label->spare_61 >= 4) ? 0.05 : 0.025); */
    q_voltage = -10.0/32768;

    if (label->mode == MUL_MODE_HEIGHT || label->mode == MUL_MODE_DIFFERENT) {
        q = q_height;
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(field), "m");
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(field), "m");
    }
    else if (label->mode == MUL_MODE_CURRENT) {
        q = q_current;
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(field), "m");
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(field), "A");
    }
    else if (label->mode == MUL_MODE_V_X_I_Y) {
        gint xmin = label->spare_50, xmax = label->spare_51,
             ymin = label->spare_48, ymax = label->spare_49;

        q = q_height;
        gwy_data_field_set_xreal(field, MAX(abs(xmax - xmin), 1)*q_voltage);
        gwy_data_field_set_xoffset(field, xmin*q_voltage);
        gwy_data_field_set_yreal(field, MAX(abs(ymax - ymin), 1)*q_current);
        gwy_data_field_set_yoffset(field, ymin*q_voltage);
        /* No xy units, to reduce confusion as we cannot make the different. */
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(field), "m");
    }
    else if (label->mode == MUL_MODE_DI_DZ) {
        /* They say dZ is in spare54 but have I no idea how to use it. */
    }
    else if (label->mode == MUL_MODE_V_Y) {
        gint ymin = label->spare_48, ymax = label->spare_49;

        q = q_height;
        gwy_data_field_set_yreal(field, MAX(abs(ymax - ymin), 1)*q_voltage);
        gwy_data_field_set_yoffset(field, ymin*q_voltage);
        /* No xy units, to reduce confusion as we cannot make the different. */
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(field), "m");
    }
    else if (label->mode == MUL_MODE_I_Y) {
        gint ymin = label->spare_48, ymax = label->spare_49;

        q = q_height;
        gwy_data_field_set_yreal(field, MAX(abs(ymax - ymin), 1)*q_current);
        gwy_data_field_set_yoffset(field, ymin*q_current);
        /* No xy units, to reduce confusion as we cannot make the different. */
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(field), "m");
    }
    else if (label->mode == MUL_MODE_VOLTAGE) {
        q = q_voltage;
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(field), "m");
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(field), "V");
    }

    data = gwy_data_field_get_data(field);
    for (i = 0; i < label->yres; i++) {
        guint ib = (label->yres-1 - i)*label->xres;
        if (label->mode == MUL_MODE_CURRENT) {
            for (j = 0; j < label->xres; j++) {
                gint16 v = GUINT16_FROM_LE(u16[i*label->xres + j]);
                data[ib + j] = q*v;
            }
        }
        else {
            for (j = 0; j < label->xres; j++) {
                gint16 v = GINT16_FROM_LE(d16[i*label->xres + j]);
                data[ib + j] = q*v;
            }
        }
    }

    g_snprintf(key, sizeof(key), "/%d/data", label->id);
    gwy_container_set_object_by_name(container, key, field);
    g_object_unref(field);

    g_snprintf(key, sizeof(key), "/%d/data/title", label->id);
    title = g_strdup_printf("%s, %s (%u)",
                            label->sample, label->title, label->id);
    gwy_container_set_string_by_name(container, key, title);

    g_snprintf(key, sizeof(key), "/%d/meta", label->id);
    meta = mul_get_meta(label);
    gwy_container_set_object_by_name(container, key, meta);
    g_object_unref(meta);

    gwy_app_channel_check_nonsquare(container, label->id);
    gwy_file_channel_import_log_add(container, label->id, NULL, filename);
}

static GwyContainer*
mul_get_meta(const MulImageLabel *label)
{
    GwyContainer *meta = gwy_container_new();

    gwy_container_set_string_by_name
        (meta, "Date", g_strdup_printf("%u-%02u-%02u %02u:%02u:%02u",
                                       label->year, label->month, label->day,
                                       label->hour, label->minute,
                                       label->second));
    gwy_container_set_string_by_name
        (meta, "X-Offset", g_strdup_printf("%g nm", label->xoff/10.0));
    gwy_container_set_string_by_name
        (meta, "Y-Offset", g_strdup_printf("%g nm", label->yoff/10.0));
    gwy_container_set_string_by_name
        (meta, "Z-Scale", g_strdup_printf("%u V", label->zscale));
    gwy_container_set_string_by_name
        (meta, "Tilt", g_strdup_printf("%u deg", label->tilt));
    gwy_container_set_string_by_name
        (meta, "Scan duration", g_strdup_printf("%g s", label->speed/100.0));
    gwy_container_set_string_by_name
        (meta, "Bias", g_strdup_printf("%g V", -10.0*label->bias/32768.0));
    gwy_container_set_string_by_name
        (meta, "Current", g_strdup_printf("%g nA", label->current/100.0));
    gwy_container_set_string_by_name
        (meta, "Bias (corrected)",
         g_strdup_printf("%g V",
                         -10.0*label->bias/32768.0 - 1e-6*label->current));
    gwy_container_set_string_by_name
        (meta, "Postprocessing", g_strdup_printf("%u", label->postpr));
    gwy_container_set_string_by_name
        (meta, "Current factor", g_strdup_printf("%u", label->curr_factor));
    gwy_container_set_string_by_name
        (meta, "Mode", g_strdup_printf("%u", label->mode));
    gwy_container_set_string_by_name
        (meta, "R_Nr", g_strdup_printf("%u", label->n_point_scans));
    gwy_container_set_string_by_name
        (meta, "Unit number", g_strdup_printf("%u", label->unitnr));
    gwy_container_set_string_by_name
        (meta, "Version", g_strdup_printf("%u", label->version));
    gwy_container_set_string_by_name
        (meta, "Gain", g_strdup_printf("%u", label->spare_60));
    gwy_container_set_string_by_name
        (meta, "STM", g_strdup_printf("%u", label->spare_61));

    return meta;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
