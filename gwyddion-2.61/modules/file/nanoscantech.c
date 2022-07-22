/*
 *  $Id: nanoscantech.c 22562 2019-10-10 14:45:41Z yeti-dn $
 *  Copyright (C) 2012-2019 David Necas (Yeti), Daniil Bratashov (dn2010).
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

/*
 * TODO: assuming cp1251 as 8bit encoding,
 *       4d xy ranges/units/labels;
 *       4d data loading improvements for jumping mode
 */

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-nanoscantech-spm">
 *   <comment>NanoScanTech SPM data</comment>
 *   <glob pattern="*.nstdat"/>
 *   <glob pattern="*.NSTDAT"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # NanoScanTech
 * # A ZIP archive, we have to look for 0.lsdlsd as the first file.
 * 0 string PK\x03\x04
 * >30 string 0.lsdlsd NanoScanTech NSTDAT SPM data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * NanoScanTech
 * .nstdat
 * Read SPS Volume
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"
#include "gwyzip.h"

#define MAGIC "PK\x03\x04"
#define MAGIC_SIZE (sizeof(MAGIC)-1)
#define MAGIC1 "lsdlsd"
#define MAGIC1_SIZE (sizeof(MAGIC1)-1)
#define EXTENSION ".nstdat"
#define NST4DHEADER_SIZE (4 + 4 + 4 + 4 + 14 * 8)
#define UTF8_BOM "\xEF\xBB\xBF"

typedef enum {
    Vertical   = 0,
    Horizontal = 1
} NSTDirection;

typedef enum {
    BottomLeft  = 0,
    BottomRight = 1,
    TopLeft     = 2,
    TopRight    = 3
} NSTStartPoint;

typedef struct {
    NSTDirection direction; /* scan direction */
    NSTStartPoint startpoint; /* scan beginning position */
    guint nx; /* pixel numbers */
    guint ny;
    gdouble xmin; /* scan positions, m */
    gdouble xmax;
    gdouble ymin;
    gdouble ymax;
    gdouble minforf; /* convolution apply borders */
    gdouble maxforf;
    gdouble minforrec; /* spectrum position on matrix */
    gdouble maxforrec;
    gdouble laserwl; /* nm */
    gdouble centerwl; /* nm */
    gdouble dispersion; /* nm/mm */
    gdouble pixelxsize; /* mm */
    gdouble numpixels;
    gdouble centralpixel;
} NST4DHeader;

static gboolean       module_register     (void);
static gint           nst_detect          (const GwyFileDetectInfo *fileinfo,
                                           gboolean only_name);
static GwyContainer*  nst_load            (const gchar *filename,
                                           GwyRunType mode,
                                           GError **error);
static GwyDataField*  nst_read_3d         (const gchar *buffer,
                                           gsize size,
                                           gboolean is_utf,
                                           GwyContainer **metadata,
                                           gchar **title);
static GwyGraphModel* nst_read_2d         (const gchar *buffer,
                                           guint channel,
                                           gboolean is_utf);
static GwyBrick*      nst_read_4d         (const gchar *buffer,
                                           gsize datasize,
                                           gboolean is_utf,
                                           GwyContainer **metadata,
                                           gchar **title);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports NanoScanTech .nstdat files."),
    "Daniil Bratashov (dn2010@gmail.com)",
    "0.15",
    "David Nečas (Yeti), Daniil Bratashov (dn2010), Antony Kikaxa",
    "2012",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_file_func_register("nanoscantech",
                           N_("NanoScanTech data (.nstdat)"),
                           (GwyFileDetectFunc)&nst_detect,
                           (GwyFileLoadFunc)&nst_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
nst_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    GwyZipFile zipfile;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 15 : 0;

    /* Generic ZIP file. */
    if (fileinfo->file_size < MAGIC_SIZE
        || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    /* It contains directory Scan so this should be somewhere near
     * the begining of the file. */
    if (!gwy_memmem(fileinfo->head, fileinfo->buffer_len, MAGIC1, MAGIC1_SIZE))
        return 0;

    /* We have to realy look inside. */
    if (!(zipfile = gwyzip_open(fileinfo->name, NULL)))
        return 0;

    if (!gwyzip_locate_file(zipfile, "0.lsdlsd", 1, NULL)) {
        gwyzip_close(zipfile);
        return 0;
    }

    gwyzip_close(zipfile);

    return 100;
}

static GwyContainer*
nst_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *container = NULL, *metadata = NULL;
    GwyDataField *dfield;
    GwyGraphModel *gmodel;
    GwyBrick *brick;
    GwyZipFile zipfile;
    guint channelno = 0;
    gboolean status, is_utf;
    gchar *buffer, *line, *p, *title, *filename_curr;
    gchar *titlestr = NULL;
    gsize size = 0;
    GQuark quark;

    if (!(zipfile = gwyzip_open(filename, error)))
        return NULL;

    status = gwyzip_first_file(zipfile, error);
    if (!status)
        goto fail;

    container = gwy_container_new();
    while (status && gwyzip_get_current_filename(zipfile, &filename_curr,
                                                 NULL)) {
        if (g_str_has_suffix(filename_curr, ".lsdlsd")) {
            gwy_debug("channel %d: %s", channelno, filename_curr);
            buffer = gwyzip_get_file_content(zipfile, &size, error);
            if (!buffer) {
                g_free(filename_curr);
                goto fail;
            }
            p = buffer;
            line = gwy_str_next_line(&p);
            is_utf = g_str_has_prefix(line, UTF8_BOM);
            if (is_utf) {
                gwy_debug("UTF8 file content");
                line += 3;
            }
            g_strstrip(line);
            if (gwy_strequal(line, "3d")) {
                gwy_debug("3d: %u", channelno);
                titlestr = NULL;
                dfield = nst_read_3d(p, size, is_utf, &metadata, &titlestr);
                if (dfield) {
                    quark = gwy_app_get_data_key_for_id(channelno);
                    gwy_container_set_object(container, quark, dfield);
                    g_object_unref(dfield);
                    if (metadata) {
                        quark = gwy_app_get_data_meta_key_for_id(channelno);
                        gwy_container_set_object(container, quark, metadata);
                        g_object_unref(metadata);
                    }
                    quark = gwy_app_get_data_title_key_for_id(channelno);
                    if (!titlestr)
                        title = g_strdup_printf("Channel %u", channelno);
                    else
                        title = g_strdup_printf("%s (%u)", titlestr, channelno);
                    gwy_container_set_string(container, quark, title);

                    gwy_file_channel_import_log_add(container, channelno,
                                                    NULL, filename);
                }
                if (titlestr) {
                    g_free(titlestr);
                    titlestr = NULL;
                }
            }
            else if (gwy_strequal(line, "2d")) {
                gwy_debug("2d: %d", channelno);
                gmodel = nst_read_2d(p, channelno, is_utf);
                if (gmodel) {
                    quark = gwy_app_get_graph_key_for_id(channelno+1);
                    gwy_container_set_object(container, quark, gmodel);
                    g_object_unref(gmodel);
                }
            }
            else if (gwy_strequal(line, "4d")) {
                gwy_debug("4d: %u", channelno);
                brick = nst_read_4d(p, size, is_utf,
                                    &metadata, &titlestr);
                if (brick) {
                    quark = gwy_app_get_brick_key_for_id(channelno);
                    gwy_container_set_object(container, quark, brick);
                    quark = gwy_app_get_brick_title_key_for_id(channelno);
                    if (!titlestr)
                        title = g_strdup_printf("Channel %u", channelno);
                    else
                        title = g_strdup_printf("%s (%u)", titlestr, channelno);
                    gwy_container_set_string(container, quark, title);
                    if (metadata) {
                        quark = gwy_app_get_brick_meta_key_for_id(channelno);
                        gwy_container_set_object(container, quark, metadata);
                        g_object_unref(metadata);
                    }
                    g_object_unref(brick);
                    gwy_file_volume_import_log_add(container, channelno,
                                                   NULL, filename);
                }
                if (titlestr) {
                    g_free(titlestr);
                    titlestr = NULL;
                }
            }

            g_free(buffer);
            channelno++;
        }
        status = gwyzip_next_file(zipfile, NULL);
        g_free(filename_curr);
    }

fail:
    gwyzip_close(zipfile);
    if (!channelno) {
        GWY_OBJECT_UNREF(container);
        if (error && !*error)
            err_NO_DATA(error);
    }

    return container;
}

static gchar**
split_to_nparts(const gchar *str, const gchar *sep, guint n)
{
    gchar **parts;

    parts = g_strsplit(str, sep, n);
    if (g_strv_length(parts) != n) {
        g_strfreev(parts);
        return NULL;
    }
    return parts;
}

static inline gchar*
decode_string(const gchar *s, gboolean is_utf)
{
    return g_convert(s, -1, "UTF-8", is_utf ? "UTF-8" : "cp1251",
                     NULL, NULL, NULL);
}

static GwyDataField *
nst_read_3d(const gchar *buffer, gsize size, gboolean is_utf,
            GwyContainer **metadata, gchar **title)
{
    GwyDataField *dfield = NULL;
    GwyContainer *meta = NULL;
    GwySIUnit *siunitxy = NULL, *siunitz = NULL;
    gchar *p, *line, *unit, *key, *value;
    gchar **lineparts, **attributes;
    gint x, y, xmax = 0, ymax = 0, xres = 1, yres = 1, i, j;
    gint power10xy = 1, power10z = 1;
    gdouble z;
    gdouble xscale = 1.0, yscale = 1.0;
    gdouble xoffset = 0.0, yoffset = 0.0;
    GArray *dataarray;
    gint linecur;
    gboolean is_binary = FALSE;
    const guchar *pb;

    p = (gchar*)buffer;
    dataarray = g_array_new(FALSE, TRUE, sizeof(gdouble));
    meta = gwy_container_new();
    while ((line = gwy_str_next_line(&p))) {
        if (gwy_strequal(line, "[BeginOfItem]")) {
            if (is_binary) {
                gwy_debug("reading as binary");
                pb = p;

                if (size - (pb - (const guchar*)buffer) < sizeof(gint32))
                    goto fail;
                yres = gwy_get_guint32_le(&pb);

                for (i = 0; i < yres; i++) {
                    if (size - (pb - (const guchar*)buffer) < sizeof(gint32))
                        goto fail;
                    xres = gwy_get_guint32_le(&pb);

                    if (size - (pb - (const guchar*)buffer)
                        < xres*sizeof(gdouble))
                        goto fail;

                    for (j = 0; j < xres; j++) {
                        z = gwy_get_gdouble_le(&pb);
                        g_array_append_val(dataarray, z);
                    }
                }
                p = (gchar*)pb;
            }
            else {
                gwy_debug("reading as text");
                while ((line = gwy_str_next_line(&p))) {
                    if (!(lineparts = split_to_nparts(line, " ", 3)))
                        goto fail;
                    x = atoi(lineparts[0]);
                    y = atoi(lineparts[1]);
                    z = g_ascii_strtod(lineparts[2], NULL);
                    g_array_append_val(dataarray, z);
                    if (x > xmax)
                        xmax = x;
                    if (y > ymax)
                        ymax = y;
                    g_strfreev(lineparts);
                }
                xres = xmax+1;
                yres = ymax+1;
            }
            gwy_debug("xres = %d, yres =  %d", xres, yres);
            break;
        }
        else if (g_str_has_prefix(line, "XCUnit")) {
            if (!(lineparts = split_to_nparts(line, " ", 3)))
                goto fail;
            unit = decode_string(lineparts[1], is_utf);
            siunitxy = gwy_si_unit_new_parse(unit, &power10xy);
            g_free(unit);
            x = atoi(lineparts[2]);
            if (x != 0)
                power10xy *= x;
            g_strfreev(lineparts);
        }
        else if (g_str_has_prefix(line, "ZCUnit")) {
            if (!(lineparts = split_to_nparts(line, " ", 3)))
                goto fail;
            unit = decode_string(lineparts[1], is_utf);
            siunitz = gwy_si_unit_new_parse(unit, &power10z);
            g_free(unit);
            z = atoi(lineparts[2]);
            if (z != 0)
                power10z *= z;
            g_strfreev(lineparts);
        }
        else if (g_str_has_prefix(line, "PlotsXLimits")) {
            if (!(lineparts = split_to_nparts(line, " ", 3)))
                goto fail;
            xoffset = g_ascii_strtod(lineparts[1], NULL);
            xscale = g_ascii_strtod(lineparts[2], NULL) - xoffset;
            g_strfreev(lineparts);
        }
        else if (g_str_has_prefix(line, "PlotsYLimits")) {
            if (!(lineparts = split_to_nparts(line, " ", 3)))
                goto fail;
            yoffset = g_ascii_strtod(lineparts[1], NULL);
            yscale = g_ascii_strtod(lineparts[2], NULL) - yoffset;
            g_strfreev(lineparts);
        }
        else if (g_str_has_prefix(line, "Name")) {
            if (!(lineparts = split_to_nparts(line, " ", 2)))
                goto fail;
            *title = decode_string(lineparts[1], is_utf);
            g_strfreev(lineparts);
        }
        else if (g_str_has_prefix(line, "Attributes")) {
            if (!(lineparts = split_to_nparts(line, " ", 2)))
                goto fail;
            attributes = g_strsplit(lineparts[1], "*_*|^_^", 1024);
            g_strfreev(lineparts);
            linecur = 0;
            while ((key = attributes[linecur])
                   && (value = attributes[linecur+1])) {
                key = decode_string(key, is_utf);
                value = decode_string(value, is_utf);
                gwy_debug("%s: %s", key, value);
                gwy_container_set_const_string_by_name(meta, key, value);

                if (g_str_has_prefix(key, "Name")) {
                    if (!*title)
                        *title = g_strdup(value);
                }
                else if (g_str_has_prefix(key, "XYUnit")) {
                    if (!siunitxy)
                        siunitxy = gwy_si_unit_new_parse(value, &power10xy);
                }
                else if (g_str_has_prefix(key, "ZUnit")) {
                    if (!siunitz)
                        siunitz = gwy_si_unit_new_parse(value, &power10z);
                }
                else if (g_str_has_prefix(key, "XMin")) {
                    xoffset = g_ascii_strtod(value, NULL);
                }
                else if (g_str_has_prefix(key, "XMax")) {
                    xscale = g_ascii_strtod(value, NULL) - xoffset;
                }
                else if (g_str_has_prefix(key, "YMin")) {
                    yoffset = g_ascii_strtod(value, NULL);
                }
                else if (g_str_has_prefix(key, "YMax")) {
                    yscale = g_ascii_strtod(value, NULL) - yoffset;
                }
                else if (g_str_has_prefix(key, "RawBinData")) {
                    is_binary = !g_ascii_strcasecmp(value, "true");
                }

                g_free(key);
                g_free(value);
                linecur += 2;
            }
            g_strfreev(attributes);
        }
    }

    if (dataarray->len != xres*yres)
        goto fail;

    if (xscale <= 0.0)
        xscale = 1.0;
    if (yscale <= 0.0)
        yscale = 1.0;
    dfield = gwy_data_field_new(xres, yres,
                                xscale*pow10(power10xy),
                                yscale*pow10(power10xy), TRUE);
    gwy_data_field_set_xoffset(dfield, xoffset*pow10(power10xy));
    gwy_data_field_set_yoffset(dfield, yoffset*pow10(power10xy));

    gwy_assign(gwy_data_field_get_data(dfield), dataarray->data, xres*yres);

    if (siunitxy)
        gwy_data_field_set_si_unit_xy(dfield, siunitxy);
    if (siunitz)
        gwy_data_field_set_si_unit_z(dfield, siunitz);

    *metadata = g_object_ref(meta);

fail:
    GWY_OBJECT_UNREF(meta);
    g_array_free(dataarray, TRUE);
    GWY_OBJECT_UNREF(siunitz);
    GWY_OBJECT_UNREF(siunitxy);

    return dfield;
}

static GwyGraphModel*
nst_read_2d(const gchar *buffer, guint channel, gboolean is_utf)
{
    GwyGraphCurveModel *spectra;
    GwyGraphModel *gmodel;
    GwySIUnit *siunitx = NULL, *siunity = NULL;
    gchar *p, *line, *unit, *key, *value, *xlabel = NULL, *ylabel = NULL;
    gchar **lineparts, **attributes;
    gint linecur;
    gdouble *xdata, *ydata, x, y;
    GArray *xarray, *yarray;
    guint i, numpoints = 0, power10x = 1, power10y = 1;
    gchar *framename = NULL, *title = NULL;
    gboolean ok = FALSE;

    p = (gchar*)buffer;
    gmodel = gwy_graph_model_new();
    while ((line = gwy_str_next_line(&p))) {
        if (g_str_has_prefix(line, "[BeginOfItem]")) {
            line = gwy_str_next_line(&p);
            /* deprecated fields check */
            while (line && g_ascii_isalpha(line[0])) {
                if (g_str_has_prefix(line, "Name") && !(framename)) {
                    if ((lineparts = split_to_nparts(line, " ", 2))) {
                        framename = g_strdup(lineparts[1]);
                        g_strfreev(lineparts);
                    }
                }
                line = gwy_str_next_line(&p);
            }

            numpoints = 0;
            xarray = g_array_new(FALSE, TRUE, sizeof(gdouble));
            yarray = g_array_new(FALSE, TRUE, sizeof(gdouble));
            while (line && !gwy_strequal(line, "[EndOfItem]")) {
                if (!(lineparts = split_to_nparts(line, " ", 2)))
                    goto fail;
                x = g_ascii_strtod(lineparts[0], NULL);
                g_array_append_val(xarray, x);
                y = g_ascii_strtod(lineparts[1], NULL);
                g_array_append_val(yarray, y);
                g_strfreev(lineparts);
                numpoints++;

                line = gwy_str_next_line(&p);
            }

            if (numpoints) {
                xdata = g_new(gdouble, numpoints);
                ydata = g_new(gdouble, numpoints);

                for (i = 0; i < numpoints; i++) {
                    xdata[i] = g_array_index(xarray, gdouble, i)
                               * pow10(power10x);
                    ydata[i] = g_array_index(yarray, gdouble, i)
                               * pow10(power10y);
                }

                spectra = gwy_graph_curve_model_new();
                if (!framename) {
                    framename = g_strdup_printf("Unknown spectrum");
                }
                g_object_set(spectra,
                             "description", framename,
                             "mode", GWY_GRAPH_CURVE_LINE,
                             NULL);
                gwy_graph_curve_model_set_data(spectra,
                                               xdata, ydata, numpoints);
                gwy_graph_model_add_curve(gmodel, spectra);

                g_object_unref(spectra);
                g_free(xdata);
                g_free(ydata);
            }
            g_array_free(xarray, TRUE);
            g_array_free(yarray, TRUE);
        }
        else if (g_str_has_prefix(line, "Name")) {
            if (!(lineparts = split_to_nparts(line, " ", 2)))
                goto fail;
            if (framename)
                g_free(framename);
            framename = decode_string(lineparts[1], is_utf);
            g_strfreev(lineparts);
        }
        else if (g_str_has_prefix(line, "XCUnit")) {
            if (!(lineparts = split_to_nparts(line, " ", 3)))
                goto fail;
            unit = decode_string(lineparts[1], is_utf);
            siunitx = gwy_si_unit_new_parse(unit, &power10x);
            g_free(unit);
            x = atoi(lineparts[2]);
            if (x != 0)
                power10x *= x;
            g_strfreev(lineparts);
        }
        else if (g_str_has_prefix(line, "YCUnit")) {
            if (!(lineparts = split_to_nparts(line, " ", 3)))
                goto fail;
            unit = decode_string(lineparts[1], is_utf);
            siunity = gwy_si_unit_new_parse(unit, &power10y);
            g_free(unit);
            y = atoi(lineparts[2]);
            if (y != 0)
                power10y *= y;
            g_strfreev(lineparts);
        }
        else if (g_str_has_prefix(line, "Attributes")) {
            if (!(lineparts = split_to_nparts(line, " ", 2)))
                goto fail;
            attributes = g_strsplit(lineparts[1], "*_*|^_^", 1024);
            g_strfreev(lineparts);
            linecur = 0;
            while ((key = attributes[linecur])
                   && (value = attributes[linecur+1])) {
                key = decode_string(key, is_utf);
                value = decode_string(value, is_utf);
                gwy_debug("%s: %s", key, value);

                if (g_str_has_prefix(key, "Name")) {
                    if (!framename)
                        framename = g_strdup(value);
                }
                else if (g_str_has_prefix(key, "XLabel")) {
                    if (!xlabel)
                        xlabel = g_strdup(value);
                }
                else if (g_str_has_prefix(key, "YLabel")) {
                    if (!ylabel)
                        ylabel = g_strdup(value);
                }
                else if (g_str_has_prefix(key, "XUnit")) {
                    if (!siunitx)
                        siunitx = gwy_si_unit_new_parse(value, &power10x);
                }
                else if (g_str_has_prefix(key, "YUnit")) {
                    if (!siunity)
                        siunity = gwy_si_unit_new_parse(value, &power10y);
                }

                g_free(key);
                g_free(value);
                linecur += 2;
            }
            g_strfreev(attributes);
        }
    }

    ok = TRUE;
    if (!framename)
        title = g_strdup_printf("Graph %u", channel);
    else {
        title = g_strdup_printf("%s (%u)", framename, channel);
        g_free(framename);
    }
    g_object_set(gmodel, "title", title, NULL);

    if (siunitx)
        g_object_set(gmodel, "si-unit-x", siunitx, NULL);

    if (siunity)
        g_object_set(gmodel, "si-unit-y", siunity, NULL);

    if (xlabel)
        gwy_graph_model_set_axis_label(gmodel, GTK_POS_BOTTOM, xlabel);

    if (ylabel)
        gwy_graph_model_set_axis_label(gmodel, GTK_POS_LEFT, ylabel);

fail:
    g_free(title);
    g_free(ylabel);
    g_free(xlabel);
    GWY_OBJECT_UNREF(siunitx);
    GWY_OBJECT_UNREF(siunity);

    if (!ok)
        GWY_OBJECT_UNREF(gmodel);

    return gmodel;
}

static GwyBrick*
nst_read_4d(const gchar *buffer, gsize datasize, gboolean is_utf,
            GwyContainer **metadata, gchar **title)
{
    GwyBrick *brick = NULL, *brick_cropped = NULL;
    GwyContainer *meta = NULL;
    GwyDataLine *calibration = NULL;
    NST4DHeader *header = NULL;
    guint xres, yres, zres, zcrop;
    gdouble xreal, yreal, zreal;
    gdouble *data = NULL;
    gint i, j, k, dataleft, x0, xn, dx, y0, yn, dy, npoints;
    GwySIUnit *siunit;
    const guchar *p;
    gchar *pl;
    gchar *line, *key, *value;
    gchar **lineparts, **attributes;
    gint linecur;

    pl = (gchar *)buffer;
    dataleft = datasize;
    gwy_debug("4d size = %d", dataleft);
    header = g_new(NST4DHeader, 1);
    while ((line = gwy_str_next_line(&pl))) {
        if (gwy_strequal(line, "[BeginOfItem]")) {
            dataleft -= (gint)(pl - buffer);
            p = pl;
            if (dataleft <= NST4DHEADER_SIZE + 4) {
                goto exit;
            }
            header->direction    = (NSTDirection)gwy_get_gint32_le(&p);
            header->startpoint   = (NSTStartPoint)gwy_get_gint32_le(&p);
            header->nx           = gwy_get_guint32_le(&p);
            header->ny           = gwy_get_guint32_le(&p);
            header->xmin         = gwy_get_gdouble_le(&p);
            header->xmax         = gwy_get_gdouble_le(&p);
            header->ymin         = gwy_get_gdouble_le(&p);
            header->ymax         = gwy_get_gdouble_le(&p);
            header->minforf      = gwy_get_gdouble_le(&p);
            header->maxforf      = gwy_get_gdouble_le(&p);
            header->minforrec    = gwy_get_gdouble_le(&p);
            header->maxforrec    = gwy_get_gdouble_le(&p);
            header->laserwl      = gwy_get_gdouble_le(&p);
            header->centerwl     = gwy_get_gdouble_le(&p);
            header->dispersion   = gwy_get_gdouble_le(&p);
            header->pixelxsize   = gwy_get_gdouble_le(&p);
            header->numpixels    = gwy_get_gdouble_le(&p);
            header->centralpixel = gwy_get_gdouble_le(&p);
            dataleft -= NST4DHEADER_SIZE;
            xres = header->nx;
            yres = header->ny;
            zres = (gint)(header->maxforrec - header->minforrec);
            gwy_debug("xres=%d, yres=%d, zres=%d", xres, yres, zres);
            xreal = header->xmax - header->xmin;
            yreal = header->ymax - header->ymin;
            zreal = header->maxforrec - header->minforrec;
            brick = gwy_brick_new(xres, yres, zres,
                                  xreal, yreal, zreal, TRUE);
            gwy_brick_set_xoffset(brick, header->xmin);
            gwy_brick_set_yoffset(brick, header->ymin);
            gwy_brick_set_zoffset(brick, header->minforrec);

            if (TopLeft == header->startpoint) {
                gwy_debug("Top Left");
                x0 = 0;
                xn = xres;
                dx = 1;
                y0 = 0;
                yn = yres;
                dy = 1;
            }
            else if (TopRight == header->startpoint) {
                gwy_debug("Top Right");
                x0 = xres-1;
                xn = 0;
                dx = -1;
                y0 = 0;
                yn = yres;
                dy = 1;
            }
            else if (BottomLeft == header->startpoint) {
                gwy_debug("Bottom Left");
                x0 = 0;
                xn = xres;
                dx = 1;
                y0 = yres-1;
                yn = 0;
                dy = -1;
            }
            else if (BottomRight == header->startpoint) {
                gwy_debug("Bottom Right");
                x0 = xres-1;
                xn = 0;
                dx = -1;
                y0 = yres-1;
                yn = 0;
                dy = -1;
            }
            else {
                gwy_debug("Wrong startpoint");
                goto exit;
            }

            data = gwy_brick_get_data(brick);
            zcrop = 0;

            if (Horizontal == header->direction) {
                gwy_debug("Horizontal");
                for (i = y0; (dy > 0) ? i < yn : i >= yn; i += dy)
                    for (j = x0; (dx > 0) ? j < xn : j >= xn; j += dx) {
                        if (dataleft < 8 * zres + 4) {
                            gwy_debug("Too little data left");
                            goto exit2;
                        }
                        npoints = gwy_get_guint32_le(&p);
                        if (!zcrop) {
                            zcrop = npoints;
                        }
                        for (k = 0; k < MIN(zres, npoints); k++) {
                            data[k*xres*yres + i*xres + j]
                                = gwy_get_gdouble_le(&p);
                        }
                        dataleft -= 8 * MIN(zres, npoints) + 4;
                    }
            }
            else if (Vertical == header->direction) {
                gwy_debug("Vertical");
                for (i = x0; (dx > 0) ? i < xn : i >= xn; i += dx)
                    for (j = y0; (dy > 0) ? j < yn : j >= yn; j += dy) {
                        if (dataleft < 8 * zres + 4) {
                            gwy_debug("Too little data left");
                            goto exit2;
                        }
                        npoints = gwy_get_guint32_le(&p);
                        if (!zcrop) {
                            zcrop = npoints;
                        }
                        for (k = 0; k < MIN(zres, npoints); k++) {
                            data[k*xres*yres + j*xres + i]
                                = gwy_get_gdouble_le(&p);
                        }
                        dataleft -= 8 * MIN(zres, npoints) + 4;
                    }
            }
            else {
                gwy_debug("Wrong scan direction");
            }

            break;
        }
        else if (g_str_has_prefix(line, "Attributes")) {
            meta = gwy_container_new();
            if (!(lineparts = split_to_nparts(line, " ", 2)))
                goto exit;
            attributes = g_strsplit(lineparts[1], "*_*|^_^", 1024);
            g_strfreev(lineparts);
            linecur = 0;
            while ((key = attributes[linecur])
                   && (value = attributes[linecur+1])) {
                key = decode_string(key, is_utf);
                value = decode_string(value, is_utf);
                gwy_debug("%s: %s", key, value);
                gwy_container_set_const_string_by_name(meta, key, value);

                if (g_str_has_prefix(key, "Name")) {
                    if (!*title)
                        *title = g_strdup(value);
                }

                g_free(key);
                g_free(value);
                linecur += 2;
            }
            g_strfreev(attributes);
        }
    }

exit2:
    if (brick) {
        if (zcrop < zres) {
            brick_cropped = gwy_brick_new_part(brick, 0, 0, 0,
                                               xres, yres, zcrop,
                                               TRUE);
            g_object_unref(brick);
            brick = brick_cropped;
            zres = zcrop;
        }

        data = NULL;
        calibration = gwy_data_line_new(zres, zreal, TRUE);
        data = gwy_data_line_get_data(calibration);
        for (i = 0; i < zres; i++)
            *(data++) = 1e-9 * (header->centerwl
                      + header->dispersion * header->pixelxsize
                      * (i - header->centralpixel));
        siunit = gwy_si_unit_new("m");
        gwy_data_line_set_si_unit_y(calibration, siunit);
        g_object_unref(siunit);
        gwy_brick_set_zcalibration(brick, calibration);
        g_object_unref(calibration);

        siunit = gwy_si_unit_new("m");
        gwy_brick_set_si_unit_x(brick, siunit);
        g_object_unref(siunit);
        siunit = gwy_si_unit_new("m");
        gwy_brick_set_si_unit_y(brick, siunit);
        g_object_unref(siunit);
        siunit = gwy_si_unit_new("m");
        gwy_brick_set_si_unit_z(brick, siunit);
        g_object_unref(siunit);
        siunit = gwy_si_unit_new("Counts");
        gwy_brick_set_si_unit_w(brick, siunit);
        g_object_unref(siunit);
    }
    *metadata = meta;

exit:
    g_free(header);

    return brick;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
