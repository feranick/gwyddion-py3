/*
 *  $Id: params.c 24672 2022-03-15 12:32:09Z yeti-dn $
 *  Copyright (C) 2021-2022 David Necas (Yeti).
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

#include "config.h"
#include "libgwyddion/gwymacros.h"
#include "libprocess/gwyprocesstypes.h"
#include "libprocess/gwyprocessenums.h"
#include "settings.h"
#include "data-browser.h"
#include "gwymoduleutils.h"
#include "params.h"
#include "param-internal.h"

typedef struct _GwyParamsPrivate GwyParamsPrivate;

typedef struct {
    gchar *s;
    GwySIUnit *cached_unit;
    gint cached_power10;
    gboolean cached_valid;
} GwyParamValueUnit;

typedef struct {
    gchar *s;
    gint i;
    gboolean use_string;
} GwyParamValueCurve;

typedef union {
    gboolean b;
    gint i;
    guint u;
    gdouble d;
    gchar *s;
    GwyRGBA c;
    GwyAppDataId di;
    GwyResultsReportType rt;
    GwyParamValueUnit si;
    GwyParamValueCurve cu;
} GwyParamValue;

struct _GwyParamsPrivate {
    GwyParamDef *def;
    GwyParamValue *values;
    guint nvalues;
};

static void        gwy_params_finalize   (GObject *gobject);
static gboolean    set_boolean_value     (GwyParamValue *pvalue,
                                          gboolean value);
static gboolean    set_int_value         (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          gint value);
static gboolean    set_random_seed_value (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          gint value);
static gboolean    set_double_value      (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          gdouble value);
static gboolean    set_enum_value        (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          gint value);
static gboolean    set_flags_value       (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          guint value);
static gboolean    set_report_type_value (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          GwyResultsReportType value);
static gboolean    set_string_value      (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          const gchar *value);
static gboolean    set_curve_string_value(GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          const gchar *value);
static gboolean    set_curve_int_value   (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          const gint value);
static gboolean    set_unit_value        (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          const gchar *value);
static gboolean    set_color_value       (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          GwyRGBA value);
static gboolean    set_data_id_value     (GwyParamValue *pvalue,
                                          GwyAppDataId value);
static gboolean    set_resource_value    (GwyParamValue *pvalue,
                                          const GwyParamDefItem *def,
                                          const gchar *value);
static gint        get_enum_internal     (GwyParams *params,
                                          gint id,
                                          GType type,
                                          gint fallback_value);
static guint       get_flags_internal    (GwyParams *params,
                                          gint id,
                                          GType type);
static gboolean    reset_param_value     (GwyParams *params,
                                          guint i);
static gpointer    get_data_object       (GwyParams *params,
                                          gint id,
                                          GwyParamType param_type,
                                          GQuark (*get_key_func)(gint id),
                                          GType object_type);
static gpointer    get_data_object_for_id(GwyAppDataId dataid,
                                          GQuark (*get_key_func)(gint id),
                                          GType object_type);
static gboolean    param_find_common     (GwyParams *params,
                                          gint id,
                                          gint *i,
                                          const GwyParamDefItem **def,
                                          GwyParamType want_type);
static GHashTable* ensure_data_ids       (void);

G_LOCK_DEFINE_STATIC(data_ids);

static const GwyAppDataId noid = GWY_APP_DATA_ID_NONE;

G_DEFINE_TYPE(GwyParams, gwy_params, G_TYPE_OBJECT);

static void
gwy_params_class_init(GwyParamsClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_params_finalize;

    g_type_class_add_private(klass, sizeof(GwyParamsPrivate));
}

static void
gwy_params_init(GwyParams *params)
{
    params->priv = G_TYPE_INSTANCE_GET_PRIVATE(params, GWY_TYPE_PARAMS, GwyParamsPrivate);
}

static void
gwy_params_finalize(GObject *gobject)
{
    GwyParams *params = (GwyParams*)gobject;
    GwyParamsPrivate *priv = params->priv;
    GwyParamValue *values = priv->values;
    guint i, n = priv->nvalues;

    for (i = 0; i < n; i++) {
        GwyParamType type = _gwy_param_def_item(priv->def, i)->type;

        if (type == GWY_PARAM_STRING || type == GWY_PARAM_RESOURCE)
            g_free(values[i].s);
        else if (type == GWY_PARAM_UNIT) {
            GWY_OBJECT_UNREF(values[i].si.cached_unit);
            g_free(values[i].si.s);
        }
    }
    g_free(priv->values);

    GWY_OBJECT_UNREF(priv->def);
    G_OBJECT_CLASS(gwy_params_parent_class)->finalize(gobject);
}

/**
 * gwy_params_new:
 *
 * Creates a new empty parameter value set.
 *
 * The created object is empty (and useless) until parameter definitions are set with gwy_params_set_def().  However,
 * in modules the parameters are normally instantiated using gwy_params_new_from_settings().
 *
 * Returns: A new empty parameter value set.
 *
 * Since: 2.59
 **/
GwyParams*
gwy_params_new(void)
{
    return g_object_new(GWY_TYPE_PARAMS, NULL);
}

/**
 * gwy_params_duplicate:
 * @params: A set of parameter values.
 *
 * Creates a duplicate of a set of parameter values.
 *
 * Returns: A new empty parameter value set.
 *
 * Since: 2.59
 **/
GwyParams*
gwy_params_duplicate(GwyParams *params)
{
    GwyParamsPrivate *priv;
    GwyParamValue *values;
    GwyParams *copy;
    guint i, n;

    g_return_val_if_fail(GWY_IS_PARAMS(params), NULL);
    priv = params->priv;
    copy = gwy_params_new();
    /* This is a bit silly, but we can duplicate an unset object so why not. */
    if (!priv->def)
        return copy;

    gwy_params_set_def(copy, priv->def);

    priv = params->priv;
    n = priv->nvalues;
    values = copy->priv->values;

    /* Allocated values must be handled specially, but everything else can be just a bitwise copy. */
    gwy_assign(values, priv->values, n);
    for (i = 0; i < n; i++) {
        const GwyParamDefItem *def = _gwy_param_def_item(priv->def, i);

        if (def->type == GWY_PARAM_STRING)
            values[i].s = g_strdup(values[i].s);
        else if (def->type == GWY_PARAM_UNIT)
            values[i].si.s = g_strdup(values[i].si.s);
    }

    return copy;
}

/**
 * gwy_params_set_def:
 * @params: A parameter value set.
 * @pardef: Set of parameter definitions.
 *
 * Sets the definitions of parameters a #GwyParams should use.
 *
 * Once set, the definitions are fixed.  One #GwyParams object cannot be used with multiple definitions.
 *
 * Since: 2.59
 **/
void
gwy_params_set_def(GwyParams *params,
                   GwyParamDef *pardef)
{
    GwyParamsPrivate *priv;
    GwyParamValue *values;

    g_return_if_fail(GWY_IS_PARAMS(params));
    g_return_if_fail(GWY_IS_PARAM_DEF(pardef));
    priv = params->priv;
    if (priv->def) {
        g_critical("GwyParams definition can only be set upon construction.");
        return;
    }
    priv->def = g_object_ref(pardef);
    _gwy_param_def_use(pardef, params);
    priv->nvalues = _gwy_param_def_size(pardef);
    priv->values = values = g_new0(GwyParamValue, priv->nvalues);
    gwy_params_reset_all(params, NULL);
}

/**
 * gwy_params_get_def:
 * @params: A parameter value set.
 *
 * Gets the definitions object for a parameter set.
 *
 * Returns: The set of parameter definitions.
 *
 * Since: 2.59
 **/
GwyParamDef*
gwy_params_get_def(GwyParams *params)
{
    g_return_val_if_fail(GWY_IS_PARAMS(params), NULL);
    return params->priv->def;
}

/**
 * gwy_params_reset:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Resets a single parameter in a parameter value set to the default value.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_reset(GwyParams *params,
                 gint id)
{
    GwyParamsPrivate *priv;
    gint i;

    g_return_val_if_fail(GWY_IS_PARAMS(params), FALSE);
    priv = params->priv;
    i = _gwy_param_def_index(priv->def, id);
    g_return_val_if_fail(i >= 0 && i < priv->nvalues, FALSE);
    return reset_param_value(params, i);
}

/**
 * gwy_params_reset_all:
 * @params: A parameter value set.
 * @prefix: The prefix (leading path component) limiting parameters to reset.  Pass empty string or %NULL to reset all
 *          parameters.
 *
 * Resets all parameters in a parameter value set to default values.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * Since: 2.59
 **/
void
gwy_params_reset_all(GwyParams *params,
                     const gchar *prefix)
{
    GwyParamsPrivate *priv;
    guint i, n, pfxlen;

    g_return_if_fail(GWY_IS_PARAMS(params));
    priv = params->priv;
    n = priv->nvalues;
    pfxlen = (prefix ? strlen(prefix) : 0);
    while (pfxlen && prefix[pfxlen-1] == '/')
        pfxlen--;
    for (i = 0; i < n; i++) {
        if (pfxlen) {
            const GwyParamDefItem *def = _gwy_param_def_item(priv->def, i);
            const gchar *name = def->name;
            if (name && (strncmp(name, prefix, pfxlen) || name[pfxlen] != '/'))
                continue;
        }
        reset_param_value(params, i);
    }
}

/**
 * gwy_params_new_from_settings:
 * @pardef: Set of parameter definitions.
 *
 * Creates a new parameter value set with given definition and loads values from settings.
 *
 * The definitions @pardef must have function name set.
 *
 * Returns: A new parameter value set.
 *
 * Since: 2.59
 **/
GwyParams*
gwy_params_new_from_settings(GwyParamDef *pardef)
{
    GwyParams *params;

    params = gwy_params_new();
    gwy_params_set_def(params, pardef);
    gwy_params_load_from_settings(params);

    return params;
}

/**
 * gwy_params_load_from_settings:
 * @params: A set of parameter values.
 *
 * Loads a parameter value set from settings.
 *
 * The corresponding definitions must have function name set.  It is usually more convenient to create and load the
 * parameters at the same time using gwy_params_new_from_settings().
 *
 * Since: 2.59
 **/
void
gwy_params_load_from_settings(GwyParams *params)
{
    GwyParamsPrivate *priv;
    GwyParamValue *values;
    GwyContainer *settings;
    const gchar *modname;
    GString *str;
    GHashTable *data_ids;
    guint i, n, pfxlen;

    g_return_if_fail(GWY_IS_PARAMS(params));
    priv = params->priv;
    modname = gwy_param_def_get_function_name(priv->def);
    if (!modname) {
        g_critical("NULL function name when trying to load parameters from settings.");
        return;
    }
    settings = gwy_app_settings_get();
    G_LOCK(data_ids);
    data_ids = ensure_data_ids();

    n = priv->nvalues;
    values = priv->values;

    str = g_string_new("/module/");
    g_string_append(str, modname);
    g_string_append_c(str, '/');
    pfxlen = str->len;

    for (i = 0; i < n; i++) {
        GwyParamValue *value = values + i;
        const GwyParamDefItem *def = _gwy_param_def_item(priv->def, i);
        GwyParamType type = def->type;
        GQuark quark;

        /* Load default mask colour from global settings. */
        if (type == GWY_PARAM_COLOR && def->def.c.is_mask) {
            GwyRGBA v;
            if (gwy_rgba_get_from_container(&v, settings, "/mask"))
                set_color_value(value, def, v);
        }

        if (!def->name)
            continue;

        g_string_truncate(str, pfxlen);
        g_string_append(str, def->name);
        quark = g_quark_from_string(str->str);
        if (type == GWY_PARAM_BOOLEAN)
            gwy_container_gis_boolean(settings, quark, &value->b);
        else if (type == GWY_PARAM_INT || type == GWY_PARAM_ACTIVE_PAGE) {
            gint32 v;
            if (gwy_container_gis_int32(settings, quark, &v))
                set_int_value(value, def, v);
        }
        else if (type == GWY_PARAM_RANDOM_SEED) {
            gint32 v;
            if (gwy_container_gis_int32(settings, quark, &v))
                set_random_seed_value(value, def, v);
            /* Do not randomize here.  We may not know yet whether or not.  But remember we may have to. */
        }
        else if (type == GWY_PARAM_ENUM) {
            gint32 v;
            if (gwy_container_gis_int32(settings, quark, &v))
                set_enum_value(value, def, v);
        }
        else if (type == GWY_PARAM_FLAGS) {
            guint v;
            if (gwy_container_gis_enum(settings, quark, &v))
                set_flags_value(value, def, v);
        }
        else if (type == GWY_PARAM_REPORT_TYPE) {
            guint v;
            if (gwy_container_gis_enum(settings, quark, &v))
                set_report_type_value(value, def, v);
        }
        else if (type == GWY_PARAM_DOUBLE) {
            gdouble v;
            if (gwy_container_gis_double(settings, quark, &v))
                set_double_value(value, def, v);
        }
        else if (type == GWY_PARAM_STRING) {
            const guchar *v;
            if (gwy_container_gis_string(settings, quark, &v))
                set_string_value(value, def, v);
        }
        else if (param_type_is_curve_no(type)) {
            const guchar *v;
            if (gwy_container_gis_string(settings, quark, &v))
                set_curve_string_value(value, def, v);
        }
        else if (type == GWY_PARAM_UNIT) {
            const guchar *v;
            if (gwy_container_gis_string(settings, quark, &v))
                set_unit_value(value, def, v);
        }
        else if (type == GWY_PARAM_RESOURCE) {
            const guchar *v;
            if (gwy_container_gis_string(settings, quark, &v))
                set_resource_value(value, def, v);
        }
        else if (type == GWY_PARAM_COLOR) {
            GwyRGBA v;
            if (gwy_rgba_get_from_container(&v, settings, str->str))
                set_color_value(value, def, v);
        }
        else if (param_type_is_data_id(type)) {
            GwyAppDataId *dataid = g_hash_table_lookup(data_ids, GUINT_TO_POINTER(quark));

            if (dataid) {
                value->di = *dataid;
                gwy_debug("restoring data id { %d, %d } for %s", dataid->datano, dataid->id, str->str);
                if (type == GWY_PARAM_IMAGE_ID)
                    gwy_app_data_id_verify_channel(&value->di);
                else if (type == GWY_PARAM_GRAPH_ID)
                    gwy_app_data_id_verify_graph(&value->di);
                else if (type == GWY_PARAM_VOLUME_ID)
                    gwy_app_data_id_verify_volume(&value->di);
                else if (type == GWY_PARAM_XYZ_ID)
                    gwy_app_data_id_verify_xyz(&value->di);
                else if (type == GWY_PARAM_CURVE_MAP_ID)
                    gwy_app_data_id_verify_curve_map(&value->di);
            }
        }
        else {
            g_assert_not_reached();
        }
    }
    /* Post-loading setup. */
    for (i = 0; i < n; i++) {
        const GwyParamDefItem *def = _gwy_param_def_item(priv->def, i);
        GwyParamType type = def->type;

        if (type == GWY_PARAM_RANDOM_SEED) {
            gint randomize_id = def->def.rs.randomize_id;

            if (randomize_id >= 0 && gwy_params_get_boolean(params, randomize_id))
                gwy_params_randomize_seed(params, def->id);
        }
    }
    G_UNLOCK(data_ids);
    g_string_free(str, TRUE);
}

/**
 * gwy_params_save_to_settings:
 * @params: A parameter value set.
 *
 * Saves a parameter value set to settings.
 *
 * The associated definitions must have function name set.
 *
 * Since: 2.59
 **/
void
gwy_params_save_to_settings(GwyParams *params)
{
    GwyParamsPrivate *priv;
    GwyParamValue *values;
    GwyContainer *settings;
    const gchar *modname;
    GString *str;
    guint i, n, pfxlen;
    GHashTable *data_ids;

    g_return_if_fail(GWY_IS_PARAMS(params));
    priv = params->priv;
    n = priv->nvalues;
    values = priv->values;

    g_return_if_fail(priv->def);
    modname = gwy_param_def_get_function_name(priv->def);
    if (!modname) {
        g_critical("NULL function name when trying to save parameters to settings.");
        return;
    }
    settings = gwy_app_settings_get();

    str = g_string_new("/module/");
    g_string_append(str, modname);
    g_string_append_c(str, '/');
    pfxlen = str->len;
    G_LOCK(data_ids);
    data_ids = ensure_data_ids();
    for (i = 0; i < n; i++) {
        GwyParamValue *value = values + i;
        const GwyParamDefItem *def = _gwy_param_def_item(priv->def, i);
        GwyParamType type = def->type;
        GQuark quark;

        if (!def->name)
            continue;

        g_string_truncate(str, pfxlen);
        g_string_append(str, def->name);
        quark = g_quark_from_string(str->str);
        if (type == GWY_PARAM_BOOLEAN)
            gwy_container_set_boolean(settings, quark, value->b);
        else if (type == GWY_PARAM_INT || type == GWY_PARAM_ACTIVE_PAGE)
            gwy_container_set_int32(settings, quark, value->i);
        else if (type == GWY_PARAM_ENUM)
            gwy_container_set_int32(settings, quark, value->i);
        else if (type == GWY_PARAM_FLAGS)
            gwy_container_set_enum(settings, quark, value->u);
        else if (type == GWY_PARAM_REPORT_TYPE)
            gwy_container_set_enum(settings, quark, value->rt);
        else if (type == GWY_PARAM_RANDOM_SEED)
            gwy_container_set_int32(settings, quark, value->i);
        else if (type == GWY_PARAM_DOUBLE)
            gwy_container_set_double(settings, quark, value->d);
        else if (type == GWY_PARAM_STRING)
            gwy_container_set_const_string(settings, quark, value->s ? value->s : "");
        else if (param_type_is_curve_no(type))
            gwy_container_set_const_string(settings, quark, value->cu.s ? value->cu.s : "");
        else if (type == GWY_PARAM_UNIT)
            gwy_container_set_const_string(settings, quark, value->si.s ? value->si.s : "");
        else if (type == GWY_PARAM_RESOURCE) {
            if (value->s)
                gwy_container_set_const_string(settings, quark, value->s);
            else
                gwy_container_remove(settings, quark);
        }
        else if (type == GWY_PARAM_COLOR)
            gwy_rgba_store_to_container(&value->c, settings, str->str);
        else if (param_type_is_data_id(type)) {
            GwyAppDataId *dataid = g_hash_table_lookup(data_ids, GUINT_TO_POINTER(quark));

            if (dataid)
                *dataid = value->di;
            else {
                dataid = g_slice_dup(GwyAppDataId, &value->di);
                g_hash_table_insert(data_ids, GUINT_TO_POINTER(quark), dataid);
            }
            gwy_debug("remembering data id { %d, %d } for %s", dataid->datano, dataid->id, str->str);
        }
        else {
            g_assert_not_reached();
        }
    }
    G_UNLOCK(data_ids);
    g_string_free(str, TRUE);
}

/**
 * gwy_params_get_boolean:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the value of a boolean parameter.
 *
 * This function can be used with any boolean-valued parameter, even if it is more complex/specific than a plain
 * boolean.
 *
 * Returns: The boolean parameter value.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_get_boolean(GwyParams *params,
                       gint id)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_BOOLEAN))
        return FALSE;
    return params->priv->values[i].b;
}

/**
 * gwy_params_set_boolean:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a plain boolean parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * This function can only be used with plain boolean parameters defined by gwy_param_def_add_boolean().  More
 * complex/specific parameters need to be set using dedicated setters.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_boolean(GwyParams *params,
                       gint id,
                       gboolean value)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_BOOLEAN))
        return FALSE;
    return set_boolean_value(params->priv->values + i, value);
}

/**
 * gwy_params_get_int:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the value of a plain integer parameter.
 *
 * This function can be used with any integer-valued parameter, even if it is more complex/specific than a plain
 * integer.
 *
 * It can also be used to get a random seed parameter value.  The seed value does not change between calls unless
 * gwy_params_randomize_seed() has been called.
 *
 * Returns: The integer parameter value.
 *
 * Since: 2.59
 **/
gint
gwy_params_get_int(GwyParams *params,
                   gint id)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_NONE))
        return 0;
    if (def->type == GWY_PARAM_INT
        || def->type == GWY_PARAM_ENUM
        || def->type == GWY_PARAM_RANDOM_SEED
        || def->type == GWY_PARAM_ACTIVE_PAGE)
        return params->priv->values[i].i;
    if (param_type_is_curve_no(def->type))
        return params->priv->values[i].cu.i;
    if (def->type == GWY_PARAM_FLAGS)
        return params->priv->values[i].u;
    if (def->type == GWY_PARAM_REPORT_TYPE)
        return params->priv->values[i].rt;
    g_return_val_if_reached(0);
}

/**
 * gwy_params_set_int:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a integer parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * This function can only be used with plain integer parameters defined by gwy_param_def_add_int() and random seeds
 * created with gwy_param_def_add_seed().  More complex/specific parameters need to be set using dedicated setters.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_int(GwyParams *params,
                   gint id,
                   gint value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_NONE))
        return FALSE;
    if (def->type == GWY_PARAM_INT || def->type == GWY_PARAM_ACTIVE_PAGE)
        return set_int_value(params->priv->values + i, def, value);
    if (param_type_is_curve_no(def->type))
        return set_curve_int_value(params->priv->values + i, def, value);
    if (def->type == GWY_PARAM_RANDOM_SEED)
        return set_random_seed_value(params->priv->values + i, def, value);
    g_return_val_if_reached(0);
}

/**
 * gwy_params_get_enum:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the value of a enum parameter.
 *
 * This function can be used with any enum-valued parameter, even if it is more complex/specific than a plain
 * enum.
 *
 * Returns: The enum parameter value.
 *
 * Since: 2.59
 **/
gint
gwy_params_get_enum(GwyParams *params,
                    gint id)
{
    return get_enum_internal(params, id, 0, 0);
}

/**
 * gwy_params_get_masking:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 * @mask: Pointer to mask data field to update (can also be %NULL).
 *
 * Gets the value of a masking type enum parameter, consistently with a mask field.
 *
 * If @mask is %NULL the returned value is simply the parameter value.
 *
 * If @mask is a pointer then @mask and the return value can be modified.  The function will set @mask to %NULL if
 * masking is %GWY_MASK_IGNORE.  Conversely, it will return %GWY_MASK_IGNORE if @mask is a pointer to %NULL data
 * field.  In short, if a @mask argument is passed a non-ignore masking mode is guaranteed to coincide with non-%NULL
 * mask.
 *
 * Returns: The masking enum parameter value.
 *
 * Since: 2.59
 **/
GwyMaskingType
gwy_params_get_masking(GwyParams *params,
                       gint id,
                       GwyDataField **mask)
{
    GwyMaskingType masking;

    masking = get_enum_internal(params, id, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    if (!mask)
        return masking;

    if (*mask && masking == GWY_MASK_IGNORE)
        *mask = NULL;
    else if (!*mask)
        masking = GWY_MASK_IGNORE;

    return masking;
}

/**
 * gwy_params_set_enum:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a generic enum parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * This function can only be used with plain enum parameters defined by gwy_param_def_add_gwyenum() or
 * gwy_param_def_add_enum().  More complex/specific parameters need to be set using dedicated setters.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_enum(GwyParams *params,
                    gint id,
                    gint value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_ENUM))
        return FALSE;
    return set_enum_value(params->priv->values + i, def, value);
}

/**
 * gwy_params_get_flags:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the value of a flags parameter.
 *
 * This function can be used with any flags-valued parameter, even if it is more complex/specific than a plain
 * flag set.
 *
 * Returns: The fags parameter value.
 *
 * Since: 2.59
 **/
guint
gwy_params_get_flags(GwyParams *params,
                     gint id)
{
    return get_flags_internal(params, id, 0);
}

/**
 * gwy_params_set_flags:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a generic flags parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * This function can only be used with plain flags parameters defined by gwy_param_def_add_gwyflags().  More
 * complex/specific parameters need to be set using dedicated setters.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_flags(GwyParams *params,
                     gint id,
                     guint value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_FLAGS))
        return FALSE;
    return set_flags_value(params->priv->values + i, def, value);
}

/**
 * gwy_params_set_flag:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 * @flag: The flag bit to set or unset (it can be actually a flag combination).
 * @value: %TRUE to set the flag, %FALSE to unset the flag.
 *
 * Modifies the value of a generic flags parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * This function can only be used with plain flags parameters defined by gwy_param_def_add_gwyflags().  More
 * complex/specific parameters need to be set using dedicated setters.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_flag(GwyParams *params,
                    gint id,
                    guint flag,
                    gboolean value)
{
    const GwyParamDefItem *def;
    guint u;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_FLAGS))
        return FALSE;
    u = params->priv->values[i].u;
    if (value)
        u |= flag;
    else
        u &= ~flag;

    return set_flags_value(params->priv->values + i, def, u);
}

/**
 * gwy_params_get_report_type:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the value of a report type parameter.
 *
 * This function can only be used with a parameter defined by gwy_param_def_add_report_type().
 *
 * Returns: The report type parameter value.
 *
 * Since: 2.59
 **/
GwyResultsReportType
gwy_params_get_report_type(GwyParams *params,
                           gint id)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_REPORT_TYPE))
        return GWY_RESULTS_REPORT_COLON | GWY_RESULTS_REPORT_MACHINE;
    return params->priv->values[i].rt;
}

/**
 * gwy_params_set_report_type:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a report type parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * This function can only be used with parameters defined by gwy_param_def_add_report_type().
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_report_type(GwyParams *params,
                           gint id,
                           GwyResultsReportType value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_REPORT_TYPE))
        return FALSE;
    return set_report_type_value(params->priv->values + i, def, value);
}

/**
 * gwy_params_get_double:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the value of a floating point parameter.
 *
 * This function can be used with any double-valued parameter, even if it is more complex/specific than a plain
 * double.
 *
 * Returns: The floating point parameter value.
 *
 * Since: 2.59
 **/
gdouble
gwy_params_get_double(GwyParams *params,
                      gint id)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_DOUBLE))
        return 0.0;
    return params->priv->values[i].d;
}

/**
 * gwy_params_set_double:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a plain floating point parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * This function can only be used with plain floating point parameters defined by gwy_param_def_add_double() and
 * simple transformed parameters such as gwy_param_def_add_angle() or gwy_param_def_add_percentage().  More
 * complex/specific parameters need to be set using dedicated setters.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_double(GwyParams *params,
                      gint id,
                      gdouble value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_DOUBLE))
        return FALSE;
    return set_double_value(params->priv->values + i, def, value);
}

/**
 * gwy_params_get_string:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the value of a string parameter.
 *
 * This function can be used with any string-valued parameter (for instance units and resource names), even if it is
 * more complex/specific than a plain string.
 *
 * Returns: The string parameter value.  It is owned by @params and it is only guaranteed to be valid until the
 *          parameter change (or the object is destroyed).
 *
 * Since: 2.59
 **/
const gchar*
gwy_params_get_string(GwyParams *params,
                      gint id)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_NONE))
        return "";
    if (def->type == GWY_PARAM_STRING || def->type == GWY_PARAM_RESOURCE)
        return params->priv->values[i].s;
    if (def->type == GWY_PARAM_UNIT)
        return params->priv->values[i].si.s;
    if (param_type_is_curve_no(def->type))
        return params->priv->values[i].cu.s;
    g_return_val_if_reached("");
}

/**
 * gwy_params_set_string:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a string parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_string(GwyParams *params,
                      gint id,
                      const gchar *value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_NONE))
        return FALSE;
    if (def->type == GWY_PARAM_STRING)
        return set_string_value(params->priv->values + i, def, value);
    if (param_type_is_curve_no(def->type))
        return set_curve_string_value(params->priv->values + i, def, value);
    g_assert_not_reached();
    return FALSE;
}

/**
 * gwy_params_set_unit:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a unit parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_unit(GwyParams *params,
                    gint id,
                    const gchar *value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_UNIT))
        return FALSE;
    return set_unit_value(params->priv->values + i, def, value);
}

/**
 * gwy_params_get_unit:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @power10: Location where to store the power of 10 (or %NULL).
 *
 * Gets an SI unit object for a unit parameter.
 *
 * Use gwy_params_get_string() if you simply want the unit string.
 *
 * The returned object is not guaranteed to be updated automatically when the parameter changes.  It must not be
 * assumed it will persist past the destruction of @params.  Briefly, do not keep it around.
 *
 * Returns: An SI unit, owned by @params.  It must not be modified nor freed.
 *
 * Since: 2.59
 **/
GwySIUnit*
gwy_params_get_unit(GwyParams *params,
                    gint id,
                    gint *power10)
{
    GwyParamValueUnit *si;
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_UNIT)) {
        if (power10)
            *power10 = 10;
        /* This is a leak.  On a failed assertion, we do not care. */
        return gwy_si_unit_new(NULL);
    }

    si = &params->priv->values[i].si;
    if (!si->cached_valid) {
        if (!si->cached_unit)
            si->cached_unit = gwy_si_unit_new(NULL);
        gwy_si_unit_set_from_string_parse(si->cached_unit, si->s, &si->cached_power10);
        si->cached_valid = TRUE;
    }
    if (power10)
        *power10 = si->cached_power10;
    return si->cached_unit;
}

/**
 * gwy_params_get_color:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 *
 * Gets the value of a colour parameter.
 *
 * Returns: The colour parameter value.
 *
 * Since: 2.59
 **/
GwyRGBA
gwy_params_get_color(GwyParams *params,
                     gint id)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_COLOR)) {
        GwyRGBA color = gwy_param_fallback_color;
        return color;
    }
    return params->priv->values[i].c;
}

/**
 * gwy_params_set_color:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a colour parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_color(GwyParams *params,
                     gint id,
                     GwyRGBA value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_COLOR))
        return FALSE;
    return set_color_value(params->priv->values + i, def, value);
}

/**
 * gwy_params_get_data_id:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the value of a data identifier parameter.
 *
 * This function can be used with any specific identifier-valued parameter, even though they have dedicated creators
 * and setters.
 *
 * Returns: The data identifier parameter value.
 *
 * Since: 2.59
 **/
GwyAppDataId
gwy_params_get_data_id(GwyParams *params,
                       gint id)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_NONE))
        return noid;
    g_return_val_if_fail(param_type_is_data_id(def->type), noid);
    return params->priv->values[i].di;
}

/**
 * gwy_params_data_id_is_none:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Checks if data identifier parameter is set to no-data.
 *
 * This convenience function can be used with any specific identifier-valued parameter.
 *
 * Returns: %TRUE if the data identifier parameter is no-data and gwy_params_get_data_id() would return
 *          %GWY_APP_DATA_ID_NONE; %FALSE otherwise if some real data object is selected.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_data_id_is_none(GwyParams *params,
                           gint id)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_NONE))
        return TRUE;
    g_return_val_if_fail(param_type_is_data_id(def->type), TRUE);
    return params->priv->values[i].di.datano < 1 || params->priv->values[i].di.id < 0;
}

/**
 * gwy_params_get_image:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the data field object for an image identifier parameter.
 *
 * This is a more convenient alternative to gwy_params_get_data_id() since it fetches and checks the object.
 *
 * Returns: The image data field, or %NULL if the parameter is none (or the image no longer exists).
 *
 * Since: 2.59
 **/
GwyDataField*
gwy_params_get_image(GwyParams *params,
                     gint id)
{
    return get_data_object(params, id, GWY_PARAM_IMAGE_ID, gwy_app_get_data_key_for_id, GWY_TYPE_DATA_FIELD);
}

/**
 * gwy_params_get_mask:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the data field object for an mask identifier parameter.
 *
 * This is a more convenient alternative to gwy_params_get_data_id() since it fetches and checks the object.
 *
 * Returns: The mask data field, or %NULL if the parameter is none (or the image no longer exists).
 *
 * Since: 2.59
 **/
GwyDataField*
gwy_params_get_mask(GwyParams *params,
                    gint id)
{
    return get_data_object(params, id, GWY_PARAM_IMAGE_ID, gwy_app_get_mask_key_for_id, GWY_TYPE_DATA_FIELD);
}

/**
 * gwy_params_get_graph:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the graph model object for a graph identifier parameter.
 *
 * This is a more convenient alternative to gwy_params_get_data_id() since it fetches and checks the object.
 *
 * Returns: The graph model of the graph, or %NULL if the parameter is none (or the graph no longer exists).
 *
 * Since: 2.59
 **/
GwyGraphModel*
gwy_params_get_graph(GwyParams *params,
                     gint id)
{
    return get_data_object(params, id, GWY_PARAM_GRAPH_ID, gwy_app_get_graph_key_for_id, GWY_TYPE_GRAPH_MODEL);
}

/**
 * gwy_params_get_volume:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the brick object for a volume data identifier parameter.
 *
 * This is a more convenient alternative to gwy_params_get_data_id() since it fetches and checks the object.
 *
 * Returns: The volume data brick, or %NULL if the parameter is none (or the volume data no longer exist).
 *
 * Since: 2.59
 **/
GwyBrick*
gwy_params_get_volume(GwyParams *params,
                      gint id)
{
    return get_data_object(params, id, GWY_PARAM_VOLUME_ID, gwy_app_get_brick_key_for_id, GWY_TYPE_BRICK);
}

/**
 * gwy_params_get_xyz:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the surface object for a xyz data identifier parameter.
 *
 * This is a more convenient alternative to gwy_params_get_data_id() since it fetches and checks the object.
 *
 * Returns: The xyz data surface, or %NULL if the parameter is none (or the xyz data no longer exist).
 *
 * Since: 2.59
 **/
GwySurface*
gwy_params_get_xyz(GwyParams *params,
                   gint id)
{
    return get_data_object(params, id, GWY_PARAM_XYZ_ID, gwy_app_get_surface_key_for_id, GWY_TYPE_SURFACE);
}

/**
 * gwy_params_get_curve_map:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Gets the lawn object for a curve map data identifier parameter.
 *
 * This is a more convenient alternative to gwy_params_get_data_id() since it fetches and checks the object.
 *
 * Returns: The curve map data lawn, or %NULL if the parameter is none (or the curve map data no longer exist).
 *
 * Since: 2.60
 **/
GwyLawn*
gwy_params_get_curve_map(GwyParams *params,
                         gint id)
{
    return get_data_object(params, id, GWY_PARAM_CURVE_MAP_ID, gwy_app_get_lawn_key_for_id, GWY_TYPE_LAWN);
}

/**
 * gwy_params_set_image_id:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of an image identifier parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * If @value is not %GWY_APP_DATA_ID_NONE then the image identified by @value must currently exist.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_image_id(GwyParams *params,
                        gint id,
                        GwyAppDataId value)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_IMAGE_ID))
        return FALSE;
    g_return_val_if_fail((value.datano == 0 && value.id == -1) || gwy_app_data_id_verify_channel(&value), FALSE);
    return set_data_id_value(params->priv->values + i, value);
}

/**
 * gwy_params_set_graph_id:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a graph identifier parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * If @value is not %GWY_APP_DATA_ID_NONE then the graph identified by @value must currently exist.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_graph_id(GwyParams *params,
                        gint id,
                        GwyAppDataId value)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_GRAPH_ID))
        return FALSE;
    g_return_val_if_fail((value.datano == 0 && value.id == -1) || gwy_app_data_id_verify_graph(&value), FALSE);
    return set_data_id_value(params->priv->values + i, value);
}

/**
 * gwy_params_set_volume_id:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a volume data identifier parameter.
 *
 * If @value is not %GWY_APP_DATA_ID_NONE then the volume data identified by @value must currently exist.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_volume_id(GwyParams *params,
                         gint id,
                         GwyAppDataId value)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_VOLUME_ID))
        return FALSE;
    g_return_val_if_fail((value.datano == 0 && value.id == -1) || gwy_app_data_id_verify_volume(&value), FALSE);
    return set_data_id_value(params->priv->values + i, value);
}

/**
 * gwy_params_set_xyz_id:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of an xyz data identifier parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * If @value is not %GWY_APP_DATA_ID_NONE then the XYZ data identified by @value must currently exist.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_xyz_id(GwyParams *params,
                      gint id,
                      GwyAppDataId value)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_XYZ_ID))
        return FALSE;
    g_return_val_if_fail((value.datano == 0 && value.id == -1) || gwy_app_data_id_verify_xyz(&value), FALSE);
    return set_data_id_value(params->priv->values + i, value);
}

/**
 * gwy_params_set_curve_map_id:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a curve map data identifier parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * If @value is not %GWY_APP_DATA_ID_NONE then the curve map data identified by @value must currently exist.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.60
 **/
gboolean
gwy_params_set_curve_map_id(GwyParams *params,
                            gint id,
                            GwyAppDataId value)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, GWY_PARAM_CURVE_MAP_ID))
        return FALSE;
    g_return_val_if_fail((value.datano == 0 && value.id == -1) || gwy_app_data_id_verify_curve_map(&value), FALSE);
    return set_data_id_value(params->priv->values + i, value);
}

/**
 * gwy_params_set_curve:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @value: Value to set (a non-negative integer or -1).
 *
 * Sets the value of a graph or lawn curve number parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.60
 **/
gboolean
gwy_params_set_curve(GwyParams *params,
                     gint id,
                     gint value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_NONE))
        return FALSE;
    g_return_val_if_fail(param_type_is_curve_no(def->type), FALSE);
    return set_curve_int_value(params->priv->values + i, def, value);
}

/**
 * gwy_params_randomize_seed:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 *
 * Randomizes the value of a random seed parameter.
 *
 * You can use gwy_params_get_int() any number of times afterwards and obtain the same value.
 *
 * This function is seldom needed.  See the introduction for discussion.  In the usual setup the seed is randomised
 * when loaded from settings if the controlling randomization boolean is %TRUE.  Furthermore, #GwyParamTable knows to
 * randomize the seed when the randomization button is pressed.
 *
 * Returns: The new random seed value (for convenience).
 *
 * Since: 2.59
 **/
gint
gwy_params_randomize_seed(GwyParams *params,
                          gint id)
{
    const GwyParamDefItem *def;
    gint i, seed;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_RANDOM_SEED))
        return 42;
    seed = g_random_int() & 0x7fffffff;
    set_random_seed_value(params->priv->values + i, def, seed);
    return seed;
}

/**
 * gwy_params_set_resource:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 * @value: Value to set.
 *
 * Sets the value of a resource name parameter.
 *
 * This function is seldom needed.  See the introduction for discussion.
 *
 * Returns: %TRUE if parameter value has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_params_set_resource(GwyParams *params,
                        gint id,
                        const gchar *value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_RESOURCE))
        return FALSE;
    return set_resource_value(params->priv->values + i, def, value);
}

/**
 * gwy_params_get_resource:
 * @params: A set of parameter values.
 * @id: Parameter identifier.
 *
 * Gets the resource object of a resource name parameter.
 *
 * Returns: The resource object corresponding to the parameter value.  A default value is returned if the resource
 *          does not exist.
 *
 * Since: 2.61
 **/
GwyResource*
gwy_params_get_resource(GwyParams *params,
                        gint id)
{
    const GwyParamDefItem *def;
    gpointer item;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_RESOURCE))
        return NULL;
    /* Fall back first to the parameter default, then to rhe resource default. */
    if (!(item = gwy_inventory_get_item(def->def.res.inventory, params->priv->values[i].s)))
        item = gwy_inventory_get_item_or_default(def->def.res.inventory, def->def.res.default_value);
    return item;
}

static gboolean
set_boolean_value(GwyParamValue *pvalue, gboolean value)
{
    if (!pvalue->b ^ !value) {
        pvalue->b = !!value;
        return TRUE;
    }
    return FALSE;
}

static gboolean
set_int_value(GwyParamValue *pvalue, const GwyParamDefItem *def, gint value)
{
    gint goodvalue = _gwy_param_def_rectify_int(def, value);

    if (value != goodvalue) {
        g_warning("Value %d is not in the range of int id=%d.", value, def->id);
    }
    if (pvalue->i == goodvalue)
        return FALSE;
    pvalue->i = goodvalue;
    return TRUE;
}

static gboolean
set_random_seed_value(GwyParamValue *pvalue, const GwyParamDefItem *def, gint value)
{
    gint goodvalue = _gwy_param_def_rectify_random_seed(def, value);

    if (value != goodvalue) {
        g_warning("Value %d is not in the range of random seed id=%d.", value, def->id);
    }
    if (pvalue->i == goodvalue)
        return FALSE;
    pvalue->i = goodvalue;
    return TRUE;
}

static gboolean
set_double_value(GwyParamValue *pvalue, const GwyParamDefItem *def, gdouble value)
{
    gdouble goodvalue = _gwy_param_def_rectify_double(def, value);

    if (fabs(value - goodvalue) > 1e-14*(fabs(value) + fabs(goodvalue))) {
        g_warning("Value %.14g is not in the range of double id=%d.", value, def->id);
    }
    if (pvalue->d == goodvalue)
        return FALSE;
    pvalue->d = goodvalue;
    return TRUE;
}

static gboolean
set_enum_value(GwyParamValue *pvalue, const GwyParamDefItem *def, gint value)
{
    gint goodvalue;

    goodvalue = _gwy_param_def_rectify_enum(def, value);
    if (value != goodvalue) {
        g_warning("Value %d is not in enum id=%d.", value, def->id);
    }
    if (pvalue->i == goodvalue)
        return FALSE;
    pvalue->i = goodvalue;
    return TRUE;
}

static gboolean
set_flags_value(GwyParamValue *pvalue, const GwyParamDefItem *def, guint value)
{
    gint goodvalue;

    goodvalue = _gwy_param_def_rectify_flags(def, value);
    if (value != goodvalue) {
        g_warning("Value %u is not in flags id=%d.", value, def->id);
    }
    if (pvalue->u == goodvalue)
        return FALSE;
    pvalue->u = goodvalue;
    return TRUE;
}

static gboolean
set_report_type_value(GwyParamValue *pvalue, const GwyParamDefItem *def, GwyResultsReportType value)
{
    GwyResultsReportType goodvalue;

    goodvalue = _gwy_param_def_rectify_report_type(def, value);
    if (value != goodvalue) {
        g_warning("Value %u is not in report type id=%d.", value, def->id);
    }
    if (pvalue->rt == goodvalue)
        return FALSE;
    pvalue->rt = goodvalue;
    return TRUE;
}

static gboolean
set_string_value(GwyParamValue *pvalue, const GwyParamDefItem *def, const gchar *value)
{
    gchar *rectified = _gwy_param_def_rectify_string(def, value);

    if (!g_strcmp0(pvalue->s, rectified)) {
        g_free(rectified);
        return FALSE;
    }
    g_free(pvalue->s);
    pvalue->s = rectified;
    return TRUE;
}

static gboolean
set_curve_string_value(GwyParamValue *pvalue, const GwyParamDefItem *def, const gchar *value)
{
    g_return_val_if_fail(param_type_is_curve_no(def->type), FALSE);
    pvalue->cu.use_string = TRUE;
    return gwy_assign_string(&pvalue->cu.s, value);
}

static gboolean
set_curve_int_value(GwyParamValue *pvalue, const GwyParamDefItem *def, gint value)
{
    gint goodvalue = MAX(value, 0);
    if (value != goodvalue) {
        g_warning("Value %d is not a valid curve number in id=%d.", value, def->id);
        return FALSE;
    }
    pvalue->cu.use_string = FALSE;
    if (pvalue->cu.i == value)
        return FALSE;
    pvalue->cu.i = value;
    return TRUE;
}

gboolean
_gwy_params_curve_get_use_string(GwyParams *params, gint id)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_NONE))
        return 0;
    g_return_val_if_fail(param_type_is_curve_no(def->type), FALSE);
    return params->priv->values[i].cu.use_string;
}

static gboolean
set_unit_value(GwyParamValue *pvalue, const GwyParamDefItem *def, const gchar *value)
{
    gchar *rectified = _gwy_param_def_rectify_unit(def, value);

    if (!g_strcmp0(pvalue->si.s, rectified)) {
        g_free(rectified);
        return FALSE;
    }
    g_free(pvalue->s);
    pvalue->si.s = rectified;
    pvalue->si.cached_valid = FALSE;
    return TRUE;
}

static gboolean
set_color_value(GwyParamValue *pvalue, const GwyParamDefItem *def, GwyRGBA value)
{
    GwyRGBA goodvalue = _gwy_param_def_rectify_color(def, value);
    GwyRGBA *rgba;

    if (value.r != goodvalue.r || value.g != goodvalue.g || value.b != goodvalue.b) {
        g_warning("Color component values are not in the allowed range of color id=%d.", def->id);
    }
    if (def->def.c.has_alpha && value.a != goodvalue.a) {
        g_warning("Alpha value is not in the allowed range of color id=%d.", def->id);
    }
    rgba = &pvalue->c;
    if (rgba->r == goodvalue.r && rgba->g == goodvalue.b && rgba->b == goodvalue.b && rgba->a == goodvalue.a)
        return FALSE;
    pvalue->c = goodvalue;
    return TRUE;
}

static gboolean
set_data_id_value(GwyParamValue *pvalue, GwyAppDataId value)
{
    /* Caller verifies.  Just check if the value has changed. */
    if (pvalue->di.datano == value.datano && pvalue->di.id == value.id)
        return FALSE;
    pvalue->di = value;
    return TRUE;
}

static gboolean
set_resource_value(GwyParamValue *pvalue, const GwyParamDefItem *def, const gchar *value)
{
    const gchar *goodvalue = _gwy_param_def_rectify_resource(def, value);

    if (g_strcmp0(goodvalue, value)) {
        g_warning("Resource name does not correspond to any item in the inventory for id=%d.", def->id);
    }
    /* The empty inventory case is odd and should not normally happen. */
    return gwy_assign_string(&pvalue->s, goodvalue);
}

static gint
get_enum_internal(GwyParams *params,
                  gint id,
                  GType type,
                  gint fallback_value)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_ENUM))
        return fallback_value;
    if (type) {
        g_return_val_if_fail(def->def.e.gtype == type, fallback_value);
    }
    return params->priv->values[i].i;
}

static guint
get_flags_internal(GwyParams *params,
                   gint id,
                   GType type)
{
    const GwyParamDefItem *def;
    gint i;

    if (!param_find_common(params, id, &i, &def, GWY_PARAM_FLAGS))
        return 0;
    if (type) {
        g_return_val_if_fail(def->def.f.gtype == type, 0);
    }
    return params->priv->values[i].u;
}

static gboolean
reset_param_value(GwyParams *params, guint i)
{
    GwyParamsPrivate *priv = params->priv;
    const GwyParamDefItem *def = _gwy_param_def_item(priv->def, i);
    GwyParamValue *value = priv->values + i;
    GwyParamType type = def->type;

    /* Here we check again the validity of default values which should not be necessary.  But well… */
    if (type == GWY_PARAM_BOOLEAN)
        return set_boolean_value(value, def->def.b.default_value);
    if (type == GWY_PARAM_INT)
        return set_int_value(value, def, def->def.i.default_value);
    if (type == GWY_PARAM_ENUM)
        return set_enum_value(value, def, def->def.e.table[def->def.e.default_value_index].value);
    if (type == GWY_PARAM_FLAGS)
        return set_flags_value(value, def, def->def.f.default_value);
    if (type == GWY_PARAM_REPORT_TYPE)
        return set_report_type_value(value, def, def->def.rt.default_value);
    if (type == GWY_PARAM_RANDOM_SEED) {
        gwy_params_randomize_seed(params, def->id);
        return TRUE;
    }
    if (type == GWY_PARAM_ACTIVE_PAGE)
        return set_int_value(value, def, 0);
    if (type == GWY_PARAM_DOUBLE)
        return set_double_value(value, def, def->def.d.default_value);
    if (type == GWY_PARAM_STRING)
        return set_string_value(value, def, def->def.s.default_value);
    if (type == GWY_PARAM_UNIT)
        return set_unit_value(value, def, def->def.si.default_value);
    if (type == GWY_PARAM_RESOURCE)
        return set_resource_value(value, def, def->def.res.default_value);
    if (type == GWY_PARAM_COLOR)
        return set_color_value(value, def, def->def.c.default_value);
    if (param_type_is_data_id(type))
        return set_data_id_value(value, noid);
    if (param_type_is_curve_no(type))
        return set_curve_int_value(value, def, def->def.i.default_value);
    g_return_val_if_reached(FALSE);
}

/* This also works for curve parameters, by getting the parent object. */
static gpointer
get_data_object(GwyParams *params, gint id,
                GwyParamType param_type, GQuark (*get_key_func)(gint id), GType object_type)
{
    gint i;

    if (!param_find_common(params, id, &i, NULL, param_type))
        return NULL;
    return get_data_object_for_id(params->priv->values[i].di, get_key_func, object_type);
}

static gpointer
get_data_object_for_id(GwyAppDataId dataid, GQuark (*get_key_func)(gint id), GType object_type)
{
    GwyContainer *container;
    GObject *object;

    if (dataid.datano < 1
        || dataid.id < 0
        || !(container = gwy_app_data_browser_get(dataid.datano))
        || !gwy_container_gis_object(container, get_key_func(dataid.id), &object)
        || !g_type_is_a(G_TYPE_FROM_INSTANCE(object), object_type))
        return NULL;
    return object;
}

/* Only called when it should not fail.  So make assertions on behalf of the caller. */
static inline gboolean
param_find_common(GwyParams *params, gint id, gint *i, const GwyParamDefItem **def,
                  GwyParamType want_type)
{
    const GwyParamDefItem *mydef;
    GwyParamsPrivate *priv;

    g_return_val_if_fail(GWY_IS_PARAMS(params), FALSE);
    priv = params->priv;
    *i = _gwy_param_def_index(priv->def, id);
    g_return_val_if_fail(*i >= 0 && *i < priv->nvalues, FALSE);
    mydef = _gwy_param_def_item(priv->def, *i);
    if (want_type != GWY_PARAM_NONE && mydef->type != want_type) {
        g_warning("Parameter with id %u has type %d, not %d.", id, mydef->type, want_type);
        return FALSE;
    }
    if (def)
        *def = mydef;
    return TRUE;
}

/* NB: This keeps both data id and curve id parameter values.  This is OK as long as modules do not try something
 * silly.  Then we can get mixed DataId and CurveId params – and probably crash. */
static GHashTable*
ensure_data_ids(void)
{
    static GHashTable* data_ids = NULL;          /* Threads: protected by lock */

    if (!data_ids)
        data_ids = g_hash_table_new(g_direct_hash, g_direct_equal);

    return data_ids;
}

/**
 * SECTION:params
 * @title: GwyParams
 * @short_description: Module parameter value sets
 *
 * #GwyParams represents a set of module parameter values.  Usually it is created by loading the values from settings
 * using gwy_params_new_from_settings().  It can also be created as empty – it is necessary to tie it to a definition
 * set using gwy_params_set_def() then.  In any case, the tie to the definitions is permanent.  If you need parameter
 * value set for different definitions create a new object.
 *
 * #GwyParams is a “dumb” object, replacing a plain C struct holding the parameter values.  Beside making sure
 * parameter values stay in defined ranges, it does not do much.  In particular it does not emit any singals.
 *
 * Each parameter type has its own getters and setters, such as gwy_params_get_boolean() and gwy_params_set_boolean().
 * For a few parameter types there may also be dedicated helper functions such as gwy_params_get_masking() which
 * encapsulate common logic when dealing with these parameter.
 *
 * The setters and reset functions should be seldom needed.  When running module GUI, parameter values should be set
 * by #GwyParamTable functions to ensure the updates cascade as expected.  #GwyParamTable also has a reset function.
 * Setters do not cause any GUI response and, hence, has no place in most modules as they would lead to an
 * inconsistent state.  Sometimes, however, setting parameter values directly can be useful in non-GUI paths which
 * perform extra validation or ensure complex invariants.
 **/

/**
 * GwyParams:
 *
 * Object representing a set of parameter values.
 *
 * The #GwyParams struct contains no public fields.
 *
 * Since: 2.59
 **/

/**
 * GwyParamsClass:
 *
 * Class of parameter value sets.
 *
 * Since: 2.59
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
