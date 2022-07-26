/*
 *  $Id: imgexport.c 23912 2021-08-02 12:38:33Z yeti-dn $
 *  Copyright (C) 2014-2021 David Necas (Yeti).
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

/* TODO:
 * - alignment of image title (namely when it is on the top)
 * - JPEG2000 support? seems possible but messy
 */

/* NB: Magic is in pixmap. */

/**
 * [FILE-MAGIC-MISSING]
 * Export only.  Avoding clash with a standard file format.
 **/

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <locale.h>
#include <glib/gstdio.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include <cairo.h>

/* We want cairo_ps_surface_set_eps().  So if we don't get it we just pretend
 * cairo doesn't have the PS surface at all. */
#if CAIRO_VERSION < CAIRO_VERSION_ENCODE(1, 6, 0)
#undef CAIRO_HAS_PS_SURFACE
#endif

#ifdef CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>
#endif

#ifdef CAIRO_HAS_PS_SURFACE
#include <cairo-ps.h>
#endif

#ifdef CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>
#endif

#ifdef HAVE_PNG
#include <png.h>
#ifdef HAVE_ZLIB
#include <zlib.h>
#else
#define Z_BEST_COMPRESSION 9
#endif
#endif

#ifdef HAVE_WEBP
#include <webp/encode.h>
#endif

#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyversion.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libprocess/stats.h>
#include <libprocess/spline.h>
#include <libdraw/gwypixfield.h>
#include <libgwydgets/gwydgets.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "gwytiff.h"
#include "image-keys.h"
#include "imgexportpreset.h"

#define APP_RANGE_KEY "/app/default-range-type"

#define mm2pt (72.0/25.4)
#define pangoscale ((gdouble)PANGO_SCALE)
#define fixzero(x) (fabs(x) < 1e-14 ? 0.0 : (x))

enum {
    PREVIEW_SIZE = 480,
};

struct ImgExportFormat;

typedef struct {
    gdouble x, y;
    gdouble w, h;
} ImgExportRect;

typedef struct {
    gdouble from, to, step, base;
} RulerTicks;

typedef struct {
    /* Scaled parameters */
    SizeSettings sizes;

    /* Various component sizes */
    GwySIValueFormat *vf_hruler;
    GwySIValueFormat *vf_vruler;
    GwySIValueFormat *vf_fmruler;
    RulerTicks hruler_ticks;
    RulerTicks vruler_ticks;
    RulerTicks fmruler_ticks;
    gdouble hruler_label_height;
    gdouble vruler_label_width;
    gdouble fmruler_label_width;
    gdouble fmruler_units_width;
    gdouble fmruler_label_height;
    gdouble inset_length;
    gboolean zunits_nonempty;

    /* Actual rectangles, including positions, of the image parts. */
    ImgExportRect image;
    ImgExportRect hruler;
    ImgExportRect vruler;
    ImgExportRect inset;
    ImgExportRect fmgrad;
    ImgExportRect fmruler;
    ImgExportRect title;
    ImgExportRect maskkey;

    /* Union of all above (plus maybe borders). */
    ImgExportRect canvas;
} ImgExportSizes;

struct _ImgExportEnv {
    const struct _ImgExportFormat *format;
    GwyDataField *dfield;
    GwyDataField *mask;
    GwyContainer *data;
    GArray *selections;
    GwyRGBA mask_colour;
    GwyGradient *gradient;
    GwyGradient *grey;
    gchar *title;
    gchar *decimal_symbol;
    GwyLayerBasicRangeType fm_rangetype;
    gdouble fm_min;
    gdouble fm_max;
    gboolean fm_inverted;
    gboolean has_presentation;
    gint id;
    guint xres;
    guint yres;            /* Already after realsquare resampling! */
    gboolean realsquare;
    GQuark vlayer_sel_key;
    gboolean sel_line_have_layer;
    gboolean sel_point_have_layer;
    gboolean sel_path_have_layer;
    gdouble sel_line_thickness;
    gdouble sel_point_radius;
};

typedef struct {
    GtkWidget *label;
    GtkWidget *button;
    GtkWidget *setblack;
    GtkWidget *setwhite;
} ImgExportColourControls;

typedef struct {
    ImgExportArgs *args;
    GtkWidget *dialog;
    GtkWidget *preview;

    GtkWidget *mode;
    GtkWidget *notebook;

    /* Basic */
    GtkWidget *table_basic;
    GtkObject *zoom;        /* Pixmap only */
    GtkObject *pxwidth;     /* Vector only [mm] */
    GtkObject *ppi;         /* Vector only, pixels per inch */
    GtkObject *width;
    GtkObject *height;
    GtkWidget *font;
    GtkObject *font_size;
    GtkObject *line_width;
    GtkObject *border_width;
    GtkObject *tick_length;
    GtkWidget *scale_font;
    GtkWidget *decomma;
    GtkWidget *transparent_bg;
    ImgExportColourControls linetext_colour;
    ImgExportColourControls bg_colour;

    /* Lateral Scale */
    GtkWidget *table_lateral;
    GQuark rb_quark;
    GSList *xytype;
    GtkObject *inset_xgap;
    GtkObject *inset_ygap;
    ImgExportColourControls inset_colour;
    ImgExportColourControls inset_outline_colour;
    GtkObject *inset_outline_width;
    GtkObject *inset_opacity;
    GSList *inset_pos;
    GtkWidget *inset_pos_label[6];
    GtkWidget *inset_length;
    GtkWidget *inset_draw_ticks;
    GtkWidget *inset_draw_label;
    GtkWidget *inset_draw_text_above;

    /* Values */
    GtkWidget *table_value;
    GtkWidget *draw_frame;
    GtkWidget *draw_mask;
    GtkWidget *draw_maskkey;
    GtkWidget *mask_key;
    GtkObject *maskkey_gap;
    GtkWidget *interpolation;
    GSList *ztype;
    GtkObject *fmscale_gap;
    GtkWidget *fix_fmscale_precision;
    GtkObject *fmscale_precision;
    GtkWidget *fix_kilo_threshold;
    GtkObject *kilo_threshold;
    GtkWidget *title_type;
    GtkObject *title_gap;
    GtkWidget *units_in_title;

    /* Selection */
    GtkWidget *table_selection;
    GtkWidget *draw_selection;
    GtkWidget *selections;
    ImgExportColourControls sel_colour;
    ImgExportColourControls sel_outline_colour;
    GtkObject *sel_outline_width;
    GtkObject *sel_opacity;
    gint sel_row_start;
    GtkWidget *sel_options_label;
    GSList *sel_options;

    /* Presets */
    GtkWidget *table_presets;
    GtkWidget *presets;
    GtkWidget *preset_name;
    GtkWidget *preset_load;
    GtkWidget *preset_save;
    GtkWidget *preset_rename;
    GtkWidget *preset_delete;

    gulong sid;
    gboolean in_update;
} ImgExportControls;

typedef void (*SelOptionsFunc)(ImgExportControls *controls);
typedef void (*SelDrawFunc)(const ImgExportArgs *args,
                            const ImgExportSizes *sizes,
                            GwySelection *sel,
                            gdouble qx,
                            gdouble qy,
                            PangoLayout *layout,
                            GString *s,
                            cairo_t *cr);

typedef struct {
    const gchar *typename;
    const gchar *description;
    SelOptionsFunc create_options;
    SelDrawFunc draw;
} ImgExportSelectionType;

typedef gboolean (*WritePixbufFunc)(GdkPixbuf *pixbuf,
                                    const gchar *name,
                                    const gchar *filename,
                                    GError **error);
typedef gboolean (*WriteImageFunc)(ImgExportArgs *args,
                                   const gchar *name,
                                   const gchar *filename,
                                   GError **error);

typedef struct _ImgExportFormat {
    const gchar *name;
    const gchar *description;
    const gchar *extensions;
    WritePixbufFunc write_pixbuf;   /* If NULL, use generic GdkPixbuf func. */
    WriteImageFunc write_grey16;    /* 16bit grey */
    WriteImageFunc write_vector;    /* scalable */
    gboolean supports_transparency;
} ImgExportFormat;

static gboolean module_register     (void);
static gint     img_export_detect   (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name,
                                     const gchar *name);
static gboolean img_export_export   (GwyContainer *data,
                                     const gchar *filename,
                                     GwyRunType mode,
                                     GError **error,
                                     const gchar *name);
static void     img_export_free_env (ImgExportEnv *env);
static void     select_a_real_font  (ImgExportArgs *args,
                                     GtkWidget *widget);
static void     img_export_load_args(GwyContainer *container,
                                     ImgExportArgs *args);
static void     img_export_save_args(GwyContainer *container,
                                     ImgExportArgs *args);
static gchar*   scalebar_auto_length(GwyDataField *dfield,
                                     gdouble *p);
static gdouble  inset_length_ok     (GwyDataField *dfield,
                                     const gchar *inset_length);

#ifdef HAVE_PNG
static gboolean write_image_png16(ImgExportArgs *args,
                                  const gchar *name,
                                  const gchar *filename,
                                  GError **error);
#else
#define write_image_png16 NULL
#endif

static gboolean write_image_tiff16  (ImgExportArgs *args,
                                     const gchar *name,
                                     const gchar *filename,
                                     GError **error);
static gboolean write_image_pgm16   (ImgExportArgs *args,
                                     const gchar *name,
                                     const gchar *filename,
                                     GError **error);
static gboolean write_vector_generic(ImgExportArgs *args,
                                     const gchar *name,
                                     const gchar *filename,
                                     GError **error);
static gboolean write_pixbuf_generic(GdkPixbuf *pixbuf,
                                     const gchar *name,
                                     const gchar *filename,
                                     GError **error);
static gboolean write_pixbuf_tiff   (GdkPixbuf *pixbuf,
                                     const gchar *name,
                                     const gchar *filename,
                                     GError **error);
static gboolean write_pixbuf_ppm    (GdkPixbuf *pixbuf,
                                     const gchar *name,
                                     const gchar *filename,
                                     GError **error);
static gboolean write_pixbuf_bmp    (GdkPixbuf *pixbuf,
                                     const gchar *name,
                                     const gchar *filename,
                                     GError **error);
static gboolean write_pixbuf_targa  (GdkPixbuf *pixbuf,
                                     const gchar *name,
                                     const gchar *filename,
                                     GError **error);
#ifdef HAVE_WEBP
static gboolean write_pixbuf_webp   (GdkPixbuf *pixbuf,
                                     const gchar *name,
                                     const gchar *filename,
                                     GError **error);
#endif

#define DECLARE_SELECTION_DRAWING(name) \
    static void draw_sel_##name(const ImgExportArgs *args, \
                                const ImgExportSizes *sizes, \
                                GwySelection *sel, gdouble qx, gdouble qy, \
                                PangoLayout *layout, GString *s, cairo_t *cr)

DECLARE_SELECTION_DRAWING(axis);
DECLARE_SELECTION_DRAWING(cross);
DECLARE_SELECTION_DRAWING(ellipse);
DECLARE_SELECTION_DRAWING(line);
DECLARE_SELECTION_DRAWING(point);
DECLARE_SELECTION_DRAWING(rectangle);
DECLARE_SELECTION_DRAWING(lattice);
DECLARE_SELECTION_DRAWING(path);

static void     options_sel_line    (ImgExportControls *controls);
static void     options_sel_point   (ImgExportControls *controls);
static void     options_sel_path    (ImgExportControls *controls);

static const GwyRGBA black = GWYRGBA_BLACK;
static const GwyRGBA white = GWYRGBA_WHITE;

static ImgExportFormat image_formats[] = {
    {
        "png",
        N_("Portable Network Graphics (.png)"),
        ".png",
        NULL, write_image_png16, NULL, TRUE,
    },
    {
        "jpeg",
        N_("JPEG (.jpeg,.jpg)"),
        ".jpeg,.jpg,.jpe",
        NULL, NULL, NULL, FALSE,
    },
    {
        "tiff",
        N_("TIFF (.tiff,.tif)"),
        ".tiff,.tif",
        write_pixbuf_tiff, write_image_tiff16, NULL, FALSE,
    },
    {
        "pnm",
        N_("Portable Pixmap (.ppm,.pnm)"),
        ".ppm,.pnm",
        write_pixbuf_ppm, write_image_pgm16, NULL, FALSE,
    },
    {
        "bmp",
        N_("Windows or OS2 Bitmap (.bmp)"),
        ".bmp",
        write_pixbuf_bmp, NULL, NULL, FALSE,
    },
    {
        "tga",
        N_("TARGA (.tga,.targa)"),
        ".tga,.targa",
        write_pixbuf_targa, NULL, NULL, FALSE,
    },
#ifdef HAVE_WEBP
    {
        "webp",
        N_("WebP (.webp)"),
        ".webp",
        write_pixbuf_webp, NULL, NULL, TRUE,
    },
#endif
#ifdef CAIRO_HAS_PDF_SURFACE
    {
        "pdf",
        N_("Portable document format (.pdf)"),
        ".pdf",
        NULL, NULL, write_vector_generic, TRUE,
    },
#endif
#ifdef CAIRO_HAS_PS_SURFACE
    {
        "eps",
        N_("Encapsulated PostScript (.eps)"),
        ".eps",
        NULL, NULL, write_vector_generic, TRUE,
    },
#endif
#ifdef CAIRO_HAS_SVG_SURFACE
    {
        "svg",
        N_("Scalable Vector Graphics (.svg)"),
        ".svg",
        NULL, NULL, write_vector_generic, TRUE,
    },
#endif
};

static const ImgExportSelectionType known_selections[] =
{
    {
        "GwySelectionAxis", N_("Horiz./vert. lines"),
        NULL, &draw_sel_axis,
    },
    {
        /* FIXME: we should support mode, but that is actually a tool setting,
         * not an independent property of the selection itself. */
        "GwySelectionCross", N_("Crosses"),
        NULL, &draw_sel_cross,
    },
    {
        "GwySelectionEllipse", N_("Ellipses"),
        NULL, &draw_sel_ellipse,
    },
    {
        "GwySelectionLine", N_("Lines"),
        &options_sel_line, &draw_sel_line,
    },
    {
        "GwySelectionPoint", N_("Points"),
        &options_sel_point, &draw_sel_point,
    },
    {
        "GwySelectionRectangle", N_("Rectangles"),
        NULL, &draw_sel_rectangle,
    },
    {
        "GwySelectionLattice", N_("Lattice"),
        NULL, &draw_sel_lattice,
    },
    {
        "GwySelectionPath", N_("Path"),
        &options_sel_path, &draw_sel_path,
    },
};

static const GwyEnum lateral_types[] = {
    { N_("ruler|_None"),      IMGEXPORT_LATERAL_NONE,   },
    { N_("_Rulers"),          IMGEXPORT_LATERAL_RULERS, },
    { N_("_Inset scale bar"), IMGEXPORT_LATERAL_INSET,  },
};

static const GwyEnum value_types[] = {
    { N_("ruler|_None"),        IMGEXPORT_VALUE_NONE,    },
    { N_("_False color ruler"), IMGEXPORT_VALUE_FMSCALE, },
};

static const GwyEnum title_types[] = {
    { N_("title|None"),           IMGEXPORT_TITLE_NONE,    },
    { N_("At the top"),           IMGEXPORT_TITLE_TOP,     },
    { N_("Along the right edge"), IMGEXPORT_TITLE_FMSCALE, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Renders data into vector (SVG, PDF, EPS) and "
       "pixmap (PNG, JPEG, TIFF, WebP, PPM, BMP, TARGA) images. "
       "Export to some formats relies on GDK and other libraries thus may "
       "be installation-dependent."),
    "Yeti <yeti@gwyddion.net>",
    "2.10",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY(module_info)

static ImgExportFormat*
find_format(const gchar *name, gboolean cairoext)
{
    guint i, len;

    for (i = 0; i < G_N_ELEMENTS(image_formats); i++) {
        ImgExportFormat *format = image_formats + i;

        if (cairoext) {
            len = strlen(format->name);
            if (strncmp(name, format->name, len) == 0
                && strcmp(name + len, "cairo") == 0)
                return format;
        }
        else {
            if (gwy_strequal(name, format->name))
                return format;
        }
    }

    return NULL;
}

static gboolean
module_register(void)
{
    static gint types_initialized = 0;

    GwyResourceClass *klass;
    GSList *l, *pixbuf_formats;
    guint i;

    if (!types_initialized) {
        types_initialized += gwy_img_export_preset_get_type();
        klass = g_type_class_ref(GWY_TYPE_IMG_EXPORT_PRESET);
        gwy_resource_class_load(klass);
        g_type_class_unref(klass);
    }

    /* Find out which image formats we can write using generic GdkPixbuf
     * functions. */
    pixbuf_formats = gdk_pixbuf_get_formats();
    for (l = pixbuf_formats; l; l = g_slist_next(l)) {
        GdkPixbufFormat *pixbuf_format = (GdkPixbufFormat*)l->data;
        const gchar *name;
        ImgExportFormat *format;

        name = gdk_pixbuf_format_get_name(pixbuf_format);
        if (!gdk_pixbuf_format_is_writable(pixbuf_format)) {
            gwy_debug("Ignoring pixbuf format %s, not writable", name);
            continue;
        }

        if (!(format = find_format(name, FALSE))) {
            gwy_debug("Skipping writable pixbuf format %s "
                      "because we don't know it.", name);
            continue;
        }

        if (format->write_pixbuf) {
            gwy_debug("Skipping pixbuf format %s, we have our own writer.",
                      name);
            continue;
        }

        gwy_debug("Adding generic pixbuf writer for %s.", name);
        format->write_pixbuf = write_pixbuf_generic;
    }
    g_slist_free(pixbuf_formats);

    /* Register file functions for the formats.  We want separate functions so
     * that users can see the formats listed in the file dialog.  We must use
     * names different from the pixmap module, so append "cairo". */
    for (i = 0; i < G_N_ELEMENTS(image_formats); i++) {
        ImgExportFormat *format = image_formats + i;
        gchar *caironame;

        if (!format->write_pixbuf
            && !format->write_grey16
            && !format->write_vector)
            continue;

        caironame = g_strconcat(format->name, "cairo", NULL);
        gwy_file_func_register(caironame,
                               format->description,
                               &img_export_detect,
                               NULL,
                               NULL,
                               &img_export_export);
    }

    return TRUE;
}

static gint
img_export_detect(const GwyFileDetectInfo *fileinfo,
                  G_GNUC_UNUSED gboolean only_name,
                  const gchar *name)
{
    ImgExportFormat *format;
    gint score;
    gchar **extensions;
    guint i;

    gwy_debug("Running detection for file type %s", name);

    format = find_format(name, TRUE);
    g_return_val_if_fail(format, 0);

    extensions = g_strsplit(format->extensions, ",", 0);
    g_assert(extensions);
    for (i = 0; extensions[i]; i++) {
        if (g_str_has_suffix(fileinfo->name_lowercase, extensions[i]))
            break;
    }
    score = extensions[i] ? 20 : 0;
    g_strfreev(extensions);

    return score;
}

static gchar*
scalebar_auto_length(GwyDataField *dfield,
                     gdouble *p)
{
    static const double sizes[] = {
        1.0, 2.0, 3.0, 4.0, 5.0,
        10.0, 20.0, 30.0, 40.0, 50.0,
        100.0, 200.0, 300.0, 400.0, 500.0,
    };
    GwySIValueFormat *format;
    GwySIUnit *siunit;
    gdouble base, x, vmax, real;
    gchar *s;
    gint power10;
    guint i;

    real = gwy_data_field_get_xreal(dfield);
    siunit = gwy_data_field_get_si_unit_xy(dfield);
    vmax = 0.42*real;
    power10 = 3*(gint)(floor(log10(vmax)/3.0));
    base = pow10(power10 + 1e-14);
    x = vmax/base;
    for (i = 1; i < G_N_ELEMENTS(sizes); i++) {
        if (x < sizes[i])
            break;
    }
    x = sizes[i-1] * base;

    format = gwy_si_unit_get_format_for_power10(siunit,
                                                GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                power10, NULL);
    s = g_strdup_printf("%.*f %s",
                        format->precision, x/format->magnitude, format->units);
    gwy_si_unit_value_format_free(format);

    if (p)
        *p = x/real;

    return s;
}

static gdouble
inset_length_ok(GwyDataField *dfield,
                const gchar *inset_length)
{
    gdouble xreal, length;
    gint power10;
    GwySIUnit *siunit, *siunitxy;
    gchar *end, *plain_text_length = NULL;
    gboolean ok;

    if (!inset_length || !*inset_length)
        return 0.0;

    gwy_debug("checking inset <%s>", inset_length);
    if (!pango_parse_markup(inset_length, -1, 0,
                            NULL, &plain_text_length, NULL, NULL))
        return 0.0;

    gwy_debug("plain_text version <%s>", plain_text_length);
    length = g_strtod(plain_text_length, &end);
    gwy_debug("unit part <%s>", end);
    siunit = gwy_si_unit_new_parse(end, &power10);
    gwy_debug("power10 %d", power10);
    length *= pow10(power10);
    xreal = gwy_data_field_get_xreal(dfield);
    siunitxy = gwy_data_field_get_si_unit_xy(dfield);
    ok = (gwy_si_unit_equal(siunit, siunitxy)
          && length > 0.1*xreal
          && length < 0.85*xreal);
    g_free(plain_text_length);
    g_object_unref(siunit);
    gwy_debug("xreal %g, length %g, ok: %d", xreal, length, ok);

    return ok ? length : 0.0;
}

static PangoLayout*
create_layout(const gchar *fontname, gdouble fontsize, cairo_t *cr)
{
    PangoContext *context;
    PangoFontDescription *fontdesc;
    PangoLayout *layout;

    /* This creates a layout with private context so we can modify the
     * context at will. */
    layout = pango_cairo_create_layout(cr);

    fontdesc = pango_font_description_from_string(fontname);
    pango_font_description_set_size(fontdesc, PANGO_SCALE*fontsize);
    context = pango_layout_get_context(layout);
    pango_context_set_font_description(context, fontdesc);
    pango_font_description_free(fontdesc);
    pango_layout_context_changed(layout);
    /* XXX: Must call pango_cairo_update_layout() if we change the
     * transformation afterwards. */

    return layout;
}

static void
format_layout(PangoLayout *layout,
              PangoRectangle *logical,
              GString *string,
              const gchar *format,
              ...)
{
    gchar *buffer;
    gint length;
    va_list ap;

    g_string_truncate(string, 0);
    va_start(ap, format);
    length = g_vasprintf(&buffer, format, ap);
    va_end(ap);
    g_string_append_len(string, buffer, length);
    g_free(buffer);

    pango_layout_set_markup(layout, string->str, string->len);
    pango_layout_get_extents(layout, NULL, logical);
}

static void
format_layout_numeric(const ImgExportArgs *args,
                      PangoLayout *layout,
                      PangoRectangle *logical,
                      GString *string,
                      const gchar *format,
                      ...)
{
    const gchar *decimal_symbol = args->env->decimal_symbol;
    gchar *buffer, *s;
    gint length;
    va_list ap;

    g_string_truncate(string, 0);
    va_start(ap, format);
    length = g_vasprintf(&buffer, format, ap);
    va_end(ap);
    g_string_append_len(string, buffer, length);
    g_free(buffer);

    /* Avoid negative zero, i.e. strings that start like negative
     * zero-something but parse back as zero. */
    if (string->str[0] == '-'
        && string->str[1] == '0'
        && strtod(string->str, NULL) == 0.0)
        g_string_erase(string, 0, 1);

    /* Replace ASCII with proper minus */
    if (string->str[0] == '-') {
        g_string_erase(string, 0, 1);
        g_string_prepend_unichar(string, 0x2212);
    }

    if (args->decomma) {
        if (gwy_strequal(decimal_symbol, ".")) {
            if ((s = strchr(string->str, '.'))) {
                *s = ',';
            }
        }
        else {
            /* Keep the locale's symbol.  Most likely it's a comma.  If it
             * isn't just close eyes and pretend it is.  */
        }
    }
    else {
        if (!gwy_strequal(decimal_symbol, ".")) {
            length = strlen(decimal_symbol);
            if (length == 1 && (s = strchr(string->str, decimal_symbol[0]))) {
                *s = '.';
            }
            else if ((s = strstr(string->str, decimal_symbol))) {
                *s = '.';
                g_string_erase(string, s+1 - string->str, length-1);
            }
        }
        else {
            /* Keep the decimal dot. */
        }
    }

    pango_layout_set_markup(layout, string->str, string->len);
    pango_layout_get_extents(layout, NULL, logical);
}

static cairo_surface_t*
create_surface(const gchar *name,
               const gchar *filename,
               gdouble width, gdouble height, gboolean transparent_bg)
{
    cairo_surface_t *surface = NULL;

    if (width <= 0.0)
        width = 100.0;
    if (height <= 0.0)
        height = 100.0;

    if (gwy_stramong(name,
                     "png", "jpeg2000", "jpeg", "tiff", "pnm",
                     "bmp", "tga", "webp", NULL)) {
        cairo_format_t imageformat = (transparent_bg
                                      ? CAIRO_FORMAT_ARGB32
                                      : CAIRO_FORMAT_RGB24);
        gint iwidth = (gint)ceil(width);
        gint iheight = (gint)ceil(height);

        gwy_debug("%u %u %u", imageformat, iwidth, iheight);
        surface = cairo_image_surface_create(imageformat, iwidth, iheight);
    }
#ifdef CAIRO_HAS_PDF_SURFACE
    else if (gwy_strequal(name, "pdf"))
        surface = cairo_pdf_surface_create(filename, width, height);
#endif
#ifdef CAIRO_HAS_PS_SURFACE
    else if (gwy_strequal(name, "eps")) {
        surface = cairo_ps_surface_create(filename, width, height);
        /* Requires cairo 1.6. */
        cairo_ps_surface_set_eps(surface, TRUE);
    }
#endif
#ifdef CAIRO_HAS_SVG_SURFACE
    else if (gwy_strequal(name, "svg")) {
        surface = cairo_svg_surface_create(filename, width, height);
    }
#endif
    else {
        g_assert_not_reached();
    }

    return surface;
}

static gboolean
should_draw_frame(const ImgExportArgs *args)
{
    if (args->draw_frame)
        return TRUE;
    if (args->xytype == IMGEXPORT_LATERAL_RULERS)
        return TRUE;
    if (args->ztype == IMGEXPORT_VALUE_FMSCALE)
        return TRUE;
    return FALSE;
}

static gboolean
precision_is_sufficient(gdouble bs, guint precision)
{
    gchar *s0 = g_strdup_printf("%.*f", precision, 0.0);
    gchar *s1 = g_strdup_printf("%.*f", precision, bs);
    gchar *s2 = g_strdup_printf("%.*f", precision, 2.0*bs);
    gchar *s3 = g_strdup_printf("%.*f", precision, 3.0*bs);
    gboolean ok = (!gwy_strequal(s0, s1)
                   && !gwy_strequal(s1, s2)
                   && !gwy_strequal(s2, s3));

    gwy_debug("<%s> vs <%s> vs <%s> vs <%s>: %s",
              s0, s1, s2, s3, ok ? "OK" : "NOT OK");
    g_free(s0);
    g_free(s1);
    g_free(s2);
    g_free(s3);
    return ok;
}

static void
find_hruler_ticks(const ImgExportArgs *args, ImgExportSizes *sizes,
                  PangoLayout *layout, GString *s)
{
    GwyDataField *dfield = args->env->dfield;
    GwySIUnit *xyunit = gwy_data_field_get_si_unit_xy(dfield);
    gdouble size = sizes->image.w;
    gdouble real = gwy_data_field_get_xreal(dfield);
    gdouble offset = gwy_data_field_get_xoffset(dfield);
    RulerTicks *ticks = &sizes->hruler_ticks;
    PangoRectangle logical1, logical2;
    GwySIValueFormat *vf;
    gdouble len, bs, height;
    guint n;

    vf = gwy_si_unit_get_format_with_resolution(xyunit,
                                                GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                real, real/12,
                                                NULL);
    sizes->vf_hruler = vf;
    gwy_debug("unit '%s'", vf->units);
    offset /= vf->magnitude;
    real /= vf->magnitude;
    format_layout_numeric(args, layout, &logical2, s,
                          "%.*f %s", vf->precision, offset, vf->units);
    gwy_debug("first '%s'", s->str);
    format_layout_numeric(args, layout, &logical1, s,
                          "%.*f", vf->precision, real + offset);
    gwy_debug("right '%s'", s->str);

    height = MAX(logical1.height/pangoscale, logical2.height/pangoscale);
    sizes->hruler_label_height = height;
    len = MAX(logical1.width/pangoscale, logical2.width/pangoscale);
    gwy_debug("label len %g, height %g", len, height);
    n = CLAMP(GWY_ROUND(size/len), 1, 10);
    gwy_debug("nticks %u", n);
    ticks->step = real/n;
    ticks->base = pow10(floor(log10(ticks->step)));
    ticks->step /= ticks->base;
    if (ticks->step <= 2.0)
        ticks->step = 2.0;
    else if (ticks->step <= 5.0)
        ticks->step = 5.0;
    else {
        ticks->base *= 10.0;
        ticks->step = 1.0;
        if (vf->precision)
            vf->precision--;
    }

    bs = ticks->base * ticks->step;
    if (!precision_is_sufficient(bs, vf->precision)) {
        gwy_debug("precision %u insufficient, increasing by 1", vf->precision);
        vf->precision++;
    }
    else if (vf->precision && precision_is_sufficient(bs, vf->precision-1)) {
        gwy_debug("precision %u excessive, decreasing by 1", vf->precision);
        vf->precision--;
    }

    gwy_debug("base %g, step %g", ticks->base, ticks->step);
    ticks->from = ceil(offset/bs - 1e-14)*bs;
    ticks->from = fixzero(ticks->from);
    ticks->to = floor((real + offset)/bs + 1e-14)*bs;
    ticks->to = fixzero(ticks->to);
    gwy_debug("from %g, to %g", ticks->from, ticks->to);
}

/* This must be called after find_hruler_ticks().  For unit consistency, we
 * choose the units in the horizontal ruler and force the same here. */
static void
find_vruler_ticks(const ImgExportArgs *args, ImgExportSizes *sizes,
                  PangoLayout *layout, GString *s)
{
    GwyDataField *dfield = args->env->dfield;
    gdouble size = sizes->image.h;
    gdouble real = gwy_data_field_get_yreal(dfield);
    gdouble offset = gwy_data_field_get_yoffset(dfield);
    RulerTicks *ticks = &sizes->vruler_ticks;
    PangoRectangle logical1, logical2;
    GwySIValueFormat *vf;
    gdouble height, bs, width;

    *ticks = sizes->hruler_ticks;
    vf = sizes->vf_vruler = gwy_si_unit_value_format_copy(sizes->vf_hruler);
    offset /= vf->magnitude;
    real /= vf->magnitude;
    format_layout_numeric(args, layout, &logical1, s,
                          "%.*f", vf->precision, offset);
    gwy_debug("top '%s'", s->str);
    format_layout_numeric(args, layout, &logical2, s,
                          "%.*f", vf->precision, offset + real);
    gwy_debug("last '%s'", s->str);

    height = MAX(logical1.height/pangoscale, logical2.height/pangoscale);
    gwy_debug("label height %g", height);

    /* Fix too dense ticks */
    while (ticks->base*ticks->step/real*size < 1.1*height) {
        if (ticks->step == 1.0)
            ticks->step = 2.0;
        else if (ticks->step == 2.0)
            ticks->step = 5.0;
        else {
            ticks->step = 1.0;
            ticks->base *= 10.0;
            if (vf->precision)
                vf->precision--;
        }
    }
    /* XXX: We also want to fix too sparse ticks but we do not want to make
     * the verical ruler different from the horizontal unless it really looks
     * bad.  So some ‘looks really bad’ criterion is necessary. */
    gwy_debug("base %g, step %g", ticks->base, ticks->step);

    bs = ticks->base * ticks->step;
    ticks->from = ceil(offset/bs - 1e-14)*bs;
    ticks->from = fixzero(ticks->from);
    ticks->to = floor((real + offset)/bs + 1e-14)*bs;
    ticks->to = fixzero(ticks->to);
    gwy_debug("from %g, to %g", ticks->from, ticks->to);

    /* Update widths for the new ticks. */
    format_layout_numeric(args, layout, &logical1, s,
                          "%.*f", vf->precision, ticks->from);
    gwy_debug("top2 '%s'", s->str);
    format_layout_numeric(args, layout, &logical2, s,
                          "%.*f", vf->precision, ticks->to);
    gwy_debug("last2 '%s'", s->str);

    width = MAX(logical1.width/pangoscale, logical2.width/pangoscale);
    sizes->vruler_label_width = width;
}

static void
measure_fmscale_label(GwySIValueFormat *vf,
                      const ImgExportArgs *args, ImgExportSizes *sizes,
                      PangoLayout *layout, GString *s)
{
    PangoRectangle logical1, logical2;
    const ImgExportEnv *env = args->env;
    gdouble min, max, width, height;

    min = env->fm_min/vf->magnitude;
    max = env->fm_max/vf->magnitude;

    sizes->fmruler_units_width = 0.0;

    /* Maximum, where we attach the units. */
    format_layout_numeric(args, layout, &logical1, s,
                          "%.*f", vf->precision, max);
    if (!args->units_in_title) {
        sizes->fmruler_units_width -= logical1.width/pangoscale;
        format_layout_numeric(args, layout, &logical1, s, "%.*f %s",
                              vf->precision, max, vf->units);
        sizes->fmruler_units_width += logical1.width/pangoscale;
    }
    gwy_debug("max '%s' (%g x %g)",
              s->str, logical1.width/pangoscale, logical1.height/pangoscale);

    /* Minimum, where we do not attach the units but must include them in size
     * calculation due to alignment of the numbers. */
    format_layout_numeric(args, layout, &logical2, s,
                          "%.*f", vf->precision, min);
    if (!args->units_in_title) {
        sizes->fmruler_units_width -= logical2.width/pangoscale;
        format_layout_numeric(args, layout, &logical2, s,
                              "%.*f %s", vf->precision, min, vf->units);
        sizes->fmruler_units_width += logical2.width/pangoscale;
    }
    gwy_debug("min '%s' (%g x %g)",
              s->str, logical2.width/pangoscale, logical2.height/pangoscale);

    width = MAX(logical1.width/pangoscale, logical2.width/pangoscale);
    sizes->fmruler_label_width = (width + sizes->sizes.tick_length
                                  + sizes->sizes.line_width);
    height = MAX(logical1.height/pangoscale, logical2.height/pangoscale);
    sizes->fmruler_label_height = height;
    gwy_debug("label width %g, height %g", width, height);
    sizes->fmruler_units_width *= 0.5;
    gwy_debug("units width %g", sizes->fmruler_units_width);
}

static GwySIValueFormat*
get_value_format_with_kilo_threshold(GwySIUnit *unit,
                                     GwySIUnitFormatStyle style,
                                     gdouble max, gdouble kilo_threshold,
                                     GwySIValueFormat *vf)
{
    gint p = 3*(gint)floor(log(max)/G_LN10/3.0 + 1e-14);
    gdouble b = pow10(p);

    while (max/b < 1e-3*kilo_threshold) {
        p -= 3;
        b /= 1000.0;
    }
    while (max/b >= kilo_threshold) {
        p += 3;
        b *= 1000.0;
    }

    /* NB: This does not touch the number of decimal places.  We must decide
     * ourselves how many digits we want. */
    vf = gwy_si_unit_get_format_for_power10(unit, style, p, vf);
    vf->precision = 0;

    while (max/b < 120.0) {
        vf->precision++;
        b /= 10.0;
    }

    return vf;
}

static void
find_fmscale_ticks(const ImgExportArgs *args, ImgExportSizes *sizes,
                   PangoLayout *layout, GString *s)
{
    const ImgExportEnv *env = args->env;
    GwyDataField *dfield = env->dfield;
    GwySIUnit *zunit = gwy_data_field_get_si_unit_z(dfield);
    gdouble size = sizes->image.h;
    gdouble min, max, m, real;
    RulerTicks *ticks = &sizes->fmruler_ticks;
    GwySIValueFormat *vf;
    gdouble bs, height;
    guint n;

    min = env->fm_min;
    max = env->fm_max;
    real = max - min;
    m = MAX(fabs(min), fabs(max));

    /* This format is reused for the title. */
    if (args->fix_kilo_threshold && m) {
        vf = get_value_format_with_kilo_threshold(zunit,
                                                  GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                  m, args->kilo_threshold,
                                                  NULL);
    }
    else {
        vf = gwy_si_unit_get_format_with_resolution(zunit,
                                                    GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                    real, real/96.0,
                                                    NULL);
    }

    min /= vf->magnitude;
    max /= vf->magnitude;
    real /= vf->magnitude;

    sizes->vf_fmruler = vf;
    sizes->zunits_nonempty = !!strlen(vf->units);
    gwy_debug("unit '%s'", vf->units);

    /* Return after creating vf, we are supposed to do that. */
    if (env->has_presentation) {
        sizes->fmruler_label_width = (sizes->sizes.tick_length +
                                      sizes->sizes.line_width);
        sizes->fmruler_label_height = 0.0;
        sizes->fmruler_units_width = 0.0;
        return;
    }

    measure_fmscale_label(vf, args, sizes, layout, s);
    height = sizes->fmruler_label_height;

    /* We can afford somewhat denser ticks for adaptive mapping.  However,
     * when there are labels we definitely want the distance to the next tick
     * to be much larger than to the correct tick. */
    if (env->fm_rangetype == GWY_LAYER_BASIC_RANGE_ADAPT)
        n = CLAMP(GWY_ROUND(1.2*size/height), 1, 40);
    else
        n = CLAMP(GWY_ROUND(0.7*size/height), 1, 15);

    gwy_debug("nticks %u", n);
    ticks->step = real/n;
    ticks->base = pow10(floor(log10(ticks->step)));
    ticks->step /= ticks->base;
    gwy_debug("estimated base %g, step %g", ticks->base, ticks->step);
    if (ticks->step <= 2.0)
        ticks->step = 2.0;
    else if (ticks->step <= 5.0)
        ticks->step = 5.0;
    else {
        ticks->base *= 10.0;
        ticks->step = 1.0;
        if (vf->precision) {
            vf->precision--;
            measure_fmscale_label(vf, args, sizes, layout, s);
        }
    }
    gwy_debug("base %g, step %g", ticks->base, ticks->step);
    gwy_debug("tick distance/label height ratio %g", size/n/height);

    if (args->fix_fmscale_precision) {
        /* Do everything as normal and override the precision at the end. */
        gwy_debug("overriding precision to %d", args->fmscale_precision);
        vf->precision = args->fmscale_precision;
        measure_fmscale_label(vf, args, sizes, layout, s);
    }

    bs = ticks->base * ticks->step;
    ticks->from = ceil(min/bs - 1e-14)*bs;
    ticks->from = fixzero(ticks->from);
    ticks->to = floor(max/bs + 1e-14)*bs;
    ticks->to = fixzero(ticks->to);
    gwy_debug("from %g, to %g", ticks->from, ticks->to);
}

static void
measure_inset(const ImgExportArgs *args, ImgExportSizes *sizes,
              PangoLayout *layout, GString *s)
{
    GwyDataField *dfield = args->env->dfield;
    ImgExportRect *rect = &sizes->inset;
    gdouble hsize = sizes->image.w, vsize = sizes->image.h;
    gdouble real = gwy_data_field_get_xreal(dfield);
    PangoRectangle logical, ink;
    InsetPosType pos = args->inset_pos;
    gdouble lw = sizes->sizes.line_width;
    gdouble tl = sizes->sizes.tick_length;
    gdouble fs = sizes->sizes.font_size;

    sizes->inset_length = inset_length_ok(dfield, args->inset_length);
    if (!(sizes->inset_length > 0.0))
        return;

    rect->w = sizes->inset_length/real*(hsize - 2.0*lw);
    rect->h = lw;
    if (args->inset_draw_ticks)
        rect->h += tl + lw;

    if (args->inset_draw_label) {
        format_layout(layout, &logical, s, "%s", args->inset_length);
        rect->w = MAX(rect->w, logical.width/pangoscale);
        /* We need ink rectangle to position labels with no ink below baseline,
         * such as 100 nm, as expected. */
        pango_layout_get_extents(layout, &ink, NULL);
        rect->h += (ink.height + ink.y)/pangoscale + lw;
    }

    if (pos == INSET_POS_TOP_LEFT
        || pos == INSET_POS_TOP_CENTER
        || pos == INSET_POS_TOP_RIGHT)
        if (args->inset_draw_text_above)
            rect->y = lw + fs*args->inset_ygap + rect->h;
        else
            rect->y = lw + fs*args->inset_ygap;
    else
        if (args->inset_draw_text_above)
            if (args->inset_draw_ticks)
                rect->y = vsize - lw - tl - fs*args->inset_ygap;
            else
                rect->y = vsize - lw - fs*args->inset_ygap;
        else
            rect->y = vsize - lw - rect->h - fs*args->inset_ygap;

    if (pos == INSET_POS_TOP_LEFT || pos == INSET_POS_BOTTOM_LEFT)
        rect->x = 2.0 * lw + fs*args->inset_xgap;
    else if (pos == INSET_POS_TOP_RIGHT || pos == INSET_POS_BOTTOM_RIGHT)
        rect->x = hsize - 2.0 * lw - rect->w - fs*args->inset_xgap;
    else
        rect->x = hsize / 2 - 0.5 * rect->w;
}

static void
measure_title(const ImgExportArgs *args, ImgExportSizes *sizes,
              PangoLayout *layout, GString *s)
{
    const ImgExportEnv *env = args->env;
    ImgExportRect *rect = &sizes->title;
    PangoRectangle logical;
    gdouble fs = sizes->sizes.font_size;
    gdouble gap;

    g_string_truncate(s, 0);
    if (args->units_in_title) {
        GwySIValueFormat *vf = sizes->vf_fmruler;
        if (strlen(vf->units))
            format_layout(layout, &logical, s, "%s [%s]",
                          env->title, vf->units);
    }
    if (!s->len)
        format_layout(layout, &logical, s, "%s", env->title);

    /* Straight.  This is rotated according to the type when drawing.
     * NB: rect->h can be negative; the measurement must deal with it. */
    gap = fs*args->title_gap;
    if (args->title_type != IMGEXPORT_TITLE_FMSCALE)
        gap = MAX(gap, 0.0);
    rect->w = logical.width/pangoscale;
    rect->h = logical.height/pangoscale + gap;
}

static void
measure_mask_legend(const ImgExportArgs *args, ImgExportSizes *sizes,
                    PangoLayout *layout, GString *s)
{
    ImgExportRect *rect = &sizes->maskkey;
    PangoRectangle logical;
    gdouble fs = sizes->sizes.font_size;
    gdouble lw = sizes->sizes.line_width;
    gdouble h, hgap, vgap;

    g_string_truncate(s, 0);
    format_layout(layout, &logical, s, "%s", args->mask_key);

    h = 1.5*fs + 2.0*lw;    /* Match fmscale width */
    vgap = fs*args->maskkey_gap;
    hgap = 0.4*h;
    rect->h = h + vgap;
    rect->w = 1.4*h + hgap + logical.width/pangoscale;
}

static void
rect_move(ImgExportRect *rect, gdouble x, gdouble y)
{
    rect->x += x;
    rect->y += y;
}

static void
scale_sizes(SizeSettings *sizes, gdouble factor)
{
    sizes->line_width *= factor;
    sizes->inset_outline_width *= factor;
    sizes->sel_outline_width *= factor;
    sizes->border_width *= factor;
    sizes->font_size *= factor;
    sizes->tick_length *= factor;
}

static ImgExportSizes*
calculate_sizes(const ImgExportArgs *args,
                const gchar *name)
{
    PangoLayout *layout;
    ImgExportSizes *sizes = g_new0(ImgExportSizes, 1);
    GString *s = g_string_new(NULL);
    gdouble fw, lw, fs, borderw, tl, zoom = args->zoom;
    cairo_surface_t *surface;
    cairo_t *cr;

    gwy_debug("zoom %g", zoom);
    surface = create_surface(name, NULL, 0.0, 0.0, FALSE);
    g_return_val_if_fail(surface, NULL);
    cr = cairo_create(surface);
    /* With scale_font unset, the sizes are on the final rendering, i.e. they
     * do not scale with zoom.  When scale_font is set, they do scale with
     * zoom.  */
    sizes->sizes = args->sizes;
    if (args->scale_font)
        scale_sizes(&sizes->sizes, zoom);
    lw = sizes->sizes.line_width;
    fw = should_draw_frame(args) ? lw : 0.0;
    borderw = sizes->sizes.border_width;
    tl = sizes->sizes.tick_length;
    fs = sizes->sizes.font_size;
    layout = create_layout(args->font, fs, cr);

    gwy_debug("lw = %g, fw = %g, borderw = %g", lw, fw, borderw);
    gwy_debug("tl = %g, fs = %g", tl, fs);

    /* Data */
    sizes->image.w = zoom*args->env->xres + 2.0*fw;
    sizes->image.h = zoom*args->env->yres + 2.0*fw;

    /* Horizontal ruler */
    if (args->xytype == IMGEXPORT_LATERAL_RULERS) {
        find_hruler_ticks(args, sizes, layout, s);
        sizes->hruler.w = sizes->image.w;
        sizes->hruler.h = sizes->hruler_label_height + tl + fw;
    }

    /* Vertical ruler */
    if (args->xytype == IMGEXPORT_LATERAL_RULERS) {
        find_vruler_ticks(args, sizes, layout, s);
        sizes->vruler.w = sizes->vruler_label_width + tl + fw;
        sizes->vruler.h = sizes->image.h;
        rect_move(&sizes->hruler, sizes->vruler.w, 0.0);
        rect_move(&sizes->vruler, 0.0, sizes->hruler.h);
        rect_move(&sizes->image, sizes->vruler.w, sizes->hruler.h);
    }

    /* Inset scale bar */
    if (args->xytype == IMGEXPORT_LATERAL_INSET) {
        measure_inset(args, sizes, layout, s);
        rect_move(&sizes->inset, sizes->image.x, sizes->image.y);
    }

    /* False colour gradient. Always measure the false colour axis.  We may not
     * draw it but we may want to know how it would be drawn (e.g. units). */
    sizes->fmgrad = sizes->image;
    rect_move(&sizes->fmgrad,
              sizes->image.w + fs*args->fmscale_gap - fw, 0.0);
    find_fmscale_ticks(args, sizes, layout, s);
    if (args->ztype == IMGEXPORT_VALUE_FMSCALE) {
        /* Subtract fw here to make the fmscale visually just touch the image
         * in the case of zero gap. */
        sizes->fmgrad.w = 1.5*fs + 2.0*fw;
    }
    else {
        sizes->fmgrad.x = sizes->image.x + sizes->image.w;
        sizes->fmgrad.w = 0;
        sizes->fmruler_label_width = sizes->fmruler_units_width = 0;
    }
    sizes->fmruler.x = sizes->fmgrad.x + sizes->fmgrad.w;
    sizes->fmruler.y = sizes->fmgrad.y;
    sizes->fmruler.w = sizes->fmruler_label_width;
    sizes->fmruler.h = sizes->fmgrad.h;

    /* Title, possibly with units */
    if (args->title_type != IMGEXPORT_TITLE_NONE) {
        measure_title(args, sizes, layout, s);
        if (args->title_type == IMGEXPORT_TITLE_FMSCALE) {
            gdouble ymove = sizes->image.y + sizes->image.h;

            ymove -= 0.5*(sizes->image.h - sizes->title.w);
            /* Center the title visually, not physically. */
            if (sizes->zunits_nonempty && !args->units_in_title)
                ymove += 0.5*sizes->fmruler_label_height;

            rect_move(&sizes->title,
                      sizes->fmruler.x + sizes->fmruler.w, ymove);
        }
        else if (args->title_type == IMGEXPORT_TITLE_TOP) {
            gdouble xcentre = sizes->image.x + 0.5*sizes->image.w;

            rect_move(&sizes->title, xcentre - 0.5*sizes->title.w, 0.0);
            rect_move(&sizes->image, 0.0, sizes->title.h);
            rect_move(&sizes->vruler, 0.0, sizes->title.h);
            rect_move(&sizes->hruler, 0.0, sizes->title.h);
            rect_move(&sizes->inset, 0.0, sizes->title.h);
            rect_move(&sizes->fmgrad, 0.0, sizes->title.h);
            rect_move(&sizes->fmruler, 0.0, sizes->title.h);
        }
    }

    /* Mask key */
    if (args->env->mask && args->draw_mask && args->draw_maskkey) {
        measure_mask_legend(args, sizes, layout, s);
        rect_move(&sizes->maskkey,
                  sizes->image.x, sizes->image.y + sizes->image.h);
    }

    /* Border */
    rect_move(&sizes->image, borderw, borderw);
    rect_move(&sizes->hruler, borderw, borderw);
    rect_move(&sizes->vruler, borderw, borderw);
    rect_move(&sizes->inset, borderw, borderw);
    rect_move(&sizes->fmgrad, borderw, borderw);
    rect_move(&sizes->fmruler, borderw, borderw);
    rect_move(&sizes->title, borderw, borderw);
    rect_move(&sizes->maskkey, borderw, borderw);

    /* Ensure the image starts at integer coordinates in pixmas */
    if (cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE) {
        gdouble xmove = ceil(sizes->image.x + fw) - (sizes->image.x + fw);
        gdouble ymove = ceil(sizes->image.y + fw) - (sizes->image.y + fw);

        if (xmove < 0.98 && ymove < 0.98) {
            gwy_debug("moving image by (%g,%g) to integer coordinates",
                      xmove, ymove);
            rect_move(&sizes->image, xmove, ymove);
            rect_move(&sizes->hruler, xmove, ymove);
            rect_move(&sizes->vruler, xmove, ymove);
            rect_move(&sizes->inset, xmove, ymove);
            rect_move(&sizes->fmgrad, xmove, ymove);
            rect_move(&sizes->fmruler, xmove, ymove);
            rect_move(&sizes->title, xmove, ymove);
            rect_move(&sizes->maskkey, xmove, ymove);
        }
    }

    /* Canvas */
    sizes->canvas.w = sizes->fmruler.x + sizes->fmruler.w + borderw;
    if (args->title_type == IMGEXPORT_TITLE_FMSCALE)
        sizes->canvas.w += MAX(sizes->title.h, 0.0);

    sizes->canvas.h = (sizes->image.y + sizes->image.h + sizes->maskkey.h
                       + borderw);

    gwy_debug("canvas %g x %g at (%g, %g)",
              sizes->canvas.w, sizes->canvas.h,
              sizes->canvas.x, sizes->canvas.y);
    gwy_debug("hruler %g x %g at (%g, %g)",
              sizes->hruler.w, sizes->hruler.h,
              sizes->hruler.x, sizes->hruler.y);
    gwy_debug("vruler %g x %g at (%g, %g)",
              sizes->vruler.w, sizes->vruler.h,
              sizes->vruler.x, sizes->vruler.y);
    gwy_debug("image %g x %g at (%g, %g)",
              sizes->image.w, sizes->image.h, sizes->image.x, sizes->image.y);
    gwy_debug("inset %g x %g at (%g, %g)",
              sizes->inset.w, sizes->inset.h, sizes->inset.x, sizes->inset.y);
    gwy_debug("fmgrad %g x %g at (%g, %g)",
              sizes->fmgrad.w, sizes->fmgrad.h,
              sizes->fmgrad.x, sizes->fmgrad.y);
    gwy_debug("fmruler %g x %g at (%g, %g)",
              sizes->fmruler.w, sizes->fmruler.h,
              sizes->fmruler.x, sizes->fmruler.y);
    gwy_debug("title %g x %g at (%g, %g)",
              sizes->title.w, sizes->title.h, sizes->title.x, sizes->title.y);
    gwy_debug("maskkey %g x %g at (%g, %g)",
              sizes->maskkey.w, sizes->maskkey.h,
              sizes->maskkey.x, sizes->maskkey.y);

    g_string_free(s, TRUE);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return sizes;
}

static void
destroy_sizes(ImgExportSizes *sizes)
{
    if (sizes->vf_hruler)
        gwy_si_unit_value_format_free(sizes->vf_hruler);
    if (sizes->vf_vruler)
        gwy_si_unit_value_format_free(sizes->vf_vruler);
    if (sizes->vf_fmruler)
        gwy_si_unit_value_format_free(sizes->vf_fmruler);
    g_free(sizes);
}

static void
set_cairo_source_rgba(cairo_t *cr, const GwyRGBA *rgba)
{
    cairo_set_source_rgba(cr, rgba->r, rgba->g, rgba->b, rgba->a);
}

static void
set_cairo_source_rgb(cairo_t *cr, const GwyRGBA *rgba)
{
    cairo_set_source_rgb(cr, rgba->r, rgba->g, rgba->b);
}

static void
draw_text_outline(cairo_t *cr, PangoLayout *layout,
                  const GwyRGBA *outcolour, gdouble olw)
{
    gdouble x, y;

    cairo_get_current_point(cr, &x, &y);
    pango_cairo_layout_path(cr, layout);
    set_cairo_source_rgb(cr, outcolour);
    cairo_set_line_width(cr, 2.0*olw);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke(cr);
    cairo_move_to(cr, x, y);
}

static void
draw_text(cairo_t *cr, PangoLayout *layout, const GwyRGBA *colour)
{
    set_cairo_source_rgb(cr, colour);
    cairo_set_line_width(cr, 0.0);
    pango_cairo_show_layout(cr, layout);
}

/* Can be only used with closed paths and with SQUARE and ROUND line ends --
 * which we don't use because then we can't get invisible line end ticks. */
static void
stroke_path_outline(cairo_t *cr,
                    const GwyRGBA *outcolour, gdouble lw, gdouble olw)
{
    set_cairo_source_rgb(cr, outcolour);
    cairo_set_line_width(cr, lw + 2.0*olw);
    cairo_stroke_preserve(cr);
}

static void
stroke_path(cairo_t *cr,
            const GwyRGBA *colour, gdouble lw)
{
    set_cairo_source_rgb(cr, colour);
    cairo_set_line_width(cr, lw);
    cairo_stroke(cr);
}

/* Draw outline (only) for a BUTT line, adding correct outline at the ends. */
static void
draw_line_outline(cairo_t *cr,
                  gdouble xf, gdouble yf, gdouble xt, gdouble yt,
                  const GwyRGBA *outcolour,
                  gdouble lw, gdouble olw)
{
    gdouble vx = xt - xf, vy = yt - yf;
    gdouble len = sqrt(vx*vx + vy*vy);

    if (len < 1e-9 || olw <= 0.0)
        return;

    vx *= olw/len;
    vy *= olw/len;
    cairo_save(cr);
    cairo_move_to(cr, xf - vx, yf - vy);
    cairo_line_to(cr, xt + vx, yt + vy);
    cairo_set_line_width(cr, lw + 2.0*olw);
    set_cairo_source_rgb(cr, outcolour);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void
draw_background(const ImgExportArgs *args, cairo_t *cr)
{
    gboolean can_transp = args->env->format->supports_transparency;
    gboolean want_transp = args->transparent_bg;

    if (can_transp && want_transp)
        return;

    set_cairo_source_rgb(cr, &args->bg_color);
    cairo_paint(cr);
}

static GdkPixbuf*
draw_data_pixbuf_1_1(const ImgExportArgs *args)
{
    const ImgExportEnv *env = args->env;
    GwyDataField *dfield = env->dfield;
    GwyGradient *gradient = ((args->mode == IMGEXPORT_MODE_GREY16)
                             ? env->grey
                             : env->gradient);
    GwyLayerBasicRangeType range_type = env->fm_rangetype;
    GdkPixbuf *pixbuf;
    guint xres, yres;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, xres, yres);

    if (range_type == GWY_LAYER_BASIC_RANGE_ADAPT)
        gwy_pixbuf_draw_data_field_adaptive(pixbuf, dfield, gradient);
    else {
        gdouble min = env->fm_inverted ? env->fm_max : env->fm_min;
        gdouble max = env->fm_inverted ? env->fm_min : env->fm_max;
        gwy_pixbuf_draw_data_field_with_range(pixbuf, dfield, gradient,
                                              min, max);
    }
    return pixbuf;
}

static GdkPixbuf*
draw_data_pixbuf_resampled(const ImgExportArgs *args,
                           const ImgExportSizes *sizes)
{
    const ImgExportEnv *env = args->env;
    GwyDataField *dfield = env->dfield, *resampled;
    GwyGradient *gradient = env->gradient;
    GwyLayerBasicRangeType range_type = env->fm_rangetype;
    gdouble lw = sizes->sizes.line_width;
    gdouble fw = should_draw_frame(args) ? lw : 0.0;
    GdkPixbuf *pixbuf;
    gdouble w = sizes->image.w - 2.0*fw;
    gdouble h = sizes->image.h - 2.0*fw;
    guint width, height;

    width = GWY_ROUND(MAX(w, 2.0));
    height = GWY_ROUND(MAX(h, 2.0));
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);

    resampled = gwy_data_field_new_resampled(dfield, width, height,
                                             args->interpolation);

    /* XXX: Resampling can influence adaptive mapping, i.e. adaptive-mapping
     * resampled image is not the same as adaptive-mapping the original and
     * then resampling.  But it should not be noticeable in normal
     * circumstances.  */
    if (range_type == GWY_LAYER_BASIC_RANGE_ADAPT)
        gwy_pixbuf_draw_data_field_adaptive(pixbuf, resampled, gradient);
    else {
        gdouble min = env->fm_inverted ? env->fm_max : env->fm_min;
        gdouble max = env->fm_inverted ? env->fm_min : env->fm_max;
        gwy_pixbuf_draw_data_field_with_range(pixbuf, resampled, gradient,
                                              min, max);
    }

    g_object_unref(resampled);

    return pixbuf;
}

static GdkPixbuf*
draw_mask_pixbuf(const ImgExportArgs *args)
{
    const ImgExportEnv *env = args->env;
    GwyDataField *mask = env->mask;
    guint xres, yres;
    GdkPixbuf *pixbuf;

    g_return_val_if_fail(mask, NULL);
    xres = gwy_data_field_get_xres(mask);
    yres = gwy_data_field_get_yres(mask);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, xres, yres);
    gwy_pixbuf_draw_data_field_as_mask(pixbuf, mask, &env->mask_colour);

    return pixbuf;
}

static void
stretch_pixbuf_source(cairo_t *cr,
                      GdkPixbuf *pixbuf,
                      const ImgExportArgs *args,
                      const ImgExportSizes *sizes)
{
    gdouble mw = gdk_pixbuf_get_width(pixbuf);
    gdouble mh = gdk_pixbuf_get_height(pixbuf);
    gdouble lw = sizes->sizes.line_width;
    gdouble fw = should_draw_frame(args) ? lw : 0.0;
    gdouble w = sizes->image.w - 2.0*fw;
    gdouble h = sizes->image.h - 2.0*fw;

    cairo_scale(cr, w/mw, h/mh);
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0.0, 0.0);
}

static void
draw_data(const ImgExportArgs *args,
          const ImgExportSizes *sizes,
          cairo_t *cr)
{
    const ImgExportRect *rect = &sizes->image;
    ImgExportEnv *env = args->env;
    gboolean drawing_as_vector;
    cairo_filter_t interp;
    GdkPixbuf *pixbuf;
    gint xres = gwy_data_field_get_xres(env->dfield);
    gint yres = gwy_data_field_get_yres(env->dfield);
    gdouble lw = sizes->sizes.line_width;
    gdouble fw = should_draw_frame(args) ? lw : 0.0;
    gdouble w = rect->w - 2.0*fw;
    gdouble h = rect->h - 2.0*fw;

    /* Never draw pixmap images with anything else than CAIRO_FILTER_BILINEAR
     * because it causes bleeding and fading at the image borders. */
    interp = CAIRO_FILTER_NEAREST;
    drawing_as_vector = (cairo_surface_get_type(cairo_get_target(cr))
                         != CAIRO_SURFACE_TYPE_IMAGE);
    if (drawing_as_vector && args->interpolation != GWY_INTERPOLATION_ROUND)
        interp = CAIRO_FILTER_BILINEAR;

    if (drawing_as_vector
        || args->mode == IMGEXPORT_MODE_GREY16
        || args->interpolation == GWY_INTERPOLATION_ROUND
        || (fabs(xres - w) < 0.001 && fabs(yres - h) < 0.001))
        pixbuf = draw_data_pixbuf_1_1(args);
    else {
        pixbuf = draw_data_pixbuf_resampled(args, sizes);
        interp = CAIRO_FILTER_NEAREST;
    }

    cairo_save(cr);
    cairo_translate(cr, rect->x + fw, rect->y + fw);
    stretch_pixbuf_source(cr, pixbuf, args, sizes);
    cairo_pattern_set_filter(cairo_get_source(cr), interp);
    cairo_paint(cr);
    cairo_restore(cr);
    g_object_unref(pixbuf);

    /* Mask must be drawn pixelated. */
    if (env->mask && args->draw_mask) {
        cairo_save(cr);
        cairo_translate(cr, rect->x + fw, rect->y + fw);
        pixbuf = draw_mask_pixbuf(args);
        stretch_pixbuf_source(cr, pixbuf, args, sizes);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_paint(cr);
        cairo_restore(cr);
        g_object_unref(pixbuf);
    }
}

static void
draw_data_frame(const ImgExportArgs *args,
                const ImgExportSizes *sizes,
                cairo_t *cr)
{
    const ImgExportRect *rect = &sizes->image;
    const GwyRGBA *color = &args->linetext_color;
    gdouble fw = sizes->sizes.line_width;
    gdouble w = rect->w - 2.0*fw;
    gdouble h = rect->h - 2.0*fw;

    if (!should_draw_frame(args))
        return;

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, color);
    cairo_set_line_width(cr, fw);
    cairo_rectangle(cr, 0.5*fw, 0.5*fw, w + fw, h + fw);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void
draw_hruler(const ImgExportArgs *args,
            const ImgExportSizes *sizes,
            PangoLayout *layout,
            GString *s,
            cairo_t *cr)
{
    GwySIValueFormat *vf = sizes->vf_hruler;
    GwyDataField *dfield = args->env->dfield;
    const ImgExportRect *rect = &sizes->hruler;
    const RulerTicks *ticks = &sizes->hruler_ticks;
    const GwyRGBA *color = &args->linetext_color;
    gdouble lw = sizes->sizes.line_width;
    gdouble tl = sizes->sizes.tick_length;
    gdouble x, bs, scale, ximg, xreal, xoffset;
    gboolean units_placed = FALSE;

    if (args->xytype != IMGEXPORT_LATERAL_RULERS)
        return;

    xreal = gwy_data_field_get_xreal(dfield)/vf->magnitude;
    xoffset = gwy_data_field_get_xoffset(dfield)/vf->magnitude;
    scale = (rect->w - lw)/xreal;
    bs = ticks->step*ticks->base;

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, color);
    cairo_set_line_width(cr, lw);
    for (x = ticks->from; x <= ticks->to + 1e-14*bs; x += bs) {
        ximg = (x - xoffset)*scale + 0.5*lw;
        gwy_debug("x %g -> %g", x, ximg);
        cairo_move_to(cr, ximg, rect->h);
        cairo_line_to(cr, ximg, rect->h - tl);
    };
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, color);
    for (x = ticks->from; x <= ticks->to + 1e-14*bs; x += bs) {
        PangoRectangle logical;

        x = fixzero(x);
        ximg = (x - xoffset)*scale + 0.5*lw;
        if (!units_placed && (x >= 0.0 || ticks->to <= -1e-14)) {
            format_layout_numeric(args, layout, &logical, s,
                                  "%.*f %s", vf->precision, x, vf->units);
            units_placed = TRUE;
        }
        else
            format_layout_numeric(args, layout, &logical, s,
                                  "%.*f", vf->precision, x);

        if (ximg + logical.width/pangoscale <= rect->w) {
            cairo_move_to(cr, ximg, rect->h - tl - lw);
            cairo_rel_move_to(cr, 0.0, -logical.height/pangoscale);
            pango_cairo_show_layout(cr, layout);
        }
    };
    cairo_restore(cr);
}

static void
draw_vruler(const ImgExportArgs *args,
            const ImgExportSizes *sizes,
            PangoLayout *layout,
            GString *s,
            cairo_t *cr)
{
    GwySIValueFormat *vf = sizes->vf_vruler;
    GwyDataField *dfield = args->env->dfield;
    const ImgExportRect *rect = &sizes->vruler;
    const RulerTicks *ticks = &sizes->vruler_ticks;
    const GwyRGBA *color = &args->linetext_color;
    gdouble lw = sizes->sizes.line_width;
    gdouble tl = sizes->sizes.tick_length;
    gdouble y, bs, scale, yimg, yreal, yoffset;

    if (args->xytype != IMGEXPORT_LATERAL_RULERS)
        return;

    yreal = gwy_data_field_get_yreal(dfield)/vf->magnitude;
    yoffset = gwy_data_field_get_yoffset(dfield)/vf->magnitude;
    scale = (rect->h - lw)/yreal;
    bs = ticks->step*ticks->base;

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, color);
    cairo_set_line_width(cr, lw);
    for (y = ticks->from; y <= ticks->to + 1e-14*bs; y += bs) {
        yimg = (y - yoffset)*scale + 0.5*lw;
        gwy_debug("y %g -> %g", y, yimg);
        cairo_move_to(cr, rect->w, yimg);
        cairo_line_to(cr, rect->w - tl, yimg);
    };
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, color);
    for (y = ticks->from; y <= ticks->to + 1e-14*bs; y += bs) {
        PangoRectangle logical;

        y = fixzero(y);
        yimg = (y - yoffset)*scale + 0.5*lw;
        format_layout_numeric(args, layout, &logical, s,
                              "%.*f", vf->precision, y);
        if (yimg + logical.height/pangoscale <= rect->h) {
            cairo_move_to(cr, rect->w - tl - lw, yimg);
            cairo_rel_move_to(cr, -logical.width/pangoscale, 0.0);
            pango_cairo_show_layout(cr, layout);
        }
    };
    cairo_restore(cr);
}

static void
draw_inset(const ImgExportArgs *args,
           const ImgExportSizes *sizes,
           PangoLayout *layout,
           GString *s,
           cairo_t *cr)
{
    GwyDataField *dfield = args->env->dfield;
    gdouble xreal = gwy_data_field_get_xreal(dfield);
    const ImgExportRect *rect = &sizes->inset, *imgrect = &sizes->image;
    const GwyRGBA *colour = &args->inset_color;
    const GwyRGBA *outcolour = &args->inset_outline_color;
    PangoRectangle logical, ink;
    gdouble lw = sizes->sizes.line_width;
    gdouble tl = sizes->sizes.tick_length;
    gdouble olw = sizes->sizes.inset_outline_width;
    gdouble xcentre, length, y;
    gdouble w = imgrect->w - 2.0*lw;
    gdouble h = imgrect->h - 2.0*lw;

    if (args->xytype != IMGEXPORT_LATERAL_INSET)
        return;

    if (!(sizes->inset_length > 0.0))
        return;

    length = (sizes->image.w - 2.0*lw)/xreal*sizes->inset_length;
    xcentre = 0.5*rect->w;
    y = 0.5*lw;

    cairo_save(cr);
    cairo_rectangle(cr, imgrect->x + lw, imgrect->y + lw, w, h);
    cairo_clip(cr);
    cairo_translate(cr, rect->x, rect->y);
    cairo_push_group(cr);

    cairo_save(cr);
    if (args->inset_draw_ticks)
        y = 0.5*tl;

    if (olw > 0.0) {
        if (args->inset_draw_ticks) {
            draw_line_outline(cr,
                              xcentre - 0.5*length, 0.0,
                              xcentre - 0.5*length, tl + lw,
                              outcolour, lw, olw);
            draw_line_outline(cr,
                              xcentre + 0.5*length, 0.0,
                              xcentre + 0.5*length, tl + lw,
                              outcolour, lw, olw);
        }
        draw_line_outline(cr,
                          xcentre - 0.5*length, y + 0.5*lw,
                          xcentre + 0.5*length, y + 0.5*lw,
                          outcolour, lw, olw);

        if (args->inset_draw_text_above)
            y = -2.0*lw;
        else if (args->inset_draw_ticks)
            y = tl + 2.0*lw;
        else
            y = 2.0*lw;

        if (args->inset_draw_label) {
            cairo_save(cr);
            format_layout(layout, &logical, s, "%s", args->inset_length);
            /* We need ink rectangle to position labels with no ink below
             * baseline, such as 100 nm, as expected. */
            pango_layout_get_extents(layout, &ink, NULL);
            if (args->inset_draw_text_above)
                y -= (ink.y + ink.height)/pangoscale;
            cairo_move_to(cr, xcentre - 0.5*ink.width/pangoscale, y);
            draw_text_outline(cr, layout, outcolour, olw);
            cairo_restore(cr);
        }
    }

    y = 0.5*lw;
    if (args->inset_draw_ticks) {
        y = 0.5*tl;
        cairo_move_to(cr, xcentre - 0.5*length, 0.0);
        cairo_rel_line_to(cr, 0.0, tl + lw);
        cairo_move_to(cr, xcentre + 0.5*length, 0.0);
        cairo_rel_line_to(cr, 0.0, tl + lw);
    }
    cairo_move_to(cr, xcentre - 0.5*length, y + 0.5*lw);
    cairo_line_to(cr, xcentre + 0.5*length, y + 0.5*lw);
    cairo_set_line_width(cr, lw);
    set_cairo_source_rgba(cr, colour);
    cairo_stroke(cr);
    cairo_restore(cr);

    if (args->inset_draw_text_above)
        y = -2.0*lw;
    else if (args->inset_draw_ticks)
        y = tl + 2.0*lw;
    else
        y = 2.0*lw;

    if (args->inset_draw_label) {
        cairo_save(cr);
        format_layout(layout, &logical, s, "%s", args->inset_length);
        /* We need ink rectangle to position labels with no ink below baseline,
         * such as 100 nm, as expected. */
        pango_layout_get_extents(layout, &ink, NULL);
        if (args->inset_draw_text_above)
            y -= (ink.y + ink.height)/pangoscale;
        cairo_move_to(cr, xcentre - 0.5*ink.width/pangoscale, y);
        draw_text(cr, layout, colour);
        cairo_restore(cr);
    }
    cairo_pop_group_to_source(cr);
    /* Unlike cairo_set_source_rgb() vs cairo_set_source_rgba(), this does
     * make a difference. */
    if (colour->a < 1.0 - 1e-14)
        cairo_paint_with_alpha(cr, colour->a);
    else
        cairo_paint(cr);

    cairo_restore(cr);
}

static void
draw_title(const ImgExportArgs *args,
           const ImgExportSizes *sizes,
           PangoLayout *layout,
           GString *s,
           cairo_t *cr)
{
    const ImgExportEnv *env = args->env;
    const ImgExportRect *rect = &sizes->title;
    PangoRectangle logical;
    GwySIValueFormat *vf = sizes->vf_fmruler;
    const GwyRGBA *color = &args->linetext_color;
    gdouble fs = sizes->sizes.font_size;
    gdouble gap = 0.0;

    if (args->title_type == IMGEXPORT_TITLE_NONE)
        return;

    if (args->title_type == IMGEXPORT_TITLE_FMSCALE)
        gap = fs*args->title_gap;

    cairo_save(cr);
    cairo_translate(cr, rect->x + gap, rect->y);
    set_cairo_source_rgba(cr, color);
    if (args->units_in_title && strlen(vf->units))
        format_layout(layout, &logical, s, "%s [%s]", env->title, vf->units);
    else
        format_layout(layout, &logical, s, "%s", env->title);
    cairo_move_to(cr, 0.0, 0.0);
    if (args->title_type == IMGEXPORT_TITLE_FMSCALE)
        cairo_rotate(cr, -0.5*G_PI);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
}

static void
draw_mask_legend(const ImgExportArgs *args,
                 const ImgExportSizes *sizes,
                 PangoLayout *layout,
                 GString *s,
                 cairo_t *cr)
{
    const ImgExportEnv *env = args->env;
    const ImgExportRect *rect = &sizes->maskkey;
    const GwyRGBA *color = &args->linetext_color;
    PangoRectangle logical;
    gdouble fs = sizes->sizes.font_size;
    gdouble lw = sizes->sizes.line_width;
    gdouble h, hgap, vgap, yoff;

    if (!args->draw_mask || !args->draw_maskkey || !env->mask)
        return;

    h = 1.5*fs + 2.0*lw;    /* Match fmscale width */
    vgap = fs*args->maskkey_gap;
    hgap = 0.5*h;

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y + vgap);
    cairo_rectangle(cr, 0.5*lw, 0.5*lw, 1.4*h - lw, h - lw);
    set_cairo_source_rgba(cr, &env->mask_colour);
    cairo_fill_preserve(cr);
    set_cairo_source_rgba(cr, color);
    cairo_set_line_width(cr, lw);
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_save(cr);
    format_layout(layout, &logical, s, "%s", args->mask_key);
    yoff = 0.5*(logical.height/pangoscale - h);
    cairo_translate(cr, rect->x + 1.4*h + hgap, rect->y + vgap - yoff);
    set_cairo_source_rgba(cr, color);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
}

static void
draw_fmgrad(const ImgExportArgs *args,
            const ImgExportSizes *sizes,
            cairo_t *cr)
{
    const ImgExportEnv *env = args->env;
    const ImgExportRect *rect = &sizes->fmgrad;
    const GwyRGBA *color = &args->linetext_color;
    const GwyGradientPoint *points;
    cairo_pattern_t *pat;
    gint npoints, i;
    gdouble lw = sizes->sizes.line_width;
    gboolean inverted = env->fm_inverted;
    gdouble w = rect->w - 2.0*lw;
    gdouble h = rect->h - 2.0*lw;

    if (args->ztype != IMGEXPORT_VALUE_FMSCALE)
        return;

    if (inverted)
        pat = cairo_pattern_create_linear(0.0, lw, 0.0, lw + h);
    else
        pat = cairo_pattern_create_linear(0.0, lw + h, 0.0, lw);

    /* We don't get here in grey16 export mode so we don't care. */
    points = gwy_gradient_get_points(env->gradient, &npoints);
    for (i = 0; i < npoints; i++) {
        const GwyGradientPoint *gpt = points + i;
        const GwyRGBA *ptcolor = &gpt->color;

        cairo_pattern_add_color_stop_rgb(pat, gpt->x,
                                         ptcolor->r, ptcolor->g, ptcolor->b);
    }
    cairo_pattern_set_filter(pat, CAIRO_FILTER_BILINEAR);

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    cairo_rectangle(cr, lw, lw, w, h);
    cairo_clip(cr);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_pattern_destroy(pat);

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, color);
    cairo_set_line_width(cr, lw);
    cairo_rectangle(cr, 0.5*lw, 0.5*lw, w + lw, h + lw);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void
draw_fmruler(const ImgExportArgs *args,
             const ImgExportSizes *sizes,
             PangoLayout *layout,
             GString *s,
             cairo_t *cr)
{
    const ImgExportEnv *env = args->env;
    const ImgExportRect *rect = &sizes->fmruler;
    const GwyRGBA *color = &args->linetext_color;
    const RulerTicks *ticks = &sizes->fmruler_ticks;
    GwySIValueFormat *vf = sizes->vf_fmruler;
    gdouble lw = sizes->sizes.line_width;
    gdouble tl = sizes->sizes.tick_length;
    gdouble uw = sizes->fmruler_units_width;
    gdouble z, bs, scale, yimg, min, max, real, w, yoff;
    PangoRectangle logical, ink;
    GArray *mticks;
    guint nticks, i;

    if (args->ztype != IMGEXPORT_VALUE_FMSCALE)
        return;

    min = env->fm_min/vf->magnitude;
    max = env->fm_max/vf->magnitude;
    real = max - min;

    /* Draw the edge ticks first */
    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, color);
    cairo_set_line_width(cr, lw);
    cairo_move_to(cr, 0.0, 0.5*lw);
    cairo_rel_line_to(cr, tl, 0.0);
    cairo_move_to(cr, 0.0, rect->h - 0.5*lw);
    cairo_rel_line_to(cr, tl, 0.0);
    cairo_stroke(cr);
    cairo_restore(cr);

    if (env->has_presentation)
        return;

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, color);
    if (args->units_in_title)
        format_layout_numeric(args, layout, &logical, s,
                              "%.*f", vf->precision, max);
    else
        format_layout_numeric(args, layout, &logical, s,
                              "%.*f %s", vf->precision, max, vf->units);
    w = logical.width/pangoscale;
    pango_layout_get_extents(layout, &ink, NULL);
    yoff = (logical.height - (ink.height + ink.y))/pangoscale;
    gwy_debug("max '%s' (%g x %g)", s->str, w, logical.height/pangoscale);
    cairo_move_to(cr, rect->w - w, lw - 0.5*yoff);
    pango_cairo_show_layout(cr, layout);
    format_layout_numeric(args, layout, &logical, s,
                          "%.*f", vf->precision, min);
    w = logical.width/pangoscale;
    gwy_debug("min '%s' (%g x %g)",
              s->str, logical.width/pangoscale, logical.height/pangoscale);
    cairo_move_to(cr,
                  rect->w - uw - w, rect->h - lw - logical.height/pangoscale);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);

    if (real < 1e-14)
        return;

    scale = (rect->h - lw)/real;
    bs = ticks->step*ticks->base;

    mticks = g_array_new(FALSE, FALSE, sizeof(gdouble));
    for (z = ticks->from; z <= ticks->to + 1e-14*bs; z += bs)
        g_array_append_val(mticks, z);
    nticks = mticks->len;

    if (env->fm_rangetype == GWY_LAYER_BASIC_RANGE_ADAPT
        && env->fm_min < env->fm_max) {
        gdouble *td;

        g_array_set_size(mticks, 2*nticks);
        td = (gdouble*)mticks->data;
        for (i = 0; i < nticks; i++)
            td[i] *= vf->magnitude;
        gwy_draw_data_field_map_adaptive(env->dfield, td, td + nticks, nticks);
        for (i = 0; i < nticks; i++)
            td[i] = ticks->from + (ticks->to - ticks->from)*td[i + nticks];

        g_array_set_size(mticks, nticks);
    }

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, color);
    cairo_set_line_width(cr, lw);
    for (i = 0; i < nticks; i++) {
        z = g_array_index(mticks, gdouble, i);
        yimg = (max - z)*scale + lw;
        if (env->fm_rangetype == GWY_LAYER_BASIC_RANGE_ADAPT) {
            if (yimg <= lw || yimg + lw >= rect->h)
                continue;
        }
        else {
            if (yimg <= sizes->fmruler_label_height + 4.0*lw
                || yimg + sizes->fmruler_label_height + 4.0*lw >= rect->h)
                continue;
        }

        cairo_move_to(cr, 0.0, yimg);
        cairo_rel_line_to(cr, tl, 0.0);
    };
    cairo_stroke(cr);
    cairo_restore(cr);

    if (env->fm_rangetype == GWY_LAYER_BASIC_RANGE_ADAPT) {
        g_array_free(mticks, TRUE);
        return;
    }

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, color);
    for (i = 0; i < nticks; i++) {
        z = g_array_index(mticks, gdouble, i);
        z = fixzero(z);
        yimg = (max - z)*scale + lw;
        if (yimg <= sizes->fmruler_label_height + 4.0*lw
            || yimg + 2.0*sizes->fmruler_label_height + 4.0*lw >= rect->h)
            continue;

        format_layout_numeric(args, layout, &logical, s,
                              "%.*f", vf->precision, z);
        w = logical.width/pangoscale;
        cairo_move_to(cr, rect->w - uw - w, yimg - 0.5*yoff);
        pango_cairo_show_layout(cr, layout);
    };
    cairo_restore(cr);

    g_array_free(mticks, TRUE);
}

static const ImgExportSelectionType*
find_selection_type(const ImgExportArgs *args,
                    const gchar *name,
                    GwySelection **psel)
{
    const ImgExportEnv *env = args->env;
    const gchar *typename;
    GwySelection *sel;
    gchar *key;
    guint i;

    if (psel)
        *psel = NULL;

    if (!strlen(name))
        return NULL;

    key = g_strdup_printf("/%d/select/%s", env->id, name);
    sel = GWY_SELECTION(gwy_container_get_object_by_name(env->data, key));
    g_free(key);

    if (psel)
        *psel = sel;

    typename = G_OBJECT_TYPE_NAME(sel);
    for (i = 0; i < G_N_ELEMENTS(known_selections); i++) {
        const ImgExportSelectionType *seltype = known_selections + i;
        if (gwy_strequal(typename, seltype->typename)) {
            return seltype;
        }
    }
    return NULL;
}

static void
draw_selection(const ImgExportArgs *args,
               const ImgExportSizes *sizes,
               PangoLayout *layout,
               GString *s,
               cairo_t *cr)
{
    const ImgExportEnv *env = args->env;
    const ImgExportSelectionType *seltype;
    const ImgExportRect *rect = &sizes->image;
    gdouble lw = sizes->sizes.line_width;
    const GwyRGBA *colour = &args->sel_color;
    GwyDataField *dfield = env->dfield;
    gdouble xreal = gwy_data_field_get_xreal(dfield);
    gdouble yreal = gwy_data_field_get_yreal(dfield);
    gdouble w = rect->w - 2.0*lw;
    gdouble h = rect->h - 2.0*lw;
    gdouble qx = w/xreal;
    gdouble qy = h/yreal;
    GwySelection *sel;

    if (!args->draw_selection)
        return;

    if (!(seltype = find_selection_type(args, args->selection, &sel)))
        return;
    if (!seltype->draw) {
        g_warning("Can't draw %s yet.", seltype->typename);
        return;
    }

    cairo_save(cr);
    cairo_translate(cr, rect->x + lw, rect->y + lw);
    cairo_rectangle(cr, 0.0, 0.0, w, h);
    cairo_clip(cr);
    set_cairo_source_rgb(cr, colour);
    cairo_set_line_width(cr, lw);
    cairo_push_group(cr);
    seltype->draw(args, sizes, sel, qx, qy, layout, s, cr);
    cairo_pop_group_to_source(cr);
    /* Unlike cairo_set_source_rgb() vs cairo_set_source_rgba(), this does
     * make a difference. */
    if (colour->a < 1.0 - 1e-14)
        cairo_paint_with_alpha(cr, colour->a);
    else
        cairo_paint(cr);
    cairo_restore(cr);
}

/* We assume cr is already created for the layout with the correct scale(!). */
static void
image_draw_cairo(const ImgExportArgs *args,
                 const ImgExportSizes *sizes,
                 cairo_t *cr)
{
    PangoLayout *layout;
    GString *s = g_string_new(NULL);

    layout = create_layout(args->font, sizes->sizes.font_size, cr);

    draw_background(args, cr);
    draw_data(args, sizes, cr);
    draw_inset(args, sizes, layout, s, cr);
    draw_selection(args, sizes, layout, s, cr);
    draw_data_frame(args, sizes, cr);
    draw_hruler(args, sizes, layout, s, cr);
    draw_vruler(args, sizes, layout, s, cr);
    draw_fmgrad(args, sizes, cr);
    draw_fmruler(args, sizes, layout, s, cr);
    draw_title(args, sizes, layout, s, cr);
    draw_mask_legend(args, sizes, layout, s, cr);

    g_object_unref(layout);
    g_string_free(s, TRUE);
}

static GdkPixbuf*
render_pixbuf(const ImgExportArgs *args, const gchar *name)
{
    ImgExportSizes *sizes;
    cairo_surface_t *surface;
    GdkPixbuf *pixbuf;
    guchar *imgdata, *pixels;
    guint xres, yres, imgrowstride, pixrowstride, i, j;
    gboolean can_transp = args->env->format->supports_transparency;
    gboolean want_transp = args->transparent_bg;
    gboolean transparent_bg = (can_transp && want_transp);
    cairo_format_t imgformat;
    cairo_t *cr;

    gwy_debug("format name %s", name);
    sizes = calculate_sizes(args, name);
    g_return_val_if_fail(sizes, FALSE);
    surface = create_surface(name, NULL, sizes->canvas.w, sizes->canvas.h,
                             transparent_bg);
    cr = cairo_create(surface);
    image_draw_cairo(args, sizes, cr);
    cairo_surface_flush(surface);
    cairo_destroy(cr);

    imgdata = cairo_image_surface_get_data(surface);
    xres = cairo_image_surface_get_width(surface);
    yres = cairo_image_surface_get_height(surface);
    imgrowstride = cairo_image_surface_get_stride(surface);
    imgformat = cairo_image_surface_get_format(surface);
    if (transparent_bg) {
        g_return_val_if_fail(imgformat == CAIRO_FORMAT_ARGB32, NULL);
    }
    else {
        g_return_val_if_fail(imgformat == CAIRO_FORMAT_RGB24, NULL);
    }
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, transparent_bg, 8, xres, yres);
    pixrowstride = gdk_pixbuf_get_rowstride(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);

    /* Things can get a bit confusing here due to the heavy impedance matching
     * necessary.
     *
     * Byte order:
     * (1) GdkPixbuf is endian-independent.  The order is always R, G, B[, A],
     *     i.e. it stores individual components as bytes, not 24bit or 32bit
     *     integers.  If seen as an RGB[A] integer, it is always big-endian.
     * (2) Cairo is endian-dependent.  Pixel is a native-endian 32bit integer
     *     with alpha in the high 8 bits (if present) and then R, G, B from the
     *     highest to lowest remaining bits.
     *
     * Alpha:
     * (A) GdkPixbuf uses non-premultiplied alpha (as most image formats,
     *     except TIFF apparently).
     * (B) Cairo uses premultiplied alpha.
     **/
    for (i = 0; i < yres; i++) {
        const guchar *p = imgdata + i*imgrowstride;
        guchar *q = pixels + i*pixrowstride;

        if (G_BYTE_ORDER == G_LITTLE_ENDIAN) {
            if (transparent_bg) {
                /* Convert (A*B, A*G, A*R, A) to (R, G, B, A). */
                for (j = xres; j; j--, p += 4, q += 4) {
                    guint a = *(q + 3) = *(p + 3);
                    if (a == 0xff) {
                        *q = *(p + 2);
                        *(q + 1) = *(p + 1);
                        *(q + 2) = *p;
                    }
                    else if (a == 0x00) {
                        *q = *(q + 1) = *(q + 2) = 0;
                    }
                    else {
                        /* This is the same unpremultiplication formula as
                         * Cairo uses. */
                        *q = (*(p + 2)*0xff + a/2)/a;
                        *(q + 1) = (*(p + 1)*0xff + a/2)/a;
                        *(q + 2) = ((*p)*0xff + a/2)/a;
                    }
                }
            }
            else {
                /* Convert (B, G, R, unused) to (R, G, B). */
                for (j = xres; j; j--, p += 4, q += 3) {
                    *q = *(p + 2);
                    *(q + 1) = *(p + 1);
                    *(q + 2) = *p;
                }
            }
        }
        else {
            if (transparent_bg) {
                /* Convert (A, A*R, A*G, A*B) to (R, G, B, A). */
                for (j = xres; j; j--, p += 4, q += 4) {
                    guint a = *(q + 3) = *p;
                    if (a == 0xff) {
                        *q = *(p + 1);
                        *(q + 1) = *(p + 2);
                        *(q + 2) = *(p + 3);
                    }
                    else if (a == 0x00) {
                        *q = *(q + 1) = *(q + 2) = 0;
                    }
                    else {
                        /* This is the same unpremultiplication formula as
                         * Cairo uses. */
                        *q = (*(p + 1)*0xff + a/2)/a;
                        *(q + 1) = (*(p + 2)*0xff + a/2)/a;
                        *(q + 2) = (*(p + 3)*0xff + a/2)/a;
                    }
                }
            }
            else {
                /* Convert (unused, R, G, B) to (R, G, B). */
                for (j = xres; j; j--, p += 4, q += 3) {
                    *q = *(p + 1);
                    *(q + 1) = *(p + 2);
                    *(q + 2) = *(p + 3);
                }
            }
        }
    }

    cairo_surface_destroy(surface);
    destroy_sizes(sizes);

    return pixbuf;
}

/* Try to ensure the preview looks at least a bit like the final rendering.
 * Slight sizing issues can be forgiven but we must not change tick step and
 * tick label precision between preview and final rendering. */
static void
preview(ImgExportControls *controls)
{
    ImgExportArgs *args = controls->args;
    ImgExportArgs previewargs;
    ImgExportSizes *sizes;
    gdouble zoomcorr;
    gboolean is_vector;
    guint width, height, iter;
    GdkPixbuf *pixbuf = NULL;

    previewargs = *args;
    controls->args = &previewargs;
    is_vector = !!args->env->format->write_vector;

    if (previewargs.mode == IMGEXPORT_MODE_GREY16) {
        previewargs.xytype = IMGEXPORT_LATERAL_NONE;
        previewargs.ztype = IMGEXPORT_VALUE_NONE;
        previewargs.title_type = IMGEXPORT_TITLE_NONE;
        previewargs.sizes.line_width = 0.0;
        previewargs.draw_mask = FALSE;
        previewargs.draw_maskkey = FALSE;
        previewargs.draw_selection = FALSE;
        previewargs.interpolation = GWY_INTERPOLATION_ROUND;
    }

    sizes = calculate_sizes(&previewargs, "png");
    g_return_if_fail(sizes);
    /* Make all things in the preview scale. */
    previewargs.scale_font = TRUE;
    zoomcorr = PREVIEW_SIZE/MAX(sizes->canvas.w, sizes->canvas.h);
    destroy_sizes(sizes);
    previewargs.zoom *= zoomcorr;
    if (!args->scale_font) {
        if (is_vector)
            scale_sizes(&previewargs.sizes, 1.0/mm2pt/previewargs.pxwidth);
        else
            scale_sizes(&previewargs.sizes, 1.0/args->zoom);
    }

    for (iter = 0; iter < 4; iter++) {
        GWY_OBJECT_UNREF(pixbuf);
        pixbuf = render_pixbuf(&previewargs, "png");
        /* The sizes may be way off when the fonts are huge compared to the
         * image and so on.  Try to correct that and render again. */
        width = gdk_pixbuf_get_width(pixbuf);
        height = gdk_pixbuf_get_height(pixbuf);
        zoomcorr = (gdouble)PREVIEW_SIZE/MAX(width, height);
        gwy_debug("zoomcorr#%u %g", iter, zoomcorr);
        /* This is a big tolerance but we are almost always several percents
         * off so don't make much fuss about it. */
        if (fabs(log(zoomcorr)) < 0.05)
            break;

        previewargs.zoom *= pow(zoomcorr, 0.92);
    }

    gtk_image_set_from_pixbuf(GTK_IMAGE(controls->preview), pixbuf);
    g_object_unref(pixbuf);

    controls->args = args;
}

static gboolean
preview_gsource(gpointer user_data)
{
    ImgExportControls *controls = (ImgExportControls*)user_data;
    controls->sid = 0;
    preview(controls);
    return FALSE;
}

static void
update_preview(ImgExportControls *controls)
{
    /* create preview if instant updates are on */
    if (!controls->in_update && !controls->sid) {
        controls->sid = g_idle_add_full(G_PRIORITY_LOW, preview_gsource,
                                        controls, NULL);
    }
}

static gdouble
pxwidth_to_ppi(gdouble pxwidth)
{
    return 25.4/pxwidth;
}

static gdouble
ppi_to_pxwidth(gdouble pxwidth)
{
    return 25.4/pxwidth;
}

static void
zoom_changed(ImgExportControls *controls)
{
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;

    args->zoom = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->zoom));
    if (controls->in_update)
        return;

    g_return_if_fail(!env->format->write_vector);
    controls->in_update = TRUE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->width),
                             GWY_ROUND(args->zoom*env->xres));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->height),
                             GWY_ROUND(args->zoom*env->yres));
    controls->in_update = FALSE;

    update_preview(controls);
}

static void
width_changed_vector(ImgExportControls *controls)
{
    gdouble width = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->width));
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;
    gdouble pxwidth = width/env->xres;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->height),
                             pxwidth*env->yres);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->pxwidth), pxwidth);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->ppi),
                             pxwidth_to_ppi(pxwidth));
    controls->in_update = FALSE;

    update_preview(controls);
}

static void
width_changed_pixmap(ImgExportControls *controls)
{
    gdouble width = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->width));
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;
    gdouble zoom = width/env->xres;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->zoom), zoom);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->height),
                             GWY_ROUND(zoom*env->yres));
    controls->in_update = FALSE;

    update_preview(controls);
}

static void
height_changed_vector(ImgExportControls *controls)
{
    gdouble height = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->height));
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;
    gdouble pxwidth = height/env->yres;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->width),
                             pxwidth*env->xres);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->pxwidth), pxwidth);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->ppi),
                             pxwidth_to_ppi(pxwidth));
    controls->in_update = FALSE;

    update_preview(controls);
}

static void
height_changed_pixmap(ImgExportControls *controls)
{
    gdouble height = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->height));
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;
    gdouble zoom = height/env->yres;

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->zoom), zoom);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->width),
                             GWY_ROUND(zoom*env->xres));
    controls->in_update = FALSE;

    update_preview(controls);
}

static void
pxwidth_changed(ImgExportControls *controls)
{
    gdouble pxwidth = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->pxwidth));
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;

    args->pxwidth = pxwidth;
    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->width),
                             pxwidth*env->xres);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->height),
                             pxwidth*env->yres);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->ppi),
                             pxwidth_to_ppi(pxwidth));
    controls->in_update = FALSE;

    update_preview(controls);
}

static void
ppi_changed(ImgExportControls *controls)
{
    gdouble ppi = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->ppi));
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;
    gdouble pxwidth = ppi_to_pxwidth(ppi);

    if (controls->in_update)
        return;

    controls->in_update = TRUE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->width),
                             pxwidth*env->xres);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->height),
                             pxwidth*env->yres);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->pxwidth), pxwidth);
    controls->in_update = FALSE;

    update_preview(controls);
}

static void
font_changed(ImgExportControls *controls,
             GtkFontButton *button)
{
    ImgExportArgs *args = controls->args;
    const gchar *full_font = gtk_font_button_get_font_name(button);
    const gchar *size_pos = strrchr(full_font, ' ');
    gchar *end;
    gdouble size;

    if (!size_pos) {
        g_warning("Cannot parse font description `%s' into name and size.",
                  full_font);
        return;
    }
    size = g_ascii_strtod(size_pos+1, &end);
    if (end == size_pos+1) {
        g_warning("Cannot parse font description `%s' into name and size.",
                  full_font);
        return;
    }

    /* XXX: full_font obtained this way can have a trailing comma when the
     * comma is needed when specifying the font, so we preserve it in the font
     * name. */
    g_free(args->font);
    args->font = g_strndup(full_font, size_pos-full_font);
    g_strchomp(args->font);
    if (size > 0.0)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->font_size), size);

    update_preview(controls);
}

static void
update_selected_font(ImgExportControls *controls)
{
    ImgExportArgs *args = controls->args;
    gchar *full_font;
    gdouble font_size = args->sizes.font_size;

    full_font = g_strdup_printf("%s %.1f", controls->args->font, font_size);
    gtk_font_button_set_font_name(GTK_FONT_BUTTON(controls->font), full_font);
    g_free(full_font);
}

static void
font_size_changed(ImgExportControls *controls,
                  GtkAdjustment *adj)
{
    controls->args->sizes.font_size = gtk_adjustment_get_value(adj);
    update_selected_font(controls);
    update_preview(controls);
}

static void
line_width_changed(ImgExportControls *controls,
                   GtkAdjustment *adj)
{
    controls->args->sizes.line_width = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
border_width_changed(ImgExportControls *controls,
                     GtkAdjustment *adj)
{
    controls->args->sizes.border_width = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
tick_length_changed(ImgExportControls *controls,
                    GtkAdjustment *adj)
{
    controls->args->sizes.tick_length = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
scale_font_changed(ImgExportControls *controls,
                   GtkToggleButton *check)
{
    ImgExportArgs *args = controls->args;

    args->scale_font = gtk_toggle_button_get_active(check);
    update_selected_font(controls);
    update_preview(controls);
}

static void
decimal_comma_changed(ImgExportControls *controls,
                      GtkToggleButton *check)
{
    ImgExportArgs *args = controls->args;

    args->decomma = gtk_toggle_button_get_active(check);
    update_preview(controls);
}

static void
update_colour_controls_sensitivity(ImgExportColourControls *colourctrl,
                                   gboolean sens)
{
    gtk_widget_set_sensitive(colourctrl->label, sens);
    gtk_widget_set_sensitive(colourctrl->button, sens);
    gtk_widget_set_sensitive(colourctrl->setblack, sens);
    gtk_widget_set_sensitive(colourctrl->setwhite, sens);
}

static void
update_basic_sensitivity(ImgExportControls *controls)
{
    /* Background is transparent if it is enabled *and* supported by the
     * format -- which means controls->transparent_bg is offered. */
    gboolean bg_is_transp = (controls->args->transparent_bg
                             && controls->transparent_bg);

    update_colour_controls_sensitivity(&controls->bg_colour, !bg_is_transp);
}

static void
transparent_bg_changed(ImgExportControls *controls,
                       GtkToggleButton *check)
{
    ImgExportArgs *args = controls->args;

    args->transparent_bg = gtk_toggle_button_get_active(check);
    update_basic_sensitivity(controls);
    update_preview(controls);
}

static void
select_colour(ImgExportControls *controls,
              GwyColorButton *button)
{
    GtkColorSelection *colorsel;
    GtkWindow *parent;
    GtkWidget *dialog, *selector;
    GdkColor gdkcolor;
    GwyRGBA *target;
    gint response;

    target = (GwyRGBA*)g_object_get_data(G_OBJECT(button), "target");
    g_return_if_fail(target);

    gwy_rgba_to_gdk_color(target, &gdkcolor);

    dialog = gtk_color_selection_dialog_new(_("Select Color"));
    selector = GTK_COLOR_SELECTION_DIALOG(dialog)->colorsel;
    colorsel = GTK_COLOR_SELECTION(selector);
    gtk_color_selection_set_current_color(colorsel, &gdkcolor);
    gtk_color_selection_set_has_palette(colorsel, FALSE);
    gtk_color_selection_set_has_opacity_control(colorsel, FALSE);

    parent = GTK_WINDOW(controls->dialog);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    gtk_window_set_modal(parent, FALSE);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_color_selection_get_current_color(colorsel, &gdkcolor);
    gtk_widget_destroy(dialog);
    gtk_window_set_modal(parent, TRUE);

    if (response != GTK_RESPONSE_OK)
        return;

    gwy_rgba_from_gdk_color(target, &gdkcolor);  /* OK, doesn't touch alpha */
    gwy_color_button_set_color(button, target);
    update_preview(controls);
}

static void
set_colour_to(ImgExportControls *controls,
              GObject *button)
{
    GwyColorButton *colourbutton;
    const GwyRGBA *settocolour;
    GwyRGBA *target;

    target = (GwyRGBA*)g_object_get_data(button, "target");
    settocolour = (const GwyRGBA*)g_object_get_data(button, "settocolour");
    colourbutton = (GwyColorButton*)g_object_get_data(button, "colourbutton");
    g_return_if_fail(target);
    g_return_if_fail(colourbutton);

    *target = *settocolour;
    gwy_color_button_set_color(colourbutton, target);
    update_preview(controls);
}

static GtkWidget*
create_colour_button(const gchar *label_text,
                     GtkSizeGroup *sizegroup,
                     const GwyRGBA *rgba,
                     GwyRGBA *target,
                     GtkWidget *colourbutton,
                     ImgExportControls *controls)
{
    GtkWidget *button, *image;
    GdkPixbuf *pixbuf;
    gint width, height;
    guint32 pixel;

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
    pixel = gwy_rgba_to_pixbuf_pixel(rgba);
    gdk_pixbuf_fill(pixbuf, pixel);

    image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);

    button = gtk_button_new_with_label(label_text);
    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_size_group_add_widget(sizegroup, button);
    g_object_set_data(G_OBJECT(button), "target", target);
    g_object_set_data(G_OBJECT(button), "settocolour", (gpointer)rgba);
    g_object_set_data(G_OBJECT(button), "colourbutton", colourbutton);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(set_colour_to), controls);

    return button;
}

static void
create_colour_control(GtkTable *table,
                      guint row,
                      const gchar *name,
                      GwyRGBA *target,
                      ImgExportControls *controls,
                      ImgExportColourControls *colourctrl)
{
    GtkWidget *label, *colour, *setblack, *setwhite, *hbox;
    GtkSizeGroup *coloursize;
    gboolean do_unref = FALSE;
    gint ncols;

    hbox = gtk_hbox_new(FALSE, 2);
    g_object_get(table, "n-columns", &ncols, NULL);
    gtk_table_attach(table, hbox,
                     0, ncols-1, row, row+1, GTK_FILL, 0, 0, 0);

    coloursize = g_object_get_data(G_OBJECT(table), "colour-size-group");
    if (!coloursize) {
        coloursize = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
        g_object_set_data(G_OBJECT(table), "colour-size-group", coloursize);
        do_unref = TRUE;
    }

    label = gtk_label_new_with_mnemonic(name);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    colour = gwy_color_button_new_with_color(target);

    setwhite = create_colour_button(_("White"), coloursize, &white, target,
                                    colour, controls);
    gtk_box_pack_end(GTK_BOX(hbox), setwhite, FALSE, FALSE, 0);

    setblack = create_colour_button(_("Black"), coloursize, &black, target,
                                    colour, controls);
    gtk_box_pack_end(GTK_BOX(hbox), setblack, FALSE, FALSE, 0);

    gtk_label_set_mnemonic_widget(GTK_LABEL(label), colour);
    gtk_size_group_add_widget(coloursize, colour);
    gwy_color_button_set_use_alpha(GWY_COLOR_BUTTON(colour), FALSE);
    gtk_box_pack_end(GTK_BOX(hbox), colour, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(colour), "target", target);
    g_signal_connect_swapped(colour, "clicked",
                             G_CALLBACK(select_colour), controls);

    colourctrl->label = label;
    colourctrl->button = colour;
    colourctrl->setblack = setblack;
    colourctrl->setwhite = setwhite;

    if (do_unref)
        g_object_unref(coloursize);
}

static void
create_basic_controls(ImgExportControls *controls)
{
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;
    gboolean is_vector = !!env->format->write_vector;
    gboolean can_transp = !!env->format->supports_transparency;
    const gchar *sizeunit;
    GtkWidget *table, *spin;
    GCallback width_cb, height_cb;
    gint row = 0, digits;

    table = controls->table_basic
        = gtk_table_new(15 + 1*is_vector + 1*can_transp, 3, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);

    gtk_table_attach(GTK_TABLE(table),
                     gwy_label_new_header(_("Physical Dimensions")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    if (is_vector) {
        gdouble pxwidth = args->pxwidth;
        gdouble ppi = pxwidth_to_ppi(args->pxwidth);

        controls->pxwidth = gtk_adjustment_new(args->pxwidth, 0.01, 254.0,
                                               0.001, 0.1, 0);
        spin = gwy_table_attach_adjbar(table, row++, _("Pi_xel size:"), "mm",
                                       controls->pxwidth, GWY_HSCALE_LOG);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 3);
        g_signal_connect_swapped(controls->pxwidth, "value-changed",
                                 G_CALLBACK(pxwidth_changed), controls);

        controls->ppi = gtk_adjustment_new(ppi, 0.1, 2540.0, 0.01, 100.0, 0);
        spin = gwy_table_attach_adjbar(table, row++,
                                       _("Pixels per _inch:"), NULL,
                                       controls->ppi, GWY_HSCALE_LOG);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
        g_signal_connect_swapped(controls->ppi, "value-changed",
                                 G_CALLBACK(ppi_changed), controls);

        sizeunit = "mm";
        digits = 1;
        controls->width = gtk_adjustment_new(env->xres*pxwidth, 10.0, 1000.0,
                                             0.1, 10.0, 0);
        controls->height = gtk_adjustment_new(env->yres*pxwidth, 10.0, 1000.0,
                                              0.1, 10.0, 0);
        width_cb = G_CALLBACK(width_changed_vector);
        height_cb = G_CALLBACK(height_changed_vector);
    }
    else {
        gdouble minzoom = 2.0/MIN(env->xres, env->yres);
        gdouble maxzoom = 16384.0/MAX(env->xres, env->yres);
        gdouble w = CLAMP(args->zoom, minzoom, maxzoom)*env->xres;
        gdouble h = CLAMP(args->zoom, minzoom, maxzoom)*env->yres;

        sizeunit = "px";
        digits = 0;
        controls->zoom = gtk_adjustment_new(args->zoom, minzoom, maxzoom,
                                            0.001, 1.0, 0);
        spin = gwy_table_attach_adjbar(table, row++, _("_Zoom:"), NULL,
                                       controls->zoom, GWY_HSCALE_LOG);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 3);
        g_signal_connect_swapped(controls->zoom, "value-changed",
                                 G_CALLBACK(zoom_changed), controls);

        controls->width = gtk_adjustment_new(w, 2.0, 16384.0, 1.0, 10.0, 0);
        controls->height = gtk_adjustment_new(h, 2.0, 16384.0, 1.0, 10.0, 0);
        width_cb = G_CALLBACK(width_changed_pixmap);
        height_cb = G_CALLBACK(height_changed_pixmap);
    }

    spin = gwy_table_attach_adjbar(table, row++, _("_Width:"), sizeunit,
                                   controls->width, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), digits);
    g_signal_connect_swapped(controls->width, "value-changed",
                             G_CALLBACK(width_cb), controls);

    spin = gwy_table_attach_adjbar(table, row++, _("_Height:"), sizeunit,
                                   controls->height, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), digits);
    g_signal_connect_swapped(controls->height, "value-changed",
                             height_cb, controls);

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    gtk_table_attach(GTK_TABLE(table),
                     gwy_label_new_header(_("Parameters")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls->font = gtk_font_button_new();
    gtk_font_button_set_show_size(GTK_FONT_BUTTON(controls->font), FALSE);
    gtk_font_button_set_use_font(GTK_FONT_BUTTON(controls->font), TRUE);
    update_selected_font(controls);
    gwy_table_attach_adjbar(table, row++, _("_Font:"), NULL,
                            GTK_OBJECT(controls->font),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    g_signal_connect_swapped(controls->font, "font-set",
                             G_CALLBACK(font_changed), controls);

    controls->font_size = gtk_adjustment_new(args->sizes.font_size,
                                             1.0, 1024.0, 1.0, 10.0, 0);
    spin = gwy_table_attach_adjbar(GTK_WIDGET(table), row++,
                                   _("_Font size:"), NULL,
                                   controls->font_size, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);
    g_signal_connect_swapped(controls->font_size, "value-changed",
                             G_CALLBACK(font_size_changed), controls);

    controls->line_width = gtk_adjustment_new(args->sizes.line_width,
                                              0.0, 16.0, 0.01, 1.0, 0);
    spin = gwy_table_attach_adjbar(GTK_WIDGET(table), row++,
                                   _("Line t_hickness:"), NULL,
                                   controls->line_width, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_signal_connect_swapped(controls->line_width, "value-changed",
                             G_CALLBACK(line_width_changed), controls);

    controls->border_width = gtk_adjustment_new(args->sizes.border_width,
                                                0.0, 1024.0, 0.1, 1.0, 0);
    spin = gwy_table_attach_adjbar(GTK_WIDGET(table), row++,
                                   _("_Border width:"), NULL,
                                   controls->border_width, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);
    g_signal_connect_swapped(controls->border_width, "value-changed",
                             G_CALLBACK(border_width_changed), controls);

    controls->tick_length = gtk_adjustment_new(args->sizes.tick_length,
                                               0.0, 1024.0, 0.1, 1.0, 0);
    spin = gwy_table_attach_adjbar(GTK_WIDGET(table), row++,
                                   _("_Tick length:"), NULL,
                                   controls->tick_length, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);
    g_signal_connect_swapped(controls->tick_length, "value-changed",
                             G_CALLBACK(tick_length_changed), controls);

    controls->scale_font
        = gtk_check_button_new_with_mnemonic(_("Tie sizes to _data pixels"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->scale_font),
                                 args->scale_font);
    gtk_table_attach(GTK_TABLE(table), controls->scale_font,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls->scale_font, "toggled",
                             G_CALLBACK(scale_font_changed), controls);
    row++;

    controls->decomma
        = gtk_check_button_new_with_mnemonic(_("_Decimal separator "
                                               "is comma"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->decomma),
                                 args->decomma);
    gtk_table_attach(GTK_TABLE(table), controls->decomma,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls->decomma, "toggled",
                             G_CALLBACK(decimal_comma_changed), controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    gtk_table_attach(GTK_TABLE(table),
                     gwy_label_new_header(_("Colors")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    create_colour_control(GTK_TABLE(table), row++,
                          _("_Line and text color:"), &args->linetext_color,
                          controls, &controls->linetext_colour);

    if (can_transp) {
        controls->transparent_bg
            = gtk_check_button_new_with_mnemonic(_("_Transparent background"));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->transparent_bg),
                                     args->transparent_bg);
        gtk_table_attach(GTK_TABLE(table), controls->transparent_bg,
                         0, 2, row, row+1, GTK_FILL, 0, 0, 0);
        g_signal_connect_swapped(controls->transparent_bg, "toggled",
                                 G_CALLBACK(transparent_bg_changed), controls);
        row++;
    }

    create_colour_control(GTK_TABLE(table), row++,
                          _("_Background color:"), &args->bg_color,
                          controls, &controls->bg_colour);

    update_basic_sensitivity(controls);
}

static void
update_lateral_sensitivity(ImgExportControls *controls)
{
    gboolean insetsens = (controls->args->xytype == IMGEXPORT_LATERAL_INSET);
    gboolean hgapsens = (controls->args->inset_pos % 3 != 1);
    GSList *l;
    guint i;

    update_colour_controls_sensitivity(&controls->inset_colour, insetsens);
    update_colour_controls_sensitivity(&controls->inset_outline_colour,
                                       insetsens);
    gwy_table_hscale_set_sensitive(controls->inset_opacity, insetsens);
    gwy_table_hscale_set_sensitive(GTK_OBJECT(controls->inset_length),
                                   insetsens);
    gtk_widget_set_sensitive(controls->inset_draw_ticks, insetsens);
    gtk_widget_set_sensitive(controls->inset_draw_label, insetsens);
    gtk_widget_set_sensitive(controls->inset_draw_text_above, insetsens);
    for (i = 0; i < G_N_ELEMENTS(controls->inset_pos_label); i++)
        gtk_widget_set_sensitive(controls->inset_pos_label[i], insetsens);
    gwy_table_hscale_set_sensitive(controls->inset_xgap, insetsens*hgapsens);
    gwy_table_hscale_set_sensitive(controls->inset_ygap, insetsens);
    for (l = controls->inset_pos; l; l = g_slist_next(l))
        gtk_widget_set_sensitive(GTK_WIDGET(l->data), insetsens);
}

static void
update_value_sensitivity(ImgExportControls *controls)
{
    const ImgExportArgs *args = controls->args;
    gboolean masksens = args->draw_mask && !!args->env->mask;
    gboolean maskkeysens = masksens && args->draw_maskkey;
    gboolean fmsens = (args->ztype == IMGEXPORT_VALUE_FMSCALE);
    gboolean titlesens = (args->title_type != IMGEXPORT_TITLE_NONE);
    gboolean framesens = (args->ztype == IMGEXPORT_VALUE_NONE
                          && (args->xytype == IMGEXPORT_LATERAL_NONE
                              || args->xytype == IMGEXPORT_LATERAL_INSET)
                          && !maskkeysens);

    gwy_table_hscale_set_sensitive(controls->fmscale_gap, fmsens);
    gwy_table_hscale_set_sensitive(controls->fmscale_precision, fmsens);
    gwy_table_hscale_set_sensitive(controls->kilo_threshold,
                                   (fmsens || titlesens));
    gwy_table_hscale_set_sensitive(controls->title_gap, titlesens);
    gtk_widget_set_sensitive(controls->draw_frame, framesens);
    gtk_widget_set_sensitive(controls->draw_maskkey, masksens);
    gwy_table_hscale_set_sensitive(GTK_OBJECT(controls->mask_key), maskkeysens);
    gwy_table_hscale_set_sensitive(controls->maskkey_gap, maskkeysens);
}

static void
inset_xgap_changed(ImgExportControls *controls,
                   GtkAdjustment *adj)
{
    controls->args->inset_xgap = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
inset_ygap_changed(ImgExportControls *controls,
                   GtkAdjustment *adj)
{
    controls->args->inset_ygap = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
inset_outline_width_changed(ImgExportControls *controls,
                            GtkAdjustment *adj)
{
    controls->args->sizes.inset_outline_width = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
inset_opacity_changed(ImgExportControls *controls,
                      GtkAdjustment *adj)
{
    gdouble alpha = gtk_adjustment_get_value(adj);
    controls->args->inset_color.a = alpha;
    controls->args->inset_outline_color.a = alpha;
    update_preview(controls);
}

static void
xytype_changed(G_GNUC_UNUSED GtkToggleButton *toggle,
               ImgExportControls *controls)
{
    controls->args->xytype = gwy_radio_buttons_get_current(controls->xytype);
    update_lateral_sensitivity(controls);
    update_value_sensitivity(controls);    /* For draw_frame */
    update_preview(controls);
}

static void
inset_draw_ticks_changed(ImgExportControls *controls,
                         GtkToggleButton *button)
{
    ImgExportArgs *args = controls->args;

    args->inset_draw_ticks = gtk_toggle_button_get_active(button);
    if (args->xytype == IMGEXPORT_LATERAL_INSET)
        update_preview(controls);
}

static void
inset_draw_label_changed(ImgExportControls *controls,
                         GtkToggleButton *button)
{
    ImgExportArgs *args = controls->args;

    args->inset_draw_label = gtk_toggle_button_get_active(button);
    if (args->xytype == IMGEXPORT_LATERAL_INSET)
        update_preview(controls);
}

static void
inset_draw_text_above_changed(ImgExportControls *controls,
                              GtkToggleButton *button)
{
    ImgExportArgs *args = controls->args;

    args->inset_draw_text_above = gtk_toggle_button_get_active(button);
    if (args->xytype == IMGEXPORT_LATERAL_INSET)
        update_preview(controls);
}

static void
inset_pos_changed(ImgExportControls *controls,
                  GtkToggleButton *button)
{
    ImgExportArgs *args = controls->args;

    if (!gtk_toggle_button_get_active(button))
        return;

    args->inset_pos = gwy_radio_buttons_get_current(controls->inset_pos);
    if (args->xytype == IMGEXPORT_LATERAL_INSET) {
        if (!controls->in_update)
            update_lateral_sensitivity(controls);
        update_preview(controls);
    }
}

static void
inset_length_set_auto(ImgExportControls *controls)
{
    /* Setting an invalid value causes reset to default. */
    gtk_entry_set_text(GTK_ENTRY(controls->inset_length), "");
    gtk_widget_activate(controls->inset_length);
}

static void
inset_length_changed(ImgExportControls *controls,
                     GtkEntry *entry)
{
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;
    GwyDataField *dfield;
    const gchar *text;

    dfield = env->dfield;
    text = gtk_entry_get_text(entry);
    g_free(args->inset_length);
    if (inset_length_ok(dfield, text))
        args->inset_length = g_strdup(text);
    else {
        args->inset_length = scalebar_auto_length(dfield, NULL);
        gtk_entry_set_text(entry, args->inset_length);
    }

    if (args->xytype == IMGEXPORT_LATERAL_INSET)
        update_preview(controls);
}

static void
inset_pos_add(ImgExportControls *controls,
              GtkTable *table, gint row, gint col,
              InsetPosType pos)
{
    GtkWidget *button, *align;

    align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
    gtk_table_attach(table, align, col, col+1, row, row+1, GTK_FILL, 0, 0, 0);
    button = gtk_radio_button_new_with_label(controls->inset_pos, NULL);
    if (pos == controls->args->inset_pos)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    controls->inset_pos = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    g_object_set_qdata(G_OBJECT(button), controls->rb_quark,
                       GUINT_TO_POINTER(pos));
    gtk_container_add(GTK_CONTAINER(align), button);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(inset_pos_changed), controls);
}

static GtkWidget*
create_inset_pos_table(ImgExportControls *controls)
{
    GtkWidget *label;
    GtkTable *table;

    table = GTK_TABLE(gtk_table_new(3, 4, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);

    controls->inset_pos_label[0] = label = gwy_label_new_header(_("Placement"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->inset_pos_label[1] = label = gtk_label_new(_("left"));
    gtk_table_attach(table, label, 1, 2, 0, 1, GTK_FILL, 0, 0, 0);

    controls->inset_pos_label[2] = label = gtk_label_new(_("center"));
    gtk_table_attach(table, label, 2, 3, 0, 1, GTK_FILL, 0, 0, 0);

    controls->inset_pos_label[3] = label = gtk_label_new(_("right"));
    gtk_table_attach(table, label, 3, 4, 0, 1, GTK_FILL, 0, 0, 0);

    controls->rb_quark = g_quark_from_string("gwy-radiobuttons-key");

    controls->inset_pos_label[4] = label = gtk_label_new(_("top"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    inset_pos_add(controls, table, 1, 1, INSET_POS_TOP_LEFT);
    inset_pos_add(controls, table, 1, 2, INSET_POS_TOP_CENTER);
    inset_pos_add(controls, table, 1, 3, INSET_POS_TOP_RIGHT);

    controls->inset_pos_label[5] = label = gtk_label_new(_("bottom"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    inset_pos_add(controls, table, 2, 1, INSET_POS_BOTTOM_LEFT);
    inset_pos_add(controls, table, 2, 2, INSET_POS_BOTTOM_CENTER);
    inset_pos_add(controls, table, 2, 3, INSET_POS_BOTTOM_RIGHT);

    return GTK_WIDGET(table);
}

static void
create_lateral_controls(ImgExportControls *controls)
{
    ImgExportArgs *args = controls->args;
    GtkWidget *table, *label, *button, *postable, *spin;
    gint row = 0;

    table = controls->table_lateral = gtk_table_new(16, 3, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);

    label = gwy_label_new_header(_("Lateral scale"));
    gtk_table_attach(GTK_TABLE(table), label, 0, 3, row, row+1,
                     GTK_FILL, 0, 0, 0);
    row++;

    controls->xytype
        = gwy_radio_buttons_create(lateral_types, G_N_ELEMENTS(lateral_types),
                                   G_CALLBACK(xytype_changed), controls,
                                   args->xytype);
    row = gwy_radio_buttons_attach_to_table(controls->xytype, GTK_TABLE(table),
                                            2, row);

    controls->inset_length = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(controls->inset_length), 8);
    gtk_entry_set_text(GTK_ENTRY(controls->inset_length),
                       controls->args->inset_length);
    gwy_widget_set_activate_on_unfocus(controls->inset_length, TRUE);
    gwy_table_attach_adjbar(table, row, _("_Length:"), NULL,
                            GTK_OBJECT(controls->inset_length),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    g_signal_connect_swapped(controls->inset_length, "activate",
                             G_CALLBACK(inset_length_changed), controls);

    button = gtk_button_new_with_mnemonic(_("_Auto"));
    g_object_set_data(G_OBJECT(controls->inset_length), "units", button);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(inset_length_set_auto), controls);
    gtk_table_attach(GTK_TABLE(table), button,
                     2, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    postable = create_inset_pos_table(controls);
    gtk_table_attach(GTK_TABLE(table), postable,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls->inset_xgap = gtk_adjustment_new(args->inset_xgap,
                                              0.0, 4.0, 0.01, 0.1, 0);
    gwy_table_attach_adjbar(table, row++, _("Hori_zontal gap:"), NULL,
                            controls->inset_xgap, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls->inset_xgap, "value-changed",
                             G_CALLBACK(inset_xgap_changed), controls);

    controls->inset_ygap = gtk_adjustment_new(args->inset_ygap,
                                              0.0, 2.0, 0.01, 0.1, 0);
    gwy_table_attach_adjbar(table, row++, _("_Vertical gap:"), NULL,
                            controls->inset_ygap, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls->inset_ygap, "value-changed",
                             G_CALLBACK(inset_ygap_changed), controls);

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    label = gwy_label_new_header(_("Options"));
    gtk_table_attach(GTK_TABLE(table), label, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);
    row++;

    create_colour_control(GTK_TABLE(table), row++,
                          _("Colo_r:"), &args->inset_color,
                          controls, &controls->inset_colour);

    create_colour_control(GTK_TABLE(table), row++,
                          _("Out_line color:"), &args->inset_outline_color,
                          controls, &controls->inset_outline_colour);

    controls->inset_outline_width
        = gtk_adjustment_new(args->sizes.inset_outline_width,
                             0.0, 16.0, 0.01, 1.0, 0);
    spin = gwy_table_attach_adjbar(GTK_WIDGET(table), row++,
                                   _("O_utline thickness:"), NULL,
                                   controls->inset_outline_width,
                                   GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_signal_connect_swapped(controls->inset_outline_width, "value-changed",
                             G_CALLBACK(inset_outline_width_changed), controls);

    controls->inset_opacity = gtk_adjustment_new(args->inset_color.a,
                                                 0.0, 1.0, 0.001, 0.1, 0);
    gwy_table_attach_adjbar(table, row++, _("O_pacity:"), NULL,
                            controls->inset_opacity, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls->inset_opacity, "value-changed",
                             G_CALLBACK(inset_opacity_changed), controls);

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls->inset_draw_ticks
        = gtk_check_button_new_with_mnemonic(_("Draw _ticks"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->inset_draw_ticks),
                                 args->inset_draw_ticks);
    gtk_table_attach(GTK_TABLE(table), controls->inset_draw_ticks,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls->inset_draw_ticks, "toggled",
                             G_CALLBACK(inset_draw_ticks_changed), controls);
    row++;

    controls->inset_draw_label
        = gtk_check_button_new_with_mnemonic(_("Draw _label"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->inset_draw_label),
                                 args->inset_draw_label);
    gtk_table_attach(GTK_TABLE(table), controls->inset_draw_label,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls->inset_draw_label, "toggled",
                             G_CALLBACK(inset_draw_label_changed), controls);
    row++;

    controls->inset_draw_text_above
        = gtk_check_button_new_with_mnemonic(_("Draw text _above scale bar"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->inset_draw_text_above),
                                 args->inset_draw_text_above);
    gtk_table_attach(GTK_TABLE(table), controls->inset_draw_text_above,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls->inset_draw_text_above, "toggled",
                             G_CALLBACK(inset_draw_text_above_changed),
                             controls);
    row++;

    update_lateral_sensitivity(controls);
}

static void
interpolation_changed(GtkComboBox *combo,
                      ImgExportControls *controls)
{
    controls->args->interpolation = gwy_enum_combo_box_get_active(combo);
    update_preview(controls);
}

static void
fmscale_gap_changed(ImgExportControls *controls,
                    GtkAdjustment *adj)
{
    controls->args->fmscale_gap = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
fmscale_precision_changed(ImgExportControls *controls,
                          GtkAdjustment *adj)
{
    controls->args->fmscale_precision = gwy_adjustment_get_int(adj);
    update_preview(controls);
}

static void
fix_fmscale_precision_changed(ImgExportControls *controls,
                              GtkToggleButton *toggle)
{
    ImgExportArgs *args = controls->args;

    args->fix_fmscale_precision = gtk_toggle_button_get_active(toggle);
    update_preview(controls);
}

static void
kilo_threshold_changed(ImgExportControls *controls,
                       GtkAdjustment *adj)
{
    controls->args->kilo_threshold = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
fix_kilo_threshold_changed(ImgExportControls *controls,
                           GtkToggleButton *toggle)
{
    ImgExportArgs *args = controls->args;

    args->fix_kilo_threshold = gtk_toggle_button_get_active(toggle);
    update_preview(controls);
}

static void
ztype_changed(G_GNUC_UNUSED GtkToggleButton *toggle,
              ImgExportControls *controls)
{
    controls->args->ztype = gwy_radio_buttons_get_current(controls->ztype);
    update_value_sensitivity(controls);
    update_preview(controls);
}

static void
draw_frame_changed(ImgExportControls *controls,
                  GtkToggleButton *button)
{
    ImgExportArgs *args = controls->args;

    args->draw_frame = gtk_toggle_button_get_active(button);
    update_preview(controls);
}

static void
draw_mask_changed(ImgExportControls *controls,
                  GtkToggleButton *button)
{
    ImgExportArgs *args = controls->args;

    args->draw_mask = gtk_toggle_button_get_active(button);
    update_value_sensitivity(controls);
    update_preview(controls);
}

static void
draw_maskkey_changed(ImgExportControls *controls,
                     GtkToggleButton *button)
{
    ImgExportArgs *args = controls->args;

    args->draw_maskkey = gtk_toggle_button_get_active(button);
    update_value_sensitivity(controls);
    update_preview(controls);
}

static void
mask_key_changed(ImgExportControls *controls,
                 GtkEntry *entry)
{
    ImgExportArgs *args = controls->args;

    g_free(args->mask_key);
    args->mask_key = g_strdup(gtk_entry_get_text(entry));
    update_preview(controls);
}

static void
maskkey_gap_changed(ImgExportControls *controls,
                    GtkAdjustment *adj)
{
    controls->args->maskkey_gap = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
title_type_changed(GtkComboBox *combo, ImgExportControls *controls)
{
    controls->args->title_type = gwy_enum_combo_box_get_active(combo);
    update_value_sensitivity(controls);
    update_preview(controls);
}

static void
title_gap_changed(ImgExportControls *controls,
                  GtkAdjustment *adj)
{
    controls->args->title_gap = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
units_in_title_changed(ImgExportControls *controls,
                       GtkToggleButton *button)
{
    ImgExportArgs *args = controls->args;

    args->units_in_title = gtk_toggle_button_get_active(button);
    update_preview(controls);
}

static void
create_value_controls(ImgExportControls *controls)
{
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;
    GtkWidget *table, *label, *check;
    gint row = 0;

    table = controls->table_value = gtk_table_new(16, 4, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);

    gtk_table_attach(GTK_TABLE(table), gwy_label_new_header(_("Image")),
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Interpolation type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    if (controls->args->env->format->write_vector) {
        /* Vector formats can only handle these two. */
        if (args->interpolation != GWY_INTERPOLATION_ROUND)
            args->interpolation = GWY_INTERPOLATION_LINEAR;

        controls->interpolation
            = gwy_enum_combo_box_newl(G_CALLBACK(interpolation_changed),
                                      controls,
                                      args->interpolation,
                                      _("Round"), GWY_INTERPOLATION_ROUND,
                                      _("Linear"), GWY_INTERPOLATION_LINEAR,
                                      NULL);
    }
    else {
        controls->interpolation
            = gwy_enum_combo_box_new(gwy_interpolation_type_get_enum(), -1,
                                     G_CALLBACK(interpolation_changed),
                                     controls, args->interpolation, TRUE);
    }
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls->interpolation);
    gtk_table_attach(GTK_TABLE(table), controls->interpolation,
                     1, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls->draw_frame
        = check = gtk_check_button_new_with_mnemonic(_("Draw _frame"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), args->draw_frame);
    gtk_table_attach(GTK_TABLE(table), check, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(check, "toggled",
                             G_CALLBACK(draw_frame_changed), controls);
    row++;

    controls->draw_mask
        = check = gtk_check_button_new_with_mnemonic(_("Draw _mask"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), args->draw_mask);
    gtk_widget_set_sensitive(check, !!env->mask);
    gtk_table_attach(GTK_TABLE(table), check, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(check, "toggled",
                             G_CALLBACK(draw_mask_changed), controls);
    row++;

    controls->draw_maskkey
        = check = gtk_check_button_new_with_mnemonic(_("Draw mask _legend"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), args->draw_maskkey);
    gtk_widget_set_sensitive(check, !!env->mask && args->draw_mask);
    gtk_table_attach(GTK_TABLE(table), check, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(check, "toggled",
                             G_CALLBACK(draw_maskkey_changed), controls);
    row++;

    controls->mask_key = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(controls->mask_key), 8);
    gtk_entry_set_text(GTK_ENTRY(controls->mask_key), controls->args->mask_key);
    gwy_widget_set_activate_on_unfocus(controls->mask_key, TRUE);
    g_signal_connect_swapped(controls->mask_key, "activate",
                             G_CALLBACK(mask_key_changed), controls);
    gwy_table_attach_adjbar(table, row++, _("_Label:"), NULL,
                            GTK_OBJECT(controls->mask_key), GWY_HSCALE_WIDGET);
    row++;

    controls->maskkey_gap = gtk_adjustment_new(args->maskkey_gap,
                                               0.0, 2.0, 0.01, 0.1, 0);
    gwy_table_attach_adjbar(table, row, _("_Vertical gap:"), NULL,
                            controls->maskkey_gap, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls->maskkey_gap, "value-changed",
                             G_CALLBACK(maskkey_gap_changed), controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    gtk_table_attach(GTK_TABLE(table), gwy_label_new_header(_("Value Scale")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls->ztype
        = gwy_radio_buttons_create(value_types, G_N_ELEMENTS(value_types),
                                   G_CALLBACK(ztype_changed), controls,
                                   args->ztype);
    row = gwy_radio_buttons_attach_to_table(controls->ztype,
                                            GTK_TABLE(table), 2, row);

    controls->fmscale_gap = gtk_adjustment_new(args->fmscale_gap,
                                               0.0, 2.0, 0.01, 0.1, 0);
    gwy_table_attach_adjbar(table, row, _("Hori_zontal gap:"), NULL,
                            controls->fmscale_gap, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls->fmscale_gap, "value-changed",
                             G_CALLBACK(fmscale_gap_changed), controls);
    row++;

    controls->fmscale_precision = gtk_adjustment_new(args->fmscale_precision,
                                                     0.0, 16.0, 1.0, 5.0, 0);
    gwy_table_attach_adjbar(table, row, _("Fi_xed precision:"), NULL,
                            controls->fmscale_precision,
                            GWY_HSCALE_LINEAR | GWY_HSCALE_CHECK | GWY_HSCALE_SNAP);
    controls->fix_fmscale_precision
        = gwy_table_hscale_get_check(controls->fmscale_precision);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->fix_fmscale_precision),
                                 args->fix_fmscale_precision);
    g_signal_connect_swapped(controls->fmscale_precision, "value-changed",
                             G_CALLBACK(fmscale_precision_changed), controls);
    g_signal_connect_swapped(controls->fix_fmscale_precision, "toggled",
                             G_CALLBACK(fix_fmscale_precision_changed), controls);
    row++;

    controls->kilo_threshold = gtk_adjustment_new(args->kilo_threshold,
                                                  1.0, 100000.0, 1.0, 100.0, 0);
    gwy_table_attach_adjbar(table, row, _("Fixed _kilo threshold:"), NULL,
                            controls->kilo_threshold,
                            GWY_HSCALE_CHECK | GWY_HSCALE_LOG);
    controls->fix_kilo_threshold
        = gwy_table_hscale_get_check(controls->kilo_threshold);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->fix_kilo_threshold),
                                 args->fix_kilo_threshold);
    g_signal_connect_swapped(controls->kilo_threshold, "value-changed",
                             G_CALLBACK(kilo_threshold_changed), controls);
    g_signal_connect_swapped(controls->fix_kilo_threshold, "toggled",
                             G_CALLBACK(fix_kilo_threshold_changed), controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    gtk_table_attach(GTK_TABLE(table), gwy_label_new_header(_("Title")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls->title_type
        = gwy_enum_combo_box_new(title_types, G_N_ELEMENTS(title_types),
                                 G_CALLBACK(title_type_changed), controls,
                                 args->title_type, TRUE);
    gwy_table_attach_adjbar(table, row, _("Posi_tion:"), NULL,
                            GTK_OBJECT(controls->title_type),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    controls->title_gap = gtk_adjustment_new(args->title_gap,
                                             -2.0, 1.0, 0.01, 0.1, 0);
    gwy_table_attach_adjbar(table, row, _("_Gap:"), NULL,
                            controls->title_gap, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls->title_gap, "value-changed",
                             G_CALLBACK(title_gap_changed), controls);
    row++;

    controls->units_in_title
        = check = gtk_check_button_new_with_mnemonic(_("Put _units to title"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->units_in_title),
                                 args->units_in_title);
    gtk_table_attach(GTK_TABLE(table), check, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(check, "toggled",
                             G_CALLBACK(units_in_title_changed), controls);
    row++;

    update_value_sensitivity(controls);
}

static void
update_selection_sensitivity(ImgExportControls *controls)
{
    gboolean sens = controls->args->draw_selection;
    GSList *l;

    gtk_widget_set_sensitive(controls->selections, sens);
    gtk_widget_set_sensitive(controls->sel_options_label, sens);
    update_colour_controls_sensitivity(&controls->sel_colour, sens);
    update_colour_controls_sensitivity(&controls->sel_outline_colour, sens);
    gwy_table_hscale_set_sensitive(controls->sel_opacity, sens);
    for (l = controls->sel_options; l; l = g_slist_next(l))
        gtk_widget_set_sensitive(GTK_WIDGET(l->data), sens);
}

static void
sel_render_name(G_GNUC_UNUSED GtkTreeViewColumn *column,
                GtkCellRenderer *renderer,
                GtkTreeModel *model,
                GtkTreeIter *iter,
                gpointer user_data)
{
    ImgExportControls *controls = (ImgExportControls*)user_data;
    GArray *selections = controls->args->env->selections;
    GQuark quark;
    guint id;

    gtk_tree_model_get(model, iter, 0, &id, -1);
    quark = g_array_index(selections, GQuark, id);
    g_object_set(renderer, "text", g_quark_to_string(quark), NULL);
}

static void
sel_render_type(G_GNUC_UNUSED GtkTreeViewColumn *column,
                GtkCellRenderer *renderer,
                GtkTreeModel *model,
                GtkTreeIter *iter,
                gpointer user_data)
{
    const ImgExportSelectionType *seltype;
    ImgExportControls *controls = (ImgExportControls*)user_data;
    GQuark quark;
    guint id;

    gtk_tree_model_get(model, iter, 0, &id, -1);
    quark = g_array_index(controls->args->env->selections, GQuark, id);
    seltype = find_selection_type(controls->args, g_quark_to_string(quark),
                                  NULL);
    g_object_set(renderer, "text", _(seltype->description), NULL);
}

static void
sel_render_objects(G_GNUC_UNUSED GtkTreeViewColumn *column,
                   GtkCellRenderer *renderer,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   gpointer user_data)
{

    ImgExportControls *controls = (ImgExportControls*)user_data;
    const ImgExportEnv *env = controls->args->env;
    GArray *selections = env->selections;
    GQuark quark;
    GwySelection *sel;
    gchar *key;
    gchar buf[12];
    guint id;

    gtk_tree_model_get(model, iter, 0, &id, -1);
    quark = g_array_index(selections, GQuark, id);

    key = g_strdup_printf("/%d/select/%s", env->id, g_quark_to_string(quark));
    sel = GWY_SELECTION(gwy_container_get_object_by_name(env->data, key));
    g_free(key);

    g_snprintf(buf, sizeof(buf), "%u", gwy_selection_get_data(sel, NULL));
    g_object_set(renderer, "text", buf, NULL);
}

static void
draw_selection_changed(ImgExportControls *controls,
                       GtkToggleButton *button)
{
    ImgExportArgs *args = controls->args;

    args->draw_selection = gtk_toggle_button_get_active(button);
    update_selection_sensitivity(controls);
    update_preview(controls);
}

static void
sel_outline_width_changed(ImgExportControls *controls,
                          GtkAdjustment *adj)
{
    controls->args->sizes.sel_outline_width = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
sel_opacity_changed(ImgExportControls *controls,
                    GtkAdjustment *adj)
{
    gdouble alpha = gtk_adjustment_get_value(adj);
    controls->args->sel_color.a = alpha;
    controls->args->sel_outline_color.a = alpha;
    update_preview(controls);
}

static void
update_selection_options(ImgExportControls *controls)
{
    const ImgExportSelectionType *seltype;
    ImgExportArgs *args = controls->args;
    GSList *l;

    for (l = controls->sel_options; l; l = g_slist_next(l))
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_slist_free(controls->sel_options);
    controls->sel_options = NULL;

    if ((seltype = find_selection_type(controls->args, args->selection, NULL))
        && seltype->create_options) {
        gtk_widget_set_no_show_all(controls->sel_options_label, FALSE);
        seltype->create_options(controls);
    }
    else {
        gtk_widget_set_no_show_all(controls->sel_options_label, TRUE);
        gtk_widget_hide(controls->sel_options_label);
    }

    gtk_widget_show_all(controls->table_selection);
}

static void
selection_selected(ImgExportControls *controls,
                   GtkTreeSelection *selection)
{
    ImgExportArgs *args = controls->args;
    GArray *selections = args->env->selections;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GQuark quark;
    guint id;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, 0, &id, -1);
        g_free(args->selection);
        quark = g_array_index(selections, GQuark, id);
        args->selection = g_strdup(g_quark_to_string(quark));
    }
    else {
        g_free(args->selection);
        args->selection = g_strdup("");
    }
    update_selection_options(controls);
    update_preview(controls);
}

static void
create_selection_controls(ImgExportControls *controls)
{
    ImgExportArgs *args = controls->args;
    ImgExportEnv *env = args->env;
    GArray *selections = env->selections;
    GtkWidget *table, *check, *spin;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GwyNullStore *store;
    GtkTreeView *treeview;
    GtkTreeSelection *treesel;
    GtkTreeIter iter;
    gint row = 0;
    guint i;

    table = controls->table_selection = gtk_table_new(12, 3, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);

    controls->draw_selection
        = check = gtk_check_button_new_with_mnemonic(_("Draw _selection"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 args->draw_selection);
    gtk_table_attach(GTK_TABLE(table), check, 0, 3, row, row+1,
                     GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(check, "toggled",
                             G_CALLBACK(draw_selection_changed), controls);
    row++;

    store = gwy_null_store_new(selections->len);

    controls->selections = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    treeview = GTK_TREE_VIEW(controls->selections);
    gtk_table_attach(GTK_TABLE(table), controls->selections, 0, 3, row, row+1,
                     GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
    row++;

    treesel = gtk_tree_view_get_selection(treeview);
    gtk_tree_selection_set_mode(treesel, GTK_SELECTION_BROWSE);
    for (i = 0; i < selections->len; i++) {
        GQuark quark = g_array_index(selections, GQuark, i);
        if (gwy_strequal(args->selection, g_quark_to_string(quark))) {
            gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store), &iter, NULL,
                                          i);
            gtk_tree_selection_select_iter(treesel, &iter);
            break;
        }
    }
    if (i == selections->len) {
        g_assert(selections->len == 0);
    }
    g_signal_connect_swapped(treesel, "changed",
                             G_CALLBACK(selection_selected), controls);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Name"));
    gtk_tree_view_append_column(treeview, column);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            sel_render_name, controls, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Type"));
    gtk_tree_view_append_column(treeview, column);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            sel_render_type, controls, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Objects"));
    gtk_tree_view_append_column(treeview, column);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            sel_render_objects, controls, NULL);

    create_colour_control(GTK_TABLE(table), row++,
                          _("Colo_r:"), &args->sel_color,
                          controls, &controls->sel_colour);

    create_colour_control(GTK_TABLE(table), row++,
                          _("Out_line color:"), &args->sel_outline_color,
                          controls, &controls->sel_outline_colour);

    controls->sel_outline_width
        = gtk_adjustment_new(args->sizes.sel_outline_width,
                             0.0, 16.0, 0.01, 1.0, 0);
    spin = gwy_table_attach_adjbar(GTK_WIDGET(table), row++,
                                   _("O_utline thickness:"), NULL,
                                   controls->sel_outline_width,
                                   GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_signal_connect_swapped(controls->sel_outline_width, "value-changed",
                             G_CALLBACK(sel_outline_width_changed), controls);

    controls->sel_opacity = gtk_adjustment_new(args->sel_color.a,
                                               0.0, 1.0, 0.001, 0.1, 0);
    gwy_table_attach_adjbar(table, row, _("O_pacity:"), NULL,
                            controls->sel_opacity, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls->sel_opacity, "value-changed",
                             G_CALLBACK(sel_opacity_changed), controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);

    controls->sel_options_label = gwy_label_new_header(_("Options"));
    gtk_table_attach(GTK_TABLE(table), controls->sel_options_label,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls->sel_row_start = row;

    update_selection_options(controls);
    update_selection_sensitivity(controls);
}

static void
reset_to_preset(ImgExportControls *controls,
                const ImgExportArgs *src)
{
    ImgExportArgs *args = controls->args;
    gboolean sel_number_objects = args->sel_number_objects;
    gdouble sel_line_thickness = args->sel_line_thickness;
    gdouble sel_point_radius = args->sel_point_radius;

    gwy_img_export_preset_data_copy(src, args);
    /* XXX: Maybe we should reset these too; however, the defaults for
     * selection-specific settings actually come from the environment. */
    args->sel_number_objects = sel_number_objects;
    args->sel_line_thickness = sel_line_thickness;
    args->sel_point_radius = sel_point_radius;

    if (controls->mode)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->mode),
                                     src->mode == IMGEXPORT_MODE_GREY16);

    if (controls->pxwidth)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->pxwidth),
                                 src->pxwidth);
    if (controls->zoom)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->zoom),
                                 src->zoom);

    if (controls->transparent_bg)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->transparent_bg),
                                     src->transparent_bg);

    args->linetext_color = src->linetext_color;
    gwy_color_button_set_color(GWY_COLOR_BUTTON(controls->linetext_colour.button),
                               &args->linetext_color);
    args->bg_color = src->bg_color;
    gwy_color_button_set_color(GWY_COLOR_BUTTON(controls->bg_colour.button),
                               &args->bg_color);

    update_selected_font(controls);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->font_size),
                             src->sizes.font_size);

    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->line_width),
                             src->sizes.line_width);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->tick_length),
                             src->sizes.tick_length);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->border_width),
                             src->sizes.border_width);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->scale_font),
                                 src->scale_font);

    gwy_radio_buttons_set_current(controls->xytype, src->xytype);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->inset_xgap),
                             src->inset_xgap);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->inset_ygap),
                             src->inset_ygap);
    gwy_radio_buttons_set_current(controls->inset_pos, src->inset_pos);
    inset_length_set_auto(controls);
    args->inset_color = src->inset_color;
    gwy_color_button_set_color(GWY_COLOR_BUTTON(controls->inset_colour.button),
                               &args->inset_color);
    args->inset_outline_color = src->inset_outline_color;
    gwy_color_button_set_color(GWY_COLOR_BUTTON(controls->inset_outline_colour.button),
                               &args->inset_outline_color);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->inset_draw_ticks),
                                 src->inset_draw_ticks);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->inset_draw_label),
                                 src->inset_draw_label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->inset_draw_text_above),
                                 src->inset_draw_text_above);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->draw_frame),
                                 src->draw_frame);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->draw_mask),
                                 src->draw_mask);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->draw_maskkey),
                                 src->draw_maskkey);
    gtk_entry_set_text(GTK_ENTRY(controls->mask_key), src->mask_key);
    gtk_widget_activate(controls->mask_key);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->maskkey_gap),
                             src->maskkey_gap);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->interpolation),
                                  src->interpolation);
    gwy_radio_buttons_set_current(controls->ztype, src->ztype);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->fmscale_gap),
                             src->fmscale_gap);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->fix_fmscale_precision),
                                 src->fix_fmscale_precision);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->fmscale_precision),
                             src->fmscale_precision);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->fix_kilo_threshold),
                                 src->fix_kilo_threshold);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->kilo_threshold),
                             src->kilo_threshold);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->title_type),
                                  src->title_type);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->title_gap),
                             src->title_gap);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->units_in_title),
                                 src->units_in_title);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->draw_selection),
                                 src->draw_selection);
    args->sel_color = src->sel_color;
    gwy_color_button_set_color(GWY_COLOR_BUTTON(controls->sel_colour.button),
                               &args->sel_color);
    args->sel_outline_color = src->sel_outline_color;
    gwy_color_button_set_color(GWY_COLOR_BUTTON(controls->sel_outline_colour.button),
                               &args->sel_outline_color);
}

static gboolean
preset_validate_name(const gchar *name)
{
    if (!name || !*name || strchr(name, '/') || strchr(name, '\\'))
        return FALSE;
    return TRUE;
}

static void
update_preset_sensitivity(ImgExportControls *controls)
{
    GwyInventory *inventory;
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    const gchar *name;
    gboolean sens, goodname, havename;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(controls->presets));
    sens = gtk_tree_selection_get_selected(selection, &model, &iter);
    name = gtk_entry_get_text(GTK_ENTRY(controls->preset_name));
    inventory = gwy_img_export_presets();
    goodname = preset_validate_name(name);
    havename = !!gwy_inventory_get_item(inventory, name);
    gwy_debug("selected: %d, goodname: %d, havename: %d",
              sens, goodname, havename);

    gtk_widget_set_sensitive(controls->preset_load, sens);
    gtk_widget_set_sensitive(controls->preset_delete, sens);
    gtk_widget_set_sensitive(controls->preset_rename,
                             sens && goodname && !havename);
    gtk_widget_set_sensitive(controls->preset_save, goodname);
}

static void
preset_render_name(G_GNUC_UNUSED GtkTreeViewColumn *column,
                   GtkCellRenderer *cell,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   G_GNUC_UNUSED gpointer user_data)
{
    GwyImgExportPreset *preset;

    gtk_tree_model_get(model, iter, 0, &preset, -1);
    g_object_set(cell, "text", gwy_resource_get_name(GWY_RESOURCE(preset)),
                 NULL);
}

static void
preset_render_lateral(G_GNUC_UNUSED GtkTreeViewColumn *column,
                      GtkCellRenderer *cell,
                      GtkTreeModel *model,
                      GtkTreeIter *iter,
                      G_GNUC_UNUSED gpointer user_data)
{
    GwyImgExportPreset *preset;
    const gchar *type;
    gchar *s;

    gtk_tree_model_get(model, iter, 0, &preset, -1);
    type = gwy_enum_to_string(preset->data.xytype,
                              lateral_types, G_N_ELEMENTS(lateral_types));
    s = gwy_strkill(g_strdup(gwy_sgettext(type)), "_:");
    g_object_set(cell, "text", s, NULL);
    g_free(s);
}

static void
preset_render_value(G_GNUC_UNUSED GtkTreeViewColumn *column,
                    GtkCellRenderer *cell,
                    GtkTreeModel *model,
                    GtkTreeIter *iter,
                    G_GNUC_UNUSED gpointer user_data)
{
    GwyImgExportPreset *preset;
    const gchar *type;
    gchar *s;

    gtk_tree_model_get(model, iter, 0, &preset, -1);
    type = gwy_enum_to_string(preset->data.ztype,
                              value_types, G_N_ELEMENTS(value_types));
    s = gwy_strkill(g_strdup(gwy_sgettext(type)), "_:");
    g_object_set(cell, "text", s, NULL);
    g_free(s);
}

static void
preset_render_title(G_GNUC_UNUSED GtkTreeViewColumn *column,
                    GtkCellRenderer *cell,
                    GtkTreeModel *model,
                    GtkTreeIter *iter,
                    G_GNUC_UNUSED gpointer user_data)
{
    GwyImgExportPreset *preset;
    const gchar *type;
    gchar *s;

    gtk_tree_model_get(model, iter, 0, &preset, -1);
    type = gwy_enum_to_string(preset->data.title_type,
                              title_types, G_N_ELEMENTS(title_types));
    s = gwy_strkill(g_strdup(gwy_sgettext(type)), "_:");
    g_object_set(cell, "text", s, NULL);
    g_free(s);
}

static void
preset_selected(ImgExportControls *controls)
{
    GwyImgExportPreset *preset;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    const gchar *name;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(controls->presets));
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, 0, &preset, -1);
        name = gwy_resource_get_name(GWY_RESOURCE(preset));
        gtk_entry_set_text(GTK_ENTRY(controls->preset_name), name);
        g_free(controls->args->preset_name);
        controls->args->preset_name = g_strdup(name);
    }
    else {
        gtk_entry_set_text(GTK_ENTRY(controls->preset_name), "");
        g_free(controls->args->preset_name);
        controls->args->preset_name = NULL;
    }
}

static void
load_preset(ImgExportControls *controls)
{
    GwyImgExportPreset *preset;
    GtkTreeModel *store;
    GtkTreeSelection *selection;
    GtkTreeIter iter;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(controls->presets));
    if (!gtk_tree_selection_get_selected(selection, &store, &iter))
        return;

    gtk_tree_model_get(store, &iter, 0, &preset, -1);
    reset_to_preset(controls, &preset->data);
}

static void
store_preset(ImgExportControls *controls)
{
    GwyImgExportPreset *preset;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    const gchar *name;
    gchar *filename;
    GString *str;
    FILE *fh;

    name = gtk_entry_get_text(GTK_ENTRY(controls->preset_name));
    if (!preset_validate_name(name))
        return;

    gwy_debug("Now I'm saving `%s'", name);
    preset = gwy_inventory_get_item(gwy_img_export_presets(), name);
    if (!preset) {
        gwy_debug("Appending `%s'", name);
        preset = gwy_img_export_preset_new(name, controls->args, FALSE);
        gwy_inventory_insert_item(gwy_img_export_presets(), preset);
        g_object_unref(preset);
    }
    else {
        gwy_debug("Setting `%s'", name);
        gwy_img_export_preset_data_copy(controls->args, &preset->data);
        gwy_resource_data_changed(GWY_RESOURCE(preset));
    }

    filename = gwy_resource_build_filename(GWY_RESOURCE(preset));
    fh = gwy_fopen(filename, "w");
    if (!fh) {
        g_warning("Cannot save preset: %s", filename);
        g_free(filename);
        return;
    }
    g_free(filename);

    str = gwy_resource_dump(GWY_RESOURCE(preset));
    fwrite(str->str, 1, str->len, fh);
    fclose(fh);
    g_string_free(str, TRUE);

    gwy_resource_data_saved(GWY_RESOURCE(preset));

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(controls->presets));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(controls->presets));
    gwy_inventory_store_get_iter(GWY_INVENTORY_STORE(model), name, &iter);
    gtk_tree_selection_select_iter(selection, &iter);
}

static void
rename_preset(ImgExportControls *controls)
{
    GwyImgExportPreset *preset;
    GwyInventory *inventory;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    const gchar *newname, *oldname;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(controls->presets));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    inventory = gwy_img_export_presets();
    gtk_tree_model_get(model, &iter, 0, &preset, -1);
    oldname = gwy_resource_get_name(GWY_RESOURCE(preset));
    newname = gtk_entry_get_text(GTK_ENTRY(controls->preset_name));
    if (gwy_strequal(newname, oldname)
        || !preset_validate_name(newname)
        || gwy_inventory_get_item(inventory, newname))
        return;

    gwy_debug("Now I will rename `%s' to `%s'", oldname, newname);
    if (gwy_resource_rename(GWY_RESOURCE(preset), newname)) {
        gwy_inventory_store_get_iter(GWY_INVENTORY_STORE(model),
                                     newname, &iter);
        gtk_tree_selection_select_iter(selection, &iter);
    }
}

static void
delete_preset(ImgExportControls *controls)
{
    GwyImgExportPreset *preset;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(controls->presets));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 0, &preset, -1);
    gwy_resource_delete(GWY_RESOURCE(preset));
}

static void
create_preset_controls(ImgExportControls *controls)
{
    ImgExportArgs *args = controls->args;
    GwyInventoryStore *store;
    GtkTreeSelection *selection;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeView *treeview;
    GtkTreeIter iter;
    GtkWidget *vbox, *table, *button, *scroll, *bbox;
    guint row;

    vbox = gtk_vbox_new(FALSE, 0);
    controls->table_presets = vbox;
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

    store = gwy_inventory_store_new(gwy_img_export_presets());
    controls->presets = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    treeview = GTK_TREE_VIEW(controls->presets);
    g_object_unref(store);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Name"));
    gtk_tree_view_append_column(treeview, column);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            preset_render_name, controls, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Lateral"));
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(treeview, column);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            preset_render_lateral, controls, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Value"));
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(treeview, column);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            preset_render_value, controls, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Title"));
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(treeview, column);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            preset_render_title, controls, NULL);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(scroll), controls->presets);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    bbox = gtk_hbutton_box_new();
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_START);
    gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

    button = gtk_button_new_with_mnemonic(gwy_sgettext("verb|_Load"));
    controls->preset_load = button;
    gtk_container_add(GTK_CONTAINER(bbox), button);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(load_preset), controls);

    button = gtk_button_new_with_mnemonic(gwy_sgettext("verb|_Store"));
    controls->preset_save = button;
    gtk_container_add(GTK_CONTAINER(bbox), button);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(store_preset), controls);

    button = gtk_button_new_with_mnemonic(_("_Rename"));
    controls->preset_rename = button;
    gtk_container_add(GTK_CONTAINER(bbox), button);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(rename_preset), controls);

    button = gtk_button_new_with_mnemonic(_("_Delete"));
    controls->preset_delete = button;
    gtk_container_add(GTK_CONTAINER(bbox), button);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(delete_preset), controls);

    table = gtk_table_new(1, 3, FALSE);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 4);
    row = 0;

    controls->preset_name = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(controls->preset_name),
                       args->preset_name ? args->preset_name : "");
    gwy_table_attach_row(table, row, _("Preset _name:"), "",
                         controls->preset_name);
    gtk_entry_set_max_length(GTK_ENTRY(controls->preset_name), 40);
    g_signal_connect_swapped(controls->preset_name, "changed",
                             G_CALLBACK(update_preset_sensitivity), controls);
    row++;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(controls->presets));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(preset_selected), controls);
    if (args->preset_name
        && gwy_inventory_store_get_iter(store, args->preset_name, &iter))
        gtk_tree_selection_select_iter(selection, &iter);

    update_preset_sensitivity(controls);
}

static void
unqueue_preview(ImgExportControls *controls)
{
    if (controls->sid) {
        g_source_remove(controls->sid);
        controls->sid = 0;
    }
}

static void
page_switched(ImgExportControls *controls,
              G_GNUC_UNUSED GtkNotebookPage *page,
              gint pagenum)
{
    if (controls->in_update)
        return;

    controls->args->active_page = pagenum;
}

static void
mode_changed(ImgExportControls *controls,
             GtkToggleButton *toggle)
{
    if (gtk_toggle_button_get_active(toggle)) {
        controls->args->mode = IMGEXPORT_MODE_GREY16;
        gtk_widget_set_sensitive(controls->notebook, FALSE);
    }
    else {
        controls->args->mode = IMGEXPORT_MODE_PRESENTATION;
        gtk_widget_set_sensitive(controls->notebook, TRUE);
    }

    update_preview(controls);
}

static gboolean
img_export_dialog(ImgExportArgs *args)
{
    enum { RESPONSE_RESET = 1 };

    ImgExportControls controls;
    ImgExportEnv *env = args->env;
    const ImgExportFormat *format = env->format;
    GtkWidget *dialog, *vbox, *hbox, *check;
    gint response;
    gchar *s, *title;

    gwy_clear(&controls, 1);
    controls.args = args;
    controls.in_update = TRUE;

    s = g_ascii_strup(format->name, -1);
    title = g_strdup_printf(_("Export %s"), s);
    g_free(s);
    dialog = gtk_dialog_new_with_buttons(title, NULL, 0,
                                         _("_Reset"), RESPONSE_RESET,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_free(title);
    controls.dialog = dialog;
    select_a_real_font(args, dialog);
    g_signal_connect_swapped(dialog, "destroy",
                             G_CALLBACK(unqueue_preview), &controls);

    hbox = gtk_hbox_new(FALSE, 20);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 0);

    vbox = gtk_vbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    if (format->write_grey16) {
        check = gtk_check_button_new_with_mnemonic(_("Export as 1_6 bit "
                                                     "grayscale"));
        controls.mode = check;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                     args->mode == IMGEXPORT_MODE_GREY16);
        gtk_box_pack_start(GTK_BOX(vbox), check, FALSE, FALSE, 0);
        g_signal_connect_swapped(check, "toggled",
                                 G_CALLBACK(mode_changed), &controls);
    }

    controls.notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), controls.notebook, TRUE, TRUE, 0);
    if (args->mode == IMGEXPORT_MODE_GREY16)
        gtk_widget_set_sensitive(controls.notebook, FALSE);

    create_basic_controls(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(controls.notebook),
                             controls.table_basic,
                             gtk_label_new(gwy_sgettext("adjective|Basic")));

    create_lateral_controls(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(controls.notebook),
                             controls.table_lateral,
                             gtk_label_new(_("Lateral Scale")));

    create_value_controls(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(controls.notebook),
                             controls.table_value,
                             gtk_label_new(_("Values")));

    create_selection_controls(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(controls.notebook),
                             controls.table_selection,
                             gtk_label_new(_("Selection")));

    create_preset_controls(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(controls.notebook),
                             controls.table_presets,
                             gtk_label_new(_("Presets")));

    controls.preview = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(hbox), controls.preview, FALSE, FALSE, 0);

    preview(&controls);
    controls.in_update = FALSE;
    gtk_widget_show_all(dialog);

    /* Must be done when widgets are shown, see GtkNotebook docs */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(controls.notebook),
                                  args->active_page);
    g_signal_connect_swapped(controls.notebook, "switch-page",
                             G_CALLBACK(page_switched), &controls);

    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            return FALSE;
            break;

            case GTK_RESPONSE_OK:
            break;

            case RESPONSE_RESET:
            reset_to_preset(&controls, &img_export_defaults);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);

    return TRUE;
}

static void
add_selection(gpointer hkey, gpointer hvalue, gpointer data)
{
    GQuark quark = GPOINTER_TO_UINT(hkey);
    GValue *value = (GValue*)hvalue;
    GArray *selections = (GArray*)data;
    GwySelection *sel = g_value_get_object(value);
    const gchar *s = g_quark_to_string(quark), *typename;
    guint i;

    if (!gwy_selection_get_data(sel, NULL)) {
        gwy_debug("ignoring empty selection %s", s);
        return;
    }

    typename = G_OBJECT_TYPE_NAME(sel);
    for (i = 0; i < G_N_ELEMENTS(known_selections); i++) {
        if (gwy_strequal(typename, known_selections[i].typename)) {
            if (known_selections[i].draw)
                break;
            gwy_debug("we know %s but don't have a drawing func for it",
                      typename);
        }
    }
    if (i == G_N_ELEMENTS(known_selections)) {
        gwy_debug("ignoring unknown selection %s (%s)", s, typename);
        return;
    }
    gwy_debug("found selection %s (%s)", s, typename);

    g_return_if_fail(*s == '/');
    s++;
    while (g_ascii_isdigit(*s))
        s++;
    g_return_if_fail(g_str_has_prefix(s, "/select/"));
    s += strlen("/select/");
    quark = g_quark_from_string(s);
    g_array_append_val(selections, quark);
}

/* Gather all kind of information about the data and how they are currently
 * displayed in Gwyddion so that we can mimic it. */
static void
img_export_load_env(ImgExportEnv *env,
                    GwyContainer *settings,
                    const ImgExportFormat *format,
                    GwyContainer *data)
{
    GwyDataField *dfield, *show;
    GwyDataView *dataview;
    GwyVectorLayer *vlayer;
    GObject *sel;
    GwyInventory *gradients;
    const guchar *gradname = NULL, *key;
    struct lconv *locale_data;
    guint xres, yres;
    GString *s;

    s = g_string_new(NULL);
    gwy_clear(env, 1);

    locale_data = localeconv();
    env->decimal_symbol = g_strdup(locale_data->decimal_point);
    g_assert(strlen(env->decimal_symbol) != 0);

    env->format = format;
    env->data = data;
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &env->id,
                                     GWY_APP_MASK_FIELD, &env->mask,
                                     GWY_APP_SHOW_FIELD, &show,
                                     GWY_APP_DATA_VIEW, &dataview,
                                     0);

    if (show) {
        env->has_presentation = TRUE;
        env->dfield = show;
    }
    else
        env->dfield = dfield;

    g_string_printf(s, "/%d/data/realsquare", env->id);
    gwy_container_gis_boolean_by_name(data, s->str, &env->realsquare);

    g_string_printf(s, "/%d/mask", env->id);
    if (!gwy_rgba_get_from_container(&env->mask_colour, data, s->str))
        gwy_rgba_get_from_container(&env->mask_colour, settings, "/mask");

    /* Find out native pixel sizes for the data bitmaps. */
    xres = gwy_data_field_get_xres(env->dfield);
    yres = gwy_data_field_get_yres(env->dfield);
    if (env->realsquare) {
        gdouble xreal = gwy_data_field_get_xreal(env->dfield);
        gdouble yreal = gwy_data_field_get_yreal(env->dfield);
        gdouble scale = MAX(xres/xreal, yres/yreal);
        /* This is how GwyDataView rounds it so we should get a pixmap of
         * this size. */
        env->xres = GWY_ROUND(xreal*scale);
        env->yres = GWY_ROUND(yreal*scale);
    }
    else {
        env->xres = xres;
        env->yres = yres;
    }
    gwy_debug("env->xres %u, env->yres %u", env->xres, env->yres);

    /* False colour mapping. */
    g_string_printf(s, "/%d/base/palette", env->id);
    gwy_container_gis_string_by_name(data, s->str, &gradname);

    gradients = gwy_gradients();
    env->gradient = gwy_inventory_get_item_or_default(gradients, gradname);
    gwy_resource_use(GWY_RESOURCE(env->gradient));

    env->fm_rangetype = GWY_LAYER_BASIC_RANGE_FULL;
    gwy_container_gis_enum_by_name(settings, APP_RANGE_KEY, &env->fm_rangetype);
    gwy_debug("default range type: %u", env->fm_rangetype);

    g_string_printf(s, "/%d/base/range-type", env->id);
    gwy_container_gis_enum_by_name(data, s->str, &env->fm_rangetype);
    gwy_debug("data range type: %u", env->fm_rangetype);

    /* The current behaviour is that all mappings work on presentations, but
     * fixed range is ignored so it means full. */
    gwy_data_field_get_min_max(env->dfield, &env->fm_min, &env->fm_max);
    if (env->fm_rangetype == GWY_LAYER_BASIC_RANGE_AUTO)
        gwy_data_field_get_autorange(env->dfield,
                                     &env->fm_min, &env->fm_max);
    if (!env->has_presentation
        && env->fm_rangetype == GWY_LAYER_BASIC_RANGE_FIXED) {
        /* These may not be actually set; for this we always init with full. */
        g_string_printf(s, "/%d/base/min", env->id);
        gwy_container_gis_double_by_name(data, s->str, &env->fm_min);
        g_string_printf(s, "/%d/base/max", env->id);
        gwy_container_gis_double_by_name(data, s->str, &env->fm_max);
    }

    if ((env->fm_inverted = (env->fm_max < env->fm_min)))
        GWY_SWAP(gdouble, env->fm_min, env->fm_max);

    /* Selections. */
    env->selections = g_array_new(FALSE, FALSE, sizeof(GQuark));
    g_string_printf(s, "/%d/select/", env->id);
    gwy_container_foreach(data, s->str, &add_selection, env->selections);

    if (dataview
        && (vlayer = gwy_data_view_get_top_layer(dataview))
        && (key = gwy_vector_layer_get_selection_key(vlayer))
        && g_str_has_prefix(key, s->str)
        && gwy_container_gis_object_by_name(data, key, &sel)) {
        const gchar *typename = G_OBJECT_TYPE_NAME(sel);

        env->vlayer_sel_key = g_quark_from_string(key + s->len);

        if (gwy_strequal(typename, "GwySelectionLine")) {
            gint lt;

            env->sel_line_have_layer = TRUE;
            g_object_get(vlayer, "thickness", &lt, NULL);
            gwy_debug("got thickness from layer %d", lt);
            env->sel_line_thickness = lt;
        }
        else if (gwy_strequal(typename, "GwySelectionPoint")) {
            gint pr;

            env->sel_point_have_layer = TRUE;
            g_object_get(vlayer, "marker-radius", &pr, NULL);
            gwy_debug("got radius from layer %d", pr);
            env->sel_point_radius = pr;
        }
        else if (gwy_strequal(typename, "GwySelectionPath")) {
            gint lt;

            env->sel_path_have_layer = TRUE;
            g_object_get(vlayer, "thickness", &lt, NULL);
            gwy_debug("got thickness from layer %d", lt);
            env->sel_line_thickness = lt;
        }
    }

    /* Miscellaneous stuff. */
    env->title = gwy_app_get_data_field_title(data, env->id);
    g_strstrip(env->title);

    if (format->write_grey16) {
        env->grey = gwy_inventory_get_item(gradients, "Gray");
        gwy_resource_use(GWY_RESOURCE(env->grey));
    }


    g_string_free(s, TRUE);
}

static gboolean
img_export_export(GwyContainer *data,
                  const gchar *filename,
                  GwyRunType mode,
                  GError **error,
                  const gchar *name)
{
    GwyResourceClass *rklass;
    GwyContainer *settings;
    ImgExportArgs args;
    ImgExportEnv env;
    const ImgExportFormat *format;
    gboolean ok = TRUE;
    gint id;
    guint i;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_ID, &id, 0);
    if (id < 0) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    rklass = g_type_class_peek(GWY_TYPE_IMG_EXPORT_PRESET);
    gwy_resource_class_mkdir(rklass);

    format = find_format(name, TRUE);
    g_return_val_if_fail(format, FALSE);

    settings = gwy_app_settings_get();
    img_export_load_args(settings, &args);

    if (args.mode == IMGEXPORT_MODE_GREY16 && !format->write_grey16)
        args.mode = IMGEXPORT_MODE_PRESENTATION;

    args.env = &env;
    img_export_load_env(&env, settings, format, data);

    if (!inset_length_ok(env.dfield, args.inset_length)) {
        g_free(args.inset_length);
        args.inset_length = scalebar_auto_length(env.dfield, NULL);
    }

    /* When run interactively, try to show the same selection that is currently
     * shown on the data, if any.  But in non-interactive usage strive for
     * predictable behaviour. */
    if (mode == GWY_RUN_INTERACTIVE && env.vlayer_sel_key) {
        g_free(args.selection);
        args.selection = g_strdup(g_quark_to_string(env.vlayer_sel_key));
    }

    gwy_debug("args.selection %s", args.selection);
    for (i = 0; i < env.selections->len; i++) {
        GQuark quark = g_array_index(env.selections, GQuark, i);
        if (gwy_strequal(args.selection, g_quark_to_string(quark)))
            break;
    }
    if (i == env.selections->len) {
        if (env.selections->len && mode == GWY_RUN_INTERACTIVE) {
            GQuark quark = g_array_index(env.selections, GQuark, 0);
            gwy_debug("not found, trying %s", g_quark_to_string(quark));
            g_free(args.selection);
            args.selection = g_strdup(g_quark_to_string(quark));
        }
        else {
            gwy_debug("not found, trying NONE");
            g_free(args.selection);
            args.selection = g_strdup("");
        }
    }
    gwy_debug("feasible selection %s", args.selection);

    if (mode == GWY_RUN_INTERACTIVE) {
        if (env.sel_line_have_layer || env.sel_path_have_layer) {
            args.sel_line_thickness = env.sel_line_thickness;
        }
        if (env.sel_point_have_layer) {
            args.sel_point_radius = env.sel_point_radius;
        }

        ok = img_export_dialog(&args);
    }

    if (ok) {
        if (format->write_vector)
            ok = format->write_vector(&args, format->name, filename, error);
        else if (format->write_grey16 && args.mode == IMGEXPORT_MODE_GREY16) {
            ok = format->write_grey16(&args, format->name, filename, error);
        }
        else if (format->write_pixbuf) {
            GdkPixbuf *pixbuf = render_pixbuf(&args, format->name);
            ok = format->write_pixbuf(pixbuf, format->name, filename, error);
            g_object_unref(pixbuf);
        }
        else {
            ok = FALSE;
            g_assert_not_reached();
        }
    }
    else {
        err_CANCELLED(error);
    }

    img_export_save_args(settings, &args);
    img_export_free_args(&args);
    img_export_free_env(&env);

    return ok;
}

static guint16*
render_image_grey16(GwyDataField *dfield)
{
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);
    gdouble min, max;
    guint16 *pixels;

    pixels = g_new(guint16, xres*yres);
    gwy_data_field_get_min_max(dfield, &min, &max);
    if (min == max)
        memset(pixels, 0, xres*yres*sizeof(guint16));
    else {
        const gdouble *d = gwy_data_field_get_data_const(dfield);
        gdouble q = 65535.999999/(max - min);
        guint i;

        for (i = 0; i < xres*yres; i++)
            pixels[i] = (guint16)(q*(d[i] - min));
    }

    return pixels;
}

#ifdef HAVE_PNG
static void
add_png_text_chunk_string(png_text *chunk,
                          const gchar *key,
                          const gchar *str,
                          gboolean take)
{
    chunk->compression = PNG_TEXT_COMPRESSION_NONE;
    chunk->key = (char*)key;
    chunk->text = take ? (char*)str : g_strdup(str);
    chunk->text_length = strlen(chunk->text);
}

static void
add_png_text_chunk_float(png_text *chunk,
                         const gchar *key,
                         gdouble value)
{
    gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];

    chunk->compression = PNG_TEXT_COMPRESSION_NONE;
    chunk->key = (char*)key;
    g_ascii_dtostr(buffer, sizeof(buffer), value);
    chunk->text = g_strdup(buffer);
    chunk->text_length = strlen(chunk->text);
}

static gboolean
write_image_png16(ImgExportArgs *args,
                  const gchar *name,
                  const gchar *filename,
                  GError **error)
{
    enum { NCHUNKS = 11 };

    const guchar *title = "Data";

    GwyDataField *dfield = args->env->dfield;
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);
    guint16 *pixels;
    png_structp writer;
    png_infop writer_info;
    png_byte **rows = NULL;
    png_text *text_chunks = NULL;
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
    guint transform_flags = PNG_TRANSFORM_SWAP_ENDIAN;
#endif
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    guint transform_flags = PNG_TRANSFORM_IDENTITY;
#endif
    /* A bit of convoluted typing to get a png_charpp equivalent. */
    gchar param0[G_ASCII_DTOSTR_BUF_SIZE], param1[G_ASCII_DTOSTR_BUF_SIZE];
    gchar *s, *params[2];
    gdouble min, max;
    gboolean ok = FALSE;
    FILE *fh;
    guint i;

    g_return_val_if_fail(gwy_strequal(name, "png"), FALSE);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    writer = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!writer) {
        fclose(fh);
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("libpng initialization error (in %s)"),
                    "png_create_write_struct");
        return FALSE;
    }

    writer_info = png_create_info_struct(writer);
    if (!writer_info) {
        fclose(fh);
        png_destroy_read_struct(&writer, NULL, NULL);
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("libpng initialization error (in %s)"),
                    "png_create_info_struct");
        return FALSE;
    }

    gwy_data_field_get_min_max(dfield, &min, &max);
    s = g_strdup_printf("/%d/data/title", args->env->id);
    gwy_container_gis_string_by_name(args->env->data, s, &title);
    g_free(s);

    /* Create the chunks dynamically because the fields of png_text are
     * variable. */
    text_chunks = g_new0(png_text, NCHUNKS);
    i = 0;
    /* Standard PNG keys */
    add_png_text_chunk_string(text_chunks + i++, "Title", title, FALSE);
    add_png_text_chunk_string(text_chunks + i++, "Software", "Gwyddion", FALSE);
    /* Gwyddion GSF keys */
    gwy_data_field_get_min_max(dfield, &min, &max);
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_XREAL,
                             gwy_data_field_get_xreal(dfield));
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_YREAL,
                             gwy_data_field_get_yreal(dfield));
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_XOFFSET,
                             gwy_data_field_get_xoffset(dfield));
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_YOFFSET,
                             gwy_data_field_get_yoffset(dfield));
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_ZMIN, min);
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_ZMAX, max);
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_xy(dfield),
                               GWY_SI_UNIT_FORMAT_PLAIN);
    add_png_text_chunk_string(text_chunks + i++, GWY_IMGKEY_XYUNIT, s, TRUE);
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_z(dfield),
                               GWY_SI_UNIT_FORMAT_PLAIN);
    add_png_text_chunk_string(text_chunks + i++, GWY_IMGKEY_ZUNIT, s, TRUE);
    add_png_text_chunk_string(text_chunks + i++, GWY_IMGKEY_TITLE, title, FALSE);
    g_assert(i == NCHUNKS);

    png_set_text(writer, writer_info, text_chunks, NCHUNKS);

    /* Present the scaling information also as calibration chunks.
     * Unfortunately, they cannot represent it fully – the rejected xCAL and
     * yCAL chunks would be necessary for that. */
    png_set_sCAL(writer, writer_info, PNG_SCALE_METER,  /* Usually... */
                 gwy_data_field_get_xreal(dfield),
                 gwy_data_field_get_yreal(dfield));
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_z(dfield),
                               GWY_SI_UNIT_FORMAT_PLAIN);
    g_ascii_dtostr(param0, sizeof(param0), min);
    g_ascii_dtostr(param1, sizeof(param1), (max - min)/G_MAXUINT16);
    params[0] = param0;
    params[1] = param1;
    png_set_pCAL(writer, writer_info, "Z", 0, G_MAXUINT16, 0, 2, s, params);
    g_free(s);

    pixels = render_image_grey16(dfield);
    rows = g_new(png_bytep, yres);
    for (i = 0; i < yres; i++)
        rows[i] = (png_bytep)pixels + i*xres*sizeof(guint16);

    if (setjmp(png_jmpbuf(writer))) {
        /* FIXME: Not very helpful.  Thread-unsafe. */
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("libpng error occurred"));
        ok = FALSE;   /* might be clobbered by longjmp otherwise, says gcc. */
        goto end;
    }

    png_init_io(writer, fh);
    png_set_filter(writer, 0, PNG_ALL_FILTERS);
    png_set_compression_level(writer, Z_BEST_COMPRESSION);
    png_set_IHDR(writer, writer_info, xres, yres,
                 16, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    /* XXX */
    png_set_rows(writer, writer_info, rows);
    png_write_png(writer, writer_info, transform_flags, NULL);

    ok = TRUE;

end:
    fclose(fh);
    g_free(rows);
    g_free(pixels);
    png_destroy_write_struct(&writer, &writer_info);
    for (i = 0; i < NCHUNKS; i++)
        g_free(text_chunks[i].text);
    g_free(text_chunks);

    return ok;
}
#endif

/* Expand a word and double-word into LSB-ordered sequence of bytes */
#define W(x) (x)&0xff, (x)>>8
#define Q(x) (x)&0xff, ((x)>>8)&0xff, ((x)>>16)&0xff, (x)>>24

static gboolean
write_image_tiff16(ImgExportArgs *args,
                   const gchar *name,
                   const gchar *filename,
                   GError **error)
{
    enum {
        N_ENTRIES = 11,
        ESTART = 4 + 4 + 2,
        HEAD_SIZE = ESTART + 12*N_ENTRIES + 4,  /* head + 0th directory */
        /* offsets of things we have to fill run-time */
        WIDTH_OFFSET = ESTART + 12*0 + 8,
        HEIGHT_OFFSET = ESTART + 12*1 + 8,
        BPS_OFFSET = ESTART + 12*2 + 8,
        ROWS_OFFSET = ESTART + 12*8 + 8,
        BYTES_OFFSET = ESTART + 12*9 + 8,
        BIT_DEPTH = 16,
    };

    static guchar tiff_head[] = {
        0x49, 0x49,   /* magic (LSB) */
        W(42),        /* more magic */
        Q(8),         /* 0th directory offset */
        W(N_ENTRIES), /* number of entries */
        W(GWY_TIFFTAG_IMAGE_WIDTH), W(GWY_TIFF_SHORT), Q(1), Q(0),
        W(GWY_TIFFTAG_IMAGE_LENGTH), W(GWY_TIFF_SHORT), Q(1), Q(0),
        W(GWY_TIFFTAG_BITS_PER_SAMPLE), W(GWY_TIFF_SHORT), Q(1), Q(BIT_DEPTH),
        W(GWY_TIFFTAG_COMPRESSION), W(GWY_TIFF_SHORT), Q(1),
            Q(GWY_TIFF_COMPRESSION_NONE),
        W(GWY_TIFFTAG_PHOTOMETRIC), W(GWY_TIFF_SHORT), Q(1),
            Q(GWY_TIFF_PHOTOMETRIC_MIN_IS_BLACK),
        W(GWY_TIFFTAG_STRIP_OFFSETS), W(GWY_TIFF_LONG), Q(1), Q(HEAD_SIZE),
        W(GWY_TIFFTAG_ORIENTATION), W(GWY_TIFF_SHORT), Q(1),
            Q(GWY_TIFF_ORIENTATION_TOPLEFT),
        W(GWY_TIFFTAG_SAMPLES_PER_PIXEL), W(GWY_TIFF_SHORT), Q(1), Q(1),
        W(GWY_TIFFTAG_ROWS_PER_STRIP), W(GWY_TIFF_SHORT), Q(1), Q(0),
        W(GWY_TIFFTAG_STRIP_BYTE_COUNTS), W(GWY_TIFF_LONG), Q(1), Q(0),
        W(GWY_TIFFTAG_PLANAR_CONFIG), W(GWY_TIFF_SHORT), Q(1),
            Q(GWY_TIFF_PLANAR_CONFIG_CONTIGNUOUS),
        Q(0),              /* next directory (0 = none) */
        /* Here start the image data */
    };

    GwyDataField *dfield = args->env->dfield;
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);
    guint nbytes = BIT_DEPTH*xres*yres;
    guint16 *pixels;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "tiff"), FALSE);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    *(guint32*)(tiff_head + WIDTH_OFFSET) = GUINT32_TO_LE(xres);
    *(guint32*)(tiff_head + HEIGHT_OFFSET) = GUINT32_TO_LE(yres);
    *(guint32*)(tiff_head + ROWS_OFFSET) = GUINT32_TO_LE(yres);
    *(guint32*)(tiff_head + BYTES_OFFSET) = GUINT32_TO_LE(nbytes);

    if (fwrite(tiff_head, 1, sizeof(tiff_head), fh) != sizeof(tiff_head)) {
        err_WRITE(error);
        fclose(fh);
        return FALSE;
    }

    pixels = render_image_grey16(dfield);
    if (fwrite(pixels, sizeof(guint16), xres*yres, fh) != xres*yres) {
        err_WRITE(error);
        fclose(fh);
        g_free(pixels);
        return FALSE;
    }

    fclose(fh);
    g_free(pixels);

    return TRUE;
}

#undef Q
#undef W

static void
add_ppm_comment_string(GString *str,
                       const gchar *key,
                       const gchar *value,
                       gboolean take)
{
    g_string_append_printf(str, "# %s %s\n", key, value);
    if (take)
        g_free((gpointer)value);
}

static void
add_ppm_comment_float(GString *str,
                      const gchar *key,
                      gdouble value)
{
    gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];

    g_ascii_dtostr(buffer, sizeof(buffer), value);
    g_string_append_printf(str, "# %s %s\n", key, buffer);
}

static gboolean
write_image_pgm16(ImgExportArgs *args,
                  const gchar *name,
                  const gchar *filename,
                  GError **error)
{
    static const gchar pgm_header[] = "P5\n%s%u\n%u\n65535\n";
    const guchar *title = "Data";

    GwyDataField *dfield = args->env->dfield;
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);
    guint i;
    gdouble min, max;
    gboolean ok = FALSE;
    gchar *s, *ppmh = NULL;
    GString *str;
    guint16 *pixels;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "pnm"), FALSE);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    pixels = render_image_grey16(dfield);
    gwy_data_field_get_min_max(dfield, &min, &max);

    s = g_strdup_printf("/%d/data/title", args->env->id);
    gwy_container_gis_string_by_name(args->env->data, s, &title);
    g_free(s);

    /* Gwyddion GSF keys */
    str = g_string_new(NULL);
    add_ppm_comment_float(str, GWY_IMGKEY_XREAL,
                          gwy_data_field_get_xreal(dfield));
    add_ppm_comment_float(str, GWY_IMGKEY_YREAL,
                          gwy_data_field_get_yreal(dfield));
    add_ppm_comment_float(str, GWY_IMGKEY_XOFFSET,
                          gwy_data_field_get_xoffset(dfield));
    add_ppm_comment_float(str, GWY_IMGKEY_YOFFSET,
                          gwy_data_field_get_yoffset(dfield));
    add_ppm_comment_float(str, GWY_IMGKEY_ZMIN, min);
    add_ppm_comment_float(str, GWY_IMGKEY_ZMAX, max);
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_xy(dfield),
                               GWY_SI_UNIT_FORMAT_PLAIN);
    add_ppm_comment_string(str, GWY_IMGKEY_XYUNIT, s, TRUE);
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_z(dfield),
                               GWY_SI_UNIT_FORMAT_PLAIN);
    add_ppm_comment_string(str, GWY_IMGKEY_ZUNIT, s, TRUE);
    add_ppm_comment_string(str, GWY_IMGKEY_TITLE, title, FALSE);

    ppmh = g_strdup_printf(pgm_header, str->str, xres, yres);
    g_string_free(str, TRUE);

    if (fwrite(ppmh, 1, strlen(ppmh), fh) != strlen(ppmh)) {
        err_WRITE(error);
        goto end;
    }

    if (G_BYTE_ORDER != G_BIG_ENDIAN) {
        for (i = 0; i < xres*yres; i++)
            pixels[i] = GUINT16_TO_BE(pixels[i]);
    }

    if (fwrite(pixels, sizeof(guint16), xres*yres, fh) != xres*yres) {
        err_WRITE(error);
        goto end;
    }
    ok = TRUE;

end:
    g_free(pixels);
    g_free(ppmh);
    fclose(fh);

    return ok;
}

static gboolean
write_vector_generic(ImgExportArgs *args,
                     const gchar *name,
                     const gchar *filename,
                     GError **error)
{
    gboolean ok = TRUE;
    ImgExportSizes *sizes;
    cairo_surface_t *surface;
    cairo_status_t status;
    cairo_t *cr;
    gdouble zoom = args->zoom;

    gwy_debug("requested width %g mm", args->pxwidth*args->env->xres);
    args->zoom = mm2pt*args->pxwidth;
    gwy_debug("must set zoom to %g", args->zoom);
    sizes = calculate_sizes(args, name);
    g_return_val_if_fail(sizes, FALSE);
    gwy_debug("image width %g, canvas width %g",
              sizes->image.w/mm2pt, sizes->canvas.w/mm2pt);
    surface = create_surface(name, filename, sizes->canvas.w, sizes->canvas.h,
                             TRUE);
    g_return_val_if_fail(surface, FALSE);
    cr = cairo_create(surface);
    image_draw_cairo(args, sizes, cr);
    cairo_surface_flush(surface);
    if ((status = cairo_status(cr))
        || (status = cairo_surface_status(surface))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("Cairo error occurred: %s"),
                    cairo_status_to_string(status));
        ok = FALSE;
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    destroy_sizes(sizes);
    args->zoom = zoom;

    return ok;
}

static gboolean
write_pixbuf_generic(GdkPixbuf *pixbuf,
                     const gchar *name,
                     const gchar *filename,
                     GError **error)
{
    GError *err = NULL;

    if (gdk_pixbuf_save(pixbuf, filename, name, &err, NULL))
        return TRUE;

    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                _("Pixbuf save failed: %s."), err->message);
    g_clear_error(&err);
    return FALSE;
}

/* Expand a word and double-word into LSB-ordered sequence of bytes */
#define W(x) (x)&0xff, (x)>>8
#define Q(x) (x)&0xff, ((x)>>8)&0xff, ((x)>>16)&0xff, (x)>>24

static gboolean
write_pixbuf_tiff(GdkPixbuf *pixbuf,
                  const gchar *name,
                  const gchar *filename,
                  GError **error)
{
    enum {
        N_ENTRIES = 14,
        ESTART = 4 + 4 + 2,
        HEAD_SIZE = ESTART + 12*N_ENTRIES + 4,  /* head + 0th directory */
        /* offsets of things we have to fill run-time */
        WIDTH_OFFSET = ESTART + 12*0 + 8,
        HEIGHT_OFFSET = ESTART + 12*1 + 8,
        ROWS_OFFSET = ESTART + 12*8 + 8,
        BYTES_OFFSET = ESTART + 12*9 + 8,
        BIT_DEPTH = 8,
        NCHANNELS = 3,
    };

    static guchar tiff_head[] = {
        0x49, 0x49,   /* magic (LSB) */
        W(42),        /* more magic */
        Q(8),         /* 0th directory offset */
        W(N_ENTRIES), /* number of entries */
        W(GWY_TIFFTAG_IMAGE_WIDTH), W(GWY_TIFF_SHORT), Q(1), Q(0),
        W(GWY_TIFFTAG_IMAGE_LENGTH), W(GWY_TIFF_SHORT), Q(1), Q(0),
        W(GWY_TIFFTAG_BITS_PER_SAMPLE), W(GWY_TIFF_SHORT), Q(3), Q(HEAD_SIZE),
        W(GWY_TIFFTAG_COMPRESSION), W(GWY_TIFF_SHORT), Q(1),
            Q(GWY_TIFF_COMPRESSION_NONE),
        W(GWY_TIFFTAG_PHOTOMETRIC), W(GWY_TIFF_SHORT), Q(1),
            Q(GWY_TIFF_PHOTOMETRIC_RGB),
        W(GWY_TIFFTAG_STRIP_OFFSETS), W(GWY_TIFF_LONG), Q(1),
            Q(HEAD_SIZE + 22),
        W(GWY_TIFFTAG_ORIENTATION), W(GWY_TIFF_SHORT), Q(1),
            Q(GWY_TIFF_ORIENTATION_TOPLEFT),
        W(GWY_TIFFTAG_SAMPLES_PER_PIXEL), W(GWY_TIFF_SHORT), Q(1), Q(NCHANNELS),
        W(GWY_TIFFTAG_ROWS_PER_STRIP), W(GWY_TIFF_SHORT), Q(1), Q(0),
        W(GWY_TIFFTAG_STRIP_BYTE_COUNTS), W(GWY_TIFF_LONG), Q(1), Q(0),
        W(GWY_TIFFTAG_X_RESOLUTION), W(GWY_TIFF_RATIONAL), Q(1),
            Q(HEAD_SIZE + 6),
        W(GWY_TIFFTAG_Y_RESOLUTION), W(GWY_TIFF_RATIONAL), Q(1),
            Q(HEAD_SIZE + 14),
        W(GWY_TIFFTAG_PLANAR_CONFIG), W(GWY_TIFF_SHORT), Q(1),
            Q(GWY_TIFF_PLANAR_CONFIG_CONTIGNUOUS),
        W(GWY_TIFFTAG_RESOLUTION_UNIT), W(GWY_TIFF_SHORT), Q(1),
            Q(GWY_TIFF_RESOLUTION_UNIT_INCH),
        Q(0),              /* next directory (0 = none) */
        /* header data */
        W(BIT_DEPTH), W(BIT_DEPTH), W(BIT_DEPTH),
        Q(72), Q(1),       /* x-resolution */
        Q(72), Q(1),       /* y-resolution */
        /* here starts the image data */
    };

    guint xres, yres, rowstride, i, nbytes, nchannels;
    guchar *pixels;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "tiff"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3, FALSE);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    nbytes = xres*yres*NCHANNELS;

    *(guint32*)(tiff_head + WIDTH_OFFSET) = GUINT32_TO_LE(xres);
    *(guint32*)(tiff_head + HEIGHT_OFFSET) = GUINT32_TO_LE(yres);
    *(guint32*)(tiff_head + ROWS_OFFSET) = GUINT32_TO_LE(yres);
    *(guint32*)(tiff_head + BYTES_OFFSET) = GUINT32_TO_LE(nbytes);

    if (fwrite(tiff_head, 1, sizeof(tiff_head), fh) != sizeof(tiff_head)) {
        err_WRITE(error);
        fclose(fh);
        return FALSE;
    }

    for (i = 0; i < yres; i++) {
        if (fwrite(pixels + i*rowstride, NCHANNELS, xres, fh) != xres) {
            err_WRITE(error);
            fclose(fh);
            return FALSE;
        }
    }

    fclose(fh);
    return TRUE;
}

#undef Q
#undef W

static gboolean
write_pixbuf_ppm(GdkPixbuf *pixbuf,
                 const gchar *name,
                 const gchar *filename,
                 GError **error)
{
    static const gchar ppm_header[] = "P6\n%u\n%u\n255\n";

    guint xres, yres, rowstride, nchannels, i;
    guchar *pixels;
    gboolean ok = FALSE;
    gchar *ppmh = NULL;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "pnm"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3, FALSE);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);

    ppmh = g_strdup_printf(ppm_header, xres, yres);
    if (fwrite(ppmh, 1, strlen(ppmh), fh) != strlen(ppmh)) {
        err_WRITE(error);
        goto end;
    }

    for (i = 0; i < yres; i++) {
        if (fwrite(pixels + i*rowstride, nchannels, xres, fh) != xres) {
            err_WRITE(error);
            goto end;
        }
    }

    ok = TRUE;

end:
    fclose(fh);
    g_object_unref(pixbuf);
    g_free(ppmh);
    return ok;
}

static gboolean
write_pixbuf_bmp(GdkPixbuf *pixbuf,
                 const gchar *name,
                 const gchar *filename,
                 GError **error)
{
    static guchar bmp_head[] = {
        'B', 'M',    /* magic */
        0, 0, 0, 0,  /* file size */
        0, 0, 0, 0,  /* reserved */
        54, 0, 0, 0, /* offset */
        40, 0, 0, 0, /* header size */
        0, 0, 0, 0,  /* width */
        0, 0, 0, 0,  /* height */
        1, 0,        /* bit planes */
        24, 0,       /* bpp */
        0, 0, 0, 0,  /* compression type */
        0, 0, 0, 0,  /* (compressed) image size */
        0, 0, 0, 0,  /* x resolution */
        0, 0, 0, 0,  /* y resolution */
        0, 0, 0, 0,  /* ncl */
        0, 0, 0, 0,  /* nic */
    };

    guchar *pixels, *buffer = NULL;
    guint i, j, xres, yres, nchannels, rowstride, bmplen, bmprowstride;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "bmp"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3, FALSE);

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);

    bmprowstride = ((nchannels*xres + 3)/4)*4;
    bmplen = yres*bmprowstride + sizeof(bmp_head);

    *(guint32*)(bmp_head + 2) = GUINT32_TO_LE(bmplen);
    *(guint32*)(bmp_head + 18) = GUINT32_TO_LE(xres);
    *(guint32*)(bmp_head + 22) = GUINT32_TO_LE(yres);
    *(guint32*)(bmp_head + 34) = GUINT32_TO_LE(yres*bmprowstride);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    if (fwrite(bmp_head, 1, sizeof(bmp_head), fh) != sizeof(bmp_head)) {
        err_WRITE(error);
        fclose(fh);
        return FALSE;
    }

    /* The ugly part: BMP uses BGR instead of RGB and is written upside down,
     * this silliness may originate nowhere else than in MS... */
    buffer = g_new(guchar, bmprowstride);
    memset(buffer, 0xff, sizeof(bmprowstride));
    for (i = 0; i < yres; i++) {
        const guchar *p = pixels + (yres-1 - i)*rowstride;
        guchar *q = buffer;

        for (j = xres; j; j--, p += 3, q += 3) {
            *q = *(p + 2);
            *(q + 1) = *(p + 1);
            *(q + 2) = *p;
        }
        if (fwrite(buffer, 1, bmprowstride, fh) != bmprowstride) {
            err_WRITE(error);
            fclose(fh);
            g_free(buffer);
            return FALSE;
        }
    }
    g_free(buffer);
    fclose(fh);

    return TRUE;
}

static gboolean
write_pixbuf_targa(GdkPixbuf *pixbuf,
                   const gchar *name,
                   const gchar *filename,
                   GError **error)
{
   static guchar targa_head[] = {
     0,           /* idlength */
     0,           /* colourmaptype */
     2,           /* datatypecode: uncompressed RGB */
     0, 0, 0, 0,  /* colourmaporigin, colourmaplength */
     0,           /* colourmapdepth */
     0, 0, 0, 0,  /* x-origin, y-origin */
     0, 0,        /* width */
     0, 0,        /* height */
     24,          /* bits per pixel */
     0x20,        /* image descriptor flags: origin upper */
    };

    guchar *pixels, *buffer = NULL;
    guint nchannels, xres, yres, rowstride, i, j;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "tga"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3, FALSE);

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);

    if (xres >= 65535 || yres >= 65535) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Image is too large to be stored as TARGA."));
        return FALSE;
    }

    *(guint16*)(targa_head + 12) = GUINT16_TO_LE((guint16)xres);
    *(guint16*)(targa_head + 14) = GUINT16_TO_LE((guint16)yres);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    if (fwrite(targa_head, 1, sizeof(targa_head), fh) != sizeof(targa_head)) {
        err_WRITE(error);
        fclose(fh);
        return FALSE;
    }

    /* The ugly part: TARGA uses BGR instead of RGB */
    buffer = g_new(guchar, nchannels*xres);
    memset(buffer, 0xff, nchannels*xres);
    for (i = 0; i < yres; i++) {
        const guchar *p = pixels + i*rowstride;
        guchar *q = buffer;

        for (j = xres; j; j--, p += 3, q += 3) {
            *q = *(p + 2);
            *(q + 1) = *(p + 1);
            *(q + 2) = *p;
        }
        if (fwrite(buffer, nchannels, xres, fh) != xres) {
            err_WRITE(error);
            fclose(fh);
            g_free(buffer);
            return FALSE;
        }
    }
    fclose(fh);
    g_free(buffer);

    return TRUE;
}

#ifdef HAVE_WEBP
static gboolean
write_pixbuf_webp(GdkPixbuf *pixbuf,
                  const gchar *name,
                  const gchar *filename,
                  GError **error)
{
    const guchar *pixels;
    guchar *buffer = NULL;
    guint xres, yres, nchannels, rowstride;
    size_t size;
    gboolean ok;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "webp"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3 || nchannels == 4, FALSE);

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    if (nchannels == 3)
        size = WebPEncodeLosslessRGB(pixels, xres, yres, rowstride, &buffer);
    else if (nchannels == 4)
        size = WebPEncodeLosslessRGBA(pixels, xres, yres, rowstride, &buffer);
    else {
        g_assert_not_reached();
    }

    ok = (fwrite(buffer, 1, size, fh) == size);
    if (!ok)
        err_WRITE(error);

    fclose(fh);
    /* XXX: Version at least 0.5.0 needed for WebPFree(buffer); */
    free(buffer);

    return ok;
}
#endif

/* Borrowed from libgwyui4 */
static void
draw_ellipse(cairo_t *cr,
             gdouble x, gdouble y, gdouble xr, gdouble yr)
{
    const gdouble q = 0.552;

    cairo_move_to(cr, x + xr, y);
    cairo_curve_to(cr, x + xr, y + q*yr, x + q*xr, y + yr, x, y + yr);
    cairo_curve_to(cr, x - q*xr, y + yr, x - xr, y + q*yr, x - xr, y);
    cairo_curve_to(cr, x - xr, y - q*yr, x - q*xr, y - yr, x, y - yr);
    cairo_curve_to(cr, x + q*xr, y - yr, x + xr, y - q*yr, x + xr, y);
    cairo_close_path(cr);
}

static void
draw_sel_axis(const ImgExportArgs *args,
              const ImgExportSizes *sizes,
              GwySelection *sel,
              gdouble qx, gdouble qy,
              G_GNUC_UNUSED PangoLayout *layout,
              G_GNUC_UNUSED GString *s,
              cairo_t *cr)
{
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    const GwyRGBA *outcolour = &args->sel_outline_color;
    gdouble p, xy[1];
    GwyOrientation orientation;
    gdouble w = sizes->image.w - 2.0*lw;
    gdouble h = sizes->image.h - 2.0*lw;
    guint n, i;

    g_object_get(sel, "orientation", &orientation, NULL);
    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            if (orientation == GWY_ORIENTATION_HORIZONTAL) {
                p = qy*xy[0];
                draw_line_outline(cr, 0.0, p, w, p, outcolour, lw, olw);
            }
            else {
                p = qx*xy[0];
                draw_line_outline(cr, p, 0.0, p, h, outcolour, lw, olw);
            }
        }
    }
    if (lw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            if (orientation == GWY_ORIENTATION_HORIZONTAL) {
                p = qy*xy[0];
                cairo_move_to(cr, 0.0, p);
                cairo_line_to(cr, w, p);
            }
            else {
                p = qx*xy[0];
                cairo_move_to(cr, p, 0.0);
                cairo_line_to(cr, p, h);
            }
            cairo_stroke(cr);
        }
    }
}

static void
draw_sel_cross(const ImgExportArgs *args,
               const ImgExportSizes *sizes,
               GwySelection *sel,
               gdouble qx, gdouble qy,
               G_GNUC_UNUSED PangoLayout *layout,
               G_GNUC_UNUSED GString *s,
               cairo_t *cr)
{
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    const GwyRGBA *outcolour = &args->sel_outline_color;
    gdouble p, xy[2];
    gdouble w = sizes->image.w - 2.0*lw;
    gdouble h = sizes->image.h - 2.0*lw;
    guint n, i;

    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            p = qy*xy[1];
            draw_line_outline(cr, 0.0, p, w, p, outcolour, lw, olw);
            p = qx*xy[0];
            draw_line_outline(cr, p, 0.0, p, h, outcolour, lw, olw);
        }
    }
    if (lw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            p = qy*xy[1];
            cairo_move_to(cr, 0.0, p);
            cairo_line_to(cr, w, p);
            cairo_stroke(cr);

            p = qx*xy[0];
            cairo_move_to(cr, p, 0.0);
            cairo_line_to(cr, p, h);
            cairo_stroke(cr);
        }
    }
}

static void
draw_sel_ellipse(const ImgExportArgs *args,
                 const ImgExportSizes *sizes,
                 GwySelection *sel,
                 gdouble qx, gdouble qy,
                 G_GNUC_UNUSED PangoLayout *layout,
                 G_GNUC_UNUSED GString *s,
                 cairo_t *cr)
{
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    const GwyRGBA *colour = &args->sel_color;
    const GwyRGBA *outcolour = &args->sel_outline_color;
    gdouble xf, yf, xt, yt, xy[4];
    guint n, i;

    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            xf = qx*xy[0];
            yf = qy*xy[1];
            xt = qx*xy[2];
            yt = qy*xy[3];
            draw_ellipse(cr,
                         0.5*(xf + xt), 0.5*(yf + yt),
                         0.5*(xt - xf), 0.5*(yt - yf));
            stroke_path_outline(cr, outcolour, lw, olw);
        }
    }
    if (lw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            xf = qx*xy[0];
            yf = qy*xy[1];
            xt = qx*xy[2];
            yt = qy*xy[3];
            draw_ellipse(cr,
                         0.5*(xf + xt), 0.5*(yf + yt),
                         0.5*(xt - xf), 0.5*(yt - yf));
            stroke_path(cr, colour, lw);
        }
    }
}

static void
draw_sel_line(const ImgExportArgs *args,
              const ImgExportSizes *sizes,
              GwySelection *sel,
              gdouble qx, gdouble qy,
              PangoLayout *layout,
              GString *s,
              cairo_t *cr)
{
    gdouble lw = sizes->sizes.line_width;
    gdouble lt = args->sel_line_thickness;
    gdouble olw = sizes->sizes.sel_outline_width;
    const GwyRGBA *colour = &args->sel_color;
    const GwyRGBA *outcolour = &args->sel_outline_color;
    gdouble px, py, xf, yf, xt, yt, xy[4];
    guint n, i;

    px = sizes->image.w/gwy_data_field_get_xres(args->env->dfield);
    py = sizes->image.h/gwy_data_field_get_yres(args->env->dfield);

    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            xf = qx*xy[0];
            yf = qy*xy[1];
            xt = qx*xy[2];
            yt = qy*xy[3];

            draw_line_outline(cr, xf, yf, xt, yt, outcolour, lw, olw);
            if (lt > 0.0) {
                gdouble xd = yt - yf, yd = xf - xt;
                gdouble len = sqrt(xd*xd + yd*yd);
                xd *= lt*px/len;
                yd *= lt*py/len;

                draw_line_outline(cr,
                                  xf - 0.5*xd, yf - 0.5*yd,
                                  xf + 0.5*xd, yf + 0.5*yd,
                                  outcolour, lw, olw);
                draw_line_outline(cr,
                                  xt - 0.5*xd, yt - 0.5*yd,
                                  xt + 0.5*xd, yt + 0.5*yd,
                                  outcolour, lw, olw);
            }

            if (args->sel_number_objects) {
                PangoRectangle logical;
                gdouble xc = 0.5*(xf + xt), yc = 0.5*(yf + yt);
                gdouble xd = yt - yf, yd = xf - xt;
                gdouble len = sqrt(xd*xd + yd*yd);

                if (yd < -1e-14) {
                    xd = -xd;
                    yd = -yd;
                }
                xd /= len;
                yd /= len;
                format_layout(layout, &logical, s, "%u", i+1);
                xc -= 0.5*logical.width/pangoscale;
                yc -= 0.5*logical.height/pangoscale;
                xd *= (0.5*lw + 0.45*logical.height/pangoscale);
                yd *= (0.5*lw + 0.45*logical.height/pangoscale);
                cairo_save(cr);
                cairo_move_to(cr, xc + xd, yc + yd);
                draw_text_outline(cr, layout, outcolour, olw);
                cairo_restore(cr);
            }
        }
    }
    for (i = 0; i < n; i++) {
        gwy_selection_get_object(sel, i, xy);
        xf = qx*xy[0];
        yf = qy*xy[1];
        xt = qx*xy[2];
        yt = qy*xy[3];

        cairo_move_to(cr, xf, yf);
        cairo_line_to(cr, xt, yt);

        gwy_debug("sel_line_thickness %g", lt);
        if (lt > 0.0) {
            gdouble xd = yt - yf, yd = xf - xt;
            gdouble len = sqrt(xd*xd + yd*yd);
            xd *= lt*px/len;
            yd *= lt*py/len;

            cairo_move_to(cr, xf - 0.5*xd, yf - 0.5*yd);
            cairo_rel_line_to(cr, xd, yd);
            cairo_move_to(cr, xt - 0.5*xd, yt - 0.5*yd);
            cairo_rel_line_to(cr, xd, yd);
        }

        set_cairo_source_rgb(cr, colour);
        cairo_stroke(cr);

        if (args->sel_number_objects) {
            PangoRectangle logical;
            gdouble xc = 0.5*(xf + xt), yc = 0.5*(yf + yt);
            gdouble xd = yt - yf, yd = xf - xt;
            gdouble len = sqrt(xd*xd + yd*yd);

            if (yd < -1e-14) {
                xd = -xd;
                yd = -yd;
            }
            xd /= len;
            yd /= len;
            format_layout(layout, &logical, s, "%u", i+1);
            xc -= 0.5*logical.width/pangoscale;
            yc -= 0.5*logical.height/pangoscale;
            xd *= (0.5*lw + 0.45*logical.height/pangoscale);
            yd *= (0.5*lw + 0.45*logical.height/pangoscale);
            cairo_save(cr);
            cairo_move_to(cr, xc + xd, yc + yd);
            draw_text(cr, layout, colour);
            cairo_restore(cr);
        }
    }
}

static void
draw_sel_point(const ImgExportArgs *args,
               const ImgExportSizes *sizes,
               GwySelection *sel,
               gdouble qx, gdouble qy,
               PangoLayout *layout,
               GString *s,
               cairo_t *cr)
{
    gdouble tl = G_SQRT2*sizes->sizes.tick_length;
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    const GwyRGBA *colour = &args->sel_color;
    const GwyRGBA *outcolour = &args->sel_outline_color;
    gdouble pr = args->sel_point_radius;
    gdouble px, py, x, y, xy[2];
    guint n, i;

    px = sizes->image.w/gwy_data_field_get_xres(args->env->dfield);
    py = sizes->image.h/gwy_data_field_get_yres(args->env->dfield);

    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            x = qx*xy[0];
            y = qy*xy[1];
            draw_line_outline(cr, x - 0.5*tl, y, x + 0.5*tl, y,
                              outcolour, lw, olw);
            draw_line_outline(cr, x, y - 0.5*tl, x, y + 0.5*tl,
                              outcolour, lw, olw);

            cairo_save(cr);
            if (args->sel_point_radius > 0.0) {
                gdouble xr = pr*px, yr = pr*py;
                draw_ellipse(cr, x, y, xr, yr);
                stroke_path_outline(cr, outcolour, lw, olw);
            }

            if (args->sel_number_objects) {
                PangoRectangle logical;

                format_layout(layout, &logical, s, "%u", i+1);
                cairo_move_to(cr,
                            x + lw + 0.05*logical.height/pangoscale,
                            y + lw + 0.05*logical.height/pangoscale);
                draw_text_outline(cr, layout, outcolour, olw);
            }
            cairo_restore(cr);
        }
    }
    for (i = 0; i < n; i++) {
        gwy_selection_get_object(sel, i, xy);
        x = qx*xy[0];
        y = qy*xy[1];
        cairo_move_to(cr, x - 0.5*tl, y);
        cairo_rel_line_to(cr, tl, 0.0);
        cairo_move_to(cr, x, y - 0.5*tl);
        cairo_rel_line_to(cr, 0.0, tl);
        cairo_stroke(cr);

        cairo_save(cr);
        if (args->sel_point_radius > 0.0) {
            gdouble xr = pr*px, yr = pr*py;
            draw_ellipse(cr, x, y, xr, yr);
            stroke_path(cr, colour, lw);
        }

        if (args->sel_number_objects) {
            PangoRectangle logical;

            format_layout(layout, &logical, s, "%u", i+1);
            cairo_move_to(cr,
                          x + lw + 0.05*logical.height/pangoscale,
                          y + lw + 0.05*logical.height/pangoscale);
            draw_text(cr, layout, colour);
        }
        cairo_restore(cr);
    }
}

static void
draw_sel_rectangle(const ImgExportArgs *args,
                   const ImgExportSizes *sizes,
                   GwySelection *sel,
                   gdouble qx, gdouble qy,
                   G_GNUC_UNUSED PangoLayout *layout,
                   G_GNUC_UNUSED GString *s,
                   cairo_t *cr)
{
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    const GwyRGBA *colour = &args->sel_color;
    const GwyRGBA *outcolour = &args->sel_outline_color;
    gdouble xf, yf, xt, yt, xy[4];
    guint n, i;

    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            xf = qx*xy[0];
            yf = qy*xy[1];
            xt = qx*xy[2];
            yt = qy*xy[3];
            cairo_rectangle(cr, xf, yf, xt - xf, yt - yf);
            stroke_path_outline(cr, outcolour, lw, olw);
        }
    }
    if (lw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            xf = qx*xy[0];
            yf = qy*xy[1];
            xt = qx*xy[2];
            yt = qy*xy[3];
            cairo_rectangle(cr, xf, yf, xt - xf, yt - yf);
            stroke_path(cr, colour, lw);
        }
    }
}

static void
draw_sel_lattice(const ImgExportArgs *args,
                 const ImgExportSizes *sizes,
                 GwySelection *sel,
                 gdouble qx, gdouble qy,
                 G_GNUC_UNUSED PangoLayout *layout,
                 G_GNUC_UNUSED GString *s,
                 cairo_t *cr)
{
    enum { maxlines = 80 };

    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    const GwyRGBA *colour = &args->sel_color;
    const GwyRGBA *outcolour = &args->sel_outline_color;
    gdouble xf, yf, xt, yt, xy[4];
    gdouble w = sizes->image.w - 2.0*lw;
    gdouble h = sizes->image.h - 2.0*lw;
    guint n;
    gint i;

    n = gwy_selection_get_data(sel, NULL);
    if (n < 1)
        return;

    /* XXX: Draw the first lattice.  It makes little sense to have multiple
     * objects in this selection type. */
    gwy_selection_get_object(sel, 0, xy);
    if (olw > 0.0) {
        for (i = -maxlines; i <= maxlines; i++) {
            xf = qx*(i*xy[0] - maxlines*xy[2]) + 0.5*w;
            yf = qy*(i*xy[1] - maxlines*xy[3]) + 0.5*h;
            xt = qx*(i*xy[0] + maxlines*xy[2]) + 0.5*w;
            yt = qy*(i*xy[1] + maxlines*xy[3]) + 0.5*h;
            cairo_move_to(cr, xf, yf);
            cairo_line_to(cr, xt, yt);
        }
        for (i = -maxlines; i <= maxlines; i++) {
            xf = qx*(-maxlines*xy[0] + i*xy[2]) + 0.5*w;
            yf = qy*(-maxlines*xy[1] + i*xy[3]) + 0.5*h;
            xt = qx*(maxlines*xy[0] + i*xy[2]) + 0.5*w;
            yt = qy*(maxlines*xy[1] + i*xy[3]) + 0.5*h;
            cairo_move_to(cr, xf, yf);
            cairo_line_to(cr, xt, yt);
        }
        stroke_path_outline(cr, outcolour, lw, olw);
    }
    if (lw > 0.0) {
        for (i = -maxlines; i <= maxlines; i++) {
            xf = qx*(i*xy[0] - maxlines*xy[2]) + 0.5*w;
            yf = qy*(i*xy[1] - maxlines*xy[3]) + 0.5*h;
            xt = qx*(i*xy[0] + maxlines*xy[2]) + 0.5*w;
            yt = qy*(i*xy[1] + maxlines*xy[3]) + 0.5*h;
            cairo_move_to(cr, xf, yf);
            cairo_line_to(cr, xt, yt);
        }
        for (i = -maxlines; i <= maxlines; i++) {
            xf = qx*(-maxlines*xy[0] + i*xy[2]) + 0.5*w;
            yf = qy*(-maxlines*xy[1] + i*xy[3]) + 0.5*h;
            xt = qx*(maxlines*xy[0] + i*xy[2]) + 0.5*w;
            yt = qy*(maxlines*xy[1] + i*xy[3]) + 0.5*h;
            cairo_move_to(cr, xf, yf);
            cairo_line_to(cr, xt, yt);
        }
        stroke_path(cr, colour, lw);
    }
}

static void
draw_sel_path(const ImgExportArgs *args,
              const ImgExportSizes *sizes,
              GwySelection *sel,
              gdouble qx, gdouble qy,
              G_GNUC_UNUSED PangoLayout *layout,
              G_GNUC_UNUSED GString *s,
              cairo_t *cr)
{
    gboolean is_vector = !!args->env->format->write_vector;
    gdouble lw = sizes->sizes.line_width;
    gdouble lt = args->sel_line_thickness;
    gdouble olw = sizes->sizes.sel_outline_width;
    const GwyRGBA *colour = &args->sel_color;
    const GwyRGBA *outcolour = &args->sel_outline_color;
    gdouble slackness, q, px, py, vx, vy, len, xy[2];
    GwyXY *pts;
    const GwyXY *natpts, *tangents;
    GwySpline *spline;
    gboolean closed;
    guint n, nn, i;

    g_object_get(sel, "slackness", &slackness, "closed", &closed, NULL);
    n = gwy_selection_get_data(sel, NULL);
    if (n < 2)
        return;

    px = sizes->image.w/gwy_data_field_get_xres(args->env->dfield);
    py = sizes->image.h/gwy_data_field_get_yres(args->env->dfield);

    /* XXX: This is dirty.  Unfortunately, we need to know the natural units
     * for good spline sampling and the vector ones are too coarse.   Hence
     * we artificially refine them and cross fingers. */
    q = is_vector ? 8.0 : 1.0;
    pts = g_new(GwyXY, n);
    for (i = 0; i < n; i++) {
        gwy_selection_get_object(sel, i, xy);
        pts[i].x = q*qx*xy[0];
        pts[i].y = q*qy*xy[1];
    }
    spline = gwy_spline_new_from_points(pts, n);
    gwy_spline_set_slackness(spline, slackness);
    gwy_spline_set_closed(spline, closed);

    tangents = gwy_spline_get_tangents(spline);
    natpts = gwy_spline_sample_naturally(spline, &nn);
    g_return_if_fail(nn >= 2);

    /* Path outline */
    if (olw > 0.0) {
        cairo_save(cr);
        cairo_set_line_width(cr, lw + 2.0*olw);
        set_cairo_source_rgb(cr, outcolour);

        if (closed)
            cairo_move_to(cr, natpts[0].x/q, natpts[0].y/q);
        else {
            /* BUTT caps */
            vx = natpts[0].x - natpts[1].x;
            vy = natpts[0].y - natpts[1].y;
            len = sqrt(vx*vx + vy*vy);
            vx *= olw/len;
            vy *= olw/len;
            cairo_move_to(cr, natpts[0].x/q + vx, natpts[0].y/q + vy);
        }

        for (i = 1; i < nn-1; i++)
            cairo_line_to(cr, natpts[i].x/q, natpts[i].y/q);

        if (closed) {
            cairo_line_to(cr, natpts[nn-1].x/q, natpts[nn-1].y/q);
            cairo_close_path(cr);
        }
        else {
            /* BUTT caps */
            vx = natpts[nn-1].x - natpts[nn-2].x;
            vy = natpts[nn-1].y - natpts[nn-2].y;
            len = sqrt(vx*vx + vy*vy);
            vx *= olw/len;
            vy *= olw/len;
            cairo_line_to(cr, natpts[nn-1].x/q + vx, natpts[nn-1].y/q + vy);
        }

        cairo_stroke(cr);
        cairo_restore(cr);
    }

    /* Tick outline */
    if (olw > 0.0 && lt > 0.0) {
        for (i = 0; i < n; i++) {
            vx = tangents[i].y;
            vy = -tangents[i].x;
            len = sqrt(vx*vx + vy*vy);
            vx *= lt*px/len;
            vy *= lt*py/len;
            draw_line_outline(cr,
                              pts[i].x/q - 0.5*vx, pts[i].y/q - 0.5*vy,
                              pts[i].x/q + 0.5*vx, pts[i].y/q + 0.5*vy,
                              outcolour, lw, olw);
        }
    }

    /* Path */
    if (lw > 0.0) {
        cairo_set_line_width(cr, lw);
        set_cairo_source_rgb(cr, colour);
        cairo_move_to(cr, natpts[0].x/q, natpts[0].y/q);
        for (i = 1; i < nn; i++)
            cairo_line_to(cr, natpts[i].x/q, natpts[i].y/q);
        if (closed)
            cairo_close_path(cr);
        cairo_stroke(cr);
    }

    /* Tick */
    if (lw > 0.0 && lt > 0.0) {
        for (i = 0; i < n; i++) {
            vx = tangents[i].y;
            vy = -tangents[i].x;
            len = sqrt(vx*vx + vy*vy);
            vx *= lt*px/len;
            vy *= lt*py/len;
            cairo_move_to(cr, pts[i].x/q - 0.5*vx, pts[i].y/q - 0.5*vy);
            cairo_line_to(cr, pts[i].x/q + 0.5*vx, pts[i].y/q + 0.5*vy);
        }
        cairo_stroke(cr);
    }

    gwy_spline_free(spline);
    g_free(pts);
}

static void
sel_number_objects_changed(ImgExportControls *controls,
                           GtkToggleButton *toggle)
{
    controls->args->sel_number_objects = gtk_toggle_button_get_active(toggle);
    update_preview(controls);
}

static void
sel_line_thickness_changed(ImgExportControls *controls,
                           GtkAdjustment *adj)
{
    controls->args->sel_line_thickness = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static void
sel_point_radius_changed(ImgExportControls *controls,
                         GtkAdjustment *adj)
{
    controls->args->sel_point_radius = gtk_adjustment_get_value(adj);
    update_preview(controls);
}

static GSList*
add_table_row_to_list(GtkWidget *table, gint row, guint ncols, GSList *list)
{
    GtkWidget *w;
    guint i;

    for (i = 0; i < ncols; i++) {
        w = gwy_table_get_child_widget(table, row, i);
        list = g_slist_prepend(list, w);
    }
    return list;
}

static void
options_sel_line(ImgExportControls *controls)
{
    GtkTable *table = GTK_TABLE(controls->table_selection);
    ImgExportArgs *args = controls->args;
    GtkWidget *check;
    GtkObject *adj;
    gint row = controls->sel_row_start;

    check = gtk_check_button_new_with_mnemonic(_("Draw _numbers"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 args->sel_number_objects);
    gtk_table_attach(table, check, 0, 3, row, row+1,
                     GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(check, "toggled",
                             G_CALLBACK(sel_number_objects_changed), controls);
    controls->sel_options = g_slist_prepend(controls->sel_options, check);
    row++;

    adj = gtk_adjustment_new(args->sel_line_thickness, 0.0, 128.0, 1.0, 5.0, 0);
    gwy_table_attach_spinbutton(GTK_WIDGET(table), row,
                                _("_End marker length:"), "px", adj);
    controls->sel_options = add_table_row_to_list(GTK_WIDGET(table), row, 3,
                                                  controls->sel_options);
    g_signal_connect_swapped(adj, "value-changed",
                             G_CALLBACK(sel_line_thickness_changed), controls);
    row++;
}

static void
options_sel_point(ImgExportControls *controls)
{
    GtkTable *table = GTK_TABLE(controls->table_selection);
    ImgExportArgs *args = controls->args;
    GtkWidget *check;
    GtkObject *adj;
    gint row = controls->sel_row_start;

    check = gtk_check_button_new_with_mnemonic(_("Draw _numbers"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                 args->sel_number_objects);
    g_signal_connect_swapped(check, "toggled",
                             G_CALLBACK(sel_number_objects_changed), controls);
    gtk_table_attach(table, check, 0, 3, row, row+1,
                     GTK_FILL, 0, 0, 0);
    controls->sel_options = g_slist_prepend(controls->sel_options, check);
    row++;

    adj = gtk_adjustment_new(args->sel_point_radius, 0.0, 1024.0, 1.0, 10.0, 0);
    gwy_table_attach_spinbutton(GTK_WIDGET(table), row,
                                _("Marker _radius:"), "px", adj);
    controls->sel_options = add_table_row_to_list(GTK_WIDGET(table), row, 3,
                                                  controls->sel_options);
    g_signal_connect_swapped(adj, "value-changed",
                             G_CALLBACK(sel_point_radius_changed), controls);
    row++;
}

static void
options_sel_path(ImgExportControls *controls)
{
    GtkTable *table = GTK_TABLE(controls->table_selection);
    ImgExportArgs *args = controls->args;
    GtkObject *adj;
    gint row = controls->sel_row_start;

    adj = gtk_adjustment_new(args->sel_line_thickness, 0.0, 1024.0, 1.0, 5.0,
                             0);
    gwy_table_attach_spinbutton(GTK_WIDGET(table), row,
                                _("_End marker length:"), "px", adj);
    controls->sel_options = add_table_row_to_list(GTK_WIDGET(table), row, 3,
                                                  controls->sel_options);
    g_signal_connect_swapped(adj, "value-changed",
                             G_CALLBACK(sel_line_thickness_changed), controls);
    row++;
}

/* Use the pixmap prefix for compatibility */
static const gchar active_page_key[]           = "/module/pixmap/active_page";
static const gchar bg_color_key[]              = "/module/pixmap/bg_color";
static const gchar border_width_key[]          = "/module/pixmap/border_width";
static const gchar decomma_key[]               = "/module/pixmap/decomma";
static const gchar draw_frame_key[]            = "/module/pixmap/draw_frame";
static const gchar draw_maskkey_key[]          = "/module/pixmap/draw_maskkey";
static const gchar draw_mask_key[]             = "/module/pixmap/draw_mask";
static const gchar draw_selection_key[]        = "/module/pixmap/draw_selection";
static const gchar fix_fmscale_precision_key[] = "/module/pixmap/fix_fmscale_precision";
static const gchar fix_kilo_threshold_key[]    = "/module/pixmap/fix_kilo_threshold";
static const gchar fmscale_gap_key[]           = "/module/pixmap/fmscale_gap";
static const gchar fmscale_precision_key[]     = "/module/pixmap/fmscale_precision";
static const gchar font_key[]                  = "/module/pixmap/font";
static const gchar font_size_key[]             = "/module/pixmap/font_size";
static const gchar inset_color_key[]           = "/module/pixmap/inset_color";
static const gchar inset_draw_label_key[]      = "/module/pixmap/inset_draw_label";
static const gchar inset_draw_text_above_key[] = "/module/pixmap/inset_draw_text_above";
static const gchar inset_draw_ticks_key[]      = "/module/pixmap/inset_draw_ticks";
static const gchar inset_length_key[]          = "/module/pixmap/inset_length";
static const gchar inset_outline_color_key[]   = "/module/pixmap/inset_outline_color";
static const gchar inset_outline_width_key[]   = "/module/pixmap/inset_outline_width";
static const gchar inset_pos_key[]             = "/module/pixmap/inset_pos";
static const gchar inset_xgap_key[]            = "/module/pixmap/inset_xgap";
static const gchar inset_ygap_key[]            = "/module/pixmap/inset_ygap";
static const gchar interpolation_key[]         = "/module/pixmap/interpolation";
static const gchar kilo_threshold_key[]        = "/module/pixmap/kilo_threshold";
static const gchar linetext_color_key[]        = "/module/pixmap/linetext_color";
static const gchar line_width_key[]            = "/module/pixmap/line_width";
static const gchar maskkey_gap_key[]           = "/module/pixmap/maskkey_gap";
static const gchar mask_key_key[]              = "/module/pixmap/mask_key";
static const gchar mode_key[]                  = "/module/pixmap/mode";
static const gchar pxwidth_key[]               = "/module/pixmap/pxwidth";
static const gchar scale_font_key[]            = "/module/pixmap/scale_font";
static const gchar sel_color_key[]             = "/module/pixmap/sel_color";
static const gchar selection_key[]             = "/module/pixmap/selection";
static const gchar sel_line_thickness_key[]    = "/module/pixmap/sel_line_thickness";
static const gchar sel_number_objects_key[]    = "/module/pixmap/sel_number_objects";
static const gchar sel_outline_color_key[]     = "/module/pixmap/sel_outline_color";
static const gchar sel_outline_width_key[]     = "/module/pixmap/sel_outline_width";
static const gchar sel_point_radius_key[]      = "/module/pixmap/sel_point_radius";
static const gchar tick_length_key[]           = "/module/pixmap/tick_length";
static const gchar title_gap_key[]             = "/module/pixmap/title_gap";
static const gchar title_type_key[]            = "/module/pixmap/title_type";
static const gchar transparent_bg_key[]        = "/module/pixmap/transparent_bg";
static const gchar units_in_title_key[]        = "/module/pixmap/units_in_title";
static const gchar xytype_key[]                = "/module/pixmap/xytype";
static const gchar zoom_key[]                  = "/module/pixmap/zoom";
static const gchar ztype_key[]                 = "/module/pixmap/ztype";

static void
select_a_real_font(ImgExportArgs *args, GtkWidget *widget)
{
    static const gchar *fonts_to_try[] = {
        /* Linux */
        "Liberation Sans",
        "Nimbus Sans L",
        /* OS X */
        "Lucida Grande",
        "Helvetica Neue",
        /* Windows, but Arial is quite ubiquitous. */
        "Arial",
        "Helvetica",
        /* Alias, can be something odd... */
        "Sans",
    };

    PangoContext *context;
    PangoFontFamily **families = NULL;
    const gchar *name, *cname;
    gchar *currname;
    gint nfamilies, i;
    guint j;

    context = gtk_widget_get_pango_context(widget);
    pango_context_list_families(context, &families, &nfamilies);

    /* Handle possible trailing comma in the font name. */
    j = strlen(args->font);
    if (j > 0 && args->font[j-1] == ',')
        j--;
    currname = g_strndup(args->font, j);

    for (i = 0; i < nfamilies; i++) {
        name = pango_font_family_get_name(families[i]);
        gwy_debug("available family <%s>", name);
        if (g_ascii_strcasecmp(currname, name) == 0) {
            /* The font from settings seems available.   Use it. */
            gwy_debug("found font %s", currname);
            g_free(currname);
            g_free(families);
            return;
        }
    }
    gwy_debug("did not find font %s", currname);
    g_free(currname);

    /* We do not have the font from settings.  Try to find some other sane
     * sans font. */
    for (j = 0; j < G_N_ELEMENTS(fonts_to_try); j++) {
        cname = fonts_to_try[j];
        for (i = 0; i < nfamilies; i++) {
            name = pango_font_family_get_name(families[i]);
            if (g_ascii_strcasecmp(cname, name) == 0) {
                gwy_debug("found font %s", cname);
                g_free(args->font);
                args->font = g_strdup(cname);
                g_free(families);
                return;
            }
        }
    }

    /* Shrug and proceed... */
    g_free(families);
}

static void
img_export_free_env(ImgExportEnv *env)
{
    if (env->grey)
        gwy_resource_release(GWY_RESOURCE(env->grey));
    gwy_resource_release(GWY_RESOURCE(env->gradient));
    g_free(env->title);
    g_free(env->decimal_symbol);
    g_array_free(env->selections, TRUE);
}

static void
img_export_load_args(GwyContainer *container,
                     ImgExportArgs *args)
{
    *args = img_export_defaults;

    gwy_container_gis_int32_by_name(container, active_page_key,
                                    &args->active_page);
    gwy_container_gis_double_by_name(container, zoom_key, &args->zoom);
    gwy_container_gis_double_by_name(container, pxwidth_key, &args->pxwidth);
    gwy_container_gis_double_by_name(container, font_size_key,
                                     &args->sizes.font_size);
    gwy_container_gis_double_by_name(container, line_width_key,
                                     &args->sizes.line_width);
    gwy_container_gis_double_by_name(container, inset_outline_width_key,
                                     &args->sizes.inset_outline_width);
    gwy_container_gis_double_by_name(container, sel_outline_width_key,
                                     &args->sizes.sel_outline_width);
    gwy_container_gis_double_by_name(container, border_width_key,
                                     &args->sizes.border_width);
    gwy_container_gis_double_by_name(container, tick_length_key,
                                     &args->sizes.tick_length);
    gwy_container_gis_enum_by_name(container, mode_key, &args->mode);
    gwy_container_gis_enum_by_name(container, xytype_key, &args->xytype);
    gwy_container_gis_enum_by_name(container, ztype_key, &args->ztype);
    gwy_container_gis_enum_by_name(container, interpolation_key,
                                   &args->interpolation);
    gwy_container_gis_enum_by_name(container, title_type_key,
                                   &args->title_type);
    gwy_container_gis_boolean_by_name(container, transparent_bg_key,
                                      &args->transparent_bg);
    gwy_rgba_get_from_container(&args->bg_color, container, bg_color_key);
    gwy_rgba_get_from_container(&args->linetext_color, container,
                                linetext_color_key);
    gwy_rgba_get_from_container(&args->inset_color, container, inset_color_key);
    gwy_rgba_get_from_container(&args->sel_color, container, sel_color_key);
    gwy_rgba_get_from_container(&args->inset_outline_color, container,
                                inset_outline_color_key);
    gwy_rgba_get_from_container(&args->sel_outline_color, container,
                                sel_outline_color_key);
    gwy_container_gis_enum_by_name(container, inset_pos_key, &args->inset_pos);
    gwy_container_gis_string_by_name(container, inset_length_key,
                                     (const guchar**)&args->inset_length);
    gwy_container_gis_boolean_by_name(container, draw_frame_key,
                                      &args->draw_frame);
    gwy_container_gis_boolean_by_name(container, draw_mask_key,
                                      &args->draw_mask);
    gwy_container_gis_boolean_by_name(container, draw_maskkey_key,
                                      &args->draw_maskkey);
    gwy_container_gis_boolean_by_name(container, draw_selection_key,
                                      &args->draw_selection);
    gwy_container_gis_string_by_name(container, mask_key_key,
                                     (const guchar**)&args->mask_key);
    gwy_container_gis_string_by_name(container, font_key,
                                     (const guchar**)&args->font);
    gwy_container_gis_boolean_by_name(container, scale_font_key,
                                      &args->scale_font);
    gwy_container_gis_boolean_by_name(container, decomma_key,
                                      &args->decomma);
    gwy_container_gis_double_by_name(container, fmscale_gap_key,
                                     &args->fmscale_gap);
    gwy_container_gis_double_by_name(container, inset_xgap_key,
                                     &args->inset_xgap);
    gwy_container_gis_double_by_name(container, inset_ygap_key,
                                     &args->inset_ygap);
    gwy_container_gis_double_by_name(container, title_gap_key,
                                     &args->title_gap);
    gwy_container_gis_double_by_name(container, maskkey_gap_key,
                                     &args->maskkey_gap);
    gwy_container_gis_boolean_by_name(container, fix_fmscale_precision_key,
                                      &args->fix_fmscale_precision);
    gwy_container_gis_int32_by_name(container, fmscale_precision_key,
                                    &args->fmscale_precision);
    gwy_container_gis_boolean_by_name(container, fix_kilo_threshold_key,
                                      &args->fix_kilo_threshold);
    gwy_container_gis_double_by_name(container, kilo_threshold_key,
                                     &args->kilo_threshold);
    gwy_container_gis_boolean_by_name(container, inset_draw_ticks_key,
                                      &args->inset_draw_ticks);
    gwy_container_gis_boolean_by_name(container, inset_draw_label_key,
                                      &args->inset_draw_label);
    gwy_container_gis_boolean_by_name(container, inset_draw_text_above_key,
                                      &args->inset_draw_text_above);
    gwy_container_gis_boolean_by_name(container, units_in_title_key,
                                      &args->units_in_title);
    gwy_container_gis_string_by_name(container, selection_key,
                                     (const guchar**)&args->selection);
    gwy_container_gis_boolean_by_name(container, sel_number_objects_key,
                                      &args->sel_number_objects);
    gwy_container_gis_double_by_name(container, sel_line_thickness_key,
                                     &args->sel_line_thickness);
    gwy_container_gis_double_by_name(container, sel_point_radius_key,
                                     &args->sel_point_radius);

    img_export_unconst_args(args);
    img_export_sanitize_args(args);
}

static void
img_export_save_args(GwyContainer *container,
                     ImgExportArgs *args)
{
    gwy_container_set_int32_by_name(container, active_page_key,
                                    args->active_page);
    gwy_container_set_double_by_name(container, zoom_key, args->zoom);
    gwy_container_set_double_by_name(container, pxwidth_key, args->pxwidth);
    gwy_container_set_double_by_name(container, font_size_key,
                                     args->sizes.font_size);
    gwy_container_set_double_by_name(container, line_width_key,
                                     args->sizes.line_width);
    gwy_container_set_double_by_name(container, inset_outline_width_key,
                                     args->sizes.inset_outline_width);
    gwy_container_set_double_by_name(container, sel_outline_width_key,
                                     args->sizes.sel_outline_width);
    gwy_container_set_double_by_name(container, border_width_key,
                                     args->sizes.border_width);
    gwy_container_set_double_by_name(container, tick_length_key,
                                     args->sizes.tick_length);
    gwy_container_set_enum_by_name(container, mode_key, args->mode);
    gwy_container_set_enum_by_name(container, xytype_key, args->xytype);
    gwy_container_set_enum_by_name(container, ztype_key, args->ztype);
    gwy_container_set_enum_by_name(container, interpolation_key,
                                   args->interpolation);
    gwy_container_set_enum_by_name(container, title_type_key,
                                   args->title_type);
    gwy_container_set_boolean_by_name(container, transparent_bg_key,
                                      args->transparent_bg);
    gwy_rgba_store_to_container(&args->linetext_color, container,
                                linetext_color_key);
    gwy_rgba_store_to_container(&args->bg_color, container, bg_color_key);
    gwy_rgba_store_to_container(&args->inset_color, container, inset_color_key);
    gwy_rgba_store_to_container(&args->sel_color, container, sel_color_key);
    gwy_rgba_store_to_container(&args->inset_outline_color, container,
                                inset_outline_color_key);
    gwy_rgba_store_to_container(&args->sel_outline_color, container,
                                sel_outline_color_key);
    gwy_container_set_enum_by_name(container, inset_pos_key, args->inset_pos);
    gwy_container_set_const_string_by_name(container, inset_length_key,
                                     args->inset_length);
    gwy_container_set_boolean_by_name(container, draw_frame_key,
                                      args->draw_frame);
    gwy_container_set_boolean_by_name(container, draw_mask_key,
                                      args->draw_mask);
    gwy_container_set_boolean_by_name(container, draw_maskkey_key,
                                      args->draw_maskkey);
    gwy_container_set_boolean_by_name(container, draw_selection_key,
                                      args->draw_selection);
    gwy_container_set_const_string_by_name(container, mask_key_key,
                                           args->mask_key);
    gwy_container_set_const_string_by_name(container, font_key, args->font);
    gwy_container_set_boolean_by_name(container, scale_font_key,
                                      args->scale_font);
    gwy_container_set_boolean_by_name(container, decomma_key,
                                      args->decomma);
    gwy_container_set_double_by_name(container, fmscale_gap_key,
                                     args->fmscale_gap);
    gwy_container_set_double_by_name(container, inset_xgap_key,
                                     args->inset_xgap);
    gwy_container_set_double_by_name(container, inset_ygap_key,
                                     args->inset_ygap);
    gwy_container_set_double_by_name(container, title_gap_key,
                                     args->title_gap);
    gwy_container_set_double_by_name(container, maskkey_gap_key,
                                     args->maskkey_gap);
    gwy_container_set_boolean_by_name(container, fix_fmscale_precision_key,
                                      args->fix_fmscale_precision);
    gwy_container_set_int32_by_name(container, fmscale_precision_key,
                                    args->fmscale_precision);
    gwy_container_set_boolean_by_name(container, fix_kilo_threshold_key,
                                      args->fix_kilo_threshold);
    gwy_container_set_double_by_name(container, kilo_threshold_key,
                                     args->kilo_threshold);
    gwy_container_set_boolean_by_name(container, inset_draw_ticks_key,
                                      args->inset_draw_ticks);
    gwy_container_set_boolean_by_name(container, inset_draw_label_key,
                                      args->inset_draw_label);
    gwy_container_set_boolean_by_name(container, inset_draw_text_above_key,
                                      args->inset_draw_text_above);
    gwy_container_set_boolean_by_name(container, units_in_title_key,
                                      args->units_in_title);
    gwy_container_set_const_string_by_name(container, selection_key,
                                           args->selection);
    gwy_container_set_boolean_by_name(container, sel_number_objects_key,
                                      args->sel_number_objects);
    gwy_container_set_double_by_name(container, sel_line_thickness_key,
                                     args->sel_line_thickness);
    gwy_container_set_double_by_name(container, sel_point_radius_key,
                                     args->sel_point_radius);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
