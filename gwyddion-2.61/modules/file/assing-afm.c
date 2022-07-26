/*
 *  $Id: assing-afm.c 22667 2019-11-10 10:26:25Z yeti-dn $
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
 * [FILE-MAGIC-USERGUIDE]
 * Assing AFM
 * .afm
 * Read Export
 **/

#include "config.h"
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

#define EXTENSION ".afm"

#define Angstrom (1e-10)

typedef struct {
    guint res;
    gdouble real;
    gdouble range;
} AFMFile;

static gboolean      module_register(void);
static gint          aafm_detect    (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name);
static GwyContainer* aafm_load      (const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);
static gboolean      aafm_export    (GwyContainer *data,
                                     const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Assing AFM data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.18",
    "David Nečas (Yeti) & Petr Klapetek",
    "2005",
};

GWY_MODULE_QUERY2(module_info, assing_afm)

static gboolean
module_register(void)
{
    gwy_file_func_register("assing-afm",
                           N_("Assing AFM files (.afm)"),
                           (GwyFileDetectFunc)&aafm_detect,
                           (GwyFileLoadFunc)&aafm_load,
                           NULL,
                           (GwyFileSaveFunc)&aafm_export);

    return TRUE;
}

static gint
aafm_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    gint score = 0;
    guint res;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 12 : 0;

    if (fileinfo->buffer_len >= 12
        && (res = ((guint)fileinfo->head[1] << 8 | fileinfo->head[0]))
        && fileinfo->file_size == 2*res*res + 10)
        score = 90;

    return score;
}

static GwyContainer*
aafm_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    GwySIUnit *unit;
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    const guchar *p;
    gsize size = 0;
    GError *err = NULL;
    AFMFile afmfile;
    GwyDataField *dfield, *rawfield;
    gdouble min, max;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (size < 12) {
        err_TOO_SHORT(error);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    p = buffer;
    afmfile.res = gwy_get_guint16_le(&p);
    if (err_DIMENSION(error, afmfile.res)) {
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }
    if (err_SIZE_MISMATCH(error, afmfile.res * afmfile.res + 10, size, FALSE)) {
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    afmfile.real = Angstrom*gwy_get_gfloat_le(&p);
    if (!(afmfile.real = fabs(afmfile.real))) {
        g_warning("Real size is 0.0, fixing to 1.0");
        afmfile.real = 1.0;
    }

    rawfield = gwy_data_field_new(afmfile.res, afmfile.res,
                                  afmfile.real, afmfile.real,
                                  FALSE);
    gwy_convert_raw_data(p, afmfile.res*afmfile.res, 1,
                         GWY_RAW_DATA_SINT16, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         gwy_data_field_get_data(rawfield), 1.0, 0.0);
    dfield = gwy_data_field_new_rotated_90(rawfield, FALSE);
    g_object_unref(rawfield);
    p += 2*afmfile.res*afmfile.res;

    afmfile.range = gwy_get_gfloat_le(&p);
    gwy_data_field_get_min_max(dfield, &min, &max);
    if (min == max)
        gwy_data_field_clear(dfield);
    else
        gwy_data_field_multiply(dfield, afmfile.range/(max - min)*Angstrom);

    unit = gwy_si_unit_new("m");
    gwy_data_field_set_si_unit_xy(dfield, unit);
    g_object_unref(unit);

    unit = gwy_si_unit_new("m");
    gwy_data_field_set_si_unit_z(dfield, unit);
    g_object_unref(unit);

    container = gwy_container_new();
    gwy_container_set_object_by_name(container, "/0/data", dfield);
    gwy_container_set_string_by_name(container, "/0/data/title",
                                     g_strdup("Topography"));
    g_object_unref(dfield);

    gwy_file_channel_import_log_add(container, 0, NULL, filename);

    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

static gboolean
aafm_export(G_GNUC_UNUSED GwyContainer *data,
            const gchar *filename,
            G_GNUC_UNUSED GwyRunType mode,
            GError **error)
{
    union { guchar pp[4]; float f; } z;
    guint16 res, r;
    gint16 *x;
    gint16 v;
    gint i, j, xres, yres, n;
    GwyDataField *dfield;
    const gdouble *d;
    gdouble min, max, q, z0;
    FILE *fh;
    gboolean ok = TRUE;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield, 0);
    if (!dfield) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    d = gwy_data_field_get_data_const(dfield);

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    res = MIN(MIN(xres, yres), 32767);
    n = (gint)res*(gint)res;
    r = GUINT16_TO_LE(res);
    fwrite(&res, 1, sizeof(r), fh);

    gwy_data_field_get_min_max(dfield, &min, &max);
    if (min == max) {
        q = 0.0;
        z0 = 0.0;
    }
    else {
        q = 65533.0/(max - min);
        z0 = -32766.5*(max + min)/(max - min);
    }
    z.f = MIN(gwy_data_field_get_xreal(dfield),
              gwy_data_field_get_yreal(dfield))/Angstrom;
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    GWY_SWAP(guchar, z.pp[0], z.pp[3]);
    GWY_SWAP(guchar, z.pp[1], z.pp[2]);
#endif
    fwrite(&z, 1, sizeof(z), fh);

    x = g_new(gint16, n);
    for (i = 0; i < res; i++) {
        for (j = 0; j < res; j++) {
            v = GWY_ROUND(d[(res-1 - j)*res + i]*q + z0);
            x[i*res + j] = GINT16_TO_LE(v);
        }
    }

    if (!(ok = (fwrite(x, 1, 2*n, fh) == 2*n))) {
        err_WRITE(error);
        g_unlink(filename);
    }
    else {
        z.f = (max - min)/Angstrom;
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
        GWY_SWAP(guchar, z.pp[0], z.pp[3]);
        GWY_SWAP(guchar, z.pp[1], z.pp[2]);
#endif
        fwrite(&z, 1, sizeof(z), fh);
    }

    fclose(fh);
    g_free(x);

    return ok;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
