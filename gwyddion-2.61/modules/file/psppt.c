/*
 *  $Id: psppt.c 24791 2022-04-28 13:46:18Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-park-ps-ppt-spm">
 *   <comment>Park Systems PS-PPT</comment>
 *   <magic priority="75">
 *     <match type="string" offset="0" value="PS-PPT/v1\n"/>
 *   </magic>
 *   <glob pattern="*.ps-ppt"/>
 *   <glob pattern="*.PS-PPT"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Park Systems PS-PPT
 * 0 string PS-PPT/v1\x0a Park Systems PS-PPT curve map data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Park Systems PS-PPT
 * .ps-ppt
 * Read
 **/

#include "config.h"
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libprocess/spectra.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>
#include <app/wait.h>
#include <jansson.h>

#include "get.h"
#include "err.h"

#define MAGIC "PS-PPT/v1\n"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

enum {
    HEADER_SIZE = 16,
    FRAME_SIZE = 8
};

typedef enum {
    PSPPT_SCAN_START = 0,
    PSPPT_SCAN_STOP  = 1,
    PSPPT_PARAM      = 16,
    PSPPT_RTFD       = 17,
    PSPPT_UNUSED     = 255,
} PSPPTDataType;

typedef struct {
    gchar magic[MAGIC_SIZE];
    guint unused1;
    guint nframes;
    guint next_table_offset_unused;
    guint reserved1;
    guint reserved2;
} PSPPTHeader;

typedef struct {
    /* Frame table. */
    PSPPTDataType type;
    guint reserved;
    guint offset;
    /* JSON or precalculated. */
    guint size;
} PSPPTFrame;

typedef struct {
    json_t *root;
    gint xres;
    gint yres;
    gchar *direction;
    gdouble xreal;
    gdouble yreal;
} PSPPTScanStart;

typedef struct {
    json_t *root;
} PSPPTScanStop;

typedef struct {
    json_t *root;
} PSPPTParam;

typedef struct {
    PSPPTHeader header;
    PSPPTScanStart scanstart;
    PSPPTScanStop scanstop;
    PSPPTParam param;
    guint nframes;           /* Real number; can be smaller than header.frames. */
    PSPPTFrame *frames;
    gchar **ids;
    gchar **units;
    gdouble *power10;
    gint *reorder;
    GString *str;
    GArray *databuf;
    GwyLawn *lawn;
    GwyContainer *meta;
} PSPPTFile;

static gboolean      module_register       (void);
static gint          psppt_detect          (const GwyFileDetectInfo *fileinfo,
                                            gboolean only_name);
static GwyContainer* psppt_load            (const gchar *filename,
                                            GwyRunType mode,
                                            GError **error);
static gboolean      check_string_list_item(json_t *root,
                                            const gchar *name,
                                            gchar **values,
                                            guint i,
                                            GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    module_register,
    N_("Imports Park Systems PS-PPT data files."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, psppt)

static gboolean
module_register(void)
{
    gwy_file_func_register("psppt",
                           N_("Park Systems PS-PPT data files (.ps-ppt)"),
                           (GwyFileDetectFunc)&psppt_detect,
                           (GwyFileLoadFunc)&psppt_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
psppt_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, ".ps-ppt") ? 20 : 0;

    if (fileinfo->buffer_len >= MAGIC_SIZE && !memcmp(fileinfo->head, MAGIC, MAGIC_SIZE))
        return 80;

    return 0;
}

static gboolean
psppt_read_header(PSPPTHeader *header, const guchar *buf, gsize size, gsize *pos, GError **error)
{
    const guchar *p = buf + *pos;

    if ((size - *pos) < HEADER_SIZE + MAGIC_SIZE) {
        err_FILE_TYPE(error, "PS-PPT/v1");
        return FALSE;
    }
    get_CHARARRAY(header->magic, &p);
    if (memcmp(header->magic, MAGIC, MAGIC_SIZE)) {
        err_FILE_TYPE(error, "PS-PPT/v1");
        return FALSE;
    }
    /* They have a one-byte field and three-byte field there.  Read it as one number. */
    header->nframes = gwy_get_guint32_be(&p);
    header->unused1 = (header->nframes >> 24);
    header->nframes &= 0xffffff;
    gwy_debug("unused %u, nframes %u", header->unused1, header->nframes);
    header->next_table_offset_unused = gwy_get_guint32_be(&p);
    header->reserved1 = gwy_get_guint32_be(&p);
    header->reserved2 = gwy_get_guint32_be(&p);
    gwy_debug("next_offset %u, reserved1 %u, reserved2 %u",
              header->next_table_offset_unused, header->reserved1, header->reserved2);

    *pos += p - buf;
    return TRUE;
}

static gsize
psppt_read_frame_table(PSPPTFile *pfile, const guchar *buf, gsize size, gsize *pos, GError **error)
{
    const guchar *p = buf + *pos;
    gsize framepos;
    guint i, j, ndata = 0, nframes = pfile->header.nframes;
    PSPPTFrame *frames;

    if ((size - *pos)/FRAME_SIZE < nframes) {
        err_TRUNCATED_PART(error, "Frame Table");
        return FALSE;
    }

    framepos = *pos + nframes*FRAME_SIZE;
    pfile->frames = frames = g_new(PSPPTFrame, nframes);
    /* Compactify frames (get rid of empty ones) while reading. */
    for (i = j = 0; i < nframes; i++) {
        PSPPTFrame *frame = frames + j;

        /* They have a one-byte field and three-byte field there.  Read it as one number. */
        frame->reserved = gwy_get_guint32_be(&p);
        frame->type = (frame->reserved >> 24);
        frame->reserved &= 0xffffff;
        frame->offset = gwy_get_guint32_be(&p);
        if (frame->type == PSPPT_UNUSED)
            continue;

        gwy_debug("[%u] type %u, offset %u (reserved %u)", i, frame->type, frame->offset, frame->reserved);
        if (frame->offset >= size) {
            err_TRUNCATED_PART(error, "Frame");
            return FALSE;
        }
        if (frame->offset <= framepos) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Frame offsets do not increase monotonically."));
            return FALSE;
        }
        framepos = frame->offset;
        j++;
    }
    pfile->nframes = nframes = j;

    /* Verify frame types. */
    ndata = 0;
    for (i = 0; i < nframes; i++) {
        PSPPTDataType type = frames[i].type;
        gboolean ok = FALSE;

        if (i == 0)
            ok = (type == PSPPT_SCAN_START);
        else if (i == 1)
            ok = (type == PSPPT_PARAM);
        else if (i == nframes-1)
            ok = (type == PSPPT_SCAN_STOP);
        else {
            ok = (type == PSPPT_RTFD || type == PSPPT_PARAM);
            if (type == PSPPT_RTFD)
                ndata++;
        }

        if (!ok) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Unexpected frame with data type %d."), type);
            return FALSE;
        }
    }
    if (!ndata) {
        err_NO_DATA(error);
        return FALSE;
    }

    /* Calculate frame sizes. */
    framepos = size;
    for (i = nframes; i; i--) {
        frames[i-1].size = framepos - frames[i-1].offset;
        framepos = frames[i-1].offset;
    }
    *pos += p - buf;
    return TRUE;
}

static gboolean
err_JSON_STRUCTURE(GError **error, const gchar *what, const gchar *type)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Unexpected JSON structure: %s should be %s."), what, type);
    return FALSE;
}

static gboolean
err_INCONSISTENT(GError **error)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Inconsistent structure of individual spectra."));
    return FALSE;
}

static gboolean
get_json_with_type(json_t *object, json_t **member, const gchar *key, json_type type, GError **error)
{
    static const GwyEnum typenames[] = {
        { "object",  JSON_OBJECT,  },
        { "array",   JSON_ARRAY,   },
        { "string",  JSON_STRING,  },
        { "integer", JSON_INTEGER, },
        { "real",    JSON_REAL,    },
        { "boolean", JSON_TRUE,    },
    };

    *member = json_object_get(object, key);
    if (*member) {
        if (type == JSON_TRUE) {
            if (json_is_boolean(*member))
                return TRUE;
        }
        else if (type == JSON_REAL) {
            if (json_is_number(*member))
                return TRUE;
        }
        else {
            if (json_typeof(*member) == type)
                return TRUE;
        }
    }

    return err_JSON_STRUCTURE(error, key, gwy_enum_to_string(type, typenames, G_N_ELEMENTS(typenames)));
}

static json_t*
psppt_read_frame(PSPPTFrame *frame, const guchar *buf, GError **error)
{
    json_error_t jerror;
    const gchar *start = buf + frame->offset;
    json_t *root;

    root = json_loadb(start, frame->size, 0, &jerror);
    if (!root) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("JSON parsing error: %s"), jerror.text);
        return NULL;
    }
    if (!json_is_object(root)) {
        err_JSON_STRUCTURE(error, "root", "object");
        json_decref(root);
        return NULL;
    }

    return root;
}

static gboolean
handle_scan_start(PSPPTScanStart *scanstart, json_t *root, GError **error)
{
    json_t *type, *geometry, *direction, *pixel_width, *pixel_height, *width, *height;

    if (!get_json_with_type(root, &type, "type", JSON_STRING, error)
        || !get_json_with_type(root, &geometry, "geometry", JSON_OBJECT, error)
        || !get_json_with_type(geometry, &direction, "direction", JSON_STRING, error)
        || !get_json_with_type(geometry, &pixel_height, "pixelHeight", JSON_INTEGER, error)
        || !get_json_with_type(geometry, &pixel_width, "pixelWidth", JSON_INTEGER, error)
        || !get_json_with_type(geometry, &width, "width", JSON_REAL, error)
        || !get_json_with_type(geometry, &height, "height", JSON_REAL, error))
        return FALSE;
    if (!gwy_strequal(json_string_value(type), "scan.start"))
        return err_JSON_STRUCTURE(error, "scan.start.type", "scan.start");

    json_incref(root);
    scanstart->root = root;
    scanstart->xres = json_integer_value(pixel_width);
    scanstart->yres = json_integer_value(pixel_height);
    scanstart->xreal = json_number_value(width) * 1e-6;
    scanstart->yreal = json_number_value(height) * 1e-6;
    scanstart->direction = g_strdup(json_string_value(direction));
    gwy_debug("xres = %u, yres = %u", scanstart->xres, scanstart->yres);
    gwy_debug("xreal = %g, yreal = %g", scanstart->xreal, scanstart->yreal);
    gwy_debug("direction = %s", scanstart->direction);

    return TRUE;
}

static gboolean
handle_scan_stop(PSPPTScanStop *scanstop, json_t *root, GError **error)
{
    json_t *type;

    if (!get_json_with_type(root, &type, "type", JSON_STRING, error))
        return FALSE;
    if (!gwy_strequal(json_string_value(type), "scan.stop"))
        return err_JSON_STRUCTURE(error, "scan.stop.type", "scan.stop");

    json_incref(root);
    scanstop->root = root;

    return TRUE;
}

static gboolean
handle_param(PSPPTParam *param, json_t *root, GError **error)
{
    json_t *type;

    if (!get_json_with_type(root, &type, "type", JSON_STRING, error))
        return FALSE;
    if (!gwy_strequal(json_string_value(type), "ppt.param"))
        return err_JSON_STRUCTURE(error, "ppt.param.type", "ppt.param");

    /* We can keep the first or the last or any of them if parameter change…  Keeping the first is simplest. */
    if (!param->root) {
        json_incref(root);
        param->root = root;
    }

    return TRUE;
}

static gboolean
try_to_reorder(gchar **names, gint *order, gint n,
               const gchar *name, gint movewhere)
{
    gint i;

    if (movewhere < 0 || movewhere >= n)
        return FALSE;
    for (i = 0; i < n; i++) {
        if (gwy_strequal(names[order[i]], name))
            break;
    }
    if (i == n)
        return FALSE;
    if (i == movewhere)
        return TRUE;

    GWY_SWAP(gint, order[i], order[movewhere]);
    return TRUE;
}

static gboolean
handle_rtfd(PSPPTFile *pfile, json_t *root, GError **error)
{
    json_t *type, *info, *numbers, *channels, *indices, *fast, *slow, *padding, *item;
    guint n, i, ri, nnum, col, row, npts, base64len = 0;
    gint power10;
    GString *str = pfile->str;
    GwyLawn *lawn;
    gboolean G_GNUC_UNUSED pad, lawn_is_new = FALSE;
    gdouble *d;

    if (!get_json_with_type(root, &type, "type", JSON_STRING, error)
        || !get_json_with_type(root, &info, "info", JSON_OBJECT, error)
        || !get_json_with_type(root, &numbers, "numbers", JSON_ARRAY, error)
        || !get_json_with_type(info, &channels, "channels", JSON_ARRAY, error)
        || !get_json_with_type(info, &indices, "index", JSON_OBJECT, error)
        || !get_json_with_type(info, &padding, "padding", JSON_TRUE, error)
        || !get_json_with_type(indices, &fast, "fast", JSON_INTEGER, error)
        || !get_json_with_type(indices, &slow, "slow", JSON_INTEGER, error))
        return FALSE;
    if (!gwy_strequal(json_string_value(type), "ppt.rtfd"))
        return err_JSON_STRUCTURE(error, "ppt.rtfd.type", "ppt.rtfd");

    n = json_array_size(channels);
    nnum = json_array_size(numbers);
    col = json_integer_value(fast);
    row = json_integer_value(slow);
    pad = json_boolean_value(padding);
    gwy_debug("(%u,%u) nchannels = %u, nnumbers = %u, padding = %d", col, row, n, nnum, pad);

    /* The first time we encounter a spectrum set use it as a template.  All other sets must follow the same
     * structure. */
    lawn = pfile->lawn;
    if (!n || n != nnum || (lawn && n != gwy_lawn_get_n_curves(lawn)))
        return err_INCONSISTENT(error);

    if (!pfile->lawn) {
        const PSPPTScanStart *ss = &pfile->scanstart;

        lawn_is_new = TRUE;
        gwy_debug("creating lawn xres=%d, yres=%d", ss->xres, ss->yres);
        lawn = pfile->lawn = gwy_lawn_new(ss->xres, ss->yres, ss->xreal, ss->yreal, n, 0);
        gwy_si_unit_set_from_string(gwy_lawn_get_si_unit_xy(lawn), "m");
        /* These duplicates lawn's properties, but can be used with check_string_list_item(). */
        pfile->units = g_new0(gchar*, n+1);
        pfile->ids = g_new0(gchar*, n+1);
        pfile->power10 = g_new0(gdouble, n);
        pfile->reorder = g_new(gint, n);
    }

    for (i = 0; i < n; i++) {
        item = json_array_get(channels, i);
        if (!json_is_object(item))
            return err_JSON_STRUCTURE(error, "channels.item", "object");

        if (!check_string_list_item(item, "id", pfile->ids, i, error))
            return FALSE;
        if (!check_string_list_item(item, "unit", pfile->units, i, error))
            return FALSE;

        item = json_array_get(numbers, i);
        if (!json_is_string(item))
            return err_JSON_STRUCTURE(error, "numbers.item", "string");

        if (!i)
            base64len = json_string_length(item);
        else if (json_string_length(item) != base64len)
            return err_INCONSISTENT(error);
    }

    if (lawn_is_new) {
        for (i = 0; i < n; i++)
            pfile->reorder[i] = i;
        try_to_reorder(pfile->ids, pfile->reorder, n, "Force", 0);
        try_to_reorder(pfile->ids, pfile->reorder, n, "ZHeight", 0);
        try_to_reorder(pfile->ids, pfile->reorder, n, "Lfm", n-1);
        for (i = 0; i < n; i++) {
            ri = pfile->reorder[i];
            gwy_lawn_set_curve_label(lawn, ri, pfile->ids[i]);
            gwy_si_unit_set_from_string_parse(gwy_lawn_get_si_unit_curve(lawn, ri), pfile->units[i], &power10);
            pfile->power10[i] = pow10(power10);
        }
    }

    g_array_set_size(pfile->databuf, 0);
    for (i = 0; i < n; i++) {
        ri = pfile->reorder[i];
        item = json_array_get(numbers, i);
        g_string_assign(str, json_string_value(item));
        g_base64_decode_inplace(str->str, &str->len);
        npts = str->len/sizeof(gfloat);
        if (i) {
            if (npts != pfile->databuf->len/n)
                return err_INCONSISTENT(error);
        }
        else
            g_array_set_size(pfile->databuf, n*npts);
        d = &g_array_index(pfile->databuf, gdouble, npts*pfile->reorder[i]);
        gwy_convert_raw_data(str->str, npts, 1, GWY_RAW_DATA_FLOAT, GWY_BYTE_ORDER_LITTLE_ENDIAN, d,
                             pfile->power10[i], 0.0);
    }
    npts = pfile->databuf->len/n;
    gwy_debug("items per curve %u (%u items total)", npts, pfile->databuf->len);
    gwy_lawn_set_curves(lawn, col, row, npts, &g_array_index(pfile->databuf, gdouble, 0), NULL);

    return TRUE;
}

static gboolean
check_string_list_item(json_t *root, const gchar *name, gchar **values, guint i, GError **error)
{
    json_t *item;

    if (!get_json_with_type(root, &item, name, JSON_STRING, error))
        return FALSE;
    if (!values[i])
        values[i] = g_strdup(json_string_value(item));
    else if (!gwy_strequal(json_string_value(item), values[i]))
        return err_INCONSISTENT(error);
    return TRUE;
}

static void
add_one_meta(GwyContainer *meta, json_t *object, GString *path)
{
    guint len = path->len;
    const gchar *key;
    void *iter;
    json_t *value;

    if (json_is_object(object)) {
        g_string_append(path, "::");
        for (iter = json_object_iter(object); iter; iter = json_object_iter_next(object, iter)) {
            if ((value = json_object_iter_value(iter))) {
                key = json_object_iter_key(iter);
                if (gwy_strequal(key, "type"))
                    continue;
                g_string_append(path, key);
                add_one_meta(meta, value, path);
                g_string_truncate(path, len+2);
            }
        }
        g_string_truncate(path, len);
    }
    else if (json_is_string(object))
        gwy_container_set_const_string_by_name(meta, path->str, json_string_value(object));
    else if (json_is_integer(object))
        gwy_container_set_string_by_name(meta, path->str, g_strdup_printf("%ld", (glong)json_integer_value(object)));
    else if (json_is_real(object))
        gwy_container_set_string_by_name(meta, path->str, g_strdup_printf("%g", json_real_value(object)));
    else if (json_is_true(object))
        gwy_container_set_const_string_by_name(meta, path->str, "True");
    else if (json_is_false(object))
        gwy_container_set_const_string_by_name(meta, path->str, "False");
    else {
        g_warning("Unhandled metadata of type %d.", json_typeof(object));
    }
}

static GwyContainer*
psppt_load(const gchar *filename,
           G_GNUC_UNUSED GwyRunType mode,
           GError **error)
{
    GwyContainer *container = NULL;
    PSPPTFile pfile;
    json_t *root = NULL;
    guchar *buffer = NULL;
    GError *err = NULL;
    gsize pos, size = 0;
    gboolean waiting = FALSE;
    guint i;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    gwy_clear(&pfile, 1);
    pfile.str = g_string_new(NULL);
    pfile.databuf = g_array_new(FALSE, FALSE, sizeof(gdouble));

    pos = 0;
    if (!psppt_read_header(&pfile.header, buffer, size, &pos, error))
        goto fail;

    if (mode == GWY_RUN_INTERACTIVE) {
        gwy_app_wait_start(NULL, _("Reading frame table..."));
        waiting = TRUE;
    }

    if (!psppt_read_frame_table(&pfile, buffer, size, &pos, error))
        goto fail;

    if (waiting && !gwy_app_wait_set_message(_("Reading curve data..."))) {
        err_CANCELLED(error);
        goto fail;
    }
    for (i = 0; i < pfile.nframes; i++) {
        PSPPTFrame *frame = pfile.frames + i;
        gboolean ok;

        if (waiting && i % 100 == 0 && !gwy_app_wait_set_fraction((i + 0.5)/pfile.nframes)) {
            err_CANCELLED(error);
            goto fail;
        }
        if (!(root = psppt_read_frame(frame, buffer, error)))
            goto fail;

        /* The types have been checked so we can be a bit less paranoid here. */
        if (i == 0)
            ok = handle_scan_start(&pfile.scanstart, root, error);
        else if (i == 1 || frame->type == PSPPT_PARAM)
            ok = handle_param(&pfile.param, root, error);
        else if (i == pfile.nframes-1)
            ok = handle_scan_stop(&pfile.scanstop, root, error);
        else
            ok = handle_rtfd(&pfile, root, error);
        if (!ok)
            goto fail;
        json_decref(root);
        root = NULL;
    }

    pfile.meta = gwy_container_new();
    g_string_assign(pfile.str, "Param");
    add_one_meta(pfile.meta, pfile.param.root, pfile.str);
    g_string_assign(pfile.str, "Scan");
    add_one_meta(pfile.meta, pfile.scanstart.root, pfile.str);

    container = gwy_container_new();
    gwy_container_set_object(container, gwy_app_get_lawn_key_for_id(0), pfile.lawn);
    gwy_container_set_const_string(container, gwy_app_get_lawn_title_key_for_id(0), pfile.scanstart.direction);
    gwy_container_set_object(container, gwy_app_get_lawn_meta_key_for_id(0), pfile.meta);
    gwy_file_curve_map_import_log_add(container, 0, NULL, filename);

fail:
    if (waiting)
        gwy_app_wait_finish();
    if (root)
        json_decref(root);
    if (pfile.scanstart.root)
        json_decref(pfile.scanstart.root);
    if (pfile.scanstop.root)
        json_decref(pfile.scanstop.root);
    if (pfile.param.root)
        json_decref(pfile.param.root);
    g_free(pfile.scanstart.direction);
    g_free(pfile.power10);
    g_free(pfile.reorder);
    g_strfreev(pfile.ids);
    g_strfreev(pfile.units);
    g_free(pfile.frames);
    GWY_OBJECT_UNREF(pfile.lawn);
    GWY_OBJECT_UNREF(pfile.meta);
    g_array_free(pfile.databuf, TRUE);
    g_string_free(pfile.str, TRUE);
    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
