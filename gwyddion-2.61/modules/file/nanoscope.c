/*
 *  $Id: nanoscope.c 24615 2022-02-21 16:06:34Z yeti-dn $
 *  Copyright (C) 2004-2022 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
 * <mime-type type="application/x-nanoscope-iii-spm">
 *   <comment>Nanoscope III SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="\\*File list\r\n"/>
 *     <match type="string" offset="0" value="\\*EC File list\r\n"/>
 *     <match type="string" offset="0" value="?*File list\r\n"/>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Nanoscope III
 * # Two header variants.
 * 0 string \\*File\ list\x0d\x0a Nanoscope III SPM binary data
 * 0 string \\*EC\ File\ list\x0d\x0a Nanoscope III electrochemistry SPM binary data
 * 0 string ?*File\ list\x0d\x0a Nanoscope III SPM text data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Veeco Nanoscope III
 * .spm, .001, .002, etc.
 * Read SPS:Limited[1] Volume
 * [1] Spectra curves are imported as graphs, positional information is lost.
 **/

#include "config.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libprocess/lawn.h>
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/wait.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"

#define MAGIC_BIN "\\*File list\r\n"
#define MAGIC_TXT "?*File list\r\n"
#define MAGIC_SIZE (sizeof(MAGIC_TXT)-1)

#define MAGIC_BIN_PARTIAL "\\*File list"
#define MAGIC_TXT_PARTIAL "?*File list"
#define MAGIC_SIZE_PARTIAL (sizeof(MAGIC_TXT_PARTIAL)-1)

#define MAGIC_FORCE_BIN "\\*Force file list\r\n"
#define MAGIC_FORCE_SIZE (sizeof(MAGIC_FORCE_BIN)-1)

#define MAGIC_EC_BIN "\\*EC File list\r\n"
#define MAGIC_EC_SIZE (sizeof(MAGIC_EC_BIN)-1)

typedef enum {
    NANOSCOPE_FILE_TYPE_NONE           = 0,
    NANOSCOPE_FILE_TYPE_BIN            = 1,
    NANOSCOPE_FILE_TYPE_TXT            = 2,
    NANOSCOPE_FILE_TYPE_FORCE_BIN      = 3,
    NANOSCOPE_FILE_TYPE_FORCE_VOLUME   = 4,
    NANOSCOPE_FILE_TYPE_PROFILES       = 5,
    NANOSCOPE_FILE_TYPE_BROKEN         = 10,
    NANOSCOPE_FILE_TYPE_32BIT_FLAG     = 1024,
} NanoscopeFileType;

typedef enum {
    NANOSCOPE_VALUE_OLD = 0,
    NANOSCOPE_VALUE_VALUE,
    NANOSCOPE_VALUE_SCALE,
    NANOSCOPE_VALUE_SELECT
} NanoscopeValueType;

typedef enum {
    NANOSCOPE_SPECTRA_IV,
    NANOSCOPE_SPECTRA_FZ,
} NanoscopeSpectraType;

/*
 * Old-style record is
 * \Foo: HardValue (HardScale)
 * where HardScale is optional.
 *
 * New-style record is
 * \@Bar: V [SoftScale] (HardScale) HardValue
 * where SoftScale and HardScale are optional.
 */
typedef struct {
    NanoscopeValueType type;
    const gchar *soft_scale;
    gdouble hard_scale;
    const gchar *hard_scale_units;
    gdouble hard_value;
    const gchar *hard_value_str;
    const gchar *hard_value_units;
} NanoscopeValue;

typedef struct {
    GHashTable *hash;
    GwyDataField *dfield;
    GwyGraphModel *graph_model;
    GwyLawn *lawn;
} NanoscopeData;

static gboolean        module_register        (void);
static gint            nanoscope_detect       (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name);
static GwyContainer*   nanoscope_load         (const gchar *filename,
                                               GwyRunType mode,
                                               GError **error);
static gchar*          extract_header         (const guchar *buffer,
                                               gsize size,
                                               GError **error);
static GwyDataField*   hash_to_data_field     (GHashTable *hash,
                                               GHashTable *scannerlist,
                                               GHashTable *scanlist,
                                               GHashTable *contrlist,
                                               NanoscopeFileType file_type,
                                               gulong version,
                                               gsize bufsize,
                                               const guchar *buffer,
                                               gsize gxres,
                                               gsize gyres,
                                               gboolean gnonsquare_aspect,
                                               gchar **p,
                                               GError **error);
static GwyGraphModel*  hash_to_profiles       (GHashTable *hash,
                                               GHashTable *scannerlist,
                                               GHashTable *scanlist,
                                               GHashTable *contrlist,
                                               NanoscopeFileType file_type,
                                               gulong version,
                                               gsize bufsize,
                                               const guchar *buffer,
                                               gsize gyres,
                                               GError **error);
static GwyGraphModel*  hash_to_curve          (GHashTable *hash,
                                               GHashTable *forcelist,
                                               GHashTable *scanlist,
                                               GHashTable *scannerlist,
                                               NanoscopeFileType file_type,
                                               gulong version,
                                               gsize bufsize,
                                               const guchar *buffer,
                                               gint gxres,
                                               GError **error);
static GwyLawn*        hash_to_lawn           (GHashTable *hash,
                                               GHashTable *forcelist,
                                               GHashTable *scanlist,
                                               GHashTable *scannerlist,
                                               GHashTable *equipmentlist,
                                               NanoscopeFileType file_type,
                                               gulong version,
                                               gsize bufsize,
                                               const guchar *buffer,
                                               GError **error);
static gboolean        read_text_data         (guint n,
                                               gdouble *data,
                                               gchar **buffer,
                                               gint bpp,
                                               GError **error);
static gboolean        read_binary_data       (gint n,
                                               gdouble *data,
                                               const guchar *buffer,
                                               gint bpp,
                                               gint qbpp,
                                               GError **error);
static GHashTable*     read_hash              (gchar **buffer,
                                               GError **error);
static const gchar*    get_image_data_name    (GHashTable *hash);
static void            get_scan_list_res      (GHashTable *hash,
                                               gsize *xres,
                                               gsize *yres);
static GwySIUnit*      get_scan_size          (GHashTable *hash,
                                               gdouble *xreal,
                                               gdouble *yreal,
                                               GError **error);
static gboolean        has_nonsquare_aspect   (GHashTable *hash);
static GwySIUnit*      get_physical_scale     (GHashTable *hash,
                                               GHashTable *scannerlist,
                                               GHashTable *scanlist,
                                               GHashTable *contrlist,
                                               gulong version,
                                               gboolean try_also_xz,
                                               gdouble *scale,
                                               gint qbpp,
                                               GError **error);
static GwySIUnit*      get_spec_ordinate_scale(GHashTable *hash,
                                               GHashTable *scanlist,
                                               gulong version,
                                               gdouble *scale,
                                               gboolean *convert_to_force,
                                               gint qbpp,
                                               GError **error);
static GwySIUnit*      get_spec_abscissa_scale(GHashTable *hash,
                                               GHashTable *forcelist,
                                               GHashTable *scannerlist,
                                               GHashTable *scanlist,
                                               gdouble *xreal,
                                               gdouble *xoff,
                                               NanoscopeSpectraType *spectype,
                                               GError **error);
static void            get_bpp_and_qbpp       (GHashTable *hash,
                                               NanoscopeFileType file_type,
                                               gsize *bpp,
                                               gsize *qbpp);
static gboolean        get_offset_and_size    (GHashTable *hash,
                                               gsize bufsize,
                                               gsize *offset,
                                               gsize *size,
                                               GError **error);
static guint           get_samples_per_curve  (GHashTable *hash,
                                               GHashTable *forcelist,
                                               guint *hold_samples,
                                               guint *retract_samples);
static GwyContainer*   nanoscope_get_metadata (GHashTable *hash,
                                               GList *list);
static NanoscopeValue* parse_value            (const gchar *key,
                                               gchar *line);
static gint            rebase_curves          (GList *list,
                                               const gchar *abscissa_name);
static void            rebase_one_gmodel      (GwyGraphModel *gmodel,
                                               GwyGraphModel *zgmodel);
static gint            merge_lawns            (GList *list);
static GwyLawn*        add_ramp_to_lawn       (GwyLawn *lawn);

static gint REMOVE_ME_channel_no;

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Bruker Nanoscope data files, version 3 or newer."),
    "Yeti <yeti@gwyddion.net>",
    "0.48",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, nanoscope)

static gboolean
module_register(void)
{
    gwy_file_func_register("nanoscope",
                           N_("Nanoscope III files"),
                           (GwyFileDetectFunc)&nanoscope_detect,
                           (GwyFileLoadFunc)&nanoscope_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
nanoscope_detect(const GwyFileDetectInfo *fileinfo,
                 gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return 0;

    if (fileinfo->buffer_len > MAGIC_SIZE
        && (!memcmp(fileinfo->head, MAGIC_TXT_PARTIAL, MAGIC_SIZE_PARTIAL)
            || !memcmp(fileinfo->head, MAGIC_BIN_PARTIAL, MAGIC_SIZE_PARTIAL)
            || !memcmp(fileinfo->head, MAGIC_FORCE_BIN, MAGIC_FORCE_SIZE)
            || !memcmp(fileinfo->head, MAGIC_EC_BIN, MAGIC_EC_SIZE)))
        score = 100;

    return score;
}

static GwyContainer*
nanoscope_load(const gchar *filename,
               GwyRunType mode,
               GError **error)
{
    GwyContainer *meta, *container = NULL;
    GError *err = NULL;
    guchar *buffer = NULL;
    gchar *header = NULL, *p;
    const gchar *self, *name, *start_context = NULL;
    gsize size = 0;
    NanoscopeFileType file_type, image_file_type, base_type;
    NanoscopeData *ndata;
    NanoscopeValue *val;
    GHashTable *scannerlist = NULL, *scanlist = NULL, *forcelist = NULL, *contrlist = NULL, *equipmentlist = NULL;
    GHashTable *hash;
    GList *l, *list = NULL;
    gsize xres = 0, yres = 0;
    gint i, n;
    gboolean ok, nonsquare_aspect = FALSE, waiting = FALSE;
    GwySIUnit *unit;
    gulong version = 0;

    REMOVE_ME_channel_no = 0;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    file_type = NANOSCOPE_FILE_TYPE_NONE;
    if (size > MAGIC_SIZE) {
        if (!memcmp(buffer, MAGIC_TXT, MAGIC_SIZE))
            file_type = NANOSCOPE_FILE_TYPE_TXT;
        else if (!memcmp(buffer, MAGIC_BIN, MAGIC_SIZE) || !memcmp(buffer, MAGIC_EC_BIN, MAGIC_EC_SIZE))
            file_type = NANOSCOPE_FILE_TYPE_BIN;
        else if (!memcmp(buffer, MAGIC_FORCE_BIN, MAGIC_FORCE_SIZE))
            file_type = NANOSCOPE_FILE_TYPE_FORCE_BIN;
        else if (!memcmp(buffer, MAGIC_TXT_PARTIAL, MAGIC_SIZE_PARTIAL)
                 || !memcmp(buffer, MAGIC_BIN_PARTIAL, MAGIC_SIZE_PARTIAL))
          file_type = NANOSCOPE_FILE_TYPE_BROKEN;
    }
    if (!file_type) {
        gwy_file_abandon_contents(buffer, size, NULL);
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("File is not a Nanoscope file, or it is a unknown subtype."));
        return NULL;
    }
    if (file_type == NANOSCOPE_FILE_TYPE_BROKEN) {
        gwy_file_abandon_contents(buffer, size, NULL);
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("File has been damaged by change of line endings, resulting in corruption of the binary part "
                      "of the file."
                      "\n\n"
                      "Typically, this occurs if the file is treated as text when sent by e-mail uncompressed, sent "
                      "by FTP in ascii mode (use binary), compressed by ‘Send to compressed " "folder’ in some "
                      "versions of MS Windows, or any other file transfer that attempts to store text "
                      "platform-independently."));
        return NULL;
    }

    gwy_debug("File type: %d", file_type);
    if (!(header = extract_header(buffer, size, error))) {
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    /* as we already know file_type, fix the first char for hash reading */
    *header = '\\';

    p = header;
    while ((hash = read_hash(&p, &err))) {
        ndata = g_new0(NanoscopeData, 1);
        ndata->hash = hash;
        list = g_list_append(list, ndata);

        if ((val = g_hash_table_lookup(hash, "Operating mode"))) {
            gwy_debug("operating mode is %s", val->hard_value_str);
            if (gwy_strequal(val->hard_value_str, "Force Volume"))
                file_type = NANOSCOPE_FILE_TYPE_FORCE_VOLUME;
            else if (gwy_strequal(val->hard_value_str, "Force"))
                file_type = NANOSCOPE_FILE_TYPE_FORCE_BIN;
            else if (gwy_strequal(val->hard_value_str, "Image")) {
                if (file_type != NANOSCOPE_FILE_TYPE_TXT)
                    file_type = NANOSCOPE_FILE_TYPE_BIN;
            }
        }

        /* In version 9.2 all data magically became 32bit. */
        self = g_hash_table_lookup(hash, "#self");
        if (self && gwy_stramong(self, "File list", "EC File list", "Force file list", NULL)) {
            if ((val = g_hash_table_lookup(hash, "Version")))
                version = strtol(val->hard_value_str, NULL, 16);
            if ((val = g_hash_table_lookup(hash, "Start context")))
                start_context = val->hard_value_str;
        }
    }

    if (start_context) {
        /* This is necessary to detect .pfc force volume files which blatantly state operating mode as Image. */
        if (gwy_strequal(start_context, "FVOL")) {
            if (file_type == NANOSCOPE_FILE_TYPE_BIN) {
                gwy_debug("seeing start context %s, forcing volume data", start_context);
                file_type = NANOSCOPE_FILE_TYPE_FORCE_VOLUME;
            }
        }
        /* If there is FOL, not FVOL, the data seems just single curves. */
        if (gwy_strequal(start_context, "FOL")) {
            if (file_type == NANOSCOPE_FILE_TYPE_FORCE_VOLUME) {
                gwy_debug("seeing start context %s, forcing single curve data", start_context);
                file_type = NANOSCOPE_FILE_TYPE_FORCE_BIN;
            }
        }
        /* This is seems necessary for Deep Trench mode which lies to us by saying Image but actually saving a set
         * of unevenly spaced profiles. */
        else if (g_str_has_suffix(start_context, "VAR")) {
            if (file_type == NANOSCOPE_FILE_TYPE_BIN) {
                gwy_debug("seeing start context %s, forcing profile set", start_context);
                file_type = NANOSCOPE_FILE_TYPE_PROFILES;
            }
        }
    }

    if (version >= 0x09200000 && file_type != NANOSCOPE_FILE_TYPE_TXT) {
        gwy_debug("version is 0x%lx, assuming 32bit data", version);
        file_type |= NANOSCOPE_FILE_TYPE_32BIT_FLAG;
    }

    /* For text data reading we need @p to point in the same place but in
     * the buffer with entire file contents. */
    p = buffer + (p - header);

    image_file_type = file_type;
    base_type = file_type & ~NANOSCOPE_FILE_TYPE_32BIT_FLAG;
    if (base_type == NANOSCOPE_FILE_TYPE_FORCE_VOLUME)
        image_file_type = NANOSCOPE_FILE_TYPE_BIN | (file_type & NANOSCOPE_FILE_TYPE_32BIT_FLAG);
    else if (base_type == NANOSCOPE_FILE_TYPE_FORCE_BIN || base_type == NANOSCOPE_FILE_TYPE_PROFILES)
        image_file_type = NANOSCOPE_FILE_TYPE_NONE;

    if (err) {
        g_propagate_error(error, err);
        ok = FALSE;
    }
    else
        ok = TRUE;

    n = 0;
    if (ok && mode == GWY_RUN_INTERACTIVE && base_type == NANOSCOPE_FILE_TYPE_FORCE_VOLUME) {
        gwy_app_wait_start(NULL, _("Reading channels..."));
        waiting = TRUE;
        for (l = list; l; l = g_list_next(l)) {
            ndata = (NanoscopeData*)l->data;
            hash = ndata->hash;
            self = g_hash_table_lookup(hash, "#self");
            if (gwy_stramong(self, "AFM image list", "Ciao image list", "STM image list", "NCAFM image list",
                             "Ciao force image list", "Image list", NULL))
                n++;
        }
        if (!gwy_app_wait_set_fraction(0.01)) {
            err_CANCELLED(error);
            ok = FALSE;
        }
    }

    i = 0;
    for (l = list; ok && l; l = g_list_next(l)) {
        ndata = (NanoscopeData*)l->data;
        hash = ndata->hash;
        self = g_hash_table_lookup(hash, "#self");
        /* The alternate names were found in files written by some beast
         * called Nanoscope E software */
        if (gwy_strequal(self, "Scanner list")
            || gwy_strequal(self, "Microscope list")) {
            scannerlist = hash;
            continue;
        }
        if (gwy_strequal(self, "Equipment list")) {
            equipmentlist = hash;
            continue;
        }
        if (gwy_stramong(self, "File list", "EC File list", NULL)) {
            continue;
        }
        if (gwy_strequal(self, "Controller list")) {
            contrlist = hash;
            continue;
        }
        if (gwy_stramong(self, "Ciao scan list", "Afm list", "Stm list", "NC Afm list", NULL)) {
            get_scan_list_res(hash, &xres, &yres);
            nonsquare_aspect = has_nonsquare_aspect(hash);
            scanlist = hash;
        }
         if (gwy_stramong(self, "Ciao force list", NULL)) {
            get_scan_list_res(hash, &xres, &yres);
            nonsquare_aspect = has_nonsquare_aspect(hash);
            forcelist = hash;
        }
        if (!gwy_stramong(self, "AFM image list", "Ciao image list", "STM image list", "NCAFM image list",
                          "Ciao force image list", "Image list", NULL))
            continue;

        gwy_debug("processing hash %s", self);
        if (base_type == NANOSCOPE_FILE_TYPE_FORCE_BIN) {
            ndata->graph_model = hash_to_curve(hash, forcelist, scanlist, scannerlist, file_type, version,
                                               size, buffer, xres, error);
            ok = ok && ndata->graph_model;
        }
        else if (base_type == NANOSCOPE_FILE_TYPE_PROFILES) {
            ndata->graph_model = hash_to_profiles(hash, scannerlist, scanlist, contrlist, file_type, version,
                                                  size, buffer, yres, error);
            ok = ok && ndata->graph_model;
        }
        else if (base_type == NANOSCOPE_FILE_TYPE_FORCE_VOLUME) {
            if (gwy_strequal(self, "Ciao force image list")) {
                ndata->lawn = hash_to_lawn(hash, forcelist, scanlist, scannerlist, equipmentlist, file_type, version,
                                           size, buffer, error);
                ok = ok && ndata->lawn;
            }
            else {
                ndata->dfield = hash_to_data_field(hash, scannerlist, scanlist, contrlist, image_file_type, version,
                                                   size, buffer, xres, yres, nonsquare_aspect, &p, error);
                ok = ok && ndata->dfield;
            }
        }
        else {
            ndata->dfield = hash_to_data_field(hash, scannerlist, scanlist, contrlist, file_type, version,
                                               size, buffer, xres, yres, nonsquare_aspect, &p, error);
            ok = ok && ndata->dfield;
        }

        if (waiting) {
            i++;
            if (!gwy_app_wait_set_fraction((gdouble)i/n)) {
                err_CANCELLED(error);
                ok = FALSE;
                break;
            }
        }
    }

    if (ok) {
        if (base_type == NANOSCOPE_FILE_TYPE_FORCE_BIN)
            rebase_curves(list, "ZSensor");
        else if (base_type == NANOSCOPE_FILE_TYPE_PROFILES)
            rebase_curves(list, "Xscan");
        else if (base_type == NANOSCOPE_FILE_TYPE_FORCE_VOLUME)
            merge_lawns(list);
    }

    if (ok) {
        i = 0;
        container = gwy_container_new();
        for (l = list; l; l = g_list_next(l)) {
            ndata = (NanoscopeData*)l->data;
            if (ndata->dfield) {
                gwy_container_set_object(container, gwy_app_get_data_key_for_id(i), ndata->dfield);
                if ((name = get_image_data_name(ndata->hash)))
                    gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(i), name);

                meta = nanoscope_get_metadata(ndata->hash, list);
                gwy_container_set_object(container, gwy_app_get_data_meta_key_for_id(i), meta);
                g_object_unref(meta);

                gwy_app_channel_check_nonsquare(container, i);
                gwy_file_channel_import_log_add(container, i, NULL, filename);
                i++;
            }
            if (ndata->graph_model) {
                gwy_container_set_object(container, gwy_app_get_graph_key_for_id(i+1), ndata->graph_model);
                i++;
            }
            if (ndata->lawn) {
                g_free(g_object_steal_data(G_OBJECT(ndata->lawn), "zreal"));
                if ((unit = g_object_steal_data(G_OBJECT(ndata->lawn), "zunit")))
                    g_object_unref(unit);

                gwy_container_set_object(container, gwy_app_get_lawn_key_for_id(i), ndata->lawn);
                if ((name = get_image_data_name(ndata->hash)))
                    gwy_container_set_const_string(container, gwy_app_get_lawn_title_key_for_id(i), name);
                gwy_file_curve_map_import_log_add(container, i, NULL, filename);
                i++;
            }
        }
        if (!i)
            GWY_OBJECT_UNREF(container);
    }

    if (waiting)
        gwy_app_wait_finish();

    for (l = list; l; l = g_list_next(l)) {
        ndata = (NanoscopeData*)l->data;
        GWY_OBJECT_UNREF(ndata->dfield);
        GWY_OBJECT_UNREF(ndata->graph_model);
        GWY_OBJECT_UNREF(ndata->lawn);
        if (ndata->hash)
            g_hash_table_destroy(ndata->hash);
        g_free(ndata);
    }
    gwy_file_abandon_contents(buffer, size, NULL);
    g_free(header);
    g_list_free(list);

    if (!container && ok)
        err_NO_DATA(error);

    return container;
}

static gchar*
extract_header(const guchar *buffer, gsize size, GError **error)
{
    enum { DATA_LEN_LEN = sizeof("\\Data length: ")-1 };
    const guchar *p = buffer;
    guint i, len, header_len;
    gchar *header;

    if (size < 2) {
        err_MISSING_FIELD(error, "Data length");
        return NULL;
    }

    /* Find header size by looking for ‘Data length’ among the few first
     * fields.  All files, even historic ones, apparently carry it.  Actually
     * it is invariably the fifth field. */
    for (i = 0; i < 8; i++) {
        p = memchr(p+1, '\\', size-1 - (p - buffer));
        if (!p) {
            err_MISSING_FIELD(error, "Data length");
            return NULL;
        }
        if (p + DATA_LEN_LEN+1 - buffer >= size) {
            err_MISSING_FIELD(error, "Data length");
            return NULL;
        }
        if (memcmp(p, "\\Data length: ", DATA_LEN_LEN) == 0) {
            p += DATA_LEN_LEN;
            goto found_it;
        }
    }
    err_MISSING_FIELD(error, "Data length");
    return NULL;

found_it:
    len = size - (p - buffer);
    header_len = 0;
    for (i = 0; i < len; i++) {
        if (!g_ascii_isdigit(p[i]))
            break;
        header_len = 10*header_len + g_ascii_digit_value(p[i]);
    }

    if (header_len > size) {
        err_INVALID(error, "Data length");
        return NULL;
    }

    header = g_new(gchar, header_len+1);
    memcpy(header, buffer, header_len);
    header[header_len] = '\0';

    return header;
}

static void
add_metadata(gpointer hkey,
             gpointer hvalue,
             gpointer user_data)
{
    gchar *key = (gchar*)hkey;
    NanoscopeValue *val = (NanoscopeValue*)hvalue;
    gchar *v, *w;

    if (gwy_strequal(key, "#self")
        || !val->hard_value_str
        || !val->hard_value_str[0])
        return;

    if (key[0] == '@')
        key++;
    v = g_strdup(val->hard_value_str);
    if (strchr(v, '\272')) {
        w = gwy_strreplace(v, "\272", "deg", -1);
        g_free(v);
        v = w;
    }
    if (strchr(v, '~')) {
        w = gwy_strreplace(v, "~", "µ", -1);
        g_free(v);
        v = w;
    }
    gwy_container_set_string_by_name(GWY_CONTAINER(user_data), key, v);
}

/* FIXME: This is a bit simplistic */
static GwyContainer*
nanoscope_get_metadata(GHashTable *hash,
                       GList *list)
{
    static const gchar *hashes[] = {
        "File list", "EC File list", "Scanner list", "Equipment list", "Ciao scan list",
    };
    GwyContainer *meta;
    GList *l;
    guint i;

    meta = gwy_container_new();

    for (l = list; l; l = g_list_next(l)) {
        GHashTable *h = ((NanoscopeData*)l->data)->hash;
        for (i = 0; i < G_N_ELEMENTS(hashes); i++) {
            if (gwy_strequal(g_hash_table_lookup(h, "#self"), hashes[i])) {
                g_hash_table_foreach(h, add_metadata, meta);
                break;
            }
        }
    }
    g_hash_table_foreach(hash, add_metadata, meta);

    return meta;
}

static GwyDataField*
hash_to_data_field(GHashTable *hash,
                   GHashTable *scannerlist,
                   GHashTable *scanlist,
                   GHashTable *contrlist,
                   NanoscopeFileType file_type,
                   gulong version,
                   gsize bufsize,
                   const guchar *buffer,
                   gsize gxres,
                   gsize gyres,
                   gboolean gnonsquare_aspect,
                   gchar **p,
                   GError **error)
{
    NanoscopeFileType base_type = file_type & ~NANOSCOPE_FILE_TYPE_32BIT_FLAG;
    NanoscopeValue *val;
    GwyDataField *dfield = NULL;
    GwySIUnit *unitz = NULL, *unitxy = NULL;
    gsize xres, yres, bpp, qbpp, offset, size;
    gdouble xreal, yreal, q;
    gdouble *data;
    gboolean size_ok, use_global, nonsquare_aspect;

    if (!require_keys(hash, error, "Samps/line", "Number of lines", "Scan size", "Data offset", "Data length", NULL))
        return NULL;

    val = g_hash_table_lookup(hash, "Samps/line");
    xres = (gsize)val->hard_value;

    val = g_hash_table_lookup(hash, "Number of lines");
    yres = (gsize)val->hard_value;

    get_bpp_and_qbpp(hash, file_type, &bpp, &qbpp);
    nonsquare_aspect = has_nonsquare_aspect(hash);
    gwy_debug("xres %lu, yres %lu", (gulong)xres, (gulong)yres);
    gwy_debug("gxres %lu, gyres %lu", (gulong)gxres, (gulong)gyres);

    /* Scan size */
    if (!(unitxy = get_scan_size(hash, &xreal, &yreal, error)))
        goto fail;

    /* Prevents possible division by 0 when gxres and gyres are not set for whatever reason. */
    if (!gxres)
        gxres = xres;
    if (!gyres)
        gyres = yres;

    gwy_debug("self: %s", (char*)g_hash_table_lookup(hash, "#self"));
    offset = size = 0;
    if (base_type == NANOSCOPE_FILE_TYPE_BIN) {
        if (!get_offset_and_size(hash, bufsize, &offset, &size, error))
            goto fail;

        size_ok = FALSE;
        use_global = FALSE;

        /* Try channel size and local size */
        if (!size_ok && size == bpp*xres*yres)
            size_ok = TRUE;

        if (!size_ok && size == bpp*gxres*gyres) {
            size_ok = TRUE;
            use_global = TRUE;
        }

        /* If they don't match exactly, try whether they at least fit inside */
        if (!size_ok && size > bpp*MAX(xres*yres, gxres*gyres)) {
            size_ok = TRUE;
            use_global = (xres*yres < gxres*gyres);
        }

        if (!size_ok && size > bpp*MIN(xres*yres, gxres*gyres)) {
            size_ok = TRUE;
            use_global = (xres*yres > gxres*gyres);
        }

        if (!size_ok) {
            err_SIZE_MISMATCH(error, bpp*xres*yres, size, TRUE);
            goto fail;
        }

        if (use_global) {
            if (gxres) {
                xreal *= (gdouble)gxres/xres;
                xres = gxres;
            }
            if (gyres) {
                yreal *= (gdouble)gyres/yres;
                yres = gyres;
            }
        }
        else if (nonsquare_aspect) {
            gwy_debug("nonsquare_aspect");
            if (gnonsquare_aspect) {
                gwy_debug("gnonsquare_aspect");
                /* Reported by Peter Eaton.  Not sure if we detect it correctly. */
                yreal *= yres;
                yreal /= xres;
            }
            else {
                /* This seems to be the common case. */
                yreal *= yres;
                yreal /= gyres;
            }
        }

        if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
            goto fail;

        /* Use negated positive conditions to catch NaNs */
        if (!((xreal = fabs(xreal)) > 0)) {
            g_warning("Real x size is 0.0, fixing to 1.0");
            xreal = 1.0;
        }
        if (!((yreal = fabs(yreal)) > 0)) {
            g_warning("Real y size is 0.0, fixing to 1.0");
            yreal = 1.0;
        }
    }

    q = 1.0;
    unitz = get_physical_scale(hash, scannerlist, scanlist, contrlist, version, FALSE, &q, qbpp, error);
    if (!unitz)
        goto fail;

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
    data = gwy_data_field_get_data(dfield);
    if (file_type == NANOSCOPE_FILE_TYPE_TXT) {
        if (!read_text_data(xres*yres, data, p, qbpp, error)) {
            GWY_OBJECT_UNREF(dfield);
            goto fail;
        }
    }
    else if (base_type == NANOSCOPE_FILE_TYPE_BIN) {
        if (!read_binary_data(xres*yres, data, buffer + offset, bpp, qbpp, error)) {
            GWY_OBJECT_UNREF(dfield);
            goto fail;
        }
#if 0
        {
            gint ii, jj;
            const guchar *pp = buffer + offset;
            gchar *fnmfnm = g_strdup_printf("nanoscope-raw-data-%02d.txt", REMOVE_ME_channel_no);
            FILE *ff = fopen(fnmfnm, "w");

            for (ii = 0; ii < yres; ii++) {
                for (jj = 0; jj < xres; jj++) {
                    gint vv;
                    if (bpp == 1)
                        vv = *(pp++);
                    else if (bpp == 2)
                        vv = gwy_get_gint16_le(&pp);
                    else if (bpp == 4)
                        vv = gwy_get_gint32_le(&pp);
                    else {
                        g_assert_not_reached();
                    }
                    fprintf(ff, "%d%c", vv, jj == xres-1 ? '\n' : ' ');
                }
            }
            fclose(ff);
            g_free(fnmfnm);
            REMOVE_ME_channel_no++;
        }
#endif
    }
    else {
        g_assert_not_reached();
    }
    gwy_data_field_multiply(dfield, q);
    gwy_data_field_invert(dfield, TRUE, FALSE, FALSE);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(dfield), unitxy);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield), unitz);

fail:
    GWY_OBJECT_UNREF(unitz);
    GWY_OBJECT_UNREF(unitxy);

    return dfield;
}

static GwyGraphModel*
hash_to_profiles(GHashTable *hash,
                 GHashTable *scannerlist,
                 GHashTable *scanlist,
                 GHashTable *contrlist,
                 NanoscopeFileType file_type,
                 gulong version,
                 gsize bufsize,
                 const guchar *buffer,
                 gsize gyres,
                 GError **error)
{
    NanoscopeFileType base_type = file_type & ~NANOSCOPE_FILE_TYPE_32BIT_FLAG;
    NanoscopeValue *val;
    GwyGraphModel *gmodel = NULL;
    GwyGraphCurveModel *gcmodel;
    GwyDataLine *dline = NULL;
    GwySIUnit *unitz = NULL, *unitxy = NULL;
    gsize bpp, qbpp, offset, size;
    guint i, yres;
    guint *prof_lengths = NULL;
    gdouble xreal, yreal, q;
    const gchar *name;
    const guchar *p;
    gchar *desc;

    g_return_val_if_fail(base_type == NANOSCOPE_FILE_TYPE_PROFILES, NULL);

    /* Samps/line is some nonsense because the number of samples taken varies.  We have to read the individual lengths
     * from the binary data chunk. */
    if (!require_keys(hash, error, "Number of lines", "Scan size", "Data offset", "Data length", NULL))
        return NULL;

    val = g_hash_table_lookup(hash, "Number of lines");
    yres = (gsize)val->hard_value;

    gwy_debug("yres %lu", (gulong)yres);
    gwy_debug("gyres %lu", (gulong)gyres);

    get_bpp_and_qbpp(hash, file_type, &bpp, &qbpp);

    /* Scan size */
    if (!(unitxy = get_scan_size(hash, &xreal, &yreal, error)))
        goto fail;

    /* Prevents possible division by 0 when gxres and gyres are not set for whatever reason. */
    if (!gyres)
        gyres = yres;

    if (err_DIMENSION(error, yres))
        goto fail;

    gwy_debug("self: %s", (char*)g_hash_table_lookup(hash, "#self"));
    if (!get_offset_and_size(hash, bufsize, &offset, &size, error))
        goto fail;

    /* Use negated positive conditions to catch NaNs */
    if (!((xreal = fabs(xreal)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        xreal = 1.0;
    }
    if (!((yreal = fabs(yreal)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        yreal = 1.0;
    }

    q = 1.0;
    unitz = get_physical_scale(hash, scannerlist, scanlist, contrlist, version, TRUE, &q, qbpp, error);
    if (!unitz)
        goto fail;

    prof_lengths = g_new(guint, yres);
    p = buffer + offset;
    for (i = 0; i < yres; i++) {
        if (p - (buffer + offset) + sizeof(guint16) > size) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("File is truncated."));
            goto fail;
        }
        prof_lengths[i] = gwy_get_guint16_le(&p);
        gwy_debug("prof_lengths[%u] = %u", i, prof_lengths[i]);
        if (p - (buffer + offset) + prof_lengths[i]*bpp > size) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("File is truncated."));
            goto fail;
        }
        p += prof_lengths[i]*bpp;
    }

    gmodel = gwy_graph_model_new();
    dline = gwy_data_line_new(1, xreal, FALSE);
    gwy_si_unit_assign(gwy_data_line_get_si_unit_x(dline), unitxy);
    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(dline), unitz);
    gwy_graph_model_set_units_from_data_line(gmodel, dline);
    if ((name = get_image_data_name(hash)))
        g_object_set(gmodel, "title", name, NULL);

    p = buffer + offset;
    for (i = 0; i < yres; i++) {
        gwy_data_line_resample(dline, prof_lengths[i], GWY_INTERPOLATION_NONE);
        p += 2;
        read_binary_data(prof_lengths[i], gwy_data_line_get_data(dline), p, bpp, qbpp, NULL);
        gwy_data_line_multiply(dline, q);
        p += prof_lengths[i]*bpp;

        gcmodel = gwy_graph_curve_model_new();
        desc = g_strdup_printf(_("Profile %u"), i+1);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(i),
                     "description", desc,
                     NULL);
        g_free(desc);
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, dline, 0, 0);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

fail:
    GWY_OBJECT_UNREF(dline);
    GWY_OBJECT_UNREF(unitz);
    GWY_OBJECT_UNREF(unitxy);
    g_free(prof_lengths);

    return gmodel;
}

static GwyLawn*
hash_to_lawn(GHashTable *hash,
             GHashTable *forcelist,
             GHashTable *scanlist,
             GHashTable *scannerlist,
             GHashTable *equipmentlist,
             NanoscopeFileType file_type,
             gulong version,
             gsize bufsize,
             const guchar *buffer,
             GError **error)
{
    GwyLawn *lawn = NULL;
    NanoscopeValue *val;
    gdouble *curvedata, *pzreal;
    guint xres, yres, zres, zres2, zreshold, zrestotal, i, j, k, nsegments;
    gsize offset, size, bpp, qbpp;
    gdouble xreal, yreal, zreal, zoff, q;
    GwySIUnit *unitxy = NULL, *unitw = NULL, *unitz = NULL;
    NanoscopeSpectraType spectype;
    gboolean continuous = FALSE;
    const guchar *p;
    gint segments[6];
    const gchar *segment_labels[3];

    gwy_debug("Loading lawn");

    if (!require_keys(hash, error, "Samps/line", "Data offset", "Data length", NULL))
        return NULL;
    if (!require_keys(forcelist, error, "force/line", NULL))
        return NULL;
    if (!require_keys(scanlist, error, "Scan size", "Lines", NULL))
        return NULL;

    if ((val = g_hash_table_lookup(scanlist, "Capture Mode"))) {
        if (strstr(val->hard_value_str, "Continuous")) {
            gwy_debug("Continuous mode.  Everything is screwed up?");
            continuous = TRUE;
        }
    }

    if (!get_offset_and_size(hash, bufsize, &offset, &size, error))
        goto fail;

    zres = get_samples_per_curve(hash, forcelist, &zreshold, &zres2);
    zrestotal = zres + zres2 + zreshold;
    gwy_debug("curve samples %u + %u + %u = %u", zres, zreshold, zres2, zrestotal);

    val = g_hash_table_lookup(forcelist, "force/line");
    xres = (gsize)val->hard_value;
    gwy_debug("force curves per line %lu", (glong)xres);

    /* It seems the number of volume data lines is always simply the number of lines. */
    val = g_hash_table_lookup(scanlist, "Lines");
    yres = (gsize)val->hard_value;
    gwy_debug("Number of lines %lu", (glong)yres);

    get_bpp_and_qbpp(hash, file_type, &bpp, &qbpp);

    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres) || err_DIMENSION(error, zres))
        goto fail;
    if (zres2 && err_DIMENSION(error, zres2))
        goto fail;
    if (zreshold && err_DIMENSION(error, zreshold))
        goto fail;

    if (size != xres*yres*zrestotal*bpp) {
        if (!zreshold && !zres2 && size == 2*xres*yres*zres*bpp)
            zres2 = zres;
        else {
            err_SIZE_MISMATCH(error, xres*yres*zrestotal*bpp, size, TRUE);
            goto fail;
        }
    }

    /* Scan size */
    if (!(unitxy = get_scan_size(scanlist, &xreal, &yreal, error)))
        goto fail;

    q = 1.0;
    unitw = get_physical_scale(hash, scannerlist, scanlist, equipmentlist, version, FALSE, &q, qbpp, error);
    gwy_debug("physical scale %g", q);
    if (!unitw) {
        g_object_unref(unitxy);
        goto fail;
    }

    /* The zreal and zoffset values are probably just bogus.  We do not use them for anything unless we are desperate
     * because there is no usable abscissa. */
    if (!(unitz = get_spec_abscissa_scale(hash, forcelist, scannerlist, scanlist, &zreal, &zoff, &spectype, NULL))) {
        g_object_unref(unitxy);
        g_object_unref(unitw);
        goto fail;
    }
    gwy_debug("spectype %d", spectype);
    gwy_debug("zreal %g, zoff %g", zreal, zoff);

    nsegments = 0;
    if (zres) {
        segment_labels[nsegments] = "Approach";
        segments[2*nsegments + 0] = 0;
        segments[2*nsegments + 1] = zres;
        nsegments++;
    }
    if (zreshold) {
        segment_labels[nsegments] = "Contact";
        segments[2*nsegments + 0] = zres;
        segments[2*nsegments + 1] = zres + zreshold;
        nsegments++;
    }
    if (zres2) {
        segment_labels[nsegments] = "Retract";
        segments[2*nsegments + 0] = zres + zreshold;
        segments[2*nsegments + 1] = zrestotal;
        nsegments++;
    }
    /* Do not create full-curve segments over unsegmented curves. */
    if (nsegments == 1)
        nsegments = 0;
    gwy_debug("nsegments %d", nsegments);
    lawn = gwy_lawn_new(yres, xres, xreal, yreal, 1, nsegments);
    gwy_si_unit_assign(gwy_lawn_get_si_unit_xy(lawn), unitxy);
    gwy_si_unit_assign(gwy_lawn_get_si_unit_curve(lawn, 0), unitw);
    for (i = 0; i < nsegments; i++)
        gwy_lawn_set_segment_label(lawn, i, segment_labels[i]);

    p = buffer + offset;
    gwy_debug("coverting raw data");
    curvedata = g_new(gdouble, zrestotal);
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            /* NB: the data order is a bit confusing. */
            /* Approach curve */
            if (zres) {
                read_binary_data(zres, curvedata, p, bpp, qbpp, NULL);
                p += bpp*zres;
                for (k = 0; k < zres/2; k++)
                    GWY_SWAP(gdouble, curvedata[k], curvedata[zres-1 - k]);
            }

            /* Retract curve. */
            if (zres2) {
                read_binary_data(zres2, curvedata + zres + zreshold, p, bpp, qbpp, NULL);
                p += bpp*zres2;
            }

            /* Hold curve. */
            if (zreshold) {
                read_binary_data(zreshold, curvedata + zres, p, bpp, qbpp, NULL);
                p += bpp*zreshold;
            }

            for (k = 0; k < zrestotal; k++)
                curvedata[k] *= q;
            gwy_lawn_set_curves(lawn, j, i, zrestotal, curvedata, nsegments ? segments : NULL);
        }
    }
    g_free(curvedata);

    /* XXX: Remember zreal somewhere in case we need to make a synthetic abscissa later.  Definitely not nice. */
    pzreal = g_new(gdouble, 1);
    *pzreal = zreal;
    g_object_set_data(G_OBJECT(lawn), "zreal", pzreal);
    g_object_set_data(G_OBJECT(lawn), "zunit", g_object_ref(unitz));

fail:
    GWY_OBJECT_UNREF(unitxy);
    GWY_OBJECT_UNREF(unitw);
    GWY_OBJECT_UNREF(unitz);

    return lawn;
}

static gboolean
check_graph_model_compatibility(GwyGraphModel *gmodel1, GwyGraphModel *gmodel2)
{
    GwySIUnit *unit1 = NULL, *unit2 = NULL;
    GwyGraphCurveModel *gcmodel1, *gcmodel2;
    gint n, i;
    gboolean ok;

    n = gwy_graph_model_get_n_curves(gmodel1);
    if (gwy_graph_model_get_n_curves(gmodel2) != n) {
        gwy_debug("mismatch between graph numbers of curves");
        return FALSE;
    }

    g_object_get(gmodel1, "si-unit-x", &unit1, NULL);
    g_object_get(gmodel2, "si-unit-x", &unit2, NULL);
    ok = gwy_si_unit_equal(unit1, unit2);
    g_object_unref(unit1);
    g_object_unref(unit2);
    if (!ok) {
        gwy_debug("mismatch between graph abscissa units");
        return FALSE;
    }

    for (i = 0; i < n; i++) {
        gcmodel1 = gwy_graph_model_get_curve(gmodel1, i);
        gcmodel2 = gwy_graph_model_get_curve(gmodel2, i);
        if (gwy_graph_curve_model_get_ndata(gcmodel1) != gwy_graph_curve_model_get_ndata(gcmodel2)) {
            gwy_debug("mismatch between graph curve #%d number of points", i);
            return FALSE;
        }
    }

    gwy_debug("graph models match");
    return TRUE;
}

/* If we find a curve called @abscissa_name (like ZSensor), use it as the abscissa of all other curves. */
static gint
rebase_curves(GList *list, const gchar *abscissa_name)
{
    GList *l, *foundit = NULL;
    NanoscopeData *absdata, *orddata;
    const gchar *name;
    gint rebased = 0;
    /* In some files everything is twice.  Not sure why.  In such case rebase everything up to the first abscissa and
     * keep the rest.  This usually gives one rebased and one non-rebased instance of everything. */
    gboolean multiple_abscissae = FALSE;

    for (l = list; l; l = g_list_next(l)) {
        absdata = (NanoscopeData*)l->data;
        if (!absdata->graph_model)
            continue;
        /* Yes, use the get_image_data_name() function here.  Apparently. */
        if (!(name = get_image_data_name(absdata->hash)))
            continue;
        if (gwy_strequal(name, abscissa_name)) {
            if (foundit) {
                multiple_abscissae = TRUE;
                break;
            }
            foundit = l;
        }
    }
    if (!foundit) {
        gwy_debug("did not find %s, not rebasing.", abscissa_name);
        return 0;
    }

    absdata = (NanoscopeData*)foundit->data;
    for (l = list; l; l = g_list_next(l)) {
        orddata = (NanoscopeData*)l->data;
        if (orddata == absdata) {
            if (multiple_abscissae)
                break;
            continue;
        }
        if (!orddata->graph_model)
            continue;
        if (!check_graph_model_compatibility(orddata->graph_model, absdata->graph_model))
            continue;
        gwy_debug("rebasing gmodel %s", get_image_data_name(orddata->hash));
        rebase_one_gmodel(orddata->graph_model, absdata->graph_model);
        rebased++;
    }
    if (rebased)
        GWY_OBJECT_UNREF(absdata->graph_model);

    return rebased;
}

static void
rebase_one_gmodel(GwyGraphModel *gmodel, GwyGraphModel *basegmodel)
{
    GwyGraphModel *rebased_gmodel;
    GwyGraphCurveModel *gcmodel, *basegcmodel, *rebased_gcmodel;
    GwySIUnit *unit = NULL;
    gint i, j, n, ndata, cutbeg, cutend, cutlen;
    const gdouble *zdata, *ydata;
    gdouble *xdata;
    gchar *label;

    rebased_gmodel = gwy_graph_model_new_alike(gmodel);
    n = gwy_graph_model_get_n_curves(gmodel);
    for (i = 0; i < n; i++) {
        gcmodel = gwy_graph_model_get_curve(gmodel, i);
        basegcmodel = gwy_graph_model_get_curve(basegmodel, i);
        rebased_gcmodel = gwy_graph_curve_model_new_alike(gcmodel);
        ndata = gwy_graph_curve_model_get_ndata(gcmodel);
        ydata = gwy_graph_curve_model_get_ydata(gcmodel);
        zdata = gwy_graph_curve_model_get_ydata(basegcmodel);
        xdata = g_new(gdouble, ndata);
        {
            gdouble min = G_MAXDOUBLE, max = -G_MAXDOUBLE;
            for (j = 0; j < ndata; j++) {
                xdata[j] = zdata[j];
                min = fmin(min, xdata[j]);
                max = fmax(max, xdata[j]);
            }
        }
        /* Try to get rid of data zero data segments. */
        cutbeg = 0;
        while (cutbeg < ndata && xdata[cutbeg] == 0.0 && ydata[cutbeg] == 0.0)
            cutbeg++;

        cutend = 0;
        while (cutend < ndata-cutbeg
               && xdata[ndata-1-cutend] == 0.0
               && ydata[ndata-1-cutend] == 0.0)
            cutend++;

        if (cutbeg + cutend >= ndata) {
            cutbeg = 0;
            cutlen = MIN(1, ndata);
        }
        else
            cutlen = ndata - (cutbeg + cutend);

        gwy_graph_curve_model_set_data(rebased_gcmodel, xdata + cutbeg, ydata + cutbeg, cutlen);
        gwy_graph_curve_model_enforce_order(rebased_gcmodel);
        gwy_graph_model_add_curve(rebased_gmodel, rebased_gcmodel);
        g_object_unref(rebased_gcmodel);
        g_free(xdata);
    }

    g_object_get(basegmodel,
                 "si-unit-y", &unit,
                 "axis-label-left", &label,
                 NULL);
    g_object_set(rebased_gmodel,
                 "si-unit-x", unit,
                 "axis-label-bottom", label,
                 NULL);
    g_object_unref(unit);
    g_free(label);

    gwy_serializable_clone(G_OBJECT(rebased_gmodel), G_OBJECT(gmodel));
    g_object_unref(rebased_gmodel);
}

static gboolean
check_lawn_compatibility(GwyLawn *lawn1, GwyLawn *lawn2)
{
    return !gwy_lawn_check_compatibility(lawn1, lawn2,
                                         GWY_DATA_COMPATIBILITY_RES
                                         | GWY_DATA_COMPATIBILITY_REAL
                                         | GWY_DATA_COMPATIBILITY_LATERAL
                                         | GWY_DATA_COMPATIBILITY_CURVELEN);
}

static gint
merge_lawns(GList *list)
{
    NanoscopeData *firstdata = NULL, *data = NULL;
    const gchar *name;
    GwyLawn **lawns, *first, *merged;
    GList *l;
    gint xres, yres, i, j, m, ndata, nsegments, ncurves = 0;
    const gdouble *cd;
    GArray *curvedata;

    for (l = list; l; l = g_list_next(l)) {
        data = (NanoscopeData*)l->data;
        if (!data->lawn)
            continue;
        ncurves++;
        if (firstdata) {
            if (!check_lawn_compatibility(data->lawn, firstdata->lawn)) {
                gwy_debug("lawns are not compatible, cannot merge");
                return 0;
            }
        }
        else
            firstdata = data;
    }
    if (!ncurves || data == firstdata) {
        gwy_debug("fewer than two channels, nothing to merge");
        if ((merged = add_ramp_to_lawn(data->lawn))) {
            g_object_unref(data->lawn);
            data->lawn = merged;
        }
        return 0;
    }

    gwy_debug("merging %d lawns", ncurves);
    lawns = g_new(GwyLawn*, ncurves);
    ncurves = 0;
    for (l = list; l; l = g_list_next(l)) {
        data = (NanoscopeData*)l->data;
        if (data->lawn)
            lawns[ncurves++] = data->lawn;
    }

    first = lawns[0];
    xres = gwy_lawn_get_xres(first);
    yres = gwy_lawn_get_yres(first);
    nsegments = gwy_lawn_get_n_segments(first);
    merged = gwy_lawn_new(xres, yres, gwy_lawn_get_xreal(first), gwy_lawn_get_yreal(first), ncurves, nsegments);
    gwy_si_unit_assign(gwy_lawn_get_si_unit_xy(merged), gwy_lawn_get_si_unit_xy(first));
    gwy_lawn_set_xoffset(merged, gwy_lawn_get_xoffset(first));
    gwy_lawn_set_yoffset(merged, gwy_lawn_get_yoffset(first));

    ncurves = 0;
    for (l = list; l; l = g_list_next(l)) {
        data = (NanoscopeData*)l->data;
        if (data->lawn) {
            if ((name = get_image_data_name(data->hash)))
                gwy_lawn_set_curve_label(merged, ncurves, name);
            gwy_si_unit_assign(gwy_lawn_get_si_unit_curve(merged, ncurves), gwy_lawn_get_si_unit_curve(data->lawn, 0));
            ncurves++;
        }
    }

    curvedata = g_array_new(FALSE, FALSE, sizeof(gdouble));
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            g_array_set_size(curvedata, 0);
            for (m = 0; m < ncurves; m++) {
                cd = gwy_lawn_get_curve_data(lawns[m], j, i, 0, &ndata);
                g_array_append_vals(curvedata, cd, ndata);
            }
            gwy_lawn_set_curves(merged, j, i, ndata,
                                &g_array_index(curvedata, gdouble, 0), gwy_lawn_get_segments(first, j, i, NULL));
        }
    }
    g_array_free(curvedata, TRUE);
    g_free(lawns);

    ncurves = 0;
    for (l = list; l; l = g_list_next(l)) {
        data = (NanoscopeData*)l->data;
        if (data->lawn) {
            GWY_OBJECT_UNREF(data->lawn);
            if (!ncurves)
                data->lawn = merged;
            ncurves++;
        }
    }

    return ncurves;
}

static void
make_ramp(gdouble *data, gint n, gdouble z0, gdouble q)
{
    gint i;

    for (i = 0; i < n; i++)
        data[i] = q*i/n + z0;
}

static GwyLawn*
add_ramp_to_lawn(GwyLawn *lawn)
{
    GwySIUnit *zunit = g_object_get_data(G_OBJECT(lawn), "zunit");
    gdouble *pzreal = g_object_get_data(G_OBJECT(lawn), "zreal");
    GArray *curvedata;
    gdouble *cd;
    gdouble zreal;
    gint xres, yres, ndata, nsegments, ncurves, i, j, k, first, last;
    GwyLawn *merged;
    const gchar **seglabels = NULL;
    const gchar *clabel;
    const gint *seg;

    if (!pzreal || !(*pzreal > 0.0) || !zunit)
        return NULL;

    zreal = *pzreal;
    gwy_lawn_get_segments(lawn, 0, 0, &nsegments);
    if (nsegments > 3)
        return NULL;

    gwy_debug("trying to add abscissa channel from ramp");
    ncurves = gwy_lawn_get_n_curves(lawn);
    xres = gwy_lawn_get_xres(lawn);
    yres = gwy_lawn_get_yres(lawn);
    merged = gwy_lawn_new(xres, yres, gwy_lawn_get_xreal(lawn), gwy_lawn_get_yreal(lawn), ncurves+1, nsegments);
    gwy_si_unit_assign(gwy_lawn_get_si_unit_xy(merged), gwy_lawn_get_si_unit_xy(lawn));
    gwy_lawn_set_xoffset(merged, gwy_lawn_get_xoffset(lawn));
    gwy_lawn_set_yoffset(merged, gwy_lawn_get_yoffset(lawn));
    for (k = 0; k < ncurves; k++) {
        gwy_si_unit_assign(gwy_lawn_get_si_unit_curve(merged, k+1), gwy_lawn_get_si_unit_curve(lawn, k));
        if ((clabel = gwy_lawn_get_curve_label(lawn, k)))
            gwy_lawn_set_curve_label(merged, k, clabel);
    }
    gwy_si_unit_assign(gwy_lawn_get_si_unit_curve(merged, ncurves), zunit);
    gwy_lawn_set_curve_label(merged, ncurves, "Ramp");

    if (nsegments)
        seglabels = g_new(const gchar*, nsegments);
    for (k = 0; k < nsegments; k++) {
        seglabels[k] = gwy_lawn_get_segment_label(lawn, k);
        gwy_lawn_set_segment_label(merged, k, seglabels[k]);
    }

    curvedata = g_array_new(FALSE, FALSE, sizeof(gdouble));
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            /* Copy curves from lawn. */
            g_array_set_size(curvedata, 0);
            gwy_lawn_get_curve_data(lawn, j, i, 0, &ndata);
            for (k = 0; k < ncurves; k++)
                g_array_append_vals(curvedata, gwy_lawn_get_curve_data(lawn, j, i, k, NULL), ndata);

            /* Fill the last curve with the ramp (or ramps). */
            seg = gwy_lawn_get_segments(lawn, j, i, NULL);
            g_array_set_size(curvedata, (ncurves + 1)*ndata);
            cd = &g_array_index(curvedata, gdouble, ncurves*ndata);
            if (!nsegments)
                make_ramp(cd, ndata, 0.0, zreal);
            else {
                gwy_clear(cd, ndata);
                for (k = 0; k < nsegments; k++) {
                    first = CLAMP(seg[2*k], 0, ndata-1);
                    last = CLAMP(seg[2*k+1], seg[2*k], ndata);
                    if (gwy_strequal(seglabels[k], "Hold"))
                        make_ramp(cd + first, last - first, zreal, 0.0);
                    else if (gwy_strequal(seglabels[k], "Retract"))
                        make_ramp(cd + first, last - first, zreal, -zreal);
                    else
                        make_ramp(cd + first, last - first, 0.0, zreal);
                }
            }

            gwy_lawn_set_curves(merged, j, i, ndata, &g_array_index(curvedata, gdouble, 0), seg);
        }
    }

    g_array_free(curvedata, TRUE);
    g_free(seglabels);

    return merged;
}

#define CHECK_AND_APPLY(op, hash, key)                     \
        if (!(val = g_hash_table_lookup((hash), (key)))) { \
            err_MISSING_FIELD(error, (key));               \
            return NULL;                                   \
        }                                                  \
        *scale op val->hard_value

static GwySIUnit*
get_physical_scale(GHashTable *hash,
                   GHashTable *scannerlist,
                   GHashTable *scanlist,
                   GHashTable *contrlist,
                   G_GNUC_UNUSED gulong version,
                   gboolean try_also_xz,
                   gdouble *scale,
                   gint qbpp,
                   GError **error)
{
    GwySIUnit *siunit, *siunit2;
    NanoscopeValue *val, *sval;
    gchar *key;
    gint q1, q2;

    /* version = 4.2 */
    if ((val = g_hash_table_lookup(hash, "Z scale"))) {
        /* Old style scales */
        gwy_debug("Old-style scale, using hard units %g %s", val->hard_value, val->hard_value_units);
        siunit = gwy_si_unit_new_parse(val->hard_value_units, &q1);
        *scale = val->hard_value * pow10(q1);
        return siunit;

    }
    /* version >= 4.3 */
    else if ((val = g_hash_table_lookup(hash, "@4:Z scale"))
             || (val = g_hash_table_lookup(hash, "@2:Z scale"))
             || (try_also_xz && (val = g_hash_table_lookup(hash, "@2:Z scale X scan")))
             || (try_also_xz && (val = g_hash_table_lookup(hash, "@2:Z scale ZSensor")))) {
        /* Resolve reference to a soft scale */
        if (val->soft_scale) {
            gwy_debug("have soft scale (%s)", val->soft_scale);
            key = g_strdup_printf("@%s", val->soft_scale);

            gwy_debug("looking for %s", key);
            if (!(sval = g_hash_table_lookup(scannerlist, key))
                && (!scanlist || !(sval = g_hash_table_lookup(scanlist, key)))) {
                g_warning("`%s' not found", key);
                g_free(key);
                /* XXX */
                *scale = val->hard_value;
                return gwy_si_unit_new(NULL);
            }

#if 1
            /* What we had been traditionally doing.  This seems correct for images. */
            *scale = val->hard_value*sval->hard_value;
            gwy_debug("Hard-value scale %g (%g * %g)", *scale, val->hard_value, sval->hard_value);

            if (!sval->hard_value_units || !*sval->hard_value_units) {
                gwy_debug("No hard value units");
                if (gwy_strequal(val->soft_scale, "Sens. Phase"))
                    siunit = gwy_si_unit_new("deg");
                else
                    siunit = gwy_si_unit_new("V");
            }
            else {
                siunit = gwy_si_unit_new_parse(sval->hard_value_units, &q2);
                if (val->hard_value_units && *val->hard_value_units) {
                    siunit2 = gwy_si_unit_new_parse(val->hard_value_units, &q1);
                }
                else {
                    siunit2 = gwy_si_unit_new("V");
                    q1 = 0;
                }
                gwy_si_unit_multiply(siunit, siunit2, siunit);
                gwy_debug("Scale1 = %g V/LSB", val->hard_value*pow10(q1));
                gwy_debug("Scale2 = %g %s", sval->hard_value, sval->hard_value_units);
                *scale *= pow10(q1 + q2);
#ifdef DEBUG
                gwy_debug("Total scale = %g %s/LSB", *scale, gwy_si_unit_get_string(siunit, GWY_SI_UNIT_FORMAT_PLAIN));
#endif
                g_object_unref(siunit2);
            }
#else
            /* According to Bruker-SPM-files-format.pdf, which apparently is right for some force spectroscopy. */
            *scale = val->hard_scale*sval->hard_value;
            gwy_debug("Value scale %g (%g * %g) (including qbpp)", *scale, val->hard_scale, sval->hard_value);

            if (!sval->hard_value_units || !*sval->hard_value_units) {
                gwy_debug("No hard value units");
                if (gwy_strequal(val->soft_scale, "Sens. Phase"))
                    siunit = gwy_si_unit_new("deg");
                else
                    siunit = gwy_si_unit_new("V");
            }
            else {
                siunit = gwy_si_unit_new_parse(sval->hard_value_units, &q2);
                if (val->hard_scale_units && *val->hard_scale_units) {
                    siunit2 = gwy_si_unit_new_parse(val->hard_scale_units, &q1);
                }
                else {
                    siunit2 = gwy_si_unit_new("V");
                    q1 = 0;
                }
                gwy_si_unit_multiply(siunit, siunit2, siunit);
                gwy_debug("Scale1 = %g V/LSB", val->hard_scale*pow10(q1));
                gwy_debug("Scale2 = %g %s", sval->hard_value, sval->hard_value_units);
                *scale *= pow10(q1 + q2);
#ifdef DEBUG
                gwy_debug("Total scale = %g %s/LSB", *scale, gwy_si_unit_get_string(siunit, GWY_SI_UNIT_FORMAT_PLAIN));
#endif
                g_object_unref(siunit2);
            }
            /* The caller expects things will be divided by qbpp.  So multiply it to correct.  This is silly but tries
             * to avoid breaking things.  */
            *scale *= gwy_powi(256.0, qbpp);
            gwy_debug("LSB multiplier %g", gwy_powi(256.0, qbpp));
#endif
            g_free(key);
        }
        else {
            /* We have '@2:Z scale' but the reference to soft scale is missing, the quantity is something in the hard
             * units (seen for Potential). */
            gwy_debug("No soft scale, using hard units %g %s", val->hard_value, val->hard_value_units);
            siunit = gwy_si_unit_new_parse(val->hard_value_units, &q1);
            *scale = val->hard_value * pow10(q1);
        }
        return siunit;
    }
    else  { /* no version */
        if (!(val = g_hash_table_lookup(hash, "Image data"))) {
             err_MISSING_FIELD(error, "Image data");
             return NULL;
        }

        if (gwy_strequal(val->hard_value_str, "Deflection")) {
            siunit = gwy_si_unit_new("m"); /* always? */
            *scale = 1e-9 * 2.0/65536.0;
            CHECK_AND_APPLY(*=, hash, "Z scale defl");
            CHECK_AND_APPLY(*=, contrlist, "In1 max");
            CHECK_AND_APPLY(*=, scannerlist, "In sensitivity");
            CHECK_AND_APPLY(/=, scanlist, "Detect sens.");
            return siunit;
        }
        else if (gwy_strequal(val->hard_value_str, "Amplitude")) {
            siunit = gwy_si_unit_new("m");
            *scale = 1e-9 * 2.0/65536.0;
            CHECK_AND_APPLY(*=, hash, "Z scale ampl");
            CHECK_AND_APPLY(*=, contrlist, "In1 max");
            CHECK_AND_APPLY(*=, scannerlist, "In sensitivity");
            CHECK_AND_APPLY(/=, scanlist, "Detect sens.");
            return siunit;
        }
        else if (gwy_strequal(val->hard_value_str, "Frequency")) {
            siunit = gwy_si_unit_new("Hz");
            *scale = 25e6/32768.0;
            CHECK_AND_APPLY(*=, hash, "Z scale freq");
            return siunit;
        }
        else if (gwy_strequal(val->hard_value_str, "Current")) {
            siunit = gwy_si_unit_new("A");
            *scale = 1e-9 * 2.0/32768.0;
            CHECK_AND_APPLY(*=, hash, "Z scale amplitude");
            CHECK_AND_APPLY(*=, contrlist, "In1 max");
            CHECK_AND_APPLY(*=, scannerlist, "In sensitivity");
            return siunit;
        }
        else if (gwy_strequal(val->hard_value_str, "Phase")) {
            siunit = gwy_si_unit_new("deg");
            *scale = 180.0/65536.0;
            CHECK_AND_APPLY(*=, hash, "Z scale phase");
            return siunit;
        }
        else if (gwy_strequal(val->hard_value_str, "Height")) {
            siunit = gwy_si_unit_new("m");
            *scale = 1e-9 * 2.0/65536.0;
            CHECK_AND_APPLY(*=, hash, "Z scale height");
            CHECK_AND_APPLY(*=, contrlist, "Z max");
            CHECK_AND_APPLY(*=, scannerlist, "Z sensitivity");
            return siunit;
        }

        return NULL;
    }
}

static GwyGraphModel*
hash_to_curve(GHashTable *hash,
              GHashTable *forcelist,
              GHashTable *scanlist,
              GHashTable *scannerlist,
              NanoscopeFileType file_type,
              gulong version,
              gsize bufsize,
              const guchar *buffer,
              gint gxres,
              GError **error)
{
    NanoscopeFileType base_type = file_type & ~NANOSCOPE_FILE_TYPE_32BIT_FLAG;
    NanoscopeValue *val;
    NanoscopeSpectraType spectype;
    GwyDataLine *dline = NULL;
    GwyGraphModel *gmodel = NULL;
    GwyGraphCurveModel *gcmodel;
    GwySIUnit *unitz = NULL, *unitx = NULL;
    gsize xres, bpp, qbpp, offset, size;
    gdouble xreal, xoff, q = 1.0;
    gdouble *data;
    gboolean size_ok, use_global, convert_to_force = FALSE;
    /* Call the curves Trace and Retrace when we do now know (often not correct).  Replace the titles with something
     * meaningful for specific types. */
    const gchar *title0 = "Trace", *title1 = "Retrace";

    g_return_val_if_fail(base_type == NANOSCOPE_FILE_TYPE_FORCE_BIN, NULL);

    if (!require_keys(hash, error, "Samps/line", "Data offset", "Data length", "@4:Image Data", NULL))
        return NULL;

    if (!require_keys(scanlist, error, "Scan size", NULL))
        return NULL;

    if (!(unitx = get_spec_abscissa_scale(hash, forcelist, scannerlist, scanlist, &xreal, &xoff, &spectype, error)))
        return NULL;

    val = g_hash_table_lookup(hash, "Samps/line");
    xres = (gsize)val->hard_value;

    get_bpp_and_qbpp(hash, file_type, &bpp, &qbpp);

    if (!get_offset_and_size(hash, bufsize, &offset, &size, error))
        goto fail;

    size_ok = FALSE;
    use_global = FALSE;

    /* Try channel size and local size */
    if (!size_ok && size == 2*bpp*xres)
        size_ok = TRUE;

    if (!size_ok && size == 2*bpp*gxres) {
        size_ok = TRUE;
        use_global = TRUE;
    }

    gwy_debug("size=%lu, xres=%lu, gxres=%lu, bpp=%lu", (gulong)size, (gulong)xres, (gulong)gxres, (gulong)bpp);

    /* If they don't match exactly, try whether they at least fit inside */
    if (!size_ok && size > bpp*MAX(2*xres, 2*gxres)) {
        size_ok = TRUE;
        use_global = (xres < gxres);
    }

    if (!size_ok && size > bpp*MIN(2*xres, 2*gxres)) {
        size_ok = TRUE;
        use_global = (xres > gxres);
    }

    if (!size_ok) {
        err_SIZE_MISMATCH(error, bpp*xres, size, TRUE);
        goto fail;
    }

    if (use_global && gxres)
        xres = gxres;

    if (err_DIMENSION(error, xres))
        goto fail;

    if (err_SIZE_MISMATCH(error, offset + size, bufsize, FALSE))
        goto fail;

    /* Use negated positive conditions to catch NaNs */
    if (!((xreal = fabs(xreal)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        xreal = 1.0;
    }

    val = g_hash_table_lookup(hash, "@4:Image Data");
    gwy_debug("channel=%s", val->hard_value_str);
    if (spectype == NANOSCOPE_SPECTRA_FZ
        && gwy_strequal(val->hard_value_str, "Deflection Error"))
        convert_to_force = TRUE;

    if (!(unitz = get_spec_ordinate_scale(hash, scanlist, version, &q, &convert_to_force, qbpp, error)))
        goto fail;

    gmodel = gwy_graph_model_new();
    // TODO: Spectrum type.
    if (spectype == NANOSCOPE_SPECTRA_IV) {
        g_object_set(gmodel,
                     "title", "I-V spectrum",
                     "axis-label-bottom", "Voltage",
                     "axis-label-left", val->hard_value_str,
                     NULL);
    }
    else if (convert_to_force) {
        title0 = "Extend";
        title1 = "Retract";
        g_object_set(gmodel,
                     "title", "F-Z spectrum",
                     "axis-label-bottom", "Distance",
                     "axis-label-left", "Force",
                     NULL);
    }
    else if (spectype == NANOSCOPE_SPECTRA_FZ) {
        title0 = "Extend";
        title1 = "Retract";
        g_object_set(gmodel,
                     "title", "F-Z spectrum",
                     "axis-label-bottom", "Distance",
                     "axis-label-left", val->hard_value_str,
                     NULL);
    }

    dline = gwy_data_line_new(xres, xreal, FALSE);
    gwy_data_line_set_offset(dline, xoff);
    gwy_si_unit_assign(gwy_data_line_get_si_unit_x(dline), unitx);
    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(dline), unitz);

    data = gwy_data_line_get_data(dline);
    gwy_debug("curve 1 at offset %lu", (gulong)offset);
    if (!read_binary_data(xres, data, buffer + offset, bpp, qbpp, error)) {
        GWY_OBJECT_UNREF(gmodel);
        goto fail;
    }
    gwy_debug("multiplying values by %g", q);
    gwy_data_line_multiply(dline, q);
    if (spectype == NANOSCOPE_SPECTRA_FZ)
        gwy_data_line_invert(dline, TRUE, FALSE);
    gcmodel = gwy_graph_curve_model_new();
    gwy_graph_curve_model_set_data_from_dataline(gcmodel, dline, 0, 0);
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(0),
                 "description", title0,
                 NULL);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);

    gwy_debug("curve 2 at offset %lu", (gulong)(offset + bpp*xres));
    if (!read_binary_data(xres, data, buffer + offset + bpp*xres, bpp, qbpp, error)) {
        GWY_OBJECT_UNREF(gmodel);
        goto fail;
    }
    gwy_data_line_multiply(dline, q);
    if (spectype == NANOSCOPE_SPECTRA_FZ)
        gwy_data_line_invert(dline, TRUE, FALSE);
    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(1),
                 "description", title1,
                 NULL);
    gwy_graph_curve_model_set_data_from_dataline(gcmodel, dline, 0, 0);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);
    gwy_graph_model_set_units_from_data_line(gmodel, dline);

fail:
    GWY_OBJECT_UNREF(dline);
    GWY_OBJECT_UNREF(unitz);
    GWY_OBJECT_UNREF(unitx);

    return gmodel;
}

/*
 * get HardValue from Ciao force image list/@4:Z scale -> HARD
 * get SoftScale from Ciao force image list/@4:Z scale -> SENS
 * get \@SENS in Ciao scan list -> SOFT
 * the factor is HARD*SOFT
 */
static GwySIUnit*
get_spec_ordinate_scale(GHashTable *hash,
                        GHashTable *scanlist,
                        G_GNUC_UNUSED gulong version,
                        gdouble *scale,
                        gboolean *convert_to_force,
                        gint qbpp,
                        GError **error)
{
    GwySIUnit *siunit, *siunit2;
    NanoscopeValue *val, *sval;
    gchar *key;
    gint q;

    if (!(val = g_hash_table_lookup(hash, "@4:Z scale"))) {
        err_MISSING_FIELD(error, "Z scale");
        return NULL;
    }

    /* Resolve reference to a soft scale */
    if (val->soft_scale) {
        gwy_debug("Soft scale %s", val->soft_scale);
        key = g_strdup_printf("@%s", val->soft_scale);
        if ((!scanlist || !(sval = g_hash_table_lookup(scanlist, key)))) {
            g_warning("`%s' not found", key);
            g_free(key);
            /* XXX */
            *scale = 2.0*val->hard_value;
            *convert_to_force = FALSE;
            return gwy_si_unit_new(NULL);
        }

        *scale = val->hard_scale*sval->hard_value;

        /* Here we kind of assume hardscale is always V/LSB, which is V for us. */
        gwy_debug("Hard scale units: %s", val->hard_scale_units);
        siunit2 = gwy_si_unit_new("V");

        siunit = gwy_si_unit_new_parse(sval->hard_value_units, &q);
        gwy_si_unit_multiply(siunit, siunit2, siunit);
        gwy_debug("Scale1 = %g V/LSB", val->hard_scale);
        gwy_debug("Scale2 = %g %s", sval->hard_value, sval->hard_value_units);
        *scale *= pow10(q);
#ifdef DEBUG
        gwy_debug("Total scale = %g %s/LSB", *scale, gwy_si_unit_get_string(siunit, GWY_SI_UNIT_FORMAT_PLAIN));
#endif
        g_object_unref(siunit2);
        g_free(key);

        if (g_str_has_prefix(val->hard_scale_units, "log("))
            gwy_si_unit_set_from_string(siunit, "");

        if (*convert_to_force && (sval = g_hash_table_lookup(hash, "Spring Constant"))) {
            gwy_debug("Spring Constant: %g", sval->hard_value);
            // FIXME: Whatever.  For some reason this means Force.
            *scale *= sval->hard_value;
            gwy_si_unit_set_from_string(siunit, "N");
        }
        else
            *convert_to_force = FALSE;

        /* The caller expects things will be divided by qbpp.  So multiply it to correct.  This is silly but tries to
         * avoid breaking things.  */
        *scale *= gwy_powi(256.0, qbpp);
        gwy_debug("LSB multiplier %g", gwy_powi(256.0, qbpp));
    }
    else {
        /* FIXME: Is this possible for I-V too? */
        /* We have '@4:Z scale' but the reference to soft scale is missing, the quantity is something in the hard
         * units (seen for Potential). */
        siunit = gwy_si_unit_new_parse(val->hard_value_units, &q);
        *scale = val->hard_value * pow10(q);
        *convert_to_force = FALSE;
    }

    return siunit;
}

static GwySIUnit*
get_spec_abscissa_scale(GHashTable *hash,
                        GHashTable *forcelist,
                        GHashTable *scannerlist,
                        GHashTable *scanlist,
                        gdouble *xreal,
                        gdouble *xoff,
                        NanoscopeSpectraType *spectype,
                        GError **error)
{
    GwySIUnit *siunit, *siunit2;
    NanoscopeValue *val, *rval, *sval;
    gdouble scale = 1.0;
    gchar *key, *end;
    gint q;

    if (!(val = g_hash_table_lookup(forcelist, "@4:Ramp channel"))) {
        err_MISSING_FIELD(error, "Ramp channel");
        return NULL;
    }

    if (!val->hard_value_str) {
        err_INVALID(error, "Ramp channel");
        return NULL;
    }

    if (gwy_strequal(val->hard_value_str, "DC Sample Bias"))
        *spectype = NANOSCOPE_SPECTRA_IV;
    else if (gwy_strequal(val->hard_value_str, "Z"))
        *spectype = NANOSCOPE_SPECTRA_FZ;
    else {
        err_UNSUPPORTED(error, "Ramp channel");
        return NULL;
    }

    if (*spectype == NANOSCOPE_SPECTRA_IV) {
        if (!require_keys(forcelist, error, "@4:Ramp End DC Sample Bias", "@4:Ramp Begin DC Sample Bias", NULL))
            return NULL;
        rval = g_hash_table_lookup(forcelist, "@4:Ramp End DC Sample Bias");
        *xreal = g_ascii_strtod(rval->hard_value_str, &end);
        rval = g_hash_table_lookup(forcelist, "@4:Ramp Begin DC Sample Bias");
        *xoff = g_ascii_strtod(rval->hard_value_str, &end);
        *xreal -= *xoff;
    }
    else if (*spectype == NANOSCOPE_SPECTRA_FZ) {
        if (!require_keys(hash, error, "@4:Ramp size", "Samps/line", NULL))
            return NULL;
        rval = g_hash_table_lookup(hash, "@4:Ramp size");
        *xreal = g_ascii_strtod(rval->hard_value_str, &end);
        *xoff = 0.0;
    }
    else {
        g_assert_not_reached();
        return NULL;
    }
    gwy_debug("Hard ramp size: %g", *xreal);

    /* Resolve reference to a soft scale */
    if (rval->soft_scale) {
        key = g_strdup_printf("@%s", rval->soft_scale);
        if (scannerlist && (sval = g_hash_table_lookup(scannerlist, key))) {
            gwy_debug("Found %s in scannerlist", key);
        }
        else if (scanlist && (sval = g_hash_table_lookup(scanlist, key))) {
            gwy_debug("Found %s in scanlist", key);
        }
        else {
            g_warning("`%s' not found", key);
            g_free(key);
            /* XXX */
            scale = rval->hard_value;
            return gwy_si_unit_new(NULL);
        }

        scale = sval->hard_value;

        siunit = gwy_si_unit_new_parse(sval->hard_value_units, &q);
        siunit2 = gwy_si_unit_new("V");
        gwy_si_unit_multiply(siunit, siunit2, siunit);
        gwy_debug("Scale1 = %g V/LSB", rval->hard_value);
        gwy_debug("Scale2 = %g %s", sval->hard_value, sval->hard_value_units);
        scale *= pow10(q);
#ifdef DEBUG
        gwy_debug("Total scale = %g %s/LSB", scale, gwy_si_unit_get_string(siunit, GWY_SI_UNIT_FORMAT_PLAIN));
#endif
        g_object_unref(siunit2);
        g_free(key);
    }
    else {
        /* FIXME: Is this possible for spectra too? */
        /* We have '@4:Z scale' but the reference to soft scale is missing, the quantity is something in the hard
         * units (seen for Potential). */
        siunit = gwy_si_unit_new_parse(rval->hard_value_units, &q);
        scale = rval->hard_value * pow10(q);
    }

    *xreal *= scale;
    *xoff *= scale;

    return siunit;
}

/* Seems correct also for volume data. */
static const gchar*
get_image_data_name(GHashTable *hash)
{
    NanoscopeValue *val;
    const gchar *name = NULL;

    if ((val = g_hash_table_lookup(hash, "@2:Image Data"))
        || (val = g_hash_table_lookup(hash, "@3:Image Data"))
        || (val = g_hash_table_lookup(hash, "@4:Image Data"))) {
        if (val->soft_scale)
            name = val->soft_scale;
        else if (val->hard_value_str)
            name = val->hard_value_str;
    }
    else if ((val = g_hash_table_lookup(hash, "Image data")))
        name = val->hard_value_str;

    return name;
}

static void
get_bpp_and_qbpp(GHashTable *hash, NanoscopeFileType file_type,
                 gsize *bpp, gsize *qbpp)
{
    const NanoscopeValue *val;

    /* Bytes/pixel determines the scaling factor, not actual raw data type.
     * This is what Bruker people say. */
    val = g_hash_table_lookup(hash, "Bytes/pixel");
    *qbpp = val ? (gsize)val->hard_value : 2;
    *bpp = ((file_type & NANOSCOPE_FILE_TYPE_32BIT_FLAG) ? 4 : 2);
}

static gboolean
get_offset_and_size(GHashTable *hash, gsize bufsize, gsize *offset, gsize *size, GError **error)
{
    const NanoscopeValue *val;

    if (!(val = g_hash_table_lookup(hash, "Data offset"))) {
        err_MISSING_FIELD(error, "Data offset");
        return FALSE;
    }
    *offset = (gsize)val->hard_value;

    if (!(val = g_hash_table_lookup(hash, "Data length"))) {
        err_MISSING_FIELD(error, "Data length");
        return FALSE;
    }
    *size = (gsize)val->hard_value;

    if (*offset > bufsize || *size > bufsize - *offset) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("File is truncated."));
        return FALSE;
    }

    return TRUE;
}

static void
get_scan_list_res(GHashTable *hash, gsize *xres, gsize *yres)
{
    NanoscopeValue *val;

    /* XXX: Some observed files contained correct dimensions only in a global section, sizes in `image list' sections
     * were bogus. Version: 0x05300001 */
    if ((val = g_hash_table_lookup(hash, "Samps/line")))
        *xres = (gsize)val->hard_value;
    if ((val = g_hash_table_lookup(hash, "Lines")))
        *yres = (gsize)val->hard_value;
    gwy_debug("Global xres, yres = %lu, %lu", (gulong)*xres, (gulong)*yres);
}

static guint
get_samples_per_curve(GHashTable *hash, GHashTable *forcelist, guint *hold_samples, guint *retract_samples)
{
    NanoscopeValue *val;
    gint res, res2;

    *hold_samples = *retract_samples = 0;
    if (!(val = g_hash_table_lookup(forcelist, "Samps/line")))
        val = g_hash_table_lookup(hash, "Samps/line");
    g_return_val_if_fail(val, 0);

    if (sscanf(val->hard_value_str, "%u %u", &res, &res2) == 2) {
        gwy_debug("number of samples is a tuple %d %d", res, res2);
        *retract_samples = res2;
    }
    else {
        res = (gsize)val->hard_value;
        gwy_debug("number of samples is a single number %d", res);
    }

    if ((val = g_hash_table_lookup(forcelist, "Hold Samples"))) {
        *hold_samples = GWY_ROUND(val->hard_value);
        gwy_debug("there are %u hold samples", *hold_samples);
    }

    return res;
}

static GwySIUnit*
get_scan_size(GHashTable *hash,
              gdouble *xreal, gdouble *yreal,
              GError **error)
{
    NanoscopeValue *val;
    GwySIUnit *unit;
    gchar un[8];
    gchar *end, *s;
    gint power10;
    gdouble q;

    /* scan size */
    val = g_hash_table_lookup(hash, "Scan size");
    *xreal = g_ascii_strtod(val->hard_value_str, &end);
    if (errno || *end != ' ') {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Cannot parse `Scan size' field."));
        return NULL;
    }
    gwy_debug("xreal = %g", *xreal);
    s = end+1;
    *yreal = g_ascii_strtod(s, &end);
    if (errno || *end != ' ') {
        /* Old files don't have two numbers here, assume equal dimensions */
        *yreal = *xreal;
        end = s;
    }
    gwy_debug("yreal = %g", *yreal);
    while (g_ascii_isspace(*end))
        end++;
    if (sscanf(end, "%7s", un) != 1) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Cannot parse `Scan size' field."));
        return NULL;
    }
    gwy_debug("xy unit: <%s>", un);
    unit = gwy_si_unit_new_parse(un, &power10);
    q = pow10(power10);
    *xreal *= q;
    *yreal *= q;

    return unit;
}

static gboolean
has_nonsquare_aspect(GHashTable *hash)
{
    NanoscopeValue *val;
    gdouble ar;

    val = g_hash_table_lookup(hash, "Aspect ratio");
    if (!val || gwy_strequal(val->hard_value_str, "1:1"))
        return FALSE;

    ar = g_ascii_strtod(val->hard_value_str, NULL);
    if (ar > 0.0 && ar != 1.0)
        return TRUE;
    return FALSE;
}

static gboolean
read_text_data(guint n, gdouble *data,
               gchar **buffer,
               gint bpp,
               GError **error)
{
    guint i;
    gdouble q;
    gchar *end;
    long l, min, max;

    q = gwy_powi(1.0/256.0, bpp);
    min = 10000000;
    max = -10000000;
    for (i = 0; i < n; i++) {
        /*data[i] = q*strtol(*buffer, &end, 10);*/
        l = strtol(*buffer, &end, 10);
        min = MIN(l, min);
        max = MAX(l, max);
        data[i] = q*l;
        if (end == *buffer) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Garbage after data sample #%u."), i);
            return FALSE;
        }
        *buffer = end;
    }
    gwy_debug("min = %ld, max = %ld", min, max);
    return TRUE;
}

static gboolean
read_binary_data(gint n, gdouble *data, const guchar *buffer,
                 gint bpp,   /* actual type */
                 gint qbpp,  /* type determining the factor, may be different */
                 GError **error)
{
    static const GwyRawDataType rawtypes[] = {
        0, GWY_RAW_DATA_SINT8, GWY_RAW_DATA_SINT16, 0, GWY_RAW_DATA_SINT32,
    };

    if (bpp >= G_N_ELEMENTS(rawtypes) || !rawtypes[bpp]) {
        err_BPP(error, bpp);
        return FALSE;
    }
    /* gwy_debug("LSB multiplier 1/%g", gwy_powi(256.0, qbpp)); */
    gwy_convert_raw_data(buffer, n, 1, rawtypes[bpp], GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         data, gwy_powi(1.0/256.0, qbpp), 0.0);

    return TRUE;
}

static GHashTable*
read_hash(gchar **buffer,
          GError **error)
{
    GHashTable *hash;
    NanoscopeValue *value;
    gchar *line, *colon;

    line = gwy_str_next_line(buffer);
    if (line[0] != '\\' || line[1] != '*')
        return NULL;
    if (gwy_strequal(line, "\\*File list end")) {
        gwy_debug("FILE LIST END");
        return NULL;
    }

    hash = g_hash_table_new_full(gwy_ascii_strcase_hash, gwy_ascii_strcase_equal, NULL, g_free);
    g_hash_table_insert(hash, "#self", g_strdup(line + 2));    /* self */
    gwy_debug("hash table <%s>", line + 2);
    while ((*buffer)[0] == '\\' && (*buffer)[1] && (*buffer)[1] != '*') {
        line = gwy_str_next_line(buffer) + 1;
        if (!line || !line[0] || !line[1] || !line[2]) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Truncated header line."));
            goto fail;
        }
        colon = line;
        if (line[0] == '@' && g_ascii_isdigit(line[1]) && line[2] == ':')
            colon = line+3;
        colon = strchr(colon, ':');
        if (!colon || !g_ascii_isspace(colon[1])) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Missing colon in header line."));
            goto fail;
        }
        *colon = '\0';
        do {
            colon++;
        } while (g_ascii_isspace(*colon));
        g_strchomp(line);
        value = parse_value(line, colon);
        if (value)
            g_hash_table_insert(hash, line, value);

        while ((*buffer)[0] == '\r') {
            g_warning("Possibly split line encountered.  Trying to synchronize.");
            line = gwy_str_next_line(buffer) + 1;
            line = gwy_str_next_line(buffer) + 1;
        }
    }

    /* Fix random stuff in Nanoscope E files */
    if ((value = g_hash_table_lookup(hash, "Samps/line"))
        && !g_hash_table_lookup(hash, "Number of lines")
        && value->hard_value_units
        && g_ascii_isdigit(value->hard_value_units[0])) {
        NanoscopeValue *val;
        val = g_new0(NanoscopeValue, 1);
        val->hard_value = g_ascii_strtod(value->hard_value_units, NULL);
        val->hard_value_str = value->hard_value_units;
        g_hash_table_insert(hash, "Number of lines", val);
    }

    return hash;

fail:
    g_hash_table_destroy(hash);
    return NULL;
}

/* General parameter line parser */
static NanoscopeValue*
parse_value(const gchar *key, gchar *line)
{
    NanoscopeValue *val;
    gchar *p, *q;
    guint len;

    val = g_new0(NanoscopeValue, 1);

    /* old-style values */
    if (key[0] != '@') {
        val->hard_value = g_ascii_strtod(line, &p);
        if (p-line > 0 && *p == ' ') {
            do {
                p++;
            } while (g_ascii_isspace(*p));
            if ((q = strchr(p, '('))) {
                *q = '\0';
                q++;
                val->hard_scale = g_ascii_strtod(q, &q);
                if (*q != ')')
                    val->hard_scale = 0.0;
            }
            val->hard_value_units = p;
        }
        val->hard_value_str = line;
        return val;
    }

    /* type */
    switch (line[0]) {
        case 'V':
        val->type = NANOSCOPE_VALUE_VALUE;
        break;

        case 'S':
        val->type = NANOSCOPE_VALUE_SELECT;
        break;

        case 'C':
        val->type = NANOSCOPE_VALUE_SCALE;
        break;

        default:
        g_warning("Cannot parse value type <%s> for key <%s>", line, key);
        g_free(val);
        return NULL;
        break;
    }

    line++;
    if (line[0] != ' ') {
        g_warning("Cannot parse value type <%s> for key <%s>", line, key);
        g_free(val);
        return NULL;
    }
    do {
        line++;
    } while (g_ascii_isspace(*line));

    /* softscale */
    if (line[0] == '[') {
        if (!(p = strchr(line, ']'))) {
            g_warning("Cannot parse soft scale <%s> for key <%s>", line, key);
            g_free(val);
            return NULL;
        }
        if (p-line-1 > 0) {
            *p = '\0';
            val->soft_scale = line+1;
        }
        line = p+1;
        if (line[0] != ' ') {
            g_warning("Cannot parse soft scale <%s> for key <%s>", line, key);
            g_free(val);
            return NULL;
        }
        do {
            line++;
        } while (g_ascii_isspace(*line));
    }

    /* hardscale */
    if (line[0] == '(') {
        int paren_level;
        do {
            line++;
        } while (g_ascii_isspace(*line));
        for (p = line, paren_level = 1; *p && paren_level; p++) {
            if (*p == ')')
                paren_level--;
            else if (*p == '(')
                paren_level++;
        }
        if (!*p) {
            g_warning("Cannot parse hard scale <%s> for key <%s>", line, key);
            g_free(val);
            return NULL;
        }
        p--;
        val->hard_scale = g_ascii_strtod(line, &q);
        while (g_ascii_isspace(*q))
            q++;
        if (p-q > 0) {
            *p = '\0';
            val->hard_scale_units = q;
            g_strchomp(q);
            if (g_str_has_suffix(q, "/LSB"))
                q[strlen(q) - 4] = '\0';
        }
        line = p+1;
        if (line[0] != ' ') {
            g_warning("Cannot parse hard scale <%s> for key <%s>", line, key);
            g_free(val);
            return NULL;
        }
        do {
            line++;
        } while (g_ascii_isspace(*line));
    }

    /* hard value (everything else) */
    switch (val->type) {
        case NANOSCOPE_VALUE_SELECT:
        val->hard_value_str = line;
        len = strlen(line);
        if (line[0] == '"' && line[len-1] == '"') {
            line[len-1] = '\0';
            val->hard_value_str++;
        }
        break;

        case NANOSCOPE_VALUE_SCALE:
        val->hard_value = g_ascii_strtod(line, &p);
        break;

        case NANOSCOPE_VALUE_VALUE:
        val->hard_value = g_ascii_strtod(line, &p);
        if (p-line > 0 && *p == ' ' && !strchr(p+1, ' ')) {
            do {
                p++;
            } while (g_ascii_isspace(*p));
            val->hard_value_units = p;
        }
        val->hard_value_str = line;
        break;

        default:
        g_assert_not_reached();
        break;
    }

    return val;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
