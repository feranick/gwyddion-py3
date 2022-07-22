/*
 *  $Id: ambprofile.c 22651 2019-11-04 15:22:37Z yeti-dn $
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
 * Ambios 1D profilometry data
 * .dat, .xml
 * Read
 **/

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-ambios-profile-xml">
 *   <comment>Ambios XML profile data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="&lt;?xml">
 *       <match type="string" offset="40:120" value="&lt;ProfilometerData&gt;">
 *          <match type="string" offset="60:140" value="&lt;Header&gt;"/>
 *       </match>
 *     </match>
 *   </magic>
 * </mime-type>
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

#ifdef HAVE_MEMRCHR
#define strlenrchr(s,c,len) (gchar*)memrchr((s),(c),(len))
#else
#define strlenrchr(s,c,len) strrchr((s),(c))
#endif

#define BLOODY_UTF8_BOM "\xef\xbb\xbf"

#define MAGIC_XML "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>"
#define MAGIC_XML_SIZE (sizeof(MAGIC_XML)-1)

typedef struct {
    GHashTable *hash;
    GString *path;
    GArray *xdata;
    GArray *zdata;
} AMBProfFile;

static gboolean      module_register   (void);
static gint          ambprofxml_detect (const GwyFileDetectInfo *fileinfo,
                                        gboolean only_name);
static gint          ambprofdat_detect (const GwyFileDetectInfo *fileinfo,
                                        gboolean only_name);
static GwyContainer* ambprofxml_load   (const gchar *filename,
                                        GwyRunType mode,
                                        GError **error);
static GwyContainer* ambprofdat_load   (const gchar *filename,
                                        GwyRunType mode,
                                        GError **error);
static GwyContainer* create_graph_model(AMBProfFile *ambpfile,
                                        GError **error);
static void          ambprof_init      (AMBProfFile *ambpfile);
static void          ambprof_free      (AMBProfFile *ambpfile);
static void          start_element     (GMarkupParseContext *context,
                                        const gchar *element_name,
                                        const gchar **attribute_names,
                                        const gchar **attribute_values,
                                        gpointer user_data,
                                        GError **error);
static void          end_element       (GMarkupParseContext *context,
                                        const gchar *element_name,
                                        gpointer user_data,
                                        GError **error);
static void          text              (GMarkupParseContext *context,
                                        const gchar *value,
                                        gsize value_len,
                                        gpointer user_data,
                                        GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Ambios 1D profilometry data files."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, ambprofile)

static gboolean
module_register(void)
{
    gwy_file_func_register("ambprofxml",
                           N_("Ambios 1D profilometry data files (.xml)"),
                           (GwyFileDetectFunc)&ambprofxml_detect,
                           (GwyFileLoadFunc)&ambprofxml_load,
                           NULL,
                           NULL);
    gwy_file_func_register("ambprofdat",
                           N_("Ambios 1D profilometry data files (.dat)"),
                           (GwyFileDetectFunc)&ambprofdat_detect,
                           (GwyFileLoadFunc)&ambprofdat_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
ambprofxml_detect(const GwyFileDetectInfo *fileinfo,
                  gboolean only_name)
{
    const gchar *head = fileinfo->head;
    gsize head_len = fileinfo->buffer_len;

    if (only_name)
        return 0;

    if (g_str_has_prefix(head, BLOODY_UTF8_BOM)) {
        head += sizeof(BLOODY_UTF8_BOM)-1;
        head_len -= sizeof(BLOODY_UTF8_BOM)-1;
    }

    if (head_len <= MAGIC_XML_SIZE
        || (memcmp(head, MAGIC_XML, MAGIC_XML_SIZE) != 0))
        return 0;

    /* Look for some things that should be present after the general XML
     * header. */
    gwy_debug("magic OK");
    head += MAGIC_XML_SIZE;
    while (g_ascii_isspace(*head))
        head++;

    if (!g_str_has_prefix(head, "<ProfilometerData>"))
        return 0;

    gwy_debug("ProfilometerData tag found");
    head += strlen("<ProfilometerData>");
    if (!strstr(head, "<Header>"))
        return 0;

    return 90;
}

static gint
ambprofdat_detect(const GwyFileDetectInfo *fileinfo,
                  gboolean only_name)
{
    const gchar *head = fileinfo->head;
    gsize head_len = fileinfo->buffer_len;

    if (only_name)
        return 0;

    if (head_len < 24)
        return 0;

    /* The first line must be "DD-MM-YYYY","HH:MM:SS".  This quickly
     * filters out non-matching files.  */
    if (head[0] != '"'
        || head[3] != '-' || head[6] != '-'
        || head[11] != '"' || head[12] != ',' || head[13] != '"'
        || head[16] != ':' || head[19] != ':' || head[22] != '"')
        return 0;
    if (!g_ascii_isdigit(head[1]) || !g_ascii_isdigit(head[2])
        || !g_ascii_isdigit(head[4]) || !g_ascii_isdigit(head[5])
        || !g_ascii_isdigit(head[7]) || !g_ascii_isdigit(head[8])
        || !g_ascii_isdigit(head[9]) || !g_ascii_isdigit(head[10])
        || !g_ascii_isdigit(head[14]) || !g_ascii_isdigit(head[15])
        || !g_ascii_isdigit(head[17]) || !g_ascii_isdigit(head[18])
        || !g_ascii_isdigit(head[20]) || !g_ascii_isdigit(head[21]))
        return 0;

    head += 23;
    while (g_ascii_isspace(*head))
        head++;

    /* Then look for "X Units:", "Z Units:" and "Num Data:". */
    if (!(head = strstr(head, "\"X Units:\","))
        || !(head = strstr(head, "\"Z Units:\","))
        || !(head = strstr(head, "\"Num Data:\",")))
        return 0;

    /* FIXME: When it is fairly likely the file format matches, we might want
     * to do a few more expensive checks to be sure. */

    return 75;
}

static GwyContainer*
ambprofxml_load(const gchar *filename,
                G_GNUC_UNUSED GwyRunType mode,
                GError **error)
{
    GwyContainer *container = NULL;
    gchar *buffer = NULL, *xml;
    gsize size = 0;
    GError *err = NULL;
    GMarkupParser parser = { start_element, end_element, text, NULL, NULL };
    GMarkupParseContext *context = NULL;
    AMBProfFile ambpfile;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    xml = buffer;
    if (g_str_has_prefix(xml, BLOODY_UTF8_BOM)) {
        xml += sizeof(BLOODY_UTF8_BOM)-1;
        size -= sizeof(BLOODY_UTF8_BOM)-1;
    }

    if (memcmp(xml, MAGIC_XML, MAGIC_XML_SIZE) != 0) {
        err_FILE_TYPE(error, "Ambios profilometry XML");
        goto fail;
    }

    ambprof_init(&ambpfile);
    context = g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT,
                                         &ambpfile, NULL);
    if (!g_markup_parse_context_parse(context, xml, size, &err)
        || !g_markup_parse_context_end_parse(context, &err)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("XML parsing failed: %s"), err->message);
        g_clear_error(&err);
        goto fail;
    }

    container = create_graph_model(&ambpfile, error);

fail:
    ambprof_free(&ambpfile);
    g_free(buffer);

    return container;
}

static GwyContainer*
ambprofdat_load(const gchar *filename,
                G_GNUC_UNUSED GwyRunType mode,
                GError **error)
{
    GwyContainer *container = NULL;
    gchar *buffer = NULL, *p, *line, *end, *sep, *key, *value;
    gsize size = 0;
    GError *err = NULL;
    AMBProfFile ambpfile;
    gboolean in_data, first_line;
    gdouble x, z;
    guint len;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    ambprof_init(&ambpfile);

    p = buffer;
    in_data = FALSE;
    first_line = TRUE;
    for (line = gwy_str_next_line(&p); line; line = gwy_str_next_line(&p)) {
        /* Just skip the first line with date and time. */
        if (first_line) {
            first_line = FALSE;
            continue;
        }

        if (!*line)
            continue;

        if (!in_data && line[0] != '"')
            in_data = TRUE;

        if (in_data) {
            /* Just ignore bogus data lines. */
            x = g_ascii_strtod(line, &end);
            if (end == line)
                continue;
            if (*end != ',')
                continue;

            line = end+1;
            z = g_ascii_strtod(line, &end);
            if (end == line)
                continue;

            g_array_append_val(ambpfile.xdata, x);
            g_array_append_val(ambpfile.zdata, z);
            continue;
        }

        if (!(sep = strstr(line+1, ":\","))) {
            g_warning("Cannot parse header line %s.", line);
            continue;
        }
        if (sep == line+1)
            continue;

        key = g_strndup(line, sep-line - 1);
        line = sep + sizeof(":\",")-1;
        len = strlen(line);
        if (len > 1 && line[0] == '"' && line[len-1] == '"')
            value = g_strndup(line+1, len-2);
        else
            value = g_strdup(line);
        g_hash_table_replace(ambpfile.hash, key, value);
    }

    container = create_graph_model(&ambpfile, error);

    ambprof_free(&ambpfile);
    g_free(buffer);

    return container;
}

static GwySIUnit*
handle_units(GHashTable *hash, GArray *data,
             const gchar *id, GString *str)
{
    GwySIUnit *unit;
    const gchar *s;
    gint power10;
    guint i, res;
    gdouble q;

    g_string_assign(str, "/ProfilometerData/Header/");
    g_string_append(str, id);
    g_string_append(str, "Units");
    s = g_hash_table_lookup(hash, str->str);
    if (!s || gwy_strequal(s, "MICRON"))
        s = "µm";

    unit = gwy_si_unit_new_parse(s, &power10);
    q = pow10(power10);

    res = data->len;
    for (i = 0; i < res; i++)
        g_array_index(data, double, i) *= q;

    return unit;
}

static GwyContainer*
create_graph_model(AMBProfFile *ambpfile, GError **error)
{
    GwyContainer *container = NULL;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwySIUnit *xunit, *yunit;
    guint res;

    if (!(res = ambpfile->xdata->len)) {
        err_NO_DATA(error);
        return NULL;
    }
    if (ambpfile->zdata->len != res) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Different number of X and Z values"));
        return NULL;
    }

    /* Recycle ambpfile->path. */
    xunit = handle_units(ambpfile->hash, ambpfile->xdata, "X", ambpfile->path);
    yunit = handle_units(ambpfile->hash, ambpfile->zdata, "Z", ambpfile->path);

    container = gwy_container_new();

    gmodel = gwy_graph_model_new();
    g_object_set(gmodel,
                 "si-unit-x", xunit,
                 "si-unit-y", yunit,
                 "title", "ProfilometerData",
                 NULL);
    g_object_unref(xunit);
    g_object_unref(yunit);
    gwy_container_set_object(container,
                             gwy_app_get_graph_key_for_id(0), gmodel);
    g_object_unref(gmodel);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(0),
                 "description", "ProfilometerData",
                 NULL);
    gwy_graph_curve_model_set_data(gcmodel,
                                   &g_array_index(ambpfile->xdata, double, 0),
                                   &g_array_index(ambpfile->zdata, double, 0),
                                   res);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);

    return container;
}

static void
ambprof_init(AMBProfFile *ambpfile)
{
    gwy_clear(ambpfile, 1);

    ambpfile->hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                           g_free, g_free);
    ambpfile->path = g_string_new(NULL);
    ambpfile->xdata = g_array_new(FALSE, FALSE, sizeof(gdouble));
    ambpfile->zdata = g_array_new(FALSE, FALSE, sizeof(gdouble));
}

static void
ambprof_free(AMBProfFile *ambpfile)
{
    if (ambpfile->hash)
        g_hash_table_destroy(ambpfile->hash);
    if (ambpfile->path)
        g_string_free(ambpfile->path, TRUE);

    if (ambpfile->xdata)
        g_array_free(ambpfile->xdata, TRUE);
    if (ambpfile->zdata)
        g_array_free(ambpfile->zdata, TRUE);
}

static void
start_element(G_GNUC_UNUSED GMarkupParseContext *context,
              const gchar *element_name,
              G_GNUC_UNUSED const gchar **attribute_names,
              G_GNUC_UNUSED const gchar **attribute_values,
              gpointer user_data,
              GError **error)
{
    AMBProfFile *ambpfile = (AMBProfFile*)user_data;

    gwy_debug("<%s>", element_name);
    if (!ambpfile->path->len
        && !gwy_strequal(element_name, "ProfilometerData")) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                    _("Top-level element is not ‘%s’."), "ProfilometerData");
        return;
    }

    g_string_append_c(ambpfile->path, '/');
    g_string_append(ambpfile->path, element_name);
}

static void
end_element(G_GNUC_UNUSED GMarkupParseContext *context,
            G_GNUC_UNUSED const gchar *element_name,
            gpointer user_data,
            G_GNUC_UNUSED GError **error)
{
    AMBProfFile *ambpfile = (AMBProfFile*)user_data;
    gchar *pos;

    gwy_debug("</%s>", element_name);
    pos = strlenrchr(ambpfile->path->str, '/', ambpfile->path->len);
    g_string_truncate(ambpfile->path, pos - ambpfile->path->str);
}

static void
text(G_GNUC_UNUSED GMarkupParseContext *context,
     const gchar *value,
     gsize value_len,
     gpointer user_data,
     G_GNUC_UNUSED GError **error)
{
    AMBProfFile *ambpfile = (AMBProfFile*)user_data;
    const gchar *path = ambpfile->path->str;
    gdouble v;

    gwy_debug("%s (%lu)", path, (gulong)value_len);
    if (!value_len)
        return;

    /* Speed up the comparison by filtering out non-matching paths quickly. */
    if (path[ambpfile->path->len-1] == 'X'
        && gwy_strequal(path, "/ProfilometerData/DataBlock/Data/X")) {
        v = g_ascii_strtod(value, NULL);
        g_array_append_val(ambpfile->xdata, v);
    }
    else if (path[ambpfile->path->len-1] == 'Z'
             && gwy_strequal(path, "/ProfilometerData/DataBlock/Data/Z")) {
        v = g_ascii_strtod(value, NULL);
        g_array_append_val(ambpfile->zdata, v);
    }
    else {
        g_hash_table_replace(ambpfile->hash, g_strdup(path), g_strdup(value));
    }
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
