/*
 *  $Id: good_profile.c 24365 2021-10-13 12:45:57Z yeti-dn $
 *  Copyright (C) 2020-2021 David Necas (Yeti).
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/level.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/correct.h>
#include <libprocess/arithmetic.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    GOOD_PROF_SINGLE   = 0,
    GOOD_PROF_MULTIPLE = 1,
} GoodProfMode;

typedef enum {
    GOOD_PROF_IMAGE = 0,
    GOOD_PROF_GRAPH = 1,
} GoodProfDisplay;

enum {
    PARAM_CREATE_MASK,
    PARAM_DISPLAY,
    PARAM_MODE,
    PARAM_UPDATE,
    PARAM_TRIM_FRACTION,
    PARAM_OTHER_IMAGE,
    PARAM_TARGET_GRAPH,
    PARAM_MASK_COLOR,
    INFO_VARIATION,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyGraphModel *gmodel;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GtkWidget *dataview;
    GtkWidget *graph;
    GwySIValueFormat *varvf;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             good_profile        (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args,
                                             gdouble *variation);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static gboolean         other_image_filter  (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);
static void             preview             (gpointer user_data);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates good average row from one or multiple images of repeated scanning of the same feature."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2020",
};

GWY_MODULE_QUERY2(module_info, good_profile)

static gboolean
module_register(void)
{
    gwy_process_func_register("good_profile",
                              (GwyProcessFunc)&good_profile,
                              N_("/_Correct Data/_Good Mean Profile..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Calculate good mean profile"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum modes[] = {
        { N_("_Single image"),    GOOD_PROF_SINGLE,   },
        { N_("_Multiple images"), GOOD_PROF_MULTIPLE, },
    };
    static const GwyEnum displays[] = {
        { N_("Data"),    GOOD_PROF_IMAGE, },
        { N_("Profile"), GOOD_PROF_GRAPH, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_boolean(paramdef, PARAM_CREATE_MASK, "create_mask", _("Create _mask"), TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, "display", gwy_sgettext("verb|Display"),
                              displays, G_N_ELEMENTS(displays), GOOD_PROF_GRAPH);
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", _("Mode"),
                              modes, G_N_ELEMENTS(modes), GOOD_PROF_SINGLE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_double(paramdef, PARAM_TRIM_FRACTION, "trim_fraction", _("_Trim fraction"), 0.0, 0.9999, 0.05);
    gwy_param_def_add_image_id(paramdef, PARAM_OTHER_IMAGE, "other_image", _("Second _image"));
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
good_profile(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GwyAppDataId target_graph_id;
    GwyDataField *field;
    GwyParams *params;
    ModuleArgs args;
    GQuark mquark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     0);
    g_return_if_fail(field);

    args.field = field;
    args.mask = NULL;
    args.gmodel = gwy_graph_model_new();
    gwy_graph_model_set_units_from_data_field(args.gmodel, field, 1, 0, 0, 1);
    args.params = params = gwy_params_new_from_settings(define_module_params());
    /* Avoid Laplace interpolation in execute() if we do not need it. */
    if (runtype == GWY_RUN_INTERACTIVE || gwy_params_get_boolean(params, PARAM_CREATE_MASK)) {
        args.mask = gwy_data_field_new_alike(field, TRUE);
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.mask), NULL);
    }
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args, NULL);

    /* Target can be new graph but we always create some graph output. */
    target_graph_id = gwy_params_get_data_id(params, PARAM_TARGET_GRAPH);
    gwy_app_add_graph_or_curves(args.gmodel, data, &target_graph_id, 1);

    if (gwy_params_get_boolean(params, PARAM_CREATE_MASK)) {
        /* This is convoluted because the images may come from different files – and we have to create two separate
         * undo checkpoints then, one in each file. */
        if (gwy_params_get_enum(params, PARAM_MODE) == GOOD_PROF_MULTIPLE) {
            GwyAppDataId other_image_id = gwy_params_get_data_id(params, PARAM_OTHER_IMAGE);
            GwyContainer *otherdata = gwy_app_data_browser_get(other_image_id.datano);
            GQuark quarks[2];

            quarks[0] = mquark;
            quarks[1] = gwy_app_get_mask_key_for_id(other_image_id.id);
            if (otherdata == data)
                gwy_app_undo_qcheckpointv(data, 2, quarks);
            else {
                gwy_app_undo_qcheckpointv(data, 1, quarks);
                gwy_app_undo_qcheckpointv(otherdata, 1, quarks+1);
                /* XXX: We should sync the mask colour, but only if the user has changed it.  Otherwise the second
                 * channel must be left alone. */
            }
            field = gwy_data_field_duplicate(args.mask);
            gwy_container_set_object(data, quarks[0], args.mask);
            gwy_container_set_object(otherdata, quarks[1], field);
            g_object_unref(field);
        }
        else {
            gwy_app_undo_qcheckpointv(data, 1, &mquark);
            gwy_container_set_object(data, mquark, args.mask);
        }
    }

end:
    g_object_unref(params);
    GWY_OBJECT_UNREF(args.mask);
    g_object_unref(args.gmodel);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GoodProfDisplay display = gwy_params_get_enum(args->params, PARAM_DISPLAY);
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *vbox;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.varvf = gwy_data_field_get_value_format_z(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    gui.varvf->precision++;
    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/0/mask", args->mask);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Good Mean Profile"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    vbox = gwy_vbox_new(0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 4);

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), gui.dataview, FALSE, FALSE, 0);
    if (display != GOOD_PROF_IMAGE)
        gtk_widget_set_no_show_all(gui.dataview, TRUE);

    gui.graph = gwy_graph_new(args->gmodel);
    gtk_widget_set_size_request(gui.graph, PREVIEW_SIZE, -1);
    gwy_graph_enable_user_input(GWY_GRAPH(gui.graph), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), gui.graph, TRUE, TRUE, 0);
    if (display != GOOD_PROF_GRAPH)
        gtk_widget_set_no_show_all(gui.graph, TRUE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_radio(table, PARAM_DISPLAY);
    gwy_param_table_set_no_reset(table, PARAM_DISPLAY, TRUE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_MODE);
    gwy_param_table_append_image_id(table, PARAM_OTHER_IMAGE);
    gwy_param_table_data_id_set_filter(table, PARAM_OTHER_IMAGE, other_image_filter, args->field, NULL);
    gwy_param_table_append_slider(table, PARAM_TRIM_FRACTION);
    gwy_param_table_append_info(table, INFO_VARIATION, _("Variation"));
    gwy_param_table_set_unitstr(table, INFO_VARIATION, gui.varvf->units);

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);
    gwy_param_table_append_checkbox(table, PARAM_CREATE_MASK);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, NULL, -1);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    if (outcome != GWY_DIALOG_CANCEL && gwy_params_get_boolean(args->params, PARAM_CREATE_MASK))
        gwy_app_sync_data_items(gui.data, data, 0, id, FALSE, GWY_DATA_ITEM_MASK_COLOR, 0);

    g_object_unref(gui.data);
    gwy_si_unit_value_format_free(gui.varvf);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;
    gboolean is_multiple = (gwy_params_get_enum(params, PARAM_MODE) == GOOD_PROF_MULTIPLE);
    gboolean has_any = !gwy_params_data_id_is_none(params, PARAM_OTHER_IMAGE);

    if (id < 0)
        gwy_param_table_radio_set_sensitive(table, PARAM_MODE, GOOD_PROF_MULTIPLE, has_any);
    if (id < 0 || id == PARAM_MODE)
        gwy_param_table_set_sensitive(table, PARAM_OTHER_IMAGE, is_multiple);

    if (id < 0 || id == PARAM_DISPLAY) {
        GoodProfDisplay display = gwy_params_get_enum(params, PARAM_DISPLAY);
        if (display == GOOD_PROF_IMAGE) {
            gtk_widget_set_no_show_all(gui->graph, TRUE);
            gtk_widget_set_no_show_all(gui->dataview, FALSE);
            gtk_widget_hide(gui->graph);
            gtk_widget_show_all(gui->dataview);
        }
        else {
            gtk_widget_set_no_show_all(gui->dataview, TRUE);
            gtk_widget_set_no_show_all(gui->graph, FALSE);
            gtk_widget_hide(gui->dataview);
            gtk_widget_show_all(gui->graph);
        }
    }

    if (id != PARAM_TARGET_GRAPH && id != PARAM_CREATE_MASK && id != PARAM_DISPLAY && id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static gboolean
other_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *other_image, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &other_image))
        return FALSE;
    if (other_image == field)
        return FALSE;

    return !gwy_data_field_check_compatibility(other_image, field, GWY_DATA_COMPATIBILITY_ALL);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwySIValueFormat *vf = gui->varvf;
    gdouble variation;
    gchar *s;

    execute(args, &variation);
    gwy_data_field_data_changed(args->mask);
    s = g_strdup_printf("%.*f", vf->precision, variation/vf->magnitude);
    gwy_param_table_info_set_valuestr(gui->table, INFO_VARIATION, s);
    g_free(s);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
good_profile_do_single(GwyDataLine *profile, ModuleArgs *args)
{
    GwyDataField *ffield, *fmask = NULL, *field = args->field, *mask = args->mask;
    gdouble trim_fraction = gwy_params_get_double(args->params, PARAM_TRIM_FRACTION);
    const gdouble *ddata, *drow;
    gdouble *data, *mdata = NULL, *row, *mrow, *ldata, *low = NULL, *high = NULL;
    gint xres, yres, ntrim, i, j, k[2];
    gdouble v[2];

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    ldata = gwy_data_line_get_data(profile);

    /* Work with a flipped field because we process it by column.  Note
     * that this means xres and yres have swapped meaning! */
    ffield = gwy_data_field_new_alike(field, FALSE);
    gwy_data_field_flip_xy(field, ffield, FALSE);
    data = gwy_data_field_get_data(ffield);
    if (mask) {
        fmask = gwy_data_field_new_alike(mask, FALSE);
        gwy_data_field_flip_xy(mask, fmask, FALSE);
        mdata = gwy_data_field_get_data(fmask);
        low = g_new(gdouble, xres);
        high = g_new(gdouble, xres);
    }

    ntrim = GWY_ROUND(0.5*trim_fraction*yres);
    /* Ensure we are at least using a single value. */
    if (2*ntrim + 1 > yres)
        ntrim = (yres-1)/2;

    k[0] = ntrim;
    k[1] = yres-1-ntrim;
    for (i = 0; i < xres; i++) {
        row = data + i*yres;
        ldata[i] = gwy_math_trimmed_mean(yres, row, ntrim, ntrim);
        if (mdata) {
            mrow = mdata + i*yres;
            gwy_math_kth_ranks(yres, row, 2, k, v);
            low[i] = v[0];
            high[i] = v[1];
        }
    }

    g_object_unref(ffield);
    if (fmask) {
        g_object_unref(fmask);
        ddata = gwy_data_field_get_data_const(field);
        mdata = gwy_data_field_get_data(mask);
        for (i = 0; i < yres; i++) {
            drow = ddata + i*xres;
            mrow = mdata + i*xres;
            for (j = 0; j < xres; j++)
                mrow[j] = (drow[j] < low[j] || drow[j] > high[j]);
        }
        g_free(low);
        g_free(high);
    }
}

static void
good_profile_do_multiple(GwyDataLine *profile, GwyDataLine *weights, ModuleArgs *args)
{
    GwyDataField *field = args->field, *mask = args->mask;
    gdouble trim_fraction = gwy_params_get_double(args->params, PARAM_TRIM_FRACTION);
    GwyDataField *field2 = gwy_params_get_image(args->params, PARAM_OTHER_IMAGE);
    GwyDataField *buf;
    const gdouble *d1, *d2;
    gdouble *b, *m;
    gdouble p, threshold;
    gint xres, yres, n, i;

    d1 = gwy_data_field_get_data_const(field);
    d2 = gwy_data_field_get_data_const(field2);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    n = xres*yres;

    buf = gwy_data_field_new_alike(field, FALSE);
    b = gwy_data_field_get_data(buf);

    if (!mask) {
        mask = gwy_data_field_new_alike(field, FALSE);
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mask), NULL);
    }
    else
        g_object_ref(mask);
    m = gwy_data_field_get_data(mask);

    /* Use m[] temporarily for the difference we pass to gwy_math_percentiles() and they get shuffled. */
    for (i = 0; i < n; i++)
        b[i] = m[i] = fabs(d1[i] - d2[i]);

    p = 100.0*(1.0 - trim_fraction);
    gwy_math_percentiles(n, m, GWY_PERCENTILE_INTERPOLATION_MIDPOINT, 1, &p, &threshold);

    for (i = 0; i < n; i++) {
        m[i] = (b[i] > threshold);
        b[i] = 0.5*(d1[i] + d2[i]);
    }
    gwy_data_field_get_line_stats_mask(buf, mask, GWY_MASK_EXCLUDE, profile, weights, 0, 0, xres, yres,
                                       GWY_LINE_STAT_MEAN, GWY_ORIENTATION_VERTICAL);

    g_object_unref(mask);
    g_object_unref(buf);
}

static void
execute(ModuleArgs *args, gdouble *variation)
{
    GoodProfMode mode = gwy_params_get_enum(args->params, PARAM_MODE);
    GwyGraphCurveModel *gcmodel;
    GwyDataField *field = args->field;
    GwyDataLine *profile, *weights = NULL;
    gdouble dx, xoff;
    gint xres, i, n;
    gdouble *d, *w;
    GwyXY *xy;

    xres = gwy_data_field_get_xres(field);
    profile = gwy_data_line_new(xres, gwy_data_field_get_xreal(field), TRUE);
    gwy_data_field_copy_units_to_data_line(field, profile);

    if (mode == GOOD_PROF_SINGLE)
        good_profile_do_single(profile, args);
    else if (mode == GOOD_PROF_MULTIPLE) {
        weights = gwy_data_line_new_alike(profile, FALSE);
        good_profile_do_multiple(profile, weights, args);
    }
    else {
        g_assert_not_reached();
    }

    g_object_set(args->gmodel, "title", _("Mean profile"), NULL);
    if (gwy_graph_model_get_n_curves(args->gmodel))
        gcmodel = gwy_graph_model_get_curve(args->gmodel, 0);
    else {
        gcmodel = gwy_graph_curve_model_new();
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description", _("Mean profile"),
                     NULL);
        gwy_graph_model_add_curve(args->gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

    if (mode == GOOD_PROF_SINGLE)
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, profile, 0, 0);
    else {
        xres = gwy_data_line_get_res(profile);
        dx = gwy_data_line_get_dx(profile);
        xoff = gwy_data_line_get_offset(profile);
        d = gwy_data_line_get_data(profile);
        w = gwy_data_line_get_data(weights);
        xy = g_new(GwyXY, xres);
        for (i = n = 0; i < xres; i++) {
            if (w[i] > 0.0) {
                xy[n].x = dx*i + xoff;
                xy[n].y = d[i];
                n++;
            }
        }
        gwy_graph_curve_model_set_data_interleaved(gcmodel, (gdouble*)xy, n);
        g_free(xy);

        if (variation) {
            for (i = 0; i < xres; i++)
                w[i] = (w[i] <= 0.0);
            gwy_data_line_correct_laplace(profile, weights);
        }
    }

    if (variation)
        *variation = gwy_data_line_get_variation(profile);

    g_object_unref(profile);
    GWY_OBJECT_UNREF(weights);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GoodProfMode mode = gwy_params_get_enum(params, PARAM_MODE);
    GwyAppDataId other_image = gwy_params_get_data_id(params, PARAM_OTHER_IMAGE);
    gboolean is_none = gwy_params_data_id_is_none(params, PARAM_OTHER_IMAGE);

    if (mode == GOOD_PROF_MULTIPLE
        && (is_none || !other_image_filter(gwy_app_data_browser_get(other_image.datano), other_image.id, args->field)))
        gwy_params_reset(params, PARAM_MODE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
