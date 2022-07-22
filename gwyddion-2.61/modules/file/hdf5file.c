/*
 *  $Id: hdf5file.c 23140 2021-02-09 12:15:52Z yeti-dn $
 *  Copyright (C) 2020 David Necas (Yeti), Petr Klapetek.
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
 * Asylum Research Ergo HDF5
 * .h5
 * Read
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Avoding clash with a standard file format.
 **/

/*
 * HDF5 changes its APIs between versions incompatibly.  Forward compatibility
 * is not guaranteed.  To prevent breakage we need to know which specific
 * version of the API we use and tell the library to provide this one through
 * compatibility macros.
 *
 * Therefore, this file must be compiled with -DH5_USE_18_API
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <hdf5.h>
#include <hdf5_hl.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

#define MAGIC "\x89HDF\r\n\x1a\n"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define EXTENSION ".h5"

typedef struct {
    gchar *name;
    GwySIUnit *xyunit;
    GwySIUnit *zunit;
    gint xypower10;
    gint zpower10;
    gdouble realcoords[4];
} ErgoChannel;

typedef struct {
    GArray *addr;
    GString *path;
    GString *buf;
    GwyContainer *meta;
    GArray *channels;
    GArray *resolutions;
    gint nframes;
} ErgoFile;

static gboolean      module_register   (void);
static gint          ergo_detect       (const GwyFileDetectInfo *fileinfo,
                                        gboolean only_name);
static GwyContainer* ergo_load         (const gchar *filename,
                                        GwyRunType mode,
                                        GError **error);
static GwyContainer* ergo_read_channels(hid_t file_id,
                                        ErgoFile *efile,
                                        GError **error);
static GwyDataField* ergo_read_image   (hid_t file_id,
                                        guint r,
                                        ErgoChannel *c,
                                        gint frameid,
                                        const gint *xyres,
                                        GString *str,
                                        GError **error);
static herr_t        ergo_scan_file    (hid_t loc_id,
                                        const char *name,
                                        const H5L_info_t *info,
                                        void *user_data);
static herr_t        process_attribute (hid_t loc_id,
                                        const char *attr_name,
                                        const H5A_info_t *ainfo,
                                        void *user_data);
static gboolean      get_ints_attr     (hid_t file_id,
                                        const gchar *obj_path,
                                        const gchar *attr_name,
                                        gint expected_rank,
                                        const gint *expected_dims,
                                        gint *v,
                                        GError **error);
static gboolean      get_int_attr      (hid_t file_id,
                                        const gchar *obj_path,
                                        const gchar *attr_name,
                                        gint *v,
                                        GError **error);
static gboolean      get_floats_attr   (hid_t file_id,
                                        const gchar *obj_path,
                                        const gchar *attr_name,
                                        gint expected_rank,
                                        const gint *expected_dims,
                                        gdouble *v,
                                        GError **error);
static gboolean      get_float_attr    (hid_t file_id,
                                        const gchar *obj_path,
                                        const gchar *attr_name,
                                        gdouble *v,
                                        GError **error);
static gboolean      get_strs_attr     (hid_t file_id,
                                        const gchar *obj_path,
                                        const gchar *attr_name,
                                        gint expected_rank,
                                        const gint *expected_dims,
                                        gchar **v,
                                        GError **error);
static gboolean      get_str_attr      (hid_t file_id,
                                        const gchar *obj_path,
                                        const gchar *attr_name,
                                        gchar **v,
                                        GError **error);

static hid_t str_vlen_type = -1;

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports files based on Hierarchical Data Format (HDF), version 5."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2020",
};

GWY_MODULE_QUERY2(module_info, hdf5file)

static gboolean
module_register(void)
{
    if (H5open() < 0) {
        g_warning("H5open() failed.");
        return FALSE;
    }
#ifndef DEBUG
    H5Eset_auto2(H5E_DEFAULT, NULL, NULL);
#endif

    /* We use the varlen string type all the time.  So keep it around.
     * However, reading strings is odd.  See the comment in get_strs_attr(). */
    str_vlen_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(str_vlen_type, H5T_VARIABLE);
    H5Tset_cset(str_vlen_type, H5T_CSET_UTF8);

    gwy_file_func_register("ergofile",
                           N_("Asylum Research Ergo HDF5 files (.h5)"),
                           (GwyFileDetectFunc)&ergo_detect,
                           (GwyFileLoadFunc)&ergo_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
ergo_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    hid_t file_id;
    gchar *format;
    gint version[3], dim = 3;

    if (only_name)
        return 0;

    if (fileinfo->buffer_len <= MAGIC_SIZE
        || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    file_id = H5Fopen(fileinfo->name, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file_id < 0)
        return 0;

    if (!get_str_attr(file_id, ".", "ARFormat", &format, NULL)
        || !get_ints_attr(file_id, ".", "ARVersion", 1, &dim, version, NULL)) {
        H5Fclose(file_id);
        return 0;
    }

    H5Fclose(file_id);

    return 100;
}

static void
err_HDF5(GError **error, const gchar *where, glong code)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                _("HDF5 library error %ld in function %s."),
                code, where);
}

static GwyContainer*
ergo_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    GwyContainer *container = NULL;
    ErgoFile efile;
    hid_t file_id;
    G_GNUC_UNUSED herr_t status;
    H5O_info_t infobuf;
    guint i;

    file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    gwy_debug("file_id %d", (gint)file_id);
    status = H5Oget_info(file_id, &infobuf);
    gwy_debug("status %d", status);

    gwy_clear(&efile, 1);
    efile.meta = gwy_container_new();
    efile.path = g_string_new(NULL);
    efile.buf = g_string_new(NULL);
    efile.addr = g_array_new(FALSE, FALSE, sizeof(haddr_t));
    efile.channels = g_array_new(FALSE, FALSE, sizeof(ErgoChannel));
    efile.resolutions = g_array_new(FALSE, FALSE, sizeof(gint));
    g_array_append_val(efile.addr, infobuf.addr);

    status = H5Literate(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL,
                        ergo_scan_file, &efile);

    H5Aiterate2(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL,
                process_attribute, &efile);

    if (get_int_attr(file_id, "DataSetInfo", "NumFrames", &efile.nframes,
                     error)) {
        gwy_debug("nframes %d", efile.nframes);
        container = ergo_read_channels(file_id, &efile, error);
    }

    status = H5Fclose(file_id);
    gwy_debug("status %d", status);

    g_array_set_size(efile.addr, efile.addr->len-1);
    g_array_free(efile.addr, TRUE);
    g_string_free(efile.path, TRUE);
    g_string_free(efile.buf, TRUE);
    for (i = 0; i < efile.channels->len; i++) {
        ErgoChannel *c = &g_array_index(efile.channels, ErgoChannel, i);
        g_free(c->name);
        GWY_OBJECT_UNREF(c->xyunit);
        GWY_OBJECT_UNREF(c->zunit);
    }
    g_array_free(efile.channels, TRUE);
    g_array_free(efile.resolutions, TRUE);
    GWY_OBJECT_UNREF(efile.meta);

    return container;
}

static GwyContainer*
ergo_read_channels(hid_t file_id, ErgoFile *efile, GError **error)
{
    GwyContainer *meta, *container = NULL;
    GArray *channels = efile->channels;
    GArray *resolutions = efile->resolutions;
    GString *str = efile->buf;
    GwyDataField *dfield;
    gint expected2[2] = { 2, 2 }, xyres[2];
    gchar *s, *s2[2];
    gint frameid, id = 0;
    GQuark quark;
    guint i, ri, r;

    for (ri = 0; ri < resolutions->len; ri++) {
        r = g_array_index(resolutions, guint, ri);
        for (i = 0; i < channels->len; i++) {
            ErgoChannel *c = &g_array_index(channels, ErgoChannel, i);

            g_string_printf(str, "DataSetInfo/Global/Channels/%s/ImageDims",
                            c->name);

            if (!get_str_attr(file_id, str->str, "DataUnits", &s, error))
                goto fail;
            gwy_debug("zunit of %s is %s", c->name, s);
            c->zunit = gwy_si_unit_new_parse(s, &c->zpower10);

            if (!get_strs_attr(file_id, str->str, "DimUnits",
                               1, expected2, s2, error))
                goto fail;
            gwy_debug("xyunits of %s are %s and %s", c->name, s2[0], s2[1]);
            if (!gwy_strequal(s2[0], s2[1]))
                g_warning("X and Y units differ, using X");
            c->xyunit = gwy_si_unit_new_parse(s2[0], &c->xypower10);

            if (!get_floats_attr(file_id, str->str, "DimScaling",
                                 2, expected2, c->realcoords, error))
                goto fail;
            gwy_debug("dims of %s are [%g, %g], [%g, %g]",
                      c->name,
                      c->realcoords[0], c->realcoords[1],
                      c->realcoords[2], c->realcoords[3]);

            g_string_append_printf(str, "/Resolution %d", r);
            if (!get_ints_attr(file_id, str->str, "DimExtents",
                               1, expected2, xyres, error))
                goto fail;
            gwy_debug("resid %u res %dx%d", r, xyres[0], xyres[1]);

            for (frameid = 0; frameid < efile->nframes; frameid++) {
                if (!(dfield = ergo_read_image(file_id, r, c, frameid, xyres,
                                               str, error)))
                    goto fail;

                if (!container)
                    container = gwy_container_new();

                quark = gwy_app_get_data_key_for_id(id);
                gwy_container_set_object(container, quark, dfield);
                g_object_unref(dfield);

                quark = gwy_app_get_data_title_key_for_id(id);
                gwy_container_set_const_string(container, quark, c->name);

                meta = gwy_container_duplicate(efile->meta);
                quark = gwy_app_get_data_meta_key_for_id(id);
                gwy_container_set_object(container, quark, meta);
                g_object_unref(meta);

                id++;
            }
        }
    }

    if (container)
        return container;

    err_NO_DATA(error);

fail:
    GWY_OBJECT_UNREF(container);
    return NULL;
}

static GwyDataField*
ergo_read_image(hid_t file_id,
                guint r, ErgoChannel *c, gint frameid, const gint *xyres,
                GString *str, GError **error)
{
    GwyDataField *dfield;
    hid_t dataset, space;
    gdouble *data;
    gint nitems;
    gdouble q, xreal, yreal, xoff, yoff;
    herr_t status;

    g_string_printf(str, "DataSet/Resolution %u/Frame %d/%s/Image",
                    r, frameid, c->name);
    if ((dataset = H5Dopen(file_id, str->str, H5P_DEFAULT)) < 0) {
        err_HDF5(error, "H5Dopen", dataset);
        return NULL;
    }
    gwy_debug("dataset %s is %d", str->str, (gint)dataset);

    if ((space = H5Dget_space(dataset)) < 0) {
        err_HDF5(error, "H5Dget_space", space);
        H5Dclose(dataset);
        return NULL;
    }
    nitems = H5Sget_simple_extent_npoints(space);
    gwy_debug("dataset space is %d with %d items", (gint)space, nitems);
    if (nitems != xyres[0]*xyres[1]) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Dataset %s has %d items, which does not match "
                      "image resolution %d×%d."),
                    str->str, nitems, xyres[0], xyres[1]);
        H5Sclose(space);
        H5Dclose(dataset);
        return NULL;
    }

    q = pow10(c->xypower10);

    xreal = c->realcoords[1] - c->realcoords[0];
    if (!((xreal = fabs(xreal)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        xreal = 1.0;
    }
    xoff = MIN(c->realcoords[0], c->realcoords[1]);

    yreal = c->realcoords[3] - c->realcoords[2];
    if (!((yreal = fabs(yreal)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        yreal = 1.0;
    }
    yoff = MIN(c->realcoords[2], c->realcoords[3]);

    dfield = gwy_data_field_new(xyres[0], xyres[1], q*xreal, q*yreal,
                                FALSE);
    gwy_data_field_set_xoffset(dfield, q*xoff);
    gwy_data_field_set_yoffset(dfield, q*yoff);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(dfield), c->xyunit);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield), c->zunit);

    data = gwy_data_field_get_data(dfield);
    status = H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                     data);
    gwy_data_field_invert(dfield, TRUE, FALSE, FALSE);
    if (c->zpower10)
        gwy_data_field_multiply(dfield, pow10(c->zpower10));

    H5Sclose(space);
    H5Dclose(dataset);

    if (status < 0) {
        err_HDF5(error, "H5Dread", status);
        GWY_OBJECT_UNREF(dfield);
    }
    return dfield;
}

static void
append_channel_name(GArray *channels, const gchar *name)
{
    ErgoChannel c;

    gwy_debug("found channel %s", name);
    gwy_clear(&c, 1);
    c.name = g_strdup(name);
    g_array_append_val(channels, c);
}

static herr_t
process_attribute(hid_t loc_id,
                  const char *attr_name,
                  G_GNUC_UNUSED const H5A_info_t *ainfo,
                  void *user_data)
{
    ErgoFile *efile = (ErgoFile*)user_data;
    GString *path = efile->path, *buf = efile->buf;
    gint i, nitems;
    guint len = path->len;
    gboolean is_vlenstr = FALSE, is_channel_names = FALSE;
    hid_t attr, attr_type, space;
    H5T_class_t type_class;
    herr_t status;

    attr = H5Aopen(loc_id, attr_name, H5P_DEFAULT);
    attr_type = H5Aget_type(attr);
    space = H5Aget_space(attr);
    nitems = H5Sget_simple_extent_npoints(space);
    type_class = H5Tget_class(attr_type);
    if (type_class == H5T_STRING)
        is_vlenstr = H5Tis_variable_str(attr_type);

    g_string_append_c(path, '/');
    g_string_append(path, attr_name);
    is_channel_names = gwy_strequal(path->str, "/DataSetInfo/ChannelNames");
    status = -1;
    /* Try to read all attribute types used by Ergo; there are just a few. */
    if (type_class == H5T_INTEGER) {
        if (nitems == 1) {
            gint v;
            if ((status = H5Aread(attr, H5T_NATIVE_INT, &v)) >= 0)
                g_string_printf(buf, "%d", v);
        }
        else if (nitems > 0) {
            gint *v = g_new(gint, nitems);
            if ((status = H5Aread(attr, H5T_NATIVE_INT, v)) >= 0) {
                g_string_printf(buf, "%d", v[0]);
                for (i = 1; i < nitems; i++)
                    g_string_append_printf(buf, "; %d", v[i]);
            }
            g_free(v);
        }
    }
    else if (type_class == H5T_FLOAT) {
        if (nitems == 1) {
            gdouble v;
            if ((status = H5Aread(attr, H5T_NATIVE_DOUBLE, &v)) >= 0)
                g_string_printf(buf, "%.8g", v);
        }
        else if (nitems > 0) {
            gdouble *v = g_new(gdouble, nitems);
            if ((status = H5Aread(attr, H5T_NATIVE_DOUBLE, v)) >= 0) {
                g_string_printf(buf, "%.8g", v[0]);
                for (i = 1; i < nitems; i++)
                    g_string_append_printf(buf, "; %.8g", v[i]);
            }
            g_free(v);
        }
    }
    else if (type_class == H5T_STRING && is_vlenstr) {
        if (nitems == 1) {
            gchar *s;
            if ((status = H5Aread(attr, str_vlen_type, &s)) >= 0) {
                g_string_printf(buf, "%s", s);
                if (is_channel_names)
                    append_channel_name(efile->channels, s);
            }
        }
        else if (nitems > 0) {
            gchar **s = g_new(gchar*, nitems);
            if ((status = H5Aread(attr, str_vlen_type, s)) >= 0) {
                g_string_assign(buf, s[0]);
                if (is_channel_names)
                    append_channel_name(efile->channels, s[0]);
                for (i = 1; i < nitems; i++) {
                    g_string_append(buf, "; ");
                    g_string_append(buf, s[i]);
                    if (is_channel_names)
                        append_channel_name(efile->channels, s[i]);
                }
            }
            g_free(s);
        }
    }

    if (status >= 0) {
        gwy_debug("[%s] = <%s>", path->str, buf->str);
        gwy_container_set_const_string_by_name(efile->meta,
                                               path->str, buf->str);
    }
    else {
        g_warning("Cannot handle attribute %d(%d)[%d]",
                  type_class, is_vlenstr, nitems);
    }
    g_string_truncate(path, len);

    H5Sclose(space);
    H5Tclose(attr_type);
    H5Aclose(attr);

    return 0;
}

/* NB: loc_id is ‘parent’ location and name is particlar item within it. */
static herr_t
ergo_scan_file(hid_t loc_id,
               const char *name,
               G_GNUC_UNUSED const H5L_info_t *info,
               void *user_data)
{
    ErgoFile *efile = (ErgoFile*)user_data;
    herr_t status, return_val = 0;
    H5O_info_t infobuf;
    GArray *addr = efile->addr;
    GString *path = efile->path;
    guint i, len = path->len;
    gchar *p;

    status = H5Oget_info_by_name(loc_id, name, &infobuf, H5P_DEFAULT);
    if (status < 0)
        return status;

    /* Detect loops. */
    for (i = 0; i < addr->len; i++) {
        if (g_array_index(addr, haddr_t, i) == infobuf.addr)
            return -1;
    }

    g_array_append_val(addr, infobuf.addr);
    g_string_append_c(path, '/');
    g_string_append(path, name);
    if (infobuf.type == H5O_TYPE_GROUP) {
        return_val = H5Literate_by_name(loc_id, name,
                                        H5_INDEX_NAME, H5_ITER_NATIVE,
                                        NULL, ergo_scan_file, user_data,
                                        H5P_DEFAULT);
        /* Enumerate resolutions. */
        if (g_str_has_prefix(path->str, "/DataSet/Resolution ")) {
            p = path->str + strlen("/DataSet/Resolution ");
            for (i = 0; g_ascii_isdigit(p[i]); i++)
                ;
            if (i > 0 && !p[i]) {
                i = atol(p);
                g_array_append_val(efile->resolutions, i);
                gwy_debug("resolution %u", i);
            }
        }
    }
    /* Nothing to do for other object types. */
    else if (infobuf.type == H5O_TYPE_DATASET) {
    }
    else if (infobuf.type == H5O_TYPE_NAMED_DATATYPE) {
    }
    else {
    }

    if (infobuf.num_attrs > 0) {
        hid_t this_id = H5Oopen(loc_id, name, H5P_DEFAULT);

        H5Aiterate2(this_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL,
                    process_attribute, user_data);
        H5Oclose(this_id);
    }

    g_string_truncate(path, len);
    g_array_set_size(addr, addr->len-1);

    return return_val;
}

static hid_t
open_and_check_attr(hid_t file_id,
                    const gchar *obj_path,
                    const gchar *attr_name,
                    H5T_class_t expected_class,
                    gint expected_rank,
                    const gint *expected_dims,
                    GError **error)
{
    hid_t attr, attr_type, space;
    H5T_class_t type_class;
    gint i, rank, status;
    hsize_t dims[3];

    gwy_debug("looking for %s in %s, class %d, rank %d",
              attr_name, obj_path, expected_class, expected_rank);
    if ((attr = H5Aopen_by_name(file_id, obj_path, attr_name,
                                H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        err_MISSING_FIELD(error, attr_name);
        return -1;
    }

    attr_type = H5Aget_type(attr);
    type_class = H5Tget_class(attr_type);
    gwy_debug("found attr %d of type %d and class %d",
              (gint)attr, (gint)attr_type, type_class);
    if (type_class != expected_class) {
        H5Tclose(attr_type);
        H5Aclose(attr);
        err_UNSUPPORTED(error, attr_name);
        return -1;
    }

    if ((space = H5Aget_space(attr)) < 0) {
        err_HDF5(error, "H5Aget_space", space);
        H5Tclose(attr_type);
        H5Aclose(attr);
    }
    rank = H5Sget_simple_extent_ndims(space);
    gwy_debug("attr space is %d with rank %d", (gint)space, rank);
    if (rank > 3 || rank != expected_rank) {
        err_UNSUPPORTED(error, attr_name);
        goto fail;
    }

    if ((status = H5Sget_simple_extent_dims(space, dims, NULL)) < 0) {
        gwy_debug("cannot get space %d extent dims", (gint)space);
        err_HDF5(error, "H5Sget_simple_extent_dims", status);
        goto fail;
    }
    for (i = 0; i < rank; i++) {
        gwy_debug("dims[%d]=%lu, expecting %lu", i,
                  (gulong)dims[i], (gulong)expected_dims[i]);
        if (dims[i] != (hsize_t)expected_dims[i]) {
            err_UNSUPPORTED(error, attr_name);
            goto fail;
        }
    }

    H5Sclose(space);
    H5Tclose(attr_type);
    gwy_debug("attr %d seems OK", (gint)attr);
    return attr;

fail:
    H5Sclose(space);
    H5Tclose(attr_type);
    H5Aclose(attr);
    return -1;
}

static gboolean
get_ints_attr(hid_t file_id,
              const gchar *obj_path,
              const gchar *attr_name,
              gint expected_rank,
              const gint *expected_dims,
              gint *v,
              GError **error)
{
    hid_t attr;
    gint status;

    if ((attr = open_and_check_attr(file_id, obj_path, attr_name,
                                    H5T_INTEGER,
                                    expected_rank, expected_dims, error)) < 0)
        return FALSE;

    status = H5Aread(attr, H5T_NATIVE_INT, v);
    H5Aclose(attr);
    if (status < 0) {
        err_HDF5(error, "H5Aread", status);
        return FALSE;
    }
    return TRUE;
}

static gboolean
get_int_attr(hid_t file_id,
             const gchar *obj_path, const gchar *attr_name,
             gint *v, GError **error)
{
    return get_ints_attr(file_id, obj_path, attr_name, 0, NULL, v, error);
}

static gboolean
get_floats_attr(hid_t file_id,
                const gchar *obj_path,
                const gchar *attr_name,
                gint expected_rank,
                const gint *expected_dims,
                gdouble *v,
                GError **error)
{
    hid_t attr;
    gint status;

    if ((attr = open_and_check_attr(file_id, obj_path, attr_name,
                                    H5T_FLOAT,
                                    expected_rank, expected_dims, error)) < 0)
        return FALSE;

    status = H5Aread(attr, H5T_NATIVE_DOUBLE, v);
    H5Aclose(attr);
    if (status < 0) {
        err_HDF5(error, "H5Aread", status);
        return FALSE;
    }
    return TRUE;
}

G_GNUC_UNUSED
static gboolean
get_float_attr(hid_t file_id,
               const gchar *obj_path, const gchar *attr_name,
               gdouble *v, GError **error)
{
    return get_floats_attr(file_id, obj_path, attr_name, 0, NULL, v, error);
}

static gboolean
get_strs_attr(hid_t file_id,
              const gchar *obj_path,
              const gchar *attr_name,
              gint expected_rank,
              const gint *expected_dims,
              gchar **v,
              GError **error)
{
    static gboolean already_did_the_silly_thing = FALSE;

    hid_t attr, attr_type;
    gboolean is_vlenstr;
    gint status;
    gchar *no_idea_why_i_have_to_do_this;

    if ((attr = open_and_check_attr(file_id, obj_path, attr_name,
                                    H5T_STRING,
                                    expected_rank, expected_dims, error)) < 0)
        return FALSE;

    attr_type = H5Aget_type(attr);
    if (attr_type < 0) {
        H5Aclose(attr);
        err_HDF5(error, "H5Aget_type", attr_type);
        return FALSE;
    }
    is_vlenstr = H5Tis_variable_str(attr_type);
    gwy_debug("attr %d is%s vlen string", (gint)attr, is_vlenstr ? "" : " not");
    if (!is_vlenstr) {
        H5Tclose(attr_type);
        H5Aclose(attr);
        /* XXX: Be more specific. */
        err_UNSUPPORTED(error, attr_name);
        return FALSE;
    }

    /* We must read a string with its own attr_type once at the beginning.
     * There are various ways to do that, for instance using
     * H5LTget_attribute_string().  This simply reproduces the important call
     * which happens inside.
     *
     * We only need to do this once, subsequent string reading then works
     * without problems.  So we *MUST* call this function with a scalar string
     * (expected_rank of 0) as the first thing.  This normally happens because
     * we look for ARFormat. */
    if (expected_rank == 0 && !already_did_the_silly_thing) {
        if (H5Aread(attr, attr_type, &no_idea_why_i_have_to_do_this) < 0) {
            gwy_debug("cannot read attr %d with its own type %d",
                      (gint)attr, (gint)attr_type);
            H5Tclose(attr_type);
            H5Aclose(attr);
            err_UNSUPPORTED(error, attr_name);
            return FALSE;
        }
        already_did_the_silly_thing = TRUE;
    }

    status = H5Aread(attr, str_vlen_type, v);
    H5Tclose(attr_type);
    H5Aclose(attr);
    if (status < 0) {
        err_HDF5(error, "H5Aread", status);
        return FALSE;
    }
    return TRUE;
}

static gboolean
get_str_attr(hid_t file_id,
             const gchar *obj_path, const gchar *attr_name,
             gchar **v, GError **error)
{
    return get_strs_attr(file_id, obj_path, attr_name, 0, NULL, v, error);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
