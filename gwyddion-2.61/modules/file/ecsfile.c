/*
 *  $Id: ecsfile.c 20674 2017-12-18 17:58:47Z yeti-dn $
 *  Copyright (C) 2006 David Necas (Yeti), Petr Klapetek, Markus Pristovsek
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net,
 *  prissi@gift.physik.tu-berlin.de.
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
 * <mime-type type="application/x-ecs-spm">
 *   <comment>ECS SPM data</comment>
 *   <magic priority="50">
 *     <match type="string" offset="0" value="\xa0\x00\x00"/>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * ECS
 * .img
 * Read
 **/

#include "config.h"
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "get.h"

/* Not a real magic header, but filters out most non-ECS files */
#define MAGIC "\xa0\x00\x00"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define EXTENSION ".img"

enum { HEADER_SIZE = 830 };

enum {
    ECS_RESOLUTION = 0x2,
    ECS_DATE = 0x9c,
    ECS_TIME = 0xeb,
    ECS_COMMENT = 0x19c,
    ECS_CHANNEL = 0x29a,
    ECS_PARAMS = 0x2c3,
    ECS_SCAN_SIZE = 0x2ec
};

static gboolean      module_register(void);
static gint          ecs_detect     (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name);
static GwyContainer* ecs_load       (const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);
static gboolean      get_scan_size  (const gchar *s,
                                     gdouble *xreal,
                                     gdouble *q,
                                     guchar *c);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports ECS IMG files."),
    "Yeti <yeti@gwyddion.net>",
    "0.7",
    "David Nečas (Yeti) & Petr Klapetek & Markus Pristovsek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, ecsfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("ecsfile",
                           N_("ECS files (.img)"),
                           (GwyFileDetectFunc)&ecs_detect,
                           (GwyFileLoadFunc)&ecs_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
ecs_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    guint xres, yres;
    const guchar *p;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 10 : 0;

    if (fileinfo->buffer_len < ECS_RESOLUTION + 2*2
        || fileinfo->file_size < HEADER_SIZE + 2
        || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    /* Check if file size matches */
    p = fileinfo->head + ECS_RESOLUTION;
    xres = gwy_get_guint16_le(&p);
    yres = gwy_get_guint16_le(&p);

    if (fileinfo->file_size != 2*xres*yres + HEADER_SIZE)
        return 0;

    return 100;
}

static GwyContainer*
ecs_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *meta, *container = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    gchar *s = NULL, *s2 = NULL;
    GwySIUnit *siunit;
    const guchar *p;
    gdouble *data, *row;
    guint xres, yres, i, j;
    gdouble xreal, q;
    const gint16 *pdata;
    guchar c;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (size < HEADER_SIZE + 2) {
        err_TOO_SHORT(error);
        goto fail;
    }

    p = buffer + ECS_RESOLUTION;
    xres = gwy_get_guint16_le(&p);
    yres = gwy_get_guint16_le(&p);
    gwy_debug("xres: %u, yres: %u", xres, yres);
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        goto fail;
    if (err_SIZE_MISMATCH(error, HEADER_SIZE + 2*xres*yres, size, TRUE))
        goto fail;

    /* Scan size */
    p = buffer + ECS_SCAN_SIZE;
    s = get_PASCAL_STRING(&p, HEADER_SIZE - ECS_SCAN_SIZE);
    if (!s) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Scan size header field overlaps with data."));
        goto fail;
    }
    gwy_debug("Scan size str: <%s>", s);
    if (!g_str_has_prefix(s, "Scan Size: ")) {
        err_FILE_TYPE(error, "ECS");
        goto fail;
    }
    if (!get_scan_size(s + strlen("Scan Size: "), &xreal, &q, &c)) {
        err_INVALID(error, "Scan Size");
        goto fail;
    }
    g_free(s);
    s = NULL;
    gwy_debug("xreal: %g q: %g unit: %s",
              xreal, q, c == 0x8f ? "Angstrom" : "Nanometer");

    /* Use negated positive conditions to catch NaNs */
    if (!((xreal = fabs(xreal)) > 0)) {
        g_warning("Real size is 0.0, fixing to 1.0");
        xreal = 1.0;
    }

    if (c == 0x8f) {
        xreal *= 1e-10;
        q *= 1e-10;
    }
    else {
        xreal *= 1e-9;
        q *= 1e-9;
    }
    q /= 65536.0;

    /* This does not make much sense when xres != yres, but it is what
     * Snomputz does. */
    dfield = gwy_data_field_new(xres, yres, xreal, xreal, FALSE);
    data = gwy_data_field_get_data(dfield);
    pdata = (const gint16*)(buffer + HEADER_SIZE);
    for (i = 0; i < yres; i++) {
        row = data + (yres-1 - i)*xres;
        for (j = 0; j < xres; j++)
            row[j] = GINT16_FROM_LE(pdata[i*xres + j])*q;
    }

    siunit = gwy_si_unit_new("m");
    gwy_data_field_set_si_unit_xy(dfield, siunit);
    g_object_unref(siunit);

    siunit = gwy_si_unit_new("m");
    gwy_data_field_set_si_unit_z(dfield, siunit);
    g_object_unref(siunit);

    container = gwy_container_new();
    gwy_container_set_object_by_name(container, "/0/data", dfield);

    /* Channel title */
    p = buffer + ECS_CHANNEL;
    s = get_PASCAL_STRING(&p, HEADER_SIZE - ECS_CHANNEL);
    if (!s || !*s)
        s = g_strdup("Topography");
    gwy_container_set_string_by_name(container, "/0/data/title", s);
    s = NULL;

    meta = gwy_container_new();

    /* Date & time */
    p = buffer + ECS_DATE;
    s = get_PASCAL_STRING(&p, HEADER_SIZE - ECS_DATE);
    if (s) {
        p = buffer + ECS_TIME;
        s2 = get_PASCAL_STRING(&p, HEADER_SIZE - ECS_TIME);
        if (s2) {
            gwy_container_set_string_by_name(meta, "Date",
                                             g_strconcat(s, " ", s2, NULL));
            g_free(s2);
            s2 = NULL;
        }
        g_free(s);
        s = NULL;
    }

    /* Channel title */
    p = buffer + ECS_CHANNEL;
    s = get_PASCAL_STRING(&p, HEADER_SIZE - ECS_CHANNEL);
    if (s && *s) {
        gwy_container_set_string_by_name(meta, "Comment", s);
        s = NULL;
    }

    if (gwy_container_get_n_items(meta))
        gwy_container_set_object_by_name(container, "/0/meta", meta);
    g_object_unref(meta);

    gwy_file_channel_import_log_add(container, 0, NULL, filename);

fail:
    g_free(s);
    g_free(s2);
    GWY_OBJECT_UNREF(dfield);
    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

static gboolean
get_scan_size(const gchar *s,
              gdouble *xreal, gdouble *q, guchar *c)
{
    gchar *end;

    *xreal = g_ascii_strtod(s, &end);
    if (end == s)
        return FALSE;
    s = end;
    *q = g_ascii_strtod(s, &end);
    if (end == s)
        return FALSE;
    s = end;
    if (!s[0])
        return FALSE;
    *c = s[0];
    return TRUE;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

