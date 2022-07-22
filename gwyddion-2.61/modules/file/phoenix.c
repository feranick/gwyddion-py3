/*
 *  $Id: phoenix.c 21783 2019-01-03 12:58:14Z yeti-dn $
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
 * [FILE-MAGIC-USERGUIDE]
 * NASA Phoenix Mars mission AFM data
 * .dat, .lbl + .tab
 * Read
 **/

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"

#define MAGIC "PDS_VERSION_ID "
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define EMPTY_KEY "                                "

enum {
    BINARY_HEADER_SIZE = 36,
    AFM_LINE_SIZE = 8,
};

typedef struct _PhoenixRecord PhoenixRecord;

struct _PhoenixRecord {
    PhoenixRecord *parent;
    gchar *name;
    gchar *value;
    GArray *records;     /* used if name is OBJECT */
};

typedef enum {
    PHOENIX_AFM_FRQTEST  = 0,
    PHOENIX_AFM_RESPONSE = 1,
    PHOENIX_AFM_SCAN     = 2,   /* The only type we read. */
    PHOENIX_AFM_TIPS     = 3,
    PHOENIX_CME_STATUS   = 4,
    PHOENIX_POWER_DATA   = 5,
    PHOENIX_TBL          = 6,
    PHOENIX_TECP         = 7,
    PHOENIX_WCL_ISES     = 8,
    PHOENIX_WCL_COND     = 9,
    PHOENIX_WCL_DOX      = 10,
    PHOENIX_WCL_CV       = 11,
    PHOENIX_WCL_CP       = 12,
    PHOENIX_WCL_AS       = 13,
    PHOENIX_WCL_PT       = 14,
} PhoenixDataType;

typedef enum {
    PHOENIX_DIRECTION_FORWARD  = 1,
    PHOENIX_DIRECTION_BACKWARD = 2,
} PhoenixDirection;

/* Make up your mind! */
typedef enum {
    PHOENIX_LINE_DIRECTION_FORWARD  = 0,
    PHOENIX_LINE_DIRECTION_BACKWARD = 1,
} PhoenixLineDirection;

typedef enum {
    PHOENIX_CHANNEL_ERROR  = 1,
    PHOENIX_CHANNEL_HEIGHT = 2,
} PhoenixChannel;

typedef struct {
    PhoenixLineDirection direction;
    guint channel_mask;   /* always 3 */
    gint lineno;
    gint zoff;
    guint zgain;
    guint Vap_unused;     /* always 0 */
} PhoenixAFMLine;

/* Generic header. */
typedef struct {
    guint cmd_secs;
    guint cmd_frac;
    guint read_secs;
    guint read_frac;
    guint data_length;
    guint nrecords;
    guint record_length;
    guint record_num;
    PhoenixDataType data_type;
    guint xres;                   /* AFM_SCAN-specific field. */
    guint yres;                   /* AFM_SCAN-specific field. */
    PhoenixDirection direction;   /* AFM_SCAN-specific field. */
    PhoenixChannel channel;       /* AFM_SCAN-specific field. */
    guint zoom_region;            /* AFM_SCAN-specific field. */
    guint ops_token;
} PhoenixBinaryHeader;

typedef struct {
    gchar *filename;
    gchar *name;
    guint lineno;
    gsize offset;
    guint rows;                   /* physically in file, not yres */
    guint columns;                /* physically in file, not xres */
    gboolean is_derivative;
} PhoenixTableFileInfo;

typedef struct {
    guint cmd_secs;
    guint cmd_frac;
    guint read_secs;
    guint read_frac;
    guint data_length;
    guint xres;
    guint yres;
    PhoenixDirection direction;
    PhoenixChannel channel;
    guint zgain;
    gchar *oimage_before;
    gchar *oimage_after;
    guint ops_token;             /* hexadecimal, not specified */
    gdouble swts_temperature;    /* speficied as integer, nonsense */
    gdouble x_scan_range;        /* two values, only one specified */
    gdouble y_scan_range;
    guint smoothing_factor;
    guint oimage_ref_x;
    guint oimage_ref_y;
    gdouble x_slope;
    gdouble y_slope;
    gdouble scan_speed;          /* guessing; nonsensical specification */
    gboolean is_derivative;      /* copied from PhoenixTableFileInfo */
} PhoenixAFMHeader;

typedef struct {
    gchar *buffer;
    gsize size;
    GStringChunk *storage;
    GArray *records;
    guint data_offset;
    guint ndata;
} PhoenixFile;

static gboolean       module_register   (void);
static gint           phoenix_detect    (const GwyFileDetectInfo *fileinfo,
                                         gboolean only_name);
static GwyContainer*  phoenix_load      (const gchar *filename,
                                         GwyRunType mode,
                                         GError **error);
static void           set_channel_meta  (GwyContainer *data,
                                         gint id,
                                         const PhoenixFile *phfile,
                                         const PhoenixBinaryHeader *bheader,
                                         const PhoenixAFMHeader *theader);
static gboolean       unquote_in_place  (gchar *s,
                                         gchar opening,
                                         gchar closing);
static gboolean       parse_text_header (PhoenixFile *phfile,
                                         GError **error);
static PhoenixRecord* find_record       (GArray *records,
                                         const gchar *name,
                                         const gchar *value,
                                         const gchar *field_desc,
                                         GError **error);
static void           unquote_values    (GArray *records);
static void           phoenix_file_free (PhoenixFile *phfile);
static void           free_records      (GArray *records);
static gboolean       read_binary_header(const PhoenixFile *phfile,
                                         PhoenixBinaryHeader *header,
                                         gsize offset,
                                         GError **error);
static GwyDataField*  read_data_field   (const PhoenixFile *phfile,
                                         const PhoenixBinaryHeader *header,
                                         gsize offset,
                                         GError **error);
static gboolean       gather_table_info (const PhoenixFile *phfile,
                                         PhoenixRecord *rec,
                                         PhoenixTableFileInfo *table,
                                         GError **error);
static GwyDataField*  read_table_file   (const PhoenixTableFileInfo *table,
                                         PhoenixAFMHeader *header,
                                         const gchar *lblfilename,
                                         guint dataid,
                                         GError **error);
static gboolean       parse_afm_d_header(gchar *line,
                                         PhoenixAFMHeader *header,
                                         GError **error);
static GwyContainer*  create_meta       (const PhoenixFile *phfile,
                                         const PhoenixBinaryHeader *bheader,
                                         const PhoenixAFMHeader *theader);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports AFM data files from NASA Phoenix Mars mission."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, phoenix)

static gboolean
module_register(void)
{
    gwy_file_func_register("phoenix",
                           N_("AFM data from NASA Phoenix mission "
                              "(.dat, .lbl + .tab)"),
                           (GwyFileDetectFunc)&phoenix_detect,
                           (GwyFileLoadFunc)&phoenix_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
phoenix_detect(const GwyFileDetectInfo *fileinfo,
               gboolean only_name)
{
    const gchar *p;

    if (only_name)
        return 0;

    if (fileinfo->buffer_len <= MAGIC_SIZE)
        return 0;
    if (memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    p = fileinfo->head + MAGIC_SIZE;
    if (!(p = strstr(p, "INSTRUMENT_NAME ")))
        return 0;

    p += strlen("INSTRUMENT_NAME ");
    while (g_ascii_isspace(*p))
        p++;
    if (*p != '=')
        return 0;
    p++;
    while (g_ascii_isspace(*p))
        p++;
    if (!g_str_has_prefix(p, "\"MECA ATOMIC FORCE MICROSCOPE\""))
        return 0;

    return 80;
}

static GwyContainer*
phoenix_load(const gchar *filename,
             G_GNUC_UNUSED GwyRunType mode,
             GError **error)
{
    PhoenixFile phfile;
    GwyContainer *data = NULL;
    GArray *records;
    GwyDataField *dfield;
    GError *err = NULL;
    gboolean ok = FALSE;
    GQuark quark;
    gsize offset;
    guint i, j;

    gwy_clear(&phfile, 1);
    if (!g_file_get_contents(filename, &phfile.buffer, &phfile.size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    phfile.storage = g_string_chunk_new(48);
    if (!parse_text_header(&phfile, error))
        goto fail;

    data = gwy_container_new();
    if ((offset = phfile.data_offset)) {
        for (i = 0; i < phfile.ndata; i++) {
            PhoenixBinaryHeader header;

            if (!read_binary_header(&phfile, &header, offset, error))
                goto fail;
            offset += BINARY_HEADER_SIZE;
            if (!(dfield = read_data_field(&phfile, &header, offset, error)))
                goto fail;
            offset += header.data_length;

            quark = gwy_app_get_data_key_for_id(i);
            gwy_container_set_object(data, quark, dfield);
            g_object_unref(dfield);

            set_channel_meta(data, i, &phfile, &header, NULL);
        }
        if (gwy_container_get_n_items(data))
            ok = TRUE;
        else
            err_NO_DATA(error);
    }
    else {
        PhoenixTableFileInfo table;
        PhoenixAFMHeader header;

        records = phfile.records;
        i = 0;
        for (j = 0; j < records->len; j++) {
            PhoenixRecord *rec = &g_array_index(records, PhoenixRecord, j);
            gdouble min, max;

            if (rec->name[0] != '^')
                continue;

            if (!gather_table_info(&phfile, rec, &table, error))
                goto fail;

            /* There are lots of pointers, structure definitions and stuff.
             * But half of it is bogus and the other half has some implicit
             * assumptions anyway.  So we ignore the AFM_D_HEADER_TABLE
             * pointer and simply read n-th line of the file to get information
             * about n-th image. */
            if (g_str_has_suffix(table.name, "_HEADER_TABLE")) {
                GWY_FREE(table.filename);
                GWY_FREE(table.name);
                continue;
            }
            if (!(dfield = read_table_file(&table, &header, filename, i,
                                           error))) {
                GWY_FREE(table.filename);
                GWY_FREE(table.name);
                goto fail;
            }

            /* Skip constant-value images.  They are present for some reason
             * or another but carry no information. */
            gwy_data_field_get_min_max(dfield, &min, &max);
            if (max > min) {
                quark = gwy_app_get_data_key_for_id(i);
                gwy_container_set_object(data, quark, dfield);
                g_object_unref(dfield);

                set_channel_meta(data, i, &phfile, NULL, &header);
            }
            else {
                gwy_debug("skipping image #%u filled with constant value %g",
                          j, max);
            }
            i++;

            GWY_FREE(table.filename);
            GWY_FREE(table.name);
        }

        if (gwy_container_get_n_items(data))
            ok = TRUE;
        else
            err_NO_DATA(error);
    }

fail:
    phoenix_file_free(&phfile);
    if (!ok)
        GWY_OBJECT_UNREF(data);

    return data;
}

static void
set_channel_meta(GwyContainer *data, gint id,
                 const PhoenixFile *phfile,
                 const PhoenixBinaryHeader *bheader,
                 const PhoenixAFMHeader *theader)
{
    PhoenixChannel channel = 0;
    PhoenixDirection direction = 0;
    gboolean is_derivative = FALSE;
    const gchar *chnl, *dir;
    GwyContainer *meta;
    gchar *title;
    GQuark quark;

    if (bheader) {
        channel = bheader->channel;
        direction = bheader->direction;
    }
    else if (theader) {
        channel = theader->channel;
        direction = theader->direction;
        is_derivative = theader->is_derivative;
    }

    chnl = "Unknown channel";
    if (channel == PHOENIX_CHANNEL_HEIGHT)
        chnl = is_derivative ? "Height derivative" : "Height";
    else if (channel == PHOENIX_CHANNEL_ERROR)
        chnl = is_derivative ? "Error derivative" : "Error";

    dir = "Unknown direction";
    if (direction == PHOENIX_DIRECTION_FORWARD)
        dir = "Forward";
    else if (direction == PHOENIX_DIRECTION_BACKWARD)
        dir = "Backward";

    title = g_strconcat(chnl, ", ", dir, NULL);
    quark = gwy_app_get_data_title_key_for_id(id);
    /* Eats title. */
    gwy_container_set_string(data, quark, (const guchar*)title);

    meta = create_meta(phfile, bheader, theader);
    quark = gwy_app_get_data_meta_key_for_id(id);
    gwy_container_set_object(data, quark, meta);
    g_object_unref(meta);
}

static inline gboolean
unquote_in_place(gchar *s, gchar opening, gchar closing)
{
    guint len = strlen(s);

    if (len >= 2 && s[0] == opening && s[len-1] == closing) {
        memmove(s, s+1, len-2);
        s[len-2] = '\0';
        return TRUE;
    }

    return FALSE;
}

/* Read the text header and object description.  We do not actually use much of
 * the object structure information because we only read AFM_SCAN files with
 * known fixed structure.  So we are mostly interested in the metadata part. */
static gboolean
parse_text_header(PhoenixFile *phfile, GError **error)
{
    gchar *line, *p, *equalsign;
    GString *str;
    gboolean ok = FALSE;
    guint lineno;
    GStringChunk *storage;
    PhoenixRecord *rec, *parent = NULL;
    GArray *records;

    p = phfile->buffer;
    storage = phfile->storage;
    str = g_string_new(NULL);
    lineno = 1;
    phfile->records = g_array_new(FALSE, FALSE, sizeof(PhoenixRecord));
    records = phfile->records;
    for (line = gwy_str_next_line(&p);
         line;
         line = gwy_str_next_line(&p), lineno++) {

        g_strchomp(line);
        if (!*line)
            continue;

        if (g_str_has_prefix(line, "/*")) {
            /* Comment.  This logic allows comments inside continued lines.
             * Not sure if such construction is valid. */
            continue;
        }

        gwy_debug("<%s>", line);
        if (gwy_strequal(line, "END")) {
            if (records != phfile->records)
                err_TRUNCATED_HEADER(error);
            else
                ok = TRUE;
            break;
        }

        while (g_ascii_isspace(*line))
            line++;

        equalsign = strstr(line, " = ");
        if (!equalsign) {
            /* No equal sign, continuing the previous one. */
            if (!records->len) {
                g_set_error(error, GWY_MODULE_FILE_ERROR,
                            GWY_MODULE_FILE_ERROR_DATA,
                            _("No previous record to continue at line %u."),
                            lineno);
                break;
            }
            gwy_debug("continuing...");

            rec = &g_array_index(records, PhoenixRecord, records->len-1);
            g_string_assign(str, rec->value);
            g_string_append_c(str, ' ');
            g_string_append(str, line);
            rec->value = g_string_chunk_insert(storage, str->str);
        }
        else {
            /* New record. */
            PhoenixRecord newrec;

            *equalsign = '\0';
            g_strchomp(line);
            g_string_truncate(str, 0);
            g_string_append(str, line);
            gwy_debug("new record <%s>", str->str);
            newrec.name = g_string_chunk_insert(storage, str->str);

            line = equalsign + strlen(" = ");
            while (g_ascii_isspace(*line))
                line++;
            g_string_truncate(str, 0);
            newrec.value = g_string_chunk_insert(storage, line);
            newrec.parent = parent;
            if (gwy_strequal(newrec.name, "END_OBJECT")) {
                /* Move up; do not add any new record. */
                gwy_debug("move up one level");
                if (!parent
                    || !gwy_strequal(parent->value, newrec.value)) {
                    g_set_error(error, GWY_MODULE_FILE_ERROR,
                                GWY_MODULE_FILE_ERROR_DATA,
                                _("Invalid object nesting at line %u."),
                                lineno);
                    break;
                }
                parent = parent->parent;
                records = parent ? parent->records : phfile->records;
            }
            else if (gwy_strequal(newrec.name, "OBJECT")) {
                /* Move down. */
                gwy_debug("move inside object");
                newrec.records = g_array_new(FALSE, FALSE,
                                             sizeof(PhoenixRecord));
                g_array_append_val(records, newrec);
                parent = &g_array_index(records, PhoenixRecord, records->len-1);
                records = newrec.records;
            }
            else {
                newrec.records = NULL;
                g_array_append_val(records, newrec);
            }
        }
    }

    g_string_free(str, TRUE);
    if (!ok)
        return FALSE;

    unquote_values(phfile->records);

    if ((rec = find_record(phfile->records, "^AFM_TABLE", NULL, NULL, NULL))) {
        phfile->data_offset = atol(rec->value) - 1;
        gwy_debug("found data offset %u", phfile->data_offset);

        if (!(rec = find_record(phfile->records, "OBJECT", "AFM_TABLE",
                                "AFM_TABLE", error)))
            return FALSE;
        if (!(rec = find_record(rec->records, "ROWS", NULL,
                                "AFM_TABLE::ROWS", error)))
            return FALSE;
        if (!(phfile->ndata = atol(rec->value))) {
            err_NO_DATA(error);
            return FALSE;
        }
    }

    return TRUE;
}

static PhoenixRecord*
find_record(GArray *records, const gchar *name, const gchar *value,
            const gchar *field_desc, GError **error)
{
    PhoenixRecord *rec, *recs = &g_array_index(records, PhoenixRecord, 0);
    guint i, n = records->len;

    for (i = 0; i < n; i++) {
        rec = recs + i;
        if ((!name || gwy_strequal(rec->name, name))
            && (!value || gwy_strequal(rec->value, value)))
            return rec;
    }

    err_MISSING_FIELD(error, field_desc);
    return NULL;
}

static void
unquote_values(GArray *records)
{
    guint i;

    for (i = 0; i < records->len; i++) {
        PhoenixRecord *rec = &g_array_index(records, PhoenixRecord, i);

        unquote_in_place(rec->value, '"', '"');
        if (rec->records)
            unquote_values(rec->records);
    }
}

static void
phoenix_file_free(PhoenixFile *phfile)
{
    g_free(phfile->buffer);
    free_records(phfile->records);
    if (phfile->storage)
        g_string_chunk_free(phfile->storage);
}

static void
free_records(GArray *records)
{
    guint i;

    if (!records)
        return;

    for (i = 0; i < records->len; i++) {
        PhoenixRecord *rec = &g_array_index(records, PhoenixRecord, i);
        if (rec->records)
            free_records(rec->records);
    }
    g_array_free(records, TRUE);
}

static gboolean
read_binary_header(const PhoenixFile *phfile, PhoenixBinaryHeader *header,
                   gsize offset, GError **error)
{
    const guchar *p;
    guint dc;

    if (offset >= phfile->size || phfile->size - offset < BINARY_HEADER_SIZE) {
        err_TRUNCATED_HEADER(error);
        return FALSE;
    }
    p = phfile->buffer + offset;

    /* Generic. */
    header->cmd_secs = gwy_get_guint32_be(&p);
    header->cmd_frac = gwy_get_guint32_be(&p);
    header->read_secs = gwy_get_guint32_be(&p);
    header->read_frac = gwy_get_guint32_be(&p);
    header->data_length = gwy_get_guint32_be(&p);
    header->nrecords = gwy_get_guint16_be(&p);
    header->record_num = gwy_get_guint16_be(&p);
    gwy_debug("data length: %u, nrecs: %u, rec num %u",
              header->data_length, header->nrecords, header->record_num);
    header->data_type = gwy_get_guint16_be(&p);
    gwy_debug("data type: %u", header->data_type);
    if (header->data_type != PHOENIX_AFM_SCAN) {
        err_DATA_TYPE(error, header->data_type);
        return FALSE;
    }

    /* AFM_SCAN-specific */
    header->xres = gwy_get_guint16_be(&p);
    header->yres = gwy_get_guint16_be(&p);
    gwy_debug("xres: %u, yres: %u", header->xres, header->yres);
    dc = *(p++);
    header->direction = (dc >> 4);
    header->channel = (dc & 0xf);
    header->zoom_region = *(p++);
    gwy_debug("direction: %u, channel: %u, zoom region %u",
              header->direction, header->channel, header->zoom_region);

    /* Generic. */
    header->ops_token = gwy_get_guint32_be(&p);

    return TRUE;
}

static GwyDataField*
read_data_field(const PhoenixFile *phfile, const PhoenixBinaryHeader *header,
                gsize offset, GError **error)
{
    GwyDataField *dfield;
    const guchar *p;
    guint xres, yres, i;
    gdouble *d, *row;
    gdouble q, zoff;
    PhoenixAFMLine afmline;

    if (offset >= phfile->size || phfile->size - offset < header->data_length) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Data block is truncated."));
        return NULL;
    }

    xres = header->xres;
    yres = header->yres;
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        return NULL;
    if (err_SIZE_MISMATCH(error,
                          (xres + AFM_LINE_SIZE)*yres,
                          header->data_length,
                          TRUE))
        return FALSE;

    dfield = gwy_data_field_new(xres, yres, xres, yres, FALSE);
    zoff = 0.0;
    q = 1.0;
    if (header->channel == PHOENIX_CHANNEL_HEIGHT) {
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "m");
    }
    else if (header->channel == PHOENIX_CHANNEL_ERROR) {
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "V");
        /* The range is centered on the setpoint, which needs to be
         * recovered from the commands (not reported back). */
        q = 20.0/255.0;
        zoff = -0.05;
    }
    else {
        g_warning("Unknown channel type %u.", header->channel);
    }

    /* The physical dimensions corresponding to the data grid are not
     * specificied in the telemetry and must be recovered from the command
     * sequence.  So... */
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "px");

    p = phfile->buffer + offset;
    d = gwy_data_field_get_data(dfield);
    for (i = 0; i < yres; i++) {
        afmline.direction = *(p++);
        afmline.channel_mask = *(p++);
        afmline.lineno = gwy_get_gint16_be(&p);
        afmline.zoff = gwy_get_gint16_be(&p);
        afmline.zgain = *(p++);
        afmline.Vap_unused = *(p++);
        row = d + i*xres;

        if (header->channel == PHOENIX_CHANNEL_HEIGHT) {
            /* XXX: The documentation says the full range corresponds to 0-255,
             * but that is clearly incorrect.   And why would it be two bytes
             * then? */
            zoff = 13.6e-6/65535.0 * afmline.zoff;
            q = 13.6e-6/255.0/gwy_powi(2.0, afmline.zgain);
        }
        /* XXX: The documentation and file header disagree about signedness.
         * File header says unsigned, but that is clearly incorrect.
         * Flip forward lines (because we have left-handed coordinates). */
        if (afmline.direction == PHOENIX_LINE_DIRECTION_FORWARD) {
            gwy_convert_raw_data(p + xres-1, xres, -1, GWY_RAW_DATA_SINT8,
                                 GWY_BYTE_ORDER_BIG_ENDIAN, row, q, zoff);
        }
        else {
            gwy_convert_raw_data(p, xres, 1, GWY_RAW_DATA_SINT8,
                                 GWY_BYTE_ORDER_BIG_ENDIAN, row, q, zoff);
        }
        p += xres;
    }

    return dfield;
}

static gboolean
gather_table_info(const PhoenixFile *phfile, PhoenixRecord *rec,
                  PhoenixTableFileInfo *table, GError **error)
{
    PhoenixRecord *trec;
    gchar *s, **ss;

    gwy_clear(table, 1);

    /* Parse the ^AFM_X_FOOBAR_TABLE = ("BLAH.TAB",12345) record */
    s = g_strdup(rec->value);
    if (!unquote_in_place(s, '(', ')')) {
        err_INVALID(error, rec->name);
        g_free(s);
        return FALSE;
    }
    ss = g_strsplit(s, ",", 0);
    g_free(s);
    if (g_strv_length(ss) != 2) {
        err_INVALID(error, rec->name);
        g_strfreev(ss);
        return FALSE;
    }

    table->filename = ss[0];
    if (!unquote_in_place(table->filename, '"', '"')) {
        err_INVALID(error, rec->name);
        table->filename = NULL;
        g_strfreev(ss);
        return FALSE;
    }
    table->lineno = atol(ss[1]);
    table->name = g_strdup(rec->name+1);
    gwy_debug("table %s file ref <%s> line %lu",
              table->name, table->filename, (gulong)table->lineno);
    g_free(ss[1]);
    g_free(ss);

    /* Find other information by locating the corresponding object. */
    if (!(rec = find_record(phfile->records, "OBJECT", table->name,
                            table->name, error)))
        goto fail;
    gwy_debug("found object for table %s", table->name);

    if (!(trec = find_record(rec->records, "COLUMNS", NULL, "COLUMNS", error)))
        goto fail;
    table->columns = atol(trec->value);

    if (!(trec = find_record(rec->records, "ROWS", NULL, "ROWS", error)))
        goto fail;
    table->rows = atol(trec->value);

    if (!g_str_has_suffix(table->name, "_HEADER_TABLE")) {
        if (!(trec = find_record(rec->records, "START_BYTE", NULL, "START_BYTE",
                                 error)))
            goto fail;
        table->offset = atol(trec->value) - 1;

        if ((trec = find_record(rec->records, "OBJECT", "CONTAINER",
                                NULL, NULL))
            && (trec = find_record(trec->records, "NAME", NULL, NULL, NULL))) {
            gwy_debug("data name <%s>\n", trec->value);
            table->is_derivative = g_str_has_suffix(trec->value, " DERIVATIVE");
        }
    }

    gwy_debug("columns %lu, rows %lu, byte offset %lu",
              (gulong)table->columns, (gulong)table->rows,
              (gulong)table->offset);

    return TRUE;

fail:
    GWY_FREE(table->name);
    GWY_FREE(table->filename);
    return FALSE;
}

/* Separate in place a string piece delimited by comma or white space. */
static gchar*
str_next_value(gchar **p)
{
    gchar *q, *v;

    if (!p || !*p)
        return NULL;

    q = v = *p;
    if (!*q) {
        *p = NULL;
        return NULL;
    }

    while (*q && *q != ',' && !g_ascii_isspace(*q))
        q++;

    if (*q) {
        *q = '\0';
        q++;
    }

    *p = q;
    return v;
}

static GwyDataField*
read_table_file(const PhoenixTableFileInfo *table,
                PhoenixAFMHeader *header,
                const gchar *lblfilename,
                guint dataid,
                GError **error)
{
    gchar *buffer, *filename, *dirname, *fullfnm, *p, *line, *q, *v;
    GwyDataField *dfield = NULL;
    GError *err = NULL;
    gsize size;
    guint i, lineno;
    gdouble *d = NULL, *row;
    gboolean ok = FALSE;

    if (table->lineno <= dataid+1) {
        err_INVALID(error, "START_LINE");
        return NULL;
    }

    dirname = g_path_get_dirname(lblfilename);
    filename = g_strdup(table->filename);
    fullfnm = g_build_filename(dirname, filename, NULL);
    gwy_debug("looking for <%s>", fullfnm);
    if (!g_file_test(fullfnm, G_FILE_TEST_IS_REGULAR)) {
        g_free(fullfnm);
        for (i = 0; filename[i]; i++)
            filename[i] = g_ascii_tolower(filename[i]);
        fullfnm = g_build_filename(dirname, filename, NULL);
        gwy_debug("looking for <%s>", fullfnm);
        if (!g_file_test(fullfnm, G_FILE_TEST_IS_REGULAR)) {
            g_free(fullfnm);
            for (i = 0; filename[i]; i++)
                filename[i] = g_ascii_toupper(filename[i]);
            fullfnm = g_build_filename(dirname, filename, NULL);
            gwy_debug("looking for <%s>", fullfnm);
            if (!g_file_test(fullfnm, G_FILE_TEST_IS_REGULAR)) {
                g_free(fullfnm);
                g_free(filename);
                g_free(dirname);
                err_DATA_PART(error, table->filename);
                return NULL;
            }
        }
        /* Overwrite the file name with the correct one. */
        memcpy(table->filename, filename, strlen(filename));
    }
    g_free(filename);
    g_free(dirname);

    if (!g_file_get_contents(fullfnm, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        g_free(fullfnm);
        return NULL;
    }

    p = buffer;
    lineno = 1;
    gwy_debug("skipping to line %u", table->lineno);
    for (line = gwy_str_next_line(&p);
         line;
         line = gwy_str_next_line(&p), lineno++) {
        /* When we encounter the line corresponding to dataid, read the
         * information. */
        if (lineno == dataid+1) {
            if (!parse_afm_d_header(line, header, error))
                goto fail;
            dfield = gwy_data_field_new(header->xres, header->yres,
                                        1e-6*header->x_scan_range,
                                        1e-6*header->y_scan_range,
                                        FALSE);
            d = gwy_data_field_get_data(dfield);
        }
        else if (lineno >= table->lineno
                 && lineno < table->lineno + header->yres) {
            q = line;
            row = d + (lineno - table->lineno)*header->xres;
            for (i = 0; i < header->xres; i++) {
                /* Values come in triples.  Skip X and Y, read Z.  We do not
                 * bother reading it as XYZ data. */
                if (!str_next_value(&q)
                    || !str_next_value(&q)
                    || !(v = str_next_value(&q))) {
                    g_set_error(error, GWY_MODULE_FILE_ERROR,
                                GWY_MODULE_FILE_ERROR_DATA,
                                _("File is truncated."));
                    goto fail;
                }
                /* Flip horizontally here. */
                row[header->xres-1 - i] = g_ascii_strtod(v, NULL);
            }
        }
        else if (lineno == table->lineno + header->yres)
            break;
    }

    if (lineno < table->lineno + header->yres) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("File is truncated."));
        goto fail;
    }

    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
    if (header->channel == PHOENIX_CHANNEL_HEIGHT) {
        /* Derivative is unitless and the microns mutually cancel. */
        if (!table->is_derivative) {
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield),
                                        "m");
            gwy_data_field_multiply(dfield, 1e-6);
        }
    }
    else if (header->channel == PHOENIX_CHANNEL_ERROR)
        if (table->is_derivative) {
            /* These are presumably the derivative units and scale.  Who knows,
             * really.*/
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield),
                                        "V/m");
            gwy_data_field_multiply(dfield, 1e6);
        }
        else {
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield),
                                        "V");
        }
    else
        g_warning("Unknown channel type %u.", header->channel);

    header->is_derivative = table->is_derivative;
    ok = TRUE;

fail:
    if (!ok)
        GWY_OBJECT_UNREF(dfield);
    g_free(fullfnm);
    g_free(buffer);

    return dfield;
}

#define afm_d_header_read_integer(field) \
    if (!(v = str_next_value(&p))) \
        goto fail; \
    header->field = atol(v)

#define afm_d_header_read_double(field) \
    if (!(v = str_next_value(&p))) \
        goto fail; \
    header->field = g_ascii_strtod(v, NULL)

#define afm_d_header_read_string(field) \
    if (!(v = str_next_value(&p))) \
        goto fail; \
    header->field = g_strdup(v); \
    unquote_in_place(header->field, '"', '"')

static gboolean
parse_afm_d_header(gchar *line,
                   PhoenixAFMHeader *header,
                   GError **error)
{
    gchar *v, *p;

    gwy_clear(header, 1);
    p = line;

    afm_d_header_read_integer(cmd_secs);
    afm_d_header_read_integer(cmd_frac);
    afm_d_header_read_integer(read_secs);
    afm_d_header_read_integer(read_frac);
    afm_d_header_read_integer(data_length);
    afm_d_header_read_integer(xres);
    afm_d_header_read_integer(yres);
    gwy_debug("xres: %u, yres: %u", header->xres, header->yres);
    afm_d_header_read_integer(direction);
    afm_d_header_read_integer(channel);
    afm_d_header_read_integer(zgain);
    gwy_debug("direction: %u, channel: %u, zgain %u",
              header->direction, header->channel, header->zgain);
    afm_d_header_read_string(oimage_before);
    afm_d_header_read_string(oimage_after);

    if (!(v = str_next_value(&p)))
        goto fail;
    header->ops_token = strtol(v, NULL, 16);

    afm_d_header_read_double(swts_temperature);
    afm_d_header_read_double(x_scan_range);
    afm_d_header_read_double(y_scan_range);
    gwy_debug("xreal: %g, yreal: %g",
              header->x_scan_range, header->y_scan_range);
    afm_d_header_read_integer(smoothing_factor);
    afm_d_header_read_integer(oimage_ref_x);
    afm_d_header_read_integer(oimage_ref_y);
    afm_d_header_read_double(x_slope);
    afm_d_header_read_double(y_slope);
    afm_d_header_read_double(scan_speed);

    return TRUE;

fail:
    GWY_FREE(header->oimage_before);
    GWY_FREE(header->oimage_after);
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Cannot parse AFM HEADER_TABLE values."));
    return FALSE;
}

static void
add_time_meta(GwyContainer *meta, const guchar *name,
              guint secs, guint frac)
{
    gdouble t;
    gchar buf[30];

    t = secs + frac/4294967296.0;
    g_snprintf(buf, sizeof(buf), "%.8f s", t);
    gwy_container_set_const_string_by_name(meta, name, buf);
}

static GwyContainer*
create_meta(const PhoenixFile *phfile,
            const PhoenixBinaryHeader *bheader,
            const PhoenixAFMHeader *theader)
{
    GwyContainer *meta;
    GArray *records;
    PhoenixRecord *rec;
    guint i;

    meta = gwy_container_new();
    records = phfile->records;
    for (i = 0; i < records->len; i++) {
        rec = &g_array_index(records, PhoenixRecord, i);
        /* Skip objects and pointers. */
        if (rec->records || !rec->name || rec->name[0] == '^')
            continue;

        gwy_container_set_const_string_by_name(meta, rec->name, rec->value);
    }

    if (bheader) {
        add_time_meta(meta, "TIME_CMD",
                      bheader->cmd_secs, bheader->cmd_frac);
        add_time_meta(meta, "TIME_READ",
                      bheader->read_secs, bheader->read_frac);
    }
    else if (theader) {
        add_time_meta(meta, "TIME_CMD",
                      theader->cmd_secs, theader->cmd_frac);
        add_time_meta(meta, "TIME_READ",
                      theader->read_secs, theader->read_frac);
    }

    return meta;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
