/*
 *  $Id: gwyrgba.c 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2004-2017 David Necas (Yeti), Petr Klapetek.
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

#include "config.h"
#include <string.h>
#include <glib-object.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include "gwyrgba.h"

#define float_to_gdk(c) ((guint16)((c)*65535.999999))
#define float_from_gdk(c) ((c)/65535.0)
#define float_to_hex(c) ((guint8)((c)*255.9999999))

static void gwy_rgba_compute_color_quarks (const gchar *prefix,
                                           GQuark quarks[4]);

GType
gwy_rgba_get_type(void)
{
    static GType rgba_type = 0;

    if (G_UNLIKELY(!rgba_type))
        rgba_type = g_boxed_type_register_static("GwyRGBA",
                                                 (GBoxedCopyFunc)gwy_rgba_copy,
                                                 (GBoxedFreeFunc)gwy_rgba_free);

    return rgba_type;
}

/**
 * gwy_rgba_new:
 * @r: The red component.
 * @g: The green component.
 * @b: The blue component.
 * @a: The alpha (opacity) value.
 *
 * Creates RGBA colour specification.
 *
 * This is mostly useful for language bindings.
 *
 * Returns: New RGBA colour structure.  The result must be freed with
 *          gwy_rgba_free(), not g_free().
 *
 * Since: 2.47
 **/
GwyRGBA*
gwy_rgba_new(gdouble r,
             gdouble g,
             gdouble b,
             gdouble a)
{
    GwyRGBA *rgba = g_slice_new(GwyRGBA);

    rgba->r = r;
    rgba->g = g;
    rgba->b = b;
    rgba->a = a;

    return rgba;
}

/**
 * gwy_rgba_copy:
 * @rgba: A RGBA color.
 *
 * Makes a copy of a rgba structure. The result must be freed using
 * gwy_rgba_free().
 *
 * Returns: A copy of @rgba.
 **/
GwyRGBA*
gwy_rgba_copy(const GwyRGBA *rgba)
{
    g_return_val_if_fail(rgba, NULL);
    return g_slice_dup(GwyRGBA, rgba);
}

/**
 * gwy_rgba_free:
 * @rgba: A RGBA color.
 *
 * Frees an rgba structure created with gwy_rgba_copy().
 **/
void
gwy_rgba_free(GwyRGBA *rgba)
{
    g_return_if_fail(rgba);
    g_slice_free(GwyRGBA, rgba);
}

/**
 * gwy_rgba_to_gdk_color:
 * @rgba: A RGBA color.
 * @gdkcolor: A #GdkColor.
 *
 * Converts a rgba to a Gdk color.
 *
 * Note no allocation is performed, just channel value conversion.
 **/
void
gwy_rgba_to_gdk_color(const GwyRGBA *rgba,
                      GdkColor *gdkcolor)
{
    gdkcolor->red   = float_to_gdk(rgba->r);
    gdkcolor->green = float_to_gdk(rgba->g);
    gdkcolor->blue  = float_to_gdk(rgba->b);
    gdkcolor->pixel = (guint32)-1;
}

/**
 * gwy_rgba_to_gdk_alpha:
 * @rgba: A RGBA color.
 *
 * Converts a rgba to a Gdk opacity value.
 *
 * Returns: The opacity value as a 16bit integer.
 **/
guint16
gwy_rgba_to_gdk_alpha(const GwyRGBA *rgba)
{
    return float_to_gdk(rgba->a);
}

/**
 * gwy_rgba_from_gdk_color:
 * @rgba: A RGBA color.
 * @gdkcolor: A #GdkColor.
 *
 * Converts a Gdk color to a rgba.
 *
 * The alpha value is unchanged, as #GdkColor has no opacity information.
 **/
void
gwy_rgba_from_gdk_color(GwyRGBA *rgba,
                        const GdkColor *gdkcolor)
{
    rgba->r = float_from_gdk(gdkcolor->red);
    rgba->g = float_from_gdk(gdkcolor->green);
    rgba->b = float_from_gdk(gdkcolor->blue);
}

/**
 * gwy_rgba_from_gdk_color_and_alpha:
 * @rgba: A RGBA color.
 * @gdkcolor: A #GdkColor.
 * @gdkalpha: Gdk 16bit opacity value.
 *
 * Converts a Gdk color plus an opacity value to a rgba.
 **/
void
gwy_rgba_from_gdk_color_and_alpha(GwyRGBA *rgba,
                                  const GdkColor *gdkcolor,
                                  guint16 gdkalpha)
{
    rgba->r = float_from_gdk(gdkcolor->red);
    rgba->g = float_from_gdk(gdkcolor->green);
    rgba->b = float_from_gdk(gdkcolor->blue);
    rgba->a = float_from_gdk(gdkalpha);
}

/**
 * gwy_rgba_interpolate:
 * @src1: Color at point @x = 0.0.
 * @src2: Color at point @x = 1.0.
 * @x: Point in interval 0..1 to take color from.
 * @rgba: A #GwyRGBA to store the result to.
 *
 * Linearly interpolates two colors, including alpha blending.
 *
 * Correct blending of two not fully opaque colors is tricky.  Always use
 * this function, not simple independent interpolation of r, g, b, and a.
 **/
void
gwy_rgba_interpolate(const GwyRGBA *src1,
                     const GwyRGBA *src2,
                     gdouble x,
                     GwyRGBA *rgba)
{
    /* for alpha = 0.0 there's actually no limit, but average is psychologicaly
     * better than some random value */
    if (G_LIKELY(src1->a == src2->a)) {
        rgba->a = src1->a;
        rgba->r = x*src2->r + (1.0 - x)*src1->r;
        rgba->g = x*src2->g + (1.0 - x)*src1->g;
        rgba->b = x*src2->b + (1.0 - x)*src1->b;
        return;
    }

    if (src2->a == 0.0) {
        rgba->a = (1.0 - x)*src1->a;
        rgba->r = src1->r;
        rgba->g = src1->g;
        rgba->b = src1->b;
        return;
    }
    if (src1->a == 0.0) {
        rgba->a = x*src2->a;
        rgba->r = src2->r;
        rgba->g = src2->g;
        rgba->b = src2->b;
        return;
    }

    /* nothing helped, it's a general case
     * however, for meaningful values, rgba->a cannot be 0.0 */
    rgba->a = x*src2->a + (1.0 - x)*src1->a;
    rgba->r = (x*src2->a*src2->r + (1.0 - x)*src1->a*src1->r)/rgba->a;
    rgba->g = (x*src2->a*src2->g + (1.0 - x)*src1->a*src1->g)/rgba->a;
    rgba->b = (x*src2->a*src2->b + (1.0 - x)*src1->a*src1->b)/rgba->a;
}

/**
 * gwy_rgba_get_from_container:
 * @rgba: A RGBA color.
 * @container: A #GwyContainer to get the color components from.
 * @prefix: Prefix in @container, e.g. "/0/mask" (it would try to fetch
 *          "/0/mask/red", "/0/mask/green", etc. then).
 *
 * Gets RGBA color components from a container.
 *
 * This is a convenience function to get the components in the common
 * arrangement.
 *
 * Returns: Whether all @rgba components were successfully found and set.
 **/
gboolean
gwy_rgba_get_from_container(GwyRGBA *rgba,
                            GwyContainer *container,
                            const gchar *prefix)
{
    GQuark keys[4];
    gboolean ok = TRUE;

    g_return_val_if_fail(rgba && container && prefix, FALSE);

    gwy_rgba_compute_color_quarks(prefix, keys);
    ok &= gwy_container_gis_double(container, keys[0], &rgba->r);
    ok &= gwy_container_gis_double(container, keys[1], &rgba->g);
    ok &= gwy_container_gis_double(container, keys[2], &rgba->b);
    ok &= gwy_container_gis_double(container, keys[3], &rgba->a);

    return ok;
}

/**
 * gwy_rgba_store_to_container:
 * @rgba: A RGBA color.
 * @container: A #GwyContainer to store the color components to.
 * @prefix: Prefix in @container, e.g. "/0/mask" (it will store
 *          "/0/mask/red", "/0/mask/green", etc. then).
 *
 * Stores RGBA color components to a container.
 *
 * This is a convenience function to store the components in the common
 * arrangement.
 **/
void
gwy_rgba_store_to_container(const GwyRGBA *rgba,
                            GwyContainer *container,
                            const gchar *prefix)
{
    GQuark keys[4];

    g_return_if_fail(rgba && container && prefix);

    gwy_rgba_compute_color_quarks(prefix, keys);
    gwy_container_set_double(container, keys[0], rgba->r);
    gwy_container_set_double(container, keys[1], rgba->g);
    gwy_container_set_double(container, keys[2], rgba->b);
    gwy_container_set_double(container, keys[3], rgba->a);
}

/**
 * gwy_rgba_remove_from_container:
 * @container: A #GwyContainer to remove the color components from.
 * @prefix: Prefix in @container, e.g. "/0/mask" (it will remove
 *          "/0/mask/red", "/0/mask/green", etc. then).
 *
 * Removes RGBA color components from a container.
 *
 * This is a convenience function to remove the components in the common
 * arrangement.
 *
 * Returns: %TRUE if anything was removed.
 **/
gboolean
gwy_rgba_remove_from_container(GwyContainer *container,
                               const gchar *prefix)
{
    GQuark keys[4];

    g_return_val_if_fail(container && prefix, FALSE);

    gwy_rgba_compute_color_quarks(prefix, keys);
    return gwy_container_remove(container, keys[0])
           + gwy_container_remove(container, keys[1])
           + gwy_container_remove(container, keys[2])
           + gwy_container_remove(container, keys[3]);
}

/**
 * gwy_rgba_set_gdk_gc_fg:
 * @rgba: A RGBA color.  Its alpha component is ignored, only RGB is used.
 * @gc: A Gdk graphics context to set forgeground color of.
 *
 * Sets foreground color of a Gdk graphics context from a RGBA color.
 *
 * This is a convenience wrapper around gdk_gc_set_rgb_fg_color(), see its
 * documentation for details and caveats.
 **/
void
gwy_rgba_set_gdk_gc_fg(const GwyRGBA *rgba,
                       GdkGC *gc)
{
    GdkColor gdkcolor;

    gdkcolor.red   = float_to_gdk(rgba->r);
    gdkcolor.green = float_to_gdk(rgba->g);
    gdkcolor.blue  = float_to_gdk(rgba->b);
    gdk_gc_set_rgb_fg_color(gc, &gdkcolor);
}

/**
 * gwy_rgba_set_gdk_gc_bg:
 * @rgba: A RGBA color.  Its alpha component is ignored, only RGB is used.
 * @gc: A Gdk graphics context to set forgeground color of.
 *
 * Sets foreground color of a Gdk graphics context from a RGBA color.
 *
 * This is a convenience wrapper around gdk_gc_set_rgb_bg_color(), see its
 * documentation for details and caveats.
 **/
void
gwy_rgba_set_gdk_gc_bg(const GwyRGBA *rgba,
                       GdkGC *gc)
{
    GdkColor gdkcolor;

    gdkcolor.red   = float_to_gdk(rgba->r);
    gdkcolor.green = float_to_gdk(rgba->g);
    gdkcolor.blue  = float_to_gdk(rgba->b);
    gdk_gc_set_rgb_bg_color(gc, &gdkcolor);
}

static void
gwy_rgba_compute_color_quarks(const gchar *prefix,
                              GQuark quarks[4])
{
    gchar *key;
    guint len;

    len = strlen(prefix);
    key = g_newa(gchar, len + sizeof("/alpha"));

    g_stpcpy(g_stpcpy(key, prefix), "/red");
    quarks[0] = g_quark_from_string(key);
    strcpy(key + len + 1, "green");
    quarks[1] = g_quark_from_string(key);
    strcpy(key + len + 1, "blue");
    quarks[2] = g_quark_from_string(key);
    strcpy(key + len + 1, "alpha");
    quarks[3] = g_quark_from_string(key);
}

/**
 * gwy_rgba_to_hex6:
 * @rgba: A RGBA color.  Its alpha component is ignored, only RGB is used.
 * @hexout: Buffer at least seven character long where the hexadecimal
 *          representation (e.g. "ff0000" for red) will be stored.
 *
 * Formats the R, G and B components of a RGBA color to hexadecimal string.
 *
 * The component order is R, G and B.  The output has always exactly 6 bytes
 * and does not include any "#" prefix which is used in some contexts.
 *
 * Since: 2.32
 **/
void
gwy_rgba_to_hex6(const GwyRGBA *rgba,
                 gchar *hexout)
{
    g_return_if_fail(rgba);
    g_return_if_fail(hexout);
    g_snprintf(hexout, 7, "%02x%02x%02x",
               float_to_hex(rgba->r),
               float_to_hex(rgba->g),
               float_to_hex(rgba->b));
}

/**
 * gwy_rgba_to_hex8:
 * @rgba: A RGBA color.
 * @hexout: Buffer at least nine character long where the hexadecimal
 *          representation (e.g. "ffff0000" for opaque red) will be stored.
 *
 * Formats all components of a RGBA color to hexadecimal string.
 *
 * The component order is A, R, G and B.  Note that while this order is a
 * common it is by no means universal.  The output has always exactly 8 bytes
 * and does not include any "#" prefix which is used in some contexts.
 *
 * Since: 2.32
 **/
void
gwy_rgba_to_hex8(const GwyRGBA *rgba,
                 gchar *hexout)
{
    g_return_if_fail(rgba);
    g_return_if_fail(hexout);
    g_snprintf(hexout, 9, "%02x%02x%02x%02x",
               float_to_hex(rgba->a),
               float_to_hex(rgba->r),
               float_to_hex(rgba->g),
               float_to_hex(rgba->b));
}

/**
 * gwy_rgba_to_pixbuf_pixel:
 * @rgba: A RGBA color.
 *
 * Converts a RGBA color to pixbuf pixel.
 *
 * The returned pixel value includes opacity.  If @rgba is partially
 * transparent, so is the pixel.
 *
 * Returns: The pixel value as a 32-bit integer.
 *
 * Since: 2.49
 **/
guint32
gwy_rgba_to_pixbuf_pixel(const GwyRGBA *rgba)
{
    g_return_val_if_fail(rgba, 0);

    return ((guint32)float_to_hex(rgba->a)
            | (((guint32)float_to_hex(rgba->b)) << 8)
            | (((guint32)float_to_hex(rgba->g)) << 16)
            | (((guint32)float_to_hex(rgba->r)) << 24));
}

/**
 * gwy_rgba_from_pixbuf_pixel:
 * @rgba: A #GwyRGBA to store the result to.
 * @pixel: Pixbuf pixel value as a 32-bit integer.
 *
 * Converts a pixbuf pixel value to a RGBA color.
 *
 * The conversion includes opacity.  If the opacity channel is undefined or
 * should be ignored, you need to either set the lowest byte of @pixel to 0xff
 * or fix @rgba afterwards.
 *
 * Since: 2.49
 **/
void
gwy_rgba_from_pixbuf_pixel(GwyRGBA *rgba,
                           guint32 pixel)
{
    g_return_if_fail(rgba);
    rgba->a = (pixel & 0xffu)/255.0;
    rgba->b = ((pixel >> 8) & 0xffu)/255.0;
    rgba->g = ((pixel >> 16) & 0xffu)/255.0;
    rgba->r = ((pixel >> 24) & 0xffu)/255.0;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyrgba
 * @title: GwyRGBA
 * @short_description: Bit depth independet RGBA colors
 *
 * #GwyRGBA is a bit depth independent representation of an RGB or RGBA color,
 * using floating point values from the [0,1] interval.
 *
 * #GwyRGBA is not an object, but a simple struct that can be allocated on
 * stack on created with g_new() or malloc(). Helper functions for conversion
 * between #GwyRGBA and #GdkColor (gwy_rgba_to_gdk_color(),
 * gwy_rgba_from_gdk_color()) and for #GwyContainer storage by component
 * (gwy_rgba_store_to_container(), gwy_rgba_get_from_container()) are provided.
 **/

/**
 * GwyRGBA:
 * @r: The red component.
 * @g: The green component.
 * @b: The blue component.
 * @a: The alpha (opacity) value.
 *
 * RGB[A] color specification type.
 *
 * All values are from the range [0,1].  The components are not premultiplied.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
