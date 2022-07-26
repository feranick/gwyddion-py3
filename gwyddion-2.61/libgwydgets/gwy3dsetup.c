/*
 *  $Id: gwy3dsetup.c 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2006-2017 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyserializable.h>
#include <libgwydgets/gwydgettypes.h>
#include <libgwydgets/gwy3dsetup.h>

#define GWY_3D_SETUP_TYPE_NAME "Gwy3DSetup"

enum {
    PROP_0,
    PROP_PROJECTION,
    PROP_VISUALIZATION,
    PROP_AXES_VISIBLE,
    PROP_LABELS_VISIBLE,
    PROP_FMSCALE_VISIBLE,
    PROP_ROTATION_X,
    PROP_ROTATION_Y,
    PROP_SCALE,
    PROP_Z_SCALE,
    PROP_LIGHT_PHI,
    PROP_LIGHT_THETA,
    PROP_HIDE_MASKED,
    PROP_LINE_WIDTH,
    PROP_FMSCALE_SIZE,
    PROP_FMSCALE_Y_ALIGN,
    PROP_FMSCALE_RESERVE_SPACE,
    PROP_LAST
};

static void        gwy_3d_setup_serializable_init(GwySerializableIface *iface);
static void        gwy_3d_setup_set_property     (GObject *object,
                                                  guint prop_id,
                                                  const GValue *value,
                                                  GParamSpec *pspec);
static void        gwy_3d_setup_get_property     (GObject*object,
                                                  guint prop_id,
                                                  GValue *value,
                                                  GParamSpec *pspec);
static GByteArray* gwy_3d_setup_serialize        (GObject *serializable,
                                                  GByteArray *buffer);
static GObject*    gwy_3d_setup_deserialize      (const guchar *buffer,
                                                  gsize size,
                                                  gsize *position);
static void        gwy_3d_setup_clone            (GObject *source,
                                                  GObject *copy);
static GObject*    gwy_3d_setup_duplicate_real   (GObject *object);
static gsize       gwy_3d_setup_get_size         (GObject *object);

G_DEFINE_TYPE_EXTENDED
    (Gwy3DSetup, gwy_3d_setup, G_TYPE_OBJECT, 0,
     GWY_IMPLEMENT_SERIALIZABLE(gwy_3d_setup_serializable_init))

static void
gwy_3d_setup_serializable_init(GwySerializableIface *iface)
{
    iface->serialize = gwy_3d_setup_serialize;
    iface->deserialize = gwy_3d_setup_deserialize;
    iface->clone = gwy_3d_setup_clone;
    iface->get_size = gwy_3d_setup_get_size;
    iface->duplicate = gwy_3d_setup_duplicate_real;
}

static void
gwy_3d_setup_class_init(Gwy3DSetupClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->set_property = gwy_3d_setup_set_property;
    gobject_class->get_property = gwy_3d_setup_get_property;

    g_object_class_install_property
        (gobject_class,
         PROP_PROJECTION,
         g_param_spec_enum("projection",
                           "Projection",
                           "The type of the projection",
                           GWY_TYPE_3D_PROJECTION,
                           GWY_3D_PROJECTION_ORTHOGRAPHIC,
                           G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_VISUALIZATION,
         g_param_spec_enum("visualization",
                           "Visualization",
                           "Data visualization type",
                           GWY_TYPE_3D_VISUALIZATION,
                           GWY_3D_VISUALIZATION_GRADIENT,
                           G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_AXES_VISIBLE,
         g_param_spec_boolean("axes-visible",
                              "Axes visible",
                              "Whether axes are visible",
                              TRUE,
                              G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_LABELS_VISIBLE,
         g_param_spec_boolean("labels-visible",
                              "Labels visible",
                              "Whether axis labels are visible if axes "
                              "are visible",
                              TRUE,
                              G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_FMSCALE_VISIBLE,
         g_param_spec_boolean("fmscale-visible",
                              "Fmscale visible",
                              "Whether false color bar is visible",
                              FALSE,
                              G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_ROTATION_X,
         g_param_spec_double("rotation-x",
                             "Rotation X",
                             "Angle of the first rotation around x-axis, "
                             "in radians",
                             -G_MAXDOUBLE, G_MAXDOUBLE, G_PI/4.0,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_ROTATION_Y,
         g_param_spec_double("rotation-y",
                             "Rotation Y",
                             "Angle of the second rotation around y-axis, "
                             "in radians",
                             -G_MAXDOUBLE, G_MAXDOUBLE, -G_PI/4.0,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_SCALE,
         g_param_spec_double("scale",
                             "Scale",
                             "Overall view scale",
                             G_MINDOUBLE, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_Z_SCALE,
         g_param_spec_double("z-scale",
                             "Z scale",
                             "Extra stretch along z (value) axis",
                             G_MINDOUBLE, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_LIGHT_PHI,
         g_param_spec_double("light-phi",
                             "Light phi",
                             "Light source direction azimuth in horizontal "
                             "plane, in radians",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_LIGHT_THETA,
         g_param_spec_double("light-theta",
                             "Light theta",
                             "Light source direction deviation from the "
                             "z axis, in radians",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_HIDE_MASKED,
         g_param_spec_boolean("hide_masked",
                              "Hide masked",
                              "Hide masked vertices",
                              FALSE,
                              G_PARAM_READWRITE));

    /**
     * Gwy3DSetup:line-width:
     *
     * Width of axis lines and ticks, in pixels.
     *
     * Since: 2.42
     **/
    g_object_class_install_property
        (gobject_class,
         PROP_LINE_WIDTH,
         g_param_spec_double("line-width",
                             "Line width",
                             "Width of axis lines and ticks, in pixels.",
                             1.0, 10.0, 1.0,
                             G_PARAM_READWRITE));

    /**
     * Gwy3DSetup:fmscale-size:
     *
     * Size of false colour scale relative to view height.
     *
     * Since: 2.49
     **/
    g_object_class_install_property
        (gobject_class,
         PROP_FMSCALE_SIZE,
         g_param_spec_double("fmscale-size",
                             "Fmscale size",
                             "Size of false colour scale relative to "
                             "view height.",
                             0.0, 1.0, 1.0,
                             G_PARAM_READWRITE));

    /**
     * Gwy3DSetup:fmscale-y-align:
     *
     * Vertical alignment of false colour scale.
     *
     * Since: 2.49
     **/
    g_object_class_install_property
        (gobject_class,
         PROP_FMSCALE_Y_ALIGN,
         g_param_spec_double("fmscale-y-align",
                             "Fmscale y align",
                             "Vertical alignment of false colour scale.",
                             0.0, 1.0, 0.5,
                             G_PARAM_READWRITE));
    /**
     * Gwy3DSetup:fmscale-reserve-space:
     *
     * Whethere to reserve entire vertical stripe of the false colour scale.
     *
     * Since: 2.49
     **/
    g_object_class_install_property
        (gobject_class,
         PROP_FMSCALE_RESERVE_SPACE,
         g_param_spec_boolean("fmscale-reserve-space",
                              "Fmscale resrrve space",
                              "Whether to reserve horizontal space for "
                              "the false colour scale.",
                              TRUE,
                              G_PARAM_READWRITE));

}

static void
gwy_3d_setup_init(Gwy3DSetup *setup)
{
    setup->projection = GWY_3D_PROJECTION_ORTHOGRAPHIC;
    setup->visualization = GWY_3D_VISUALIZATION_GRADIENT;

    setup->axes_visible = TRUE;
    setup->labels_visible = TRUE;
    setup->fmscale_visible = FALSE;

    setup->rotation_x = G_PI/4.0;
    setup->rotation_y = -G_PI/4.0;
    setup->scale = 1.0;
    setup->z_scale = 1.0;
    setup->light_phi = 0.0;
    setup->light_theta = 0.0;
    setup->line_width = 1.0;
    setup->fmscale_size = 1.0;
    setup->fmscale_yalign = 0.5;
    setup->fmscale_reserve_space = TRUE;
}

static void
gwy_3d_setup_set_property(GObject *object,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
    Gwy3DSetup *setup = GWY_3D_SETUP(object);

    switch (prop_id) {
        case PROP_PROJECTION:
        setup->projection = g_value_get_enum(value);
        break;

        case PROP_VISUALIZATION:
        setup->visualization = g_value_get_enum(value);
        break;

        case PROP_AXES_VISIBLE:
        setup->axes_visible = g_value_get_boolean(value);
        break;

        case PROP_LABELS_VISIBLE:
        setup->labels_visible = g_value_get_boolean(value);
        break;

        case PROP_FMSCALE_VISIBLE:
        setup->fmscale_visible = g_value_get_boolean(value);
        break;

        case PROP_ROTATION_X:
        setup->rotation_x = g_value_get_double(value);
        break;

        case PROP_ROTATION_Y:
        setup->rotation_y = g_value_get_double(value);
        break;

        case PROP_SCALE:
        setup->scale = g_value_get_double(value);
        break;

        case PROP_Z_SCALE:
        setup->z_scale = g_value_get_double(value);
        break;

        case PROP_LIGHT_PHI:
        setup->light_phi = g_value_get_double(value);
        break;

        case PROP_LIGHT_THETA:
        setup->light_theta = g_value_get_double(value);
        break;

        case PROP_HIDE_MASKED:
        setup->hide_masked = g_value_get_boolean(value);
        break;

        case PROP_LINE_WIDTH:
        setup->line_width = g_value_get_double(value);
        break;

        case PROP_FMSCALE_SIZE:
        setup->fmscale_size = g_value_get_double(value);
        break;

        case PROP_FMSCALE_Y_ALIGN:
        setup->fmscale_yalign = g_value_get_double(value);
        break;

        case PROP_FMSCALE_RESERVE_SPACE:
        setup->fmscale_reserve_space = g_value_get_boolean(value);
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_3d_setup_get_property(GObject *object,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
    Gwy3DSetup *setup = GWY_3D_SETUP(object);

    switch (prop_id) {
        case PROP_PROJECTION:
        g_value_set_enum(value, setup->projection);
        break;

        case PROP_VISUALIZATION:
        g_value_set_enum(value, setup->visualization);
        break;

        case PROP_AXES_VISIBLE:
        g_value_set_boolean(value, setup->axes_visible);
        break;

        case PROP_LABELS_VISIBLE:
        g_value_set_boolean(value, setup->labels_visible);
        break;

        case PROP_FMSCALE_VISIBLE:
        g_value_set_boolean(value, setup->fmscale_visible);
        break;

        case PROP_ROTATION_X:
        g_value_set_double(value, setup->rotation_x);
        break;

        case PROP_ROTATION_Y:
        g_value_set_double(value, setup->rotation_y);
        break;

        case PROP_SCALE:
        g_value_set_double(value, setup->scale);
        break;

        case PROP_Z_SCALE:
        g_value_set_double(value, setup->z_scale);
        break;

        case PROP_LIGHT_PHI:
        g_value_set_double(value, setup->light_phi);
        break;

        case PROP_LIGHT_THETA:
        g_value_set_double(value, setup->light_theta);
        break;

        case PROP_HIDE_MASKED:
        g_value_set_boolean(value, setup->hide_masked);
        break;

        case PROP_LINE_WIDTH:
        g_value_set_double(value, setup->line_width);
        break;

        case PROP_FMSCALE_SIZE:
        g_value_set_double(value, setup->fmscale_size);
        break;

        case PROP_FMSCALE_Y_ALIGN:
        g_value_set_double(value, setup->fmscale_yalign);
        break;

        case PROP_FMSCALE_RESERVE_SPACE:
        g_value_set_boolean(value, setup->fmscale_reserve_space);
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static GByteArray*
gwy_3d_setup_serialize(GObject *serializable,
                       GByteArray *buffer)
{
    Gwy3DSetup *setup;

    gwy_debug("");
    g_return_val_if_fail(GWY_IS_3D_SETUP(serializable), NULL);

    setup = GWY_3D_SETUP(serializable);
    {
        GwySerializeSpec spec[] = {
            { 'i', "projection", &setup->projection, NULL, },
            { 'i', "visualization", &setup->visualization, NULL, },
            { 'b', "axes-visible", &setup->axes_visible, NULL, },
            { 'b', "labels-visible", &setup->labels_visible, NULL, },
            { 'b', "fmscale-visible", &setup->fmscale_visible, NULL, },
            { 'd', "rotation-x", &setup->rotation_x, NULL, },
            { 'd', "rotation-y", &setup->rotation_y, NULL, },
            { 'd', "scale", &setup->scale, NULL, },
            { 'd', "z-scale", &setup->z_scale, NULL, },
            { 'd', "light-phi", &setup->light_phi, NULL, },
            { 'd', "light-theta", &setup->light_theta, NULL, },
            { 'b', "hide-masked", &setup->hide_masked, NULL, },
            { 'd', "line-width", &setup->line_width, NULL, },
            { 'd', "fmscale-size", &setup->fmscale_size, NULL, },
            { 'd', "fmscale-y-align", &setup->fmscale_yalign, NULL, },
            { 'b', "fmscale-resrrve-space", &setup->fmscale_reserve_space, NULL, },
        };

        return gwy_serialize_pack_object_struct(buffer,
                                                GWY_3D_SETUP_TYPE_NAME,
                                                G_N_ELEMENTS(spec), spec);
    }
}

static gsize
gwy_3d_setup_get_size(GObject *object)
{
    Gwy3DSetup *setup;

    gwy_debug("");
    g_return_val_if_fail(GWY_IS_3D_SETUP(object), 0);

    setup = GWY_3D_SETUP(object);
    {
        GwySerializeSpec spec[] = {
            { 'i', "projection", &setup->projection, NULL, },
            { 'i', "visualization", &setup->visualization, NULL, },
            { 'b', "axes-visible", &setup->axes_visible, NULL, },
            { 'b', "labels-visible", &setup->labels_visible, NULL, },
            { 'b', "fmscale-visible", &setup->fmscale_visible, NULL, },
            { 'd', "rotation-x", &setup->rotation_x, NULL, },
            { 'd', "rotation-y", &setup->rotation_y, NULL, },
            { 'd', "scale", &setup->scale, NULL, },
            { 'd', "z-scale", &setup->z_scale, NULL, },
            { 'd', "light-phi", &setup->light_phi, NULL, },
            { 'd', "light-theta", &setup->light_theta, NULL, },
            { 'b', "hide-masked", &setup->hide_masked, NULL, },
            { 'd', "line-width", &setup->line_width, NULL, },
            { 'd', "fmscale-size", &setup->fmscale_size, NULL, },
            { 'd', "fmscale-y-align", &setup->fmscale_yalign, NULL, },
            { 'b', "fmscale-resrrve-space", &setup->fmscale_reserve_space, NULL, },
        };

        return gwy_serialize_get_struct_size(GWY_3D_SETUP_TYPE_NAME,
                                             G_N_ELEMENTS(spec), spec);
    }
}

static GObject*
gwy_3d_setup_deserialize(const guchar *buffer,
                         gsize size,
                         gsize *position)
{
    Gwy3DSetup *setup;

    gwy_debug("");
    g_return_val_if_fail(buffer, NULL);

    setup = gwy_3d_setup_new();
    {
        GwySerializeSpec spec[] = {
            { 'i', "projection", &setup->projection, NULL, },
            { 'i', "visualization", &setup->visualization, NULL, },
            { 'b', "axes-visible", &setup->axes_visible, NULL, },
            { 'b', "labels-visible", &setup->labels_visible, NULL, },
            { 'b', "fmscale-visible", &setup->fmscale_visible, NULL, },
            { 'd', "rotation-x", &setup->rotation_x, NULL, },
            { 'd', "rotation-y", &setup->rotation_y, NULL, },
            { 'd', "scale", &setup->scale, NULL, },
            { 'd', "z-scale", &setup->z_scale, NULL, },
            { 'd', "light-phi", &setup->light_phi, NULL, },
            { 'd', "light-theta", &setup->light_theta, NULL, },
            { 'b', "hide-masked", &setup->hide_masked, NULL, },
            { 'd', "line-width", &setup->line_width, NULL, },
            { 'd', "fmscale-size", &setup->fmscale_size, NULL, },
            { 'd', "fmscale-y-align", &setup->fmscale_yalign, NULL, },
            { 'b', "fmscale-resrrve-space", &setup->fmscale_reserve_space, NULL, },
        };

        if (!gwy_serialize_unpack_object_struct(buffer, size, position,
                                                GWY_3D_SETUP_TYPE_NAME,
                                                G_N_ELEMENTS(spec), spec)) {
            return NULL;
        }
    }

    return (GObject*)setup;
}

static void
gwy_3d_setup_clone(GObject *source,
                   GObject *copy)
{
    Gwy3DSetup *setup, *clone;

    g_return_if_fail(GWY_IS_3D_SETUP(source));
    g_return_if_fail(GWY_IS_3D_SETUP(copy));

    setup = GWY_3D_SETUP(source);
    clone = GWY_3D_SETUP(copy);

    g_object_freeze_notify(copy);

    if (clone->projection != setup->projection) {
        clone->projection = setup->projection;
        g_object_notify(copy, "projection");
    }
    if (clone->visualization != setup->visualization) {
        clone->visualization = setup->visualization;
        g_object_notify(copy, "visualization");
    }

    if (clone->axes_visible != setup->axes_visible) {
        clone->axes_visible = setup->axes_visible;
        g_object_notify(copy, "axes-visible");
    }

    if (clone->labels_visible != setup->labels_visible) {
        clone->labels_visible = setup->labels_visible;
        g_object_notify(copy, "labels-visible");
    }
    if (clone->fmscale_visible != setup->fmscale_visible) {
        clone->fmscale_visible = setup->fmscale_visible;
        g_object_notify(copy, "fmscale-visible");
    }

    if (clone->rotation_x != setup->rotation_x) {
        clone->rotation_x = setup->rotation_x;
        g_object_notify(copy, "rotation-x");
    }
    if (clone->rotation_y != setup->rotation_y) {
        clone->rotation_y = setup->rotation_y;
        g_object_notify(copy, "rotation-y");
    }

    if (clone->scale != setup->scale) {
        clone->scale = setup->scale;
        g_object_notify(copy, "scale");
    }
    if (clone->z_scale != setup->z_scale) {
        clone->z_scale = setup->z_scale;
        g_object_notify(copy, "z-scale");
    }

    if (clone->light_phi != setup->light_phi) {
        clone->light_phi = setup->light_phi;
        g_object_notify(copy, "light-phi");
    }
    if (clone->light_theta != setup->light_theta) {
        clone->light_theta = setup->light_theta;
        g_object_notify(copy, "light-theta");
    }

    if (clone->hide_masked != setup->hide_masked) {
        clone->hide_masked = setup->hide_masked;
        g_object_notify(copy, "hide-masked");
    }

    if (clone->line_width != setup->line_width) {
        clone->line_width = setup->line_width;
        g_object_notify(copy, "line-width");
    }

    if (clone->fmscale_size != setup->fmscale_size) {
        clone->fmscale_size = setup->fmscale_size;
        g_object_notify(copy, "fmscale-size");
    }

    if (clone->fmscale_yalign != setup->fmscale_yalign) {
        clone->fmscale_yalign = setup->fmscale_yalign;
        g_object_notify(copy, "fmscale-y-align");
    }

    if (clone->fmscale_reserve_space != setup->fmscale_reserve_space) {
        clone->fmscale_reserve_space = setup->fmscale_reserve_space;
        g_object_notify(copy, "fmscale-reserve-space");
    }

    g_object_thaw_notify(copy);
}

static GObject*
gwy_3d_setup_duplicate_real(GObject *object)
{
    Gwy3DSetup *setup, *duplicate;

    setup = GWY_3D_SETUP(object);
    duplicate = gwy_3d_setup_new();

    duplicate->projection = setup->projection;
    duplicate->visualization = setup->visualization;

    duplicate->axes_visible = setup->axes_visible;
    duplicate->labels_visible = setup->labels_visible;
    duplicate->fmscale_visible = setup->fmscale_visible;

    duplicate->rotation_x = setup->rotation_x;
    duplicate->rotation_y = setup->rotation_y;
    duplicate->scale = setup->scale;
    duplicate->z_scale = setup->z_scale;
    duplicate->light_phi = setup->light_phi;
    duplicate->light_theta = setup->light_theta;

    duplicate->hide_masked = setup->hide_masked;

    duplicate->line_width = setup->line_width;
    duplicate->fmscale_size = setup->fmscale_size;
    duplicate->fmscale_yalign = setup->fmscale_yalign;
    duplicate->fmscale_reserve_space = setup->fmscale_reserve_space;

    return (GObject*)duplicate;
}

/**
 * gwy_3d_setup_new:
 *
 * Creates a new 3D view setup with default values.
 *
 * Returns: A newly created 3D view setup.
 **/
Gwy3DSetup*
gwy_3d_setup_new(void)
{
    return (Gwy3DSetup*)g_object_new(GWY_TYPE_3D_SETUP, NULL);
}

/************************** Documentation ****************************/

/**
 * SECTION:gwy3dsetup
 * @title: Gwy3DSetup
 * @short_description: 3D scene setup
 * @see_also: #Gwy3DView -- the basic 3D data display widget
 *
 * #Gwy3DSetup represents a basic 3D scene setup: viewpoint, projection, light,
 * scale, etc.  It is serializable and used to represent the #Gwy3DView setup.
 *
 * Its components can be read directly in the struct or generically with
 * g_object_get().  To set them you it is necessary to use g_object_set().
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
