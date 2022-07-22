/*
 *  $Id: nanonis.c 21958 2019-04-03 15:00:27Z yeti-dn $
 *  Copyright (C) 2006,2015-2019 David Necas (Yeti), Petr Klapetek.
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
 * <mime-type type="application/x-nanonis-spm">
 *   <comment>Nanonis SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value=":NANONIS_VERSION:"/>
 *   </magic>
 *   <glob pattern="*.sxm"/>
 *   <glob pattern="*.SXM"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Nanonis
 * 0 string :NANONIS_VERSION:\x0a Nanonis SXM data
 * >&0 regex [0-9]+ version %s
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Nanonis SXM
 * .sxm
 * Read
 **/

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>
#include <app/settings.h>

#include "err.h"

#define MAGIC ":NANONIS_VERSION:"
#define MAGIC_SIZE (sizeof(MAGIC) - 1)

#define EXTENSION ".sxm"

typedef enum {
    DIR_FORWARD  = 1 << 0,
    DIR_BACKWARD = 1 << 1,
    DIR_BOTH     = (DIR_FORWARD | DIR_BACKWARD)
} SXMDirection;

typedef struct {
    gint channel;
    gchar *name;
    gchar *unit;
    SXMDirection direction;
    gdouble calibration;
    gdouble offset;
} SXMDataInfo;

typedef struct {
    GHashTable *meta;
    gchar **z_controller_headers;
    gchar **z_controller_values;
    gint ndata;
    SXMDataInfo *data_info;

    gboolean ok;
    gint xres;
    gint yres;
    gdouble xreal;
    gdouble yreal;
    gdouble xoff;
    gdouble yoff;
    /* Set if the times are set to N/A or NaN, this seems to be done in slice
     * files of 3D data.  We cannot trust direction filed then as it's set to
     * `both' although the file contains one direction only. */
    gboolean bogus_scan_time;
} SXMFile;

typedef struct {
    gboolean preserve_coordinates;
} SXMArgs;

static gboolean      module_register(void);
static gint          sxm_detect     (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name);
static GwyContainer* sxm_load       (const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);
static GwyContainer* sxm_build_meta (const SXMFile *sxmfile,
                                     guint id);
static void          sxm_load_args  (GwyContainer *container,
                                     SXMArgs *args);
static void          sxm_save_args  (GwyContainer *container,
                                     const SXMArgs *args);

static const SXMArgs sxm_defaults = {
    FALSE,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Nanonis SXM data files."),
    "Yeti <yeti@gwyddion.net>",
    "1.3",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

/* FIXME: I'm making this up, never seen anything except `both' */
static const GwyEnum directions[] = {
    { "forward",  DIR_FORWARD,  },
    { "backward", DIR_BACKWARD, },
    { "both",     DIR_BOTH,     },
};

GWY_MODULE_QUERY2(module_info, nanonis)

static gboolean
module_register(void)
{
    gwy_file_func_register("nanonis",
                           N_("Nanonis SXM files (.sxm)"),
                           (GwyFileDetectFunc)&sxm_detect,
                           (GwyFileLoadFunc)&sxm_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
sxm_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;

    if (fileinfo->buffer_len > MAGIC_SIZE
        && memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) == 0)
        score = 100;

    return score;
}

static gchar**
split_line_in_place(gchar *line,
                    gchar delim)
{
    gchar **strs;
    guint i, n = 0;

    for (i = 0; line[i]; i++) {
        if ((!i || line[i-1] == delim) && (line[i] && line[i] != delim))
            n++;
    }

    strs = g_new(gchar*, n+1);
    n = 0;
    for (i = 0; line[i]; i++) {
        if ((!i || line[i-1] == delim || !line[i-1])
            && (line[i] && line[i] != delim))
            strs[n++] = line + i;
        else if (i && line[i] == delim && line[i-1] != delim)
            line[i] = '\0';
    }
    strs[n] = NULL;

#ifdef DEBUG
    for (i = 0; strs[i]; i++)
        gwy_debug("%u: <%s>", i, strs[i]);
#endif

    return strs;
}

static void
sxm_free_z_controller(SXMFile *sxmfile)
{
    g_free(sxmfile->z_controller_headers);
    sxmfile->z_controller_headers = NULL;
    g_free(sxmfile->z_controller_values);
    sxmfile->z_controller_values = NULL;
}

static guint
sxm_tag_count_lines(GPtrArray *header_lines, guint lineno)
{
    guint n = 0;

    while (lineno + n < header_lines->len) {
        gchar *line = g_ptr_array_index(header_lines, lineno + n);
        if (line[0] == ':')
            return n;
        n++;
    }
    return n;
}

static gchar*
join_lines(GPtrArray *header_lines, guint lineno, guint n)
{
    gchar **lines = g_new(gchar*, n+1);
    gchar *retval;
    guint i = 0;

    for (i = 0; i < n; i++)
        lines[i] = g_ptr_array_index(header_lines, lineno + i);
    lines[n] = NULL;
    retval = g_strjoinv(" ", lines);
    g_free(lines);

    return retval;
}

static void
err_HEADER_ENDED(GError **error)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("File header ended unexpectedly."));
}

static guint
sxm_read_tag(SXMFile *sxmfile,
             GPtrArray *header_lines,
             guint lineno,
             GError **error)
{
    gchar *line, *tag, *s;
    guint len, n;

    if (lineno >= header_lines->len) {
        err_HEADER_ENDED(error);
        return 0;
    }

    line = (gchar*)g_ptr_array_index(header_lines, lineno);
    len = strlen(line);
    if (len < 3 || line[0] != ':' || line[len-1] != ':') {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Garbage was found in place of tag header line."));
        return 0;
    }
    lineno++;
    tag = line+1;
    line[len-1] = '\0';

    if (gwy_strequal(tag, "SCANIT_END")) {
        gwy_debug("SCANIT_END");
        sxmfile->ok = TRUE;
        return lineno;
    }

    n = sxm_tag_count_lines(header_lines, lineno);
    gwy_debug("tag: <%s> (%u lines)", tag, n);
    if (gwy_strequal(tag, "Z-CONTROLLER")) {
        if (n < 2) {
            err_HEADER_ENDED(error);
            return 0;
        }

        /* Headers */
        if (sxmfile->z_controller_headers) {
            g_warning("Multiple Z-CONTROLLERs, keeping only the last");
            sxm_free_z_controller(sxmfile);
        }

        line = (gchar*)g_ptr_array_index(header_lines, lineno);
        /* Documentation says tabs, but I see also spaces in the file. */
        g_strdelimit(line, " ", '\t');
        sxmfile->z_controller_headers = split_line_in_place(line, '\t');

        line = (gchar*)g_ptr_array_index(header_lines, lineno + 1);
        sxmfile->z_controller_values = split_line_in_place(line, '\t');
        if (g_strv_length(sxmfile->z_controller_headers)
            != g_strv_length(sxmfile->z_controller_values)) {
            g_warning("The numbers of Z-CONTROLLER headers and values differ");
            sxm_free_z_controller(sxmfile);
        }
    }
    else if (gwy_strequal(tag, "DATA_INFO")) {
        SXMDataInfo di;
        gchar **columns;
        GArray *data_info;
        guint i;

        if (n < 2) {
            err_HEADER_ENDED(error);
            return 0;
        }

        /* Headers */
        line = (gchar*)g_ptr_array_index(header_lines, lineno);
        /* Documentation says tabs, but I see also spaces in the file. */
        g_strdelimit(line, " ", '\t');
        columns = split_line_in_place(line, '\t');

        if (g_strv_length(columns) < 6
            || !gwy_strequal(columns[0], "Channel")
            || !gwy_strequal(columns[1], "Name")
            || !gwy_strequal(columns[2], "Unit")
            || !gwy_strequal(columns[3], "Direction")
            || !gwy_strequal(columns[4], "Calibration")
            || !gwy_strequal(columns[5], "Offset")) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("DATA_INFO does not contain the expected "
                          "columns: %s."),
                        "Channel Name Unit Direction Calibration Offset");
            g_free(columns);
            return 0;
        }

        if (sxmfile->data_info) {
            g_warning("Multiple DATA_INFOs, keeping only the last");
            g_free(sxmfile->data_info);
            sxmfile->data_info = NULL;
        }

        data_info = g_array_new(FALSE, FALSE, sizeof(SXMDataInfo));
        for (i = 1; i < n; i++) {
            line = (gchar*)g_ptr_array_index(header_lines, lineno + i);
            if (!strlen(line))
                continue;
            columns = split_line_in_place(line, '\t');
            if (g_strv_length(columns) < 6) {
                g_set_error(error, GWY_MODULE_FILE_ERROR,
                            GWY_MODULE_FILE_ERROR_DATA,
                            _("DATA_INFO line contains fewer than %d fields."),
                            6);
                g_free(columns);
                g_array_free(data_info, TRUE);
                return 0;
            }

            di.channel = atoi(columns[0]);
            di.name = columns[1];
            di.unit = columns[2];
            di.direction = gwy_string_to_enum(columns[3],
                                              directions,
                                              G_N_ELEMENTS(directions));
            if (di.direction == (SXMDirection)-1) {
                err_INVALID(error, "Direction");
                g_free(columns);
                g_array_free(data_info, TRUE);
                return 0;
            }
            di.calibration = g_ascii_strtod(columns[4], NULL);
            di.offset = g_ascii_strtod(columns[5], NULL);
            g_array_append_val(data_info, di);

            g_free(columns);
            columns = NULL;
        }

        sxmfile->data_info = (SXMDataInfo*)data_info->data;
        sxmfile->ndata = data_info->len;
        g_array_free(data_info, FALSE);
    }
    else if (n) {
        /* Generic tag.  We replace line ends with spaces since metadata are
         * single-line strings... */
        if (n > 1)
            s = join_lines(header_lines, lineno, n);
        else
            s = g_strdup((gchar*)g_ptr_array_index(header_lines, lineno));
        g_hash_table_insert(sxmfile->meta, tag, s);
        gwy_debug("value: <%s>", s);
    }

    return lineno + n;
}

static void
read_data_field(GwyContainer *container,
                gint *id,
                const gchar *filename,
                const SXMFile *sxmfile,
                const SXMDataInfo *data_info,
                SXMDirection dir,
                SXMArgs *args,
                const guchar **p)
{
    GwyContainer *meta;
    GwyDataField *dfield, *mfield = NULL;
    gdouble *data, *mdata;
    gint j, n;
    gchar *s;
    gboolean flip_vertically = FALSE, flip_horizontally = FALSE;

    dfield = gwy_data_field_new(sxmfile->xres, sxmfile->yres,
                                sxmfile->xreal, sxmfile->yreal,
                                FALSE);
    /* This is correct for both preserved and non-preserved coordinates? */
    gwy_data_field_set_xoffset(dfield, sxmfile->xoff - 0.5*sxmfile->xreal);
    gwy_data_field_set_yoffset(dfield, sxmfile->yoff - 0.5*sxmfile->yreal);
    data = gwy_data_field_get_data(dfield);

    n = sxmfile->xres*sxmfile->yres;
    for (j = 0; j < n; j++) {
        /* This is not a perfect NaN check, but Nanonis uses ff as the payload
         * so look only for these. */
        if (G_UNLIKELY(((*p)[0] & 0x7f) == 0x7f && (*p)[1] == 0xff))
            break;

        data[j] = gwy_get_gfloat_be(p);
    }

    if (j < n) {
        mfield = gwy_data_field_new_alike(dfield, TRUE);
        mdata = gwy_data_field_get_data(mfield);
        while (j < n) {
            if (((*p)[0] & 0x7f) == 0x7f && (*p)[1] == 0xff) {
                mdata[j] = -1.0;
                *p += sizeof(gfloat);
            }
            else
                data[j] = gwy_get_gfloat_be(p);
            j++;
        }
        gwy_data_field_add(mfield, 1.0);
        gwy_app_channel_remove_bad_data(dfield, mfield);
    }

    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield),
                                data_info->unit);
    gwy_container_set_object(container, gwy_app_get_data_key_for_id(*id),
                             dfield);

    if (mfield) {
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(mfield), "m");
        gwy_container_set_object(container, gwy_app_get_mask_key_for_id(*id),
                                 mfield);
    }

    if (!dir) {
        gwy_container_set_const_string(container,
                                       gwy_app_get_data_title_key_for_id(*id),
                                       data_info->name);
    }
    else {
        gchar *title;

        title = g_strdup_printf("%s (%s)", data_info->name,
                                dir == DIR_BACKWARD ? "Backward" : "Forward");
        gwy_container_set_string(container,
                                 gwy_app_get_data_title_key_for_id(*id), title);
        /* Don't free title, container eats it */
    }

    if ((meta = sxm_build_meta(sxmfile, *id))) {
        gwy_container_set_object(container,
                                 gwy_app_get_data_meta_key_for_id(*id), meta);
        g_object_unref(meta);
    }

    gwy_app_channel_check_nonsquare(container, *id);

    if (dir == DIR_BACKWARD)
        flip_horizontally = TRUE;

    /* With preserve_coordinates images are flipped vertically with respect to
     * Nanonis software.  But it hopefully makes possible to put SPS points at
     * correct positions.  We cannot do that when reading the spectra later
     * because spectra do not know about ‘their’ images. */
    if ((s = g_hash_table_lookup(sxmfile->meta, "SCAN_DIR"))) {
        if ((args->preserve_coordinates && gwy_strequal(s, "down"))
            || (!args->preserve_coordinates && gwy_strequal(s, "up")))
            flip_vertically = TRUE;
    }

    gwy_data_field_invert(dfield, flip_vertically, flip_horizontally, FALSE);
    g_object_unref(dfield);

    if (mfield) {
        gwy_data_field_invert(mfield, flip_vertically, flip_horizontally, FALSE);
        g_object_unref(mfield);
    }

    gwy_file_channel_import_log_add(container, *id, NULL, filename);

    (*id)++;
}

static GwyContainer*
sxm_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    SXMFile sxmfile;
    SXMArgs args;
    GwyContainer *settings;
    GwyContainer *container = NULL;
    GPtrArray *header_lines;
    guchar *buffer = NULL;
    gsize size1 = 0, size = 0;
    GError *err = NULL;
    const guchar *p;
    gchar *header, *hp, *s, *endptr;
    gchar **columns;
    gint version;
    guint i;

    settings = gwy_app_settings_get();
    sxm_load_args(settings, &args);

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    if (size < MAGIC_SIZE + 400) {
        err_TOO_SHORT(error);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    if (memcmp(buffer, MAGIC, MAGIC_SIZE) != 0) {
        err_FILE_TYPE(error, "Nanonis");
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    /* Extract header (we need it writable) */
    p = memchr(buffer, '\x1a', size);
    if (!p || p + 1 == buffer + size || p[1] != '\x04') {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Missing data start marker \\x1a\\x04."));
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    gwy_clear(&sxmfile, 1);
    sxmfile.meta = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);

    header = g_memdup(buffer, p - buffer + 1);
    header[p - buffer] = '\0';
    hp = header;
    /* Move p to actual data start */
    p += 2;

    header_lines = g_ptr_array_new();
    while ((s = gwy_str_next_line(&hp))) {
        g_strstrip(s);
        g_ptr_array_add(header_lines, s);
    }

    /* Parse header */
    i = 0;
    do {
        if (!(i = sxm_read_tag(&sxmfile, header_lines, i, error))) {
            sxm_free_z_controller(&sxmfile);
            g_ptr_array_free(header_lines, TRUE);
            g_free(sxmfile.data_info);
            g_free(header);
            gwy_file_abandon_contents(buffer, size, NULL);
            return NULL;
        }
    } while (!sxmfile.ok);

    g_ptr_array_free(header_lines, TRUE);

    /* Data info */
    if (sxmfile.ok) {
        if (!sxmfile.data_info) {
            err_NO_DATA(error);
            sxmfile.ok = FALSE;
        }
    }

    /* Version */
    if ((s = g_hash_table_lookup(sxmfile.meta, "NANONIS_VERSION")))
        version = atoi(s);
    else {
        g_warning("Version is missing, assuming old files.  "
                  "How it can happen, anyway?");
        version = 0;
    }

    /* Data type */
    if (sxmfile.ok) {
        if ((s = g_hash_table_lookup(sxmfile.meta, "SCANIT_TYPE"))) {
            gwy_debug("s: <%s>", s);
            columns = split_line_in_place(s, ' ');
            if (g_strv_length(columns) == 2
                && gwy_strequal(columns[0], "FLOAT")
                /* XXX: No matter what they say, the files seems to be BE */
                && (gwy_strequal(columns[1], "LSBFIRST")
                    || gwy_strequal(columns[1], "MSBFIRST"))) {
                size1 = sizeof(gfloat);
            }
            else {
                err_UNSUPPORTED(error, "SCANIT_TYPE");
                sxmfile.ok = FALSE;
            }
            g_free(columns);
        }
        else {
            err_MISSING_FIELD(error, "SCANIT_TYPE");
            sxmfile.ok = FALSE;
        }
    }

#if 0
    /* Check for rotated data */
    if (sxmfile.ok) {
        if ((s = g_hash_table_lookup(sxmfile.meta, "SCAN_ANGLE"))) {
            if (g_ascii_strtod(s, NULL) == 90.0) {
                gwy_debug("data is rotated");
                rotated = TRUE;
            }
        }
    }
#endif

    /* Pixel sizes */
    if (sxmfile.ok) {
        if ((s = g_hash_table_lookup(sxmfile.meta, "SCAN_PIXELS"))) {
            if (sscanf(s, "%d %d", &sxmfile.xres, &sxmfile.yres) == 2) {
                /* Version 1 files have y and x swapped just for fun. */
                if (version < 2)
                    GWY_SWAP(gint, sxmfile.xres, sxmfile.yres);
                size1 *= sxmfile.xres * sxmfile.yres;
                gwy_debug("xres: %d, yres: %d", sxmfile.xres, sxmfile.yres);
                gwy_debug("size1: %u", (guint)size1);
            }
            else {
                err_INVALID(error, "SCAN_PIXELS");
                sxmfile.ok = FALSE;
            }
        }
        else {
            err_MISSING_FIELD(error, "SCAN_PIXELS");
            sxmfile.ok = FALSE;
        }

        if (sxmfile.ok
            && (err_DIMENSION(error, sxmfile.xres)
                || err_DIMENSION(error, sxmfile.yres)))
            sxmfile.ok = FALSE;
    }

    /* Physical dimensions */
    if (sxmfile.ok) {
        if ((s = g_hash_table_lookup(sxmfile.meta, "SCAN_RANGE"))) {
            sxmfile.xreal = g_ascii_strtod(s, &endptr);
            if (endptr != s) {
                s = endptr;
                sxmfile.yreal = g_ascii_strtod(s, &endptr);
                gwy_debug("xreal: %g, yreal: %g", sxmfile.xreal, sxmfile.yreal);
            }
            if (s == endptr) {
                err_INVALID(error, "SCAN_RANGE");
                sxmfile.ok = FALSE;
            }
        }
        else {
            err_MISSING_FIELD(error, "SCAN_RANGE");
            sxmfile.ok = FALSE;
        }

        if (sxmfile.ok) {
            /* Use negated positive conditions to catch NaNs */
            if (!((sxmfile.xreal = fabs(sxmfile.xreal)) > 0)) {
                g_warning("Real x size is 0.0, fixing to 1.0");
                sxmfile.xreal = 1.0;
            }
            if (!((sxmfile.yreal = fabs(sxmfile.yreal)) > 0)) {
                g_warning("Real y size is 0.0, fixing to 1.0");
                sxmfile.yreal = 1.0;
            }
        }
    }

    /* Offsets, consider them optional, although they are probably always
     * present. */
    if (sxmfile.ok && (s = g_hash_table_lookup(sxmfile.meta, "SCAN_OFFSET"))) {
        sxmfile.xoff = g_ascii_strtod(s, &endptr);
        if (endptr != s) {
            s = endptr;
            sxmfile.yoff = g_ascii_strtod(s, &endptr);
        }
        if (s == endptr)
            sxmfile.xoff = sxmfile.yoff = 0.0;
        else {
            gwy_debug("xoff: %g, yoff: %g", sxmfile.xoff, sxmfile.yoff);
        }
    }

    /* Scan times, check for bogus values indicating generated slice files. */
    if (sxmfile.ok) {
        if ((s = g_hash_table_lookup(sxmfile.meta, "ACQ_TIME"))
            && gwy_strequal(s, "N/A"))
            sxmfile.bogus_scan_time = TRUE;
        else if ((s = g_hash_table_lookup(sxmfile.meta, "SCAN_TIME"))
                 && strncmp(s, "NaN", 3) == 0)
            sxmfile.bogus_scan_time = TRUE;
    }

    /* Check file size */
    if (sxmfile.ok) {
        gsize expected_size;

        expected_size = p - buffer;
        for (i = 0; i < sxmfile.ndata; i++) {
            guint d = sxmfile.data_info[i].direction;

            if (d == DIR_BOTH) {
                /* XXX: Assume generated files lie about the direction and
                 * they are always unidirectional. */
                if (sxmfile.bogus_scan_time) {
                    sxmfile.data_info[i].direction = DIR_FORWARD;
                    expected_size += size1;
                }
                else
                    expected_size += 2*size1;
            }
            else if (d == DIR_FORWARD || d == DIR_BACKWARD)
                expected_size += size1;
            else {
                g_assert_not_reached();
            }
        }
        if (err_SIZE_MISMATCH(error, expected_size, size, TRUE))
            sxmfile.ok = FALSE;
    }

    /* Read data */
    if (sxmfile.ok) {
        gint id = 0;

        container = gwy_container_new();
        for (i = 0; i < sxmfile.ndata; i++) {
            guint d = sxmfile.data_info[i].direction;

            if (d == DIR_BOTH) {
                read_data_field(container, &id, filename,
                                &sxmfile, sxmfile.data_info + i,
                                DIR_FORWARD, &args, &p);
                read_data_field(container, &id, filename,
                                &sxmfile, sxmfile.data_info + i,
                                DIR_BACKWARD, &args, &p);
            }
            else if (d == DIR_FORWARD || d == DIR_BACKWARD) {
                read_data_field(container, &id, filename,
                                &sxmfile, sxmfile.data_info + i, d, &args, &p);
            }
            else {
                g_assert_not_reached();
            }
        }
    }

    sxm_free_z_controller(&sxmfile);
    g_free(sxmfile.data_info);
    g_hash_table_destroy(sxmfile.meta);
    g_free(header);
    gwy_file_abandon_contents(buffer, size, NULL);
    /* This may seem pointless, but it creates the line in settings file so
     * someone wishing to change it only has to change the value. */
    sxm_save_args(settings, &args);

    return container;
}

static inline gchar*
reformat_float(const gchar *format,
               const gchar *value)
{
    gdouble v = g_ascii_strtod(value, NULL);
    return g_strdup_printf(format, v);
}

static void
add_metadata(gpointer hkey,
             gpointer hvalue,
             gpointer user_data)
{
    gchar *key = (gchar*)hkey;
    gchar *value = (gchar*)hvalue;
    gchar **t;

    if (!strchr(key, '>'))
        return;

    t = g_strsplit(key, ">", 0);
    key = g_strjoinv("::", t);
    gwy_container_set_const_string_by_name(GWY_CONTAINER(user_data), key,
                                           value);
    g_free(key);
    g_strfreev(t);
}

static GwyContainer*
sxm_build_meta(const SXMFile *sxmfile,
               G_GNUC_UNUSED guint id)
{
    GwyContainer *meta = gwy_container_new();
    GHashTable *hash = sxmfile->meta;
    const gchar *value;

    if ((value = g_hash_table_lookup(hash, "COMMENT")))
        gwy_container_set_string_by_name(meta, "Comment", g_strdup(value));
    if ((value = g_hash_table_lookup(hash, "REC_DATE")))
        gwy_container_set_string_by_name(meta, "Date", g_strdup(value));
    if ((value = g_hash_table_lookup(hash, "REC_TIME")))
        gwy_container_set_string_by_name(meta, "Time", g_strdup(value));
    if ((value = g_hash_table_lookup(hash, "REC_TEMP")))
        gwy_container_set_string_by_name(meta, "Temperature",
                                         reformat_float("%g K", value));
    if ((value = g_hash_table_lookup(hash, "ACQ_TIME")))
        gwy_container_set_string_by_name(meta, "Acquistion time",
                                         reformat_float("%g s", value));
    if ((value = g_hash_table_lookup(hash, "SCAN_FILE")))
        gwy_container_set_string_by_name(meta, "File name", g_strdup(value));
    if ((value = g_hash_table_lookup(hash, "BIAS")))
        gwy_container_set_string_by_name(meta, "Bias",
                                         reformat_float("%g V", value));
    if ((value = g_hash_table_lookup(hash, "SCAN_DIR")))
        gwy_container_set_string_by_name(meta, "Direction", g_strdup(value));

    if (sxmfile->z_controller_headers && sxmfile->z_controller_values) {
        gchar **cvalues = sxmfile->z_controller_values;
        gchar **cheaders = sxmfile->z_controller_headers;
        guint i;

        for (i = 0; cheaders[i] && cvalues[i]; i++) {
            gchar *key = g_strconcat("Z controller ", cheaders[i], NULL);
            gwy_container_set_string_by_name(meta, key, g_strdup(cvalues[i]));
            g_free(key);
        }
    }

    g_hash_table_foreach(hash, add_metadata, meta);

    if (gwy_container_get_n_items(meta))
        return meta;

    g_object_unref(meta);
    return NULL;
}

static const gchar preserve_coordinates_key[] = "/module/nanonis/preserve_coordinates";

static void
sxm_sanitize_args(SXMArgs *args)
{
    args->preserve_coordinates = !!args->preserve_coordinates;
}

static void
sxm_save_args(GwyContainer *container, const SXMArgs *args)
{
    gwy_container_set_boolean_by_name(container, preserve_coordinates_key,
                                      args->preserve_coordinates);
}

static void
sxm_load_args(GwyContainer *container, SXMArgs *args)
{
    *args = sxm_defaults;

    gwy_container_gis_boolean_by_name(container, preserve_coordinates_key,
                                      &args->preserve_coordinates);

    sxm_sanitize_args(args);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
