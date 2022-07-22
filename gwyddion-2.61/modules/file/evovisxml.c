/*
 *  $Id: evovisxml.c 23016 2020-12-23 08:48:38Z yeti-dn $
 *  Copyright (C) 2020 David Necas (Yeti).
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
 * <mime-type type="application/x-evovis-xml">
 *   <comment>Evovis XML profilometry data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="&lt;?xml">
 *       <match type="string" offset="20:60" value="&lt;root Class=&quot;MeasurementSet&quot;"/>
 *     </match>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Evovis profile (XML serialisation)
 * 0 string \x3c?xml
 * >&0 search/80 \x3croot\ Class="MeasurementSet" Evovis XML profilometry data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Evovis XML profilometry data
 * .xml
 * Read
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
#include "get.h"

#ifdef HAVE_MEMRCHR
#define strlenrchr(s,c,len) (gchar*)memrchr((s),(c),(len))
#else
#define strlenrchr(s,c,len) strrchr((s),(c))
#endif

#define MAGIC "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

typedef struct {
    guint npoints;
    guchar *rawdata;
} EvovisXMLRawData;

typedef struct {
    GHashTable *hash;
    GString *path;
    EvovisXMLRawData rawdata;
} EvovisXMLFile;

static gboolean      module_register      (void);
static gint          evovisxml_detect     (const GwyFileDetectInfo *fileinfo,
                                           gboolean only_name);
static GwyContainer* evovisxml_load       (const gchar *filename,
                                           GwyRunType mode,
                                           GError **error);
static gdouble*      evovisxml_make_xydata(const EvovisXMLRawData *rawdata,
                                           guint *ndata);
static void          evovisxml_init       (EvovisXMLFile *evxfile);
static void          evovisxml_free       (EvovisXMLFile *evxfile);
static void          start_element        (GMarkupParseContext *context,
                                           const gchar *element_name,
                                           const gchar **attribute_names,
                                           const gchar **attribute_values,
                                           gpointer user_data,
                                           GError **error);
static void          end_element          (GMarkupParseContext *context,
                                           const gchar *element_name,
                                           gpointer user_data,
                                           GError **error);
static void          text                 (GMarkupParseContext *context,
                                           const gchar *value,
                                           gsize value_len,
                                           gpointer user_data,
                                           GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Evovis XML data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.1",
    "David Nečas (Yeti)",
    "2020",
};

GWY_MODULE_QUERY2(module_info, evovisxml)

static gboolean
module_register(void)
{
    gwy_file_func_register("evovisxml",
                           N_("Evovis XML data files (.xml)"),
                           (GwyFileDetectFunc)&evovisxml_detect,
                           (GwyFileLoadFunc)&evovisxml_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
evovisxml_detect(const GwyFileDetectInfo *fileinfo,
                 gboolean only_name)
{
    const gchar *head = fileinfo->head;

    if (only_name)
        return 0;

    if (fileinfo->buffer_len <= MAGIC_SIZE
        || (memcmp(head, MAGIC, MAGIC_SIZE) != 0))
        return 0;

    /* Look for some things that should be present after the general XML
     * header. */
    gwy_debug("magic OK");
    head += MAGIC_SIZE;
    while (g_ascii_isspace(*head))
        head++;

    if (!g_str_has_prefix(head, "<root Class=\"MeasurementSet\""))
        return 0;

    gwy_debug("MeasurementSet root class found");
    head += strlen("<root Class=\"MeasurementSet\"");
    if (!strstr(head, "<ListEntry Class=\"Measurement\">"))
        return 0;

    return 85;
}

static GwyContainer*
evovisxml_load(const gchar *filename,
               G_GNUC_UNUSED GwyRunType mode,
               GError **error)
{
    GwyContainer *container = NULL;
    gchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;
    GMarkupParser parser = { start_element, end_element, text, NULL, NULL };
    GMarkupParseContext *context = NULL;
    EvovisXMLFile evxfile;
    const gchar *title;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwySIUnit *xunit, *yunit;
    guint i;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    if (memcmp(buffer, MAGIC, MAGIC_SIZE) != 0) {
        err_FILE_TYPE(error, "Evovis XML");
        goto fail;
    }

    evovisxml_init(&evxfile);
    context = g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT,
                                         &evxfile, NULL);
    if (!g_markup_parse_context_parse(context, buffer, size, &err)
        || !g_markup_parse_context_end_parse(context, &err)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("XML parsing failed: %s"), err->message);
        g_clear_error(&err);
        goto fail;
    }

    if (!evxfile.rawdata.rawdata) {
        err_NO_DATA(error);
        goto fail;
    }

    gmodel = gwy_graph_model_new();
    xunit = gwy_si_unit_new("m");
    yunit = gwy_si_unit_new("m");

    if (!(title = g_hash_table_lookup(evxfile.hash,
                                      "/root/DataElements/ListEntry/Name")))
        title = "Profile";

    for (i = 0; i < 1; i++) {
        const EvovisXMLRawData *rawdata = &evxfile.rawdata;
        guint npoints;
        gdouble *xy;

        if (!(xy = evovisxml_make_xydata(rawdata, &npoints)))
            continue;

        gcmodel = gwy_graph_curve_model_new();
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(i),
                     "description", title,
                     NULL);
        gwy_graph_curve_model_set_data_interleaved(gcmodel, xy, npoints);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
        g_free(xy);
    }

    if (gwy_graph_model_get_n_curves(gmodel)) {
        g_object_set(gmodel,
                     "si-unit-x", xunit,
                     "si-unit-y", yunit,
                     "title", title,
                     NULL);
        container = gwy_container_new();
        gwy_container_set_object(container,
                                 gwy_app_get_graph_key_for_id(0), gmodel);
    }
    else
        err_NO_DATA(error);

    g_object_unref(gmodel);
    g_object_unref(xunit);
    g_object_unref(yunit);

fail:
    evovisxml_free(&evxfile);
    g_free(buffer);

    return container;
}

static gdouble*
evovisxml_make_xydata(const EvovisXMLRawData *rawdata, guint *ndata)
{
    guint i, n, npoints = rawdata->npoints;
    gdouble *xy = g_new(gdouble, 2*npoints);
    const guchar *p = rawdata->rawdata;
    gboolean have_warned = FALSE;

    for (i = n = 0; i < npoints; i++) {
        gdouble x = gwy_get_gdouble_le(&p);
        gdouble y = gwy_get_gdouble_le(&p);
        gdouble z = gwy_get_gdouble_le(&p);
        gboolean valid = *(p++);

        if (!valid)
            continue;

        if (y != 0.0 && !have_warned) {
            g_warning("Data contain non-zero Y values which we currently "
                      "ignore.");
            have_warned = TRUE;
        }

        xy[n++] = 1e-3*x;
        xy[n++] = 1e-3*z;
    }

    if (!n) {
        g_free(xy);
        *ndata = 0;
        return NULL;
    }

    *ndata = n/2;
    return xy;
}

static void
evovisxml_init(EvovisXMLFile *evxfile)
{
    gwy_clear(evxfile, 1);

    evxfile->hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, g_free);
    evxfile->path = g_string_new(NULL);
}

static void
evovisxml_free(EvovisXMLFile *evxfile)
{
    if (evxfile->hash)
        g_hash_table_destroy(evxfile->hash);
    if (evxfile->path)
        g_string_free(evxfile->path, TRUE);

    g_free(evxfile->rawdata.rawdata);
}

static void
start_element(G_GNUC_UNUSED GMarkupParseContext *context,
              const gchar *element_name,
              const gchar **attribute_names,
              const gchar **attribute_values,
              gpointer user_data,
              GError **error)
{
    EvovisXMLFile *evxfile = (EvovisXMLFile*)user_data;
    guint i;

    gwy_debug("<%s>", element_name);
    if (!evxfile->path->len && !gwy_strequal(element_name, "root")) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                    _("Top-level element is not ‘%s’."), "root");
        return;
    }

    g_string_append_c(evxfile->path, '/');
    for (i = 0; attribute_names[i]; i++) {
        if (gwy_strequal(attribute_names[i], "key")) {
            g_string_append(evxfile->path, attribute_values[i]);
            return;
        }
    }
    g_string_append(evxfile->path, element_name);
}

static void
end_element(G_GNUC_UNUSED GMarkupParseContext *context,
            G_GNUC_UNUSED const gchar *element_name,
            gpointer user_data,
            G_GNUC_UNUSED GError **error)
{
    EvovisXMLFile *evxfile = (EvovisXMLFile*)user_data;
    gchar *pos;

    gwy_debug("</%s>", element_name);
    pos = strlenrchr(evxfile->path->str, '/', evxfile->path->len);
    g_string_truncate(evxfile->path, pos - evxfile->path->str);
}

static void
text(G_GNUC_UNUSED GMarkupParseContext *context,
     const gchar *value,
     gsize value_len,
     gpointer user_data,
     G_GNUC_UNUSED GError **error)
{
    EvovisXMLFile *evxfile = (EvovisXMLFile*)user_data;
    const gchar *path = evxfile->path->str;
    gsize rawlen;
    guchar *decoded;

    gwy_debug("%s (%lu)", path, (gulong)value_len);
    if (!value_len)
        return;

    /* FIXME: The list perhaps can contain multiple profiles.  Must see on
     * real examples to implement it. */
    if (gwy_strequal(path,
                     "/root/DataElements/ListEntry/Profile/ProfilePoints")) {
        EvovisXMLRawData *rawdata = &evxfile->rawdata;

        if (rawdata->rawdata) {
            g_warning("Extend me!  Multiple profiles are not implemented.");
            return;
        }
        decoded = g_base64_decode(value, &rawlen);
        if (!rawlen || rawlen % 25 != 0) {
            g_warning("rawlen %lu is zero or not a multiple of 25",
                      (gulong)rawlen);
            g_free(decoded);
            return;
        }

        rawdata->rawdata = decoded;
        rawdata->npoints = rawlen/25;
        gwy_debug("found raw point data with %u points", rawdata->npoints);
        return;
    }

    g_hash_table_insert(evxfile->hash, g_strdup(path), g_strdup(value));
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
