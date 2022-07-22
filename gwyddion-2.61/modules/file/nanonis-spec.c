/*
 *  $Id: nanonis-spec.c 22881 2020-07-16 14:06:41Z yeti-dn $
 *  Copyright (C) 2018 David Necas (Yeti).
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
 * <mime-type type="application/x-nanonis-spectra">
 *   <comment>Nanonis SPM spectroscopy data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="Experiment\t">
 *       <match type="string" offset="12:40" value="Date\t">
 *         <match type="string" offset="36:80" value="User\t"/>
 *       </match>
 *     </match>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Nanonis STS spectroscopy
 * .dat
 * SPS
 **/

#define MAGIC1 "Experiment\t"
#define MAGIC1_SIZE (sizeof(MAGIC1)-1)

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

#include "err.h"

typedef struct {
    gchar *storage;      /* Actual storage for the other strings. */
    const gchar *name;   /* Bias, Current, LIY 1 omega, ... */
    const gchar *ext;    /* [bwd], without the brackets */
    const gchar *unit;   /* (A) or (V), without the parentheses. */
} DATColumn;

typedef struct {
    gdouble x;
    gdouble y;
    guint ncolumns;
    guint nrows;
    DATColumn *columns;
    gdouble *data;
} DATSingleFile;

typedef struct {
    GwySpectra **spectra;
    DATSingleFile *template;
} DATSpectraSet;

static gboolean       module_register          (void);
static gint           dat_detect               (const GwyFileDetectInfo *fileinfo,
                                                gboolean only_name);
static GwyContainer*  dat_load                 (const gchar *filename,
                                                GwyRunType mode,
                                                GError **error);
static gboolean       check_compatibility      (const DATSingleFile *datfile,
                                                const DATSingleFile *template);
static void           add_curves_to_spectra_set(DATSpectraSet *specset,
                                                DATSingleFile *datfile);
static DATSingleFile* read_one_dat_file        (const gchar *filename,
                                                GError **error);
static gchar**        find_all_file_names      (const gchar *filename);
static void           dat_single_file_free     (DATSingleFile *datfile);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Nanonis DAT spectrum files."),
    "Yeti <yeti@gwyddion.net>",
    "1.3",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, nanonis_spec)

static gboolean
module_register(void)
{
    gwy_file_func_register("nanonis_spec",
                           N_("Nanonis spectrum files (.dat)"),
                           (GwyFileDetectFunc)&dat_detect,
                           (GwyFileLoadFunc)&dat_load,
                           NULL,
                           NULL);

    return TRUE;
}

static const guchar*
find_field_in_head(const GwyFileDetectInfo *fileinfo,
                   const guchar *p,
                   const gchar *s)
{
    p = gwy_memmem(p, fileinfo->buffer_len - (p - fileinfo->head),
                   s, strlen(s));
    if (p && (p == fileinfo->head || *(p-1) == '\r' || *(p-1) == '\n'))
        return p;
    return NULL;
}

static gint
dat_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    const guchar *p;

    if (only_name)
        return 0;

    p = fileinfo->head;
    if (memcmp(p, MAGIC1, MAGIC1_SIZE) != 0)
        return 0;
    p += MAGIC1_SIZE;

    /* These fields seem universal. */
    if ((find_field_in_head(fileinfo, p, "Date")
         || find_field_in_head(fileinfo, p, "Saved Date"))
        && find_field_in_head(fileinfo, p, "User")
        && (find_field_in_head(fileinfo, p, "X (m)")
            || find_field_in_head(fileinfo, p, "x (m)"))
        && (find_field_in_head(fileinfo, p, "Y (m)")
            || find_field_in_head(fileinfo, p, "y (m)")))
        return 90;

    return 0;
}

static GwyContainer*
dat_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *container = NULL;
    gchar **filenames;
    DATSpectraSet specset;
    DATSingleFile *datfile;
    GQuark quark;
    guint i, j;

    gwy_clear(&specset, 1);
    filenames = find_all_file_names(filename);
    /* When we cannot enumerate files, just create a list containing the single
     * file name we were given explicitly. */
    if (!filenames) {
        filenames = g_new0(gchar*, 2);
        filenames[0] = g_strdup(filename);
    }

    /* Use the file the user selected as the template. */
    if (!(specset.template = read_one_dat_file(filename, error)))
        goto fail;

    for (i = 0; filenames[i]; i++) {
        if (!(datfile = read_one_dat_file(filenames[i], error))) {
            g_warning("Cannot read associated file %s.", filenames[i]);
            continue;
        }

        if (check_compatibility(datfile, specset.template))
            add_curves_to_spectra_set(&specset, datfile);
        /* Simply skip incompatible files. */
        dat_single_file_free(datfile);
    }

    for (i = j = 0; i < specset.template->ncolumns; i++) {
        gwy_debug("[%u:%u] %p", i, j, specset.spectra[j]);
        if (!specset.spectra[i])
            continue;

        if (!container)
            container = gwy_container_new();
        quark = gwy_app_get_spectra_key_for_id(j);
        gwy_container_set_object(container, quark, specset.spectra[i]);
        j++;
    }

    if (!container)
        err_NO_DATA(error);

fail:
    if (specset.template) {
        if (specset.spectra) {
            for (i = 0; i < specset.template->ncolumns; i++)
                GWY_OBJECT_UNREF(specset.spectra[i]);
            g_free(specset.spectra);
        }
        dat_single_file_free(specset.template);
    }
    g_strfreev(filenames);

    return container;
}

static gboolean
gwy_strequal0(const gchar *p, const gchar *q)
{
    if (!q)
        return !p;
    return p ? gwy_strequal(p, q) : FALSE;
}

static gboolean
check_compatibility(const DATSingleFile *datfile,
                    const DATSingleFile *template)
{
    const DATColumn *datcolumn, *tmplcolumn;
    guint i;

    if (datfile->ncolumns != template->ncolumns) {
        gwy_debug("datfile->ncolumns(%u) != template->ncolumns(%u)",
                  datfile->ncolumns, template->ncolumns);
        return FALSE;
    }
    for (i = 0; i < template->ncolumns; i++) {
        datcolumn = datfile->columns + i;
        tmplcolumn = template->columns + i;
        if (!gwy_strequal(datcolumn->name, tmplcolumn->name)) {
            gwy_debug("[%u] datcolumn->name(%s) != tmplcolumn->name(%s)",
                      i, datcolumn->name, tmplcolumn->name);
            return FALSE;
        }
        if (!gwy_strequal0(datcolumn->ext, tmplcolumn->ext)) {
            gwy_debug("[%u] datcolumn->ext(%s) != tmplcolumn->ext(%s)",
                      i, datcolumn->ext, tmplcolumn->ext);
            return FALSE;
        }
        if (!gwy_strequal0(datcolumn->unit, tmplcolumn->unit)) {
            gwy_debug("[%u], datcolumn->unit(%s) != tmplcolumn->unit(%s)",
                      i, datcolumn->unit, tmplcolumn->unit);
            return FALSE;
        }
    }
    return TRUE;
}

static inline void
strip_space_back(gchar *end, gchar *beg)
{
    do {
        *end = '\0';
        end--;
    } while (end >= beg && g_ascii_isspace(*end));
}

static void
parse_column_header(gchar *colname, DATColumn *column)
{
    gchar *p, *q, *s;

    s = column->storage = colname;
    column->name = s;

    if ((p = strchr(s, '['))) {
        if ((q = strchr(p+1, ']'))) {
            column->ext = p+1;
            strip_space_back(p, s);
            *q = '\0';
            s = q+1;
            while (g_ascii_isspace(*s))
                s++;
        }
        else {
            g_warning("Column header %s has only opening [.", colname);
        }
    }

    if ((p = strchr(s, '('))) {
        if ((q = strchr(p+1, ')'))) {
            column->unit = p+1;
            strip_space_back(p, s);
            *q = '\0';
            s = q+1;
            while (g_ascii_isspace(*s))
                s++;
        }
        else {
            g_warning("Column header %s has only opening (.", colname);
        }
    }
}

static gchar*
make_axis_label(const DATColumn *column)
{
    if (!column->ext)
        return g_strdup(column->name);

    return g_strconcat(column->name, " [", column->ext, "]", NULL);
}

static void
add_curves_to_spectra_set(DATSpectraSet *specset,
                          DATSingleFile *datfile)
{
    guint i, j, ncolumns, nrows;
    gdouble real, off;
    gboolean reversed;
    gdouble *data = datfile->data, *d;
    gchar *xlabel, *ylabel;
    GwyDataLine *dline;
    GwySpectra *spec;
    const DATColumn *abscissa = NULL, *ordinate;

    ncolumns = datfile->ncolumns;
    nrows = datfile->nrows;
    /* Columns that are not spectra values remain NULL later. */
    if (!specset->spectra)
        specset->spectra = g_new0(GwySpectra*, ncolumns);

    reversed = FALSE;
    /* Use the first column as the abscissa. */
    i = 0;
    abscissa = datfile->columns + i;
    real = data[i + ncolumns*(nrows - 1)];
    off = data[i];
    if (real < off) {
        GWY_SWAP(gdouble, real, off);
        reversed = TRUE;
    }
    real -= off;
    xlabel = make_axis_label(abscissa);
    if (!abscissa) {
        gwy_debug("cannot find abscissa");
        return;
    }

    /* Find ordinate columns. */
    for (i = 0; i < ncolumns; i++) {
        ordinate = datfile->columns + i;
        if (gwy_strequal(ordinate->name, abscissa->name))
            continue;

        dline = gwy_data_line_new(nrows, real, FALSE);
        gwy_data_line_set_offset(dline, off);
        gwy_si_unit_set_from_string(gwy_data_line_get_si_unit_x(dline),
                                    ordinate->unit);
        ylabel = make_axis_label(ordinate);
        gwy_si_unit_set_from_string(gwy_data_line_get_si_unit_y(dline),
                                    ordinate->unit);
        d = gwy_data_line_get_data(dline);
        if (reversed) {
            for (j = 0; j < nrows; j++)
                d[nrows-1 - j] = data[i + j*ncolumns];
        }
        else {
            for (j = 0; j < nrows; j++)
                d[j] = data[i + j*ncolumns];
        }

        if (!(spec = specset->spectra[i])) {
            spec = specset->spectra[i] = gwy_spectra_new();
            gwy_si_unit_set_from_string(gwy_spectra_get_si_unit_xy(spec), "m");
            gwy_spectra_set_title(spec, ylabel);
            gwy_spectra_set_spectrum_x_label(spec, xlabel);
            gwy_spectra_set_spectrum_y_label(spec, ylabel);
        }

        gwy_spectra_add_spectrum(specset->spectra[i], dline,
                                 datfile->x, datfile->y);
        g_object_unref(dline);
        g_free(ylabel);
    }

    g_free(xlabel);
}

static DATSingleFile*
read_one_dat_file(const gchar *filename, GError **error)
{
    DATSingleFile *datfile;
    GError *err = NULL;
    gsize size;
    gchar *buf, *line, *p, *sep, **colnames;
    gboolean in_data = FALSE;
    GArray *data = NULL;
    guint i;

    gwy_debug("reading %s", filename);
    if (!g_file_get_contents(filename, &buf, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    datfile = g_new0(DATSingleFile, 1);

    p = buf;
    while ((line = gwy_str_next_line(&p))) {
        if (!*line)
            continue;

        if (in_data && !datfile->columns) {
            gwy_debug("headers %s", line);
            colnames = g_strsplit(line, "\t", 0);
            datfile->ncolumns = g_strv_length(colnames);
            gwy_debug("ncols %u", datfile->ncolumns);
            if (!datfile->ncolumns) {
                err_NO_DATA(error);
                g_free(colnames);
                goto fail;
            }
            datfile->columns = g_new0(DATColumn, datfile->ncolumns);
            for (i = 0; i < datfile->ncolumns; i++)
                parse_column_header(colnames[i], datfile->columns + i);
            /* Individual strings are eaten by parse_column_header(). */
            g_free(colnames);
            data = g_array_new(FALSE, FALSE, sizeof(gdouble));
        }
        else if (in_data) {
            for (i = 0; i < datfile->ncolumns; i++) {
                gdouble v = g_ascii_strtod(line, &sep);

                if (sep == line) {
                    g_set_error(error, GWY_MODULE_FILE_ERROR,
                                GWY_MODULE_FILE_ERROR_DATA,
                                _("Data block is truncated"));
                    goto fail;
                }
                g_array_append_val(data, v);
                line = sep;
            }
        }
        else if (gwy_strequal(line, "[DATA]")) {
            in_data = TRUE;
        }
        else {
            sep = strchr(line, '\t');
            if (!sep)
                continue;
            *sep = '\0';
            sep++;
            if (gwy_stramong(line, "X (m)", "x (m)", NULL)) {
                datfile->x = g_ascii_strtod(sep, NULL);
                gwy_debug("x %g", datfile->x);
            }
            else if (gwy_stramong(line, "Y (m)", "y (m)", NULL)) {
                datfile->y = g_ascii_strtod(sep, NULL);
                gwy_debug("y %g", datfile->y);
            }
            /* We do not care about the other fields because we cannot do
             * anything meaningful with them. */
        }
    }

    g_free(buf);
    datfile->nrows = data->len/datfile->ncolumns;
    datfile->data = (gdouble*)g_array_free(data, FALSE);
    gwy_debug("nrows %u", datfile->nrows);

    return datfile;

fail:
    g_free(buf);
    if (data)
        g_array_free(data, TRUE);
    if (datfile->columns) {
        for (i = 0; i < datfile->ncolumns; i++)
            g_free(datfile->columns[i].storage);
    }
    g_free(datfile);

    return NULL;
}

static void
dat_single_file_free(DATSingleFile *datfile)
{
    guint i;

    for (i = 0; i < datfile->ncolumns; i++)
        g_free(datfile->columns[i].storage);
    g_free(datfile->data);
    g_free(datfile);
}

static gint
compare_filenames(gconstpointer pa, gconstpointer pb)
{
    return strcmp((const gchar*)pa, (const gchar*)pb);
}

static gchar**
find_all_file_names(const gchar *filename)
{
    guint i, numlen, len;
    gchar *dirname, *basename;
    const gchar *fname;
    GPtrArray *fnames;
    GDir *dir;

    basename = g_path_get_basename(filename);
    len = strlen(basename);

    if (len < 6) {
        g_free(basename);
        return NULL;
    }

    i = len-4;
    if (!gwy_stramong(basename + len-4, ".dat", ".DAT", NULL)) {
        g_free(basename);
        return NULL;
    }

    do {
        i--;
    } while (i && g_ascii_isdigit(basename[i]));
    i++;

    numlen = (len-4) - i;
    if (!numlen) {
        g_free(basename);
        return NULL;
    }

    dirname = g_path_get_dirname(filename);
    if (!(dir = g_dir_open(dirname, 0, NULL))) {
        /* We will likely fail anyway when this happen, but fail with some
         * cannot-read-given-file message... */
        g_free(dirname);
        g_free(basename);
        return NULL;
    }

    fnames = g_ptr_array_new();
    /* Find files with the same filename pattern fooNNN.dat */
    while ((fname = g_dir_read_name(dir))) {
        gwy_debug("found file %s", fname);
        if (strlen(fname) != len
            || strncmp(fname, basename, len-4 - numlen) != 0
            || !gwy_stramong(fname + len-4, ".dat", ".DAT", NULL))
            continue;

        for (i = len-4 - numlen; i < len-4; i++) {
            if (!g_ascii_isdigit(fname[i]))
                break;
        }
        if (i < len-4)
            continue;

        gwy_debug("seems matching");
        g_ptr_array_add(fnames, g_build_filename(dirname, fname, NULL));
    }
    g_dir_close(dir);
    g_free(dirname);
    g_free(basename);

    /* This should not normally happen, but something might be changing files
     * on disk... */
    if (!fnames->len) {
        g_ptr_array_free(fnames, TRUE);
        return NULL;
    }

    g_ptr_array_sort(fnames, compare_filenames);
    g_ptr_array_add(fnames, NULL);

    return (gchar**)g_ptr_array_free(fnames, FALSE);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
