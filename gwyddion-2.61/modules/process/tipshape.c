/*
 *  @(#) $Id: tipshape.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2018-2021 David Necas (Yeti), Anna Charvatova Campbell
 *  E-mail: yeti@gwyddion.net, acampbellova@cmi.cz
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
#include <libgwyddion/gwyutils.h>
#include <libgwymodule/gwymodule-process.h>
#include <libprocess/stats.h>
#include <libprocess/gwyprocess.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_RESOLUTION,
    PARAM_RANGEMAX,
    PARAM_RANGEMIN,
    PARAM_CALC_UNC,
    PARAM_UNCX,
    PARAM_UNCY,
    PARAM_UNCZ,
    PARAM_TARGET_GRAPH,
    LABEL_RECOMMENDED,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyGraphModel *gmodel;
    /* Cached input image properties. */
    gdouble zedgemax;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             tipshape            (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static gboolean         tipshape_calc       (ModuleArgs *args,
                                             GwyDataLine *line,
                                             GwyDataLine *uline);
static gdouble          get_zedge_max       (GwyDataField *field);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates the tip area function."),
    "Anna Charvatova Campbell <acampbellova@cmi.cz>",
    "2.0",
    "Anna Charvatova Campbell",
    "2018",
};

GWY_MODULE_QUERY2(module_info, tipshape)

static gboolean
module_register(void)
{
    gwy_process_func_register("tipshape",
                              (GwyProcessFunc)&tipshape,
                              N_("/SPM M_odes/_Force and Indentation/_Area function..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Calculate tip area function."));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_int(paramdef, PARAM_RESOLUTION, "resolution", _("_Resolution"), 10, 1000, 100);
    gwy_param_def_add_double(paramdef, PARAM_RANGEMIN, "rangemin", _("Range minimum"), 0.0, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_RANGEMAX, "rangemax", _("Range maximum"), 0.0, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_boolean(paramdef, PARAM_CALC_UNC, "calc_unc", _("Calculate uncertainties"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_UNCX, "uncx", _("_X pixel size uncertainty"), 0.0, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_UNCY, "uncy", _("_Y pixel size uncertainty"), 0.0, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_UNCZ, "uncz", _("Uncertainty _z"), 0.0, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    return paramdef;
}

static void
tipshape(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GwyAppDataId target_graph_id;
    ModuleArgs args;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field, 0);
    g_return_if_fail(args.field);

    args.zedgemax = get_zedge_max(args.field);
    args.gmodel = gwy_graph_model_new();
    gwy_graph_model_set_units_from_data_field(args.gmodel, args.field, 0, 1, 2, 0);

    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    target_graph_id = gwy_params_get_data_id(args.params, PARAM_TARGET_GRAPH);
    gwy_app_add_graph_or_curves(args.gmodel, data, &target_graph_id, 2);

end:
    g_object_unref(args.gmodel);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    static const guint range_params[] = { PARAM_RANGEMIN, PARAM_RANGEMAX };
    static const guint xyunc_params[] = { PARAM_UNCX, PARAM_UNCY };
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *hbox, *graph;
    GwySIUnit *unit;
    GwySIValueFormat *vfxy;
    GwySIValueFormat *vfz;
    ModuleGUI gui;
    gdouble zmin, zmax, dx, dy;
    guint i;
    gint id;
    gchar *s;

    gwy_clear(&gui, 1);
    gui.args = args;

    dx = gwy_data_field_get_dx(args->field);
    dy = gwy_data_field_get_dy(args->field);
    gwy_data_field_get_min_max(args->field, &zmin, &zmax);

    unit = gwy_data_field_get_si_unit_xy(args->field);
    vfxy = gwy_si_unit_get_format_with_digits(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, 5.0*fmax(dx, dy), 5, NULL);
    vfz = gwy_data_field_get_value_format_z(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);

    gui.dialog = gwy_dialog_new(_("Tip Area Function"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Tip Area Function"));
    gwy_param_table_append_slider(table, PARAM_RESOLUTION);
    for (i = 0; i < G_N_ELEMENTS(range_params); i++) {
        id = range_params[i];
        gwy_param_table_append_slider(table, id);
        gwy_param_table_slider_restrict_range(table, id, 0.0, zmax - zmin);
        gwy_param_table_slider_set_factor(table, id, 1.0/vfz->magnitude);
        gwy_param_table_slider_set_digits(table, id, 3);
        gwy_param_table_set_unitstr(table, id, vfz->units);
    }
    s = g_strdup_printf("%s: %.*f%s%s",
                        _("Recommended maximum"),
                        vfz->precision, (zmax - args->zedgemax)/vfz->magnitude,
                        *vfz->units ? " " : "", vfz->units);
    gwy_param_table_append_message(table, LABEL_RECOMMENDED, s);
    g_free(s);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);

    gwy_param_table_append_header(table, -1, _("Uncertainties"));
    gwy_param_table_append_checkbox(table, PARAM_CALC_UNC);
    for (i = 0; i < G_N_ELEMENTS(range_params); i++) {
        id = xyunc_params[i];
        gwy_param_table_append_slider(table, id);
        gwy_param_table_slider_restrict_range(table, id, 0.0, 10.0*fmax(dx, dy));
        gwy_param_table_slider_set_factor(table, id, 1.0/vfxy->magnitude);
        gwy_param_table_slider_set_digits(table, id, 3);
        gwy_param_table_set_unitstr(table, id, vfxy->units);
    }
    gwy_param_table_append_slider(table, PARAM_UNCZ);
    gwy_param_table_slider_restrict_range(table, PARAM_UNCZ, 0.0, 0.5*(zmax - zmin));
    gwy_param_table_slider_set_factor(table, PARAM_UNCZ, 1.0/vfz->magnitude);
    gwy_param_table_slider_set_digits(table, PARAM_UNCZ, 3);
    gwy_param_table_set_unitstr(table, PARAM_UNCZ, vfz->units);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    graph = gwy_graph_new(args->gmodel);
    gtk_widget_set_size_request(graph, 4*PREVIEW_SMALL_SIZE/3, PREVIEW_SMALL_SIZE);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 4);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    gwy_si_unit_value_format_free(vfxy);
    gwy_si_unit_value_format_free(vfz);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table;
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_CALC_UNC) {
        gboolean calc_unc = gwy_params_get_boolean(params, PARAM_CALC_UNC);
        gwy_param_table_set_sensitive(table, PARAM_UNCX, calc_unc);
        gwy_param_table_set_sensitive(table, PARAM_UNCY, calc_unc);
        gwy_param_table_set_sensitive(table, PARAM_UNCZ, calc_unc);
    }

    if (id != PARAM_TARGET_GRAPH)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    GwyDataLine *areafunc, *uncline = NULL;
    GwyGraphModel *gmodel = args->gmodel;
    GwyGraphCurveModel *gcmodel;

    areafunc = gwy_data_line_new(1, 1.0, FALSE);
    if (gwy_params_get_boolean(args->params, PARAM_CALC_UNC))
        uncline = gwy_data_line_new(1, 1.0, FALSE);

    gwy_graph_model_remove_all_curves(args->gmodel);
    if (!tipshape_calc(args, areafunc, uncline))
        goto end;

    g_object_set(gmodel,
                 "title", _("Area function"),
                 "axis-label-bottom", "depth",
                 "axis-label-left", "area",
                 NULL);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "description", _("Area function"),
                 "color", gwy_graph_get_preset_color(0),
                 NULL);
    gwy_graph_curve_model_set_data_from_dataline(gcmodel, areafunc, 0, 0);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);

    if (uncline) {
        gcmodel = gwy_graph_curve_model_new();
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description", "uncertainty",
                     "color", gwy_graph_get_preset_color(1),
                     NULL);
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, uncline, 0, 0);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

end:
    g_object_unref(areafunc);
    GWY_OBJECT_UNREF(uncline);
}

static gboolean
tipshape_calc(ModuleArgs *args, GwyDataLine *line, GwyDataLine *uline)
{
    gdouble rangemin = gwy_params_get_double(args->params, PARAM_RANGEMIN);
    gdouble rangemax = gwy_params_get_double(args->params, PARAM_RANGEMAX);
    gint rangeres = gwy_params_get_int(args->params, PARAM_RESOLUTION);
    gdouble dx, dy, dz, zmax, dl, z;
    gdouble *p, *l;
    const gdouble *d;
    gint i, j, n;

    if (rangemin >= rangemax)
        return FALSE;

    n = gwy_data_field_get_xres(args->field) * gwy_data_field_get_yres(args->field);
    dx = gwy_data_field_get_dx(args->field);
    dy = gwy_data_field_get_dy(args->field);
    zmax = gwy_data_field_get_max(args->field);

    gwy_data_line_resample(line, rangeres, GWY_INTERPOLATION_NONE);
    gwy_data_line_clear(line);
    l = gwy_data_line_get_data(line);

    d = gwy_data_field_get_data_const(args->field);
    dz = (rangemax - rangemin)/rangeres;
    for (i = 0; i < n; i++) {
        j = (gint)floor((zmax - rangemin - d[i])/dz + 0.999999);
        if (j < rangeres)
            l[MAX(j, 0)]++;
    }
    gwy_data_line_cumulate(line);
    gwy_data_line_multiply(line, dx*dy);
    gwy_data_line_set_offset(line, rangemin);
    gwy_data_line_set_real(line, rangemax - rangemin);

    gwy_si_unit_assign(gwy_data_line_get_si_unit_x(line), gwy_data_field_get_si_unit_z(args->field));
    gwy_si_unit_power(gwy_data_field_get_si_unit_xy(args->field), 2, gwy_data_line_get_si_unit_y(line));

    if (uline) {
        gdouble uncx = gwy_params_get_double(args->params, PARAM_UNCX);
        gdouble uncy = gwy_params_get_double(args->params, PARAM_UNCY);
        gdouble uncz = gwy_params_get_double(args->params, PARAM_UNCZ);
        gdouble uSxy2, uSz, wp, wm;

        gwy_data_line_assign(uline, line);
        p = gwy_data_line_get_data(uline);

        uSxy2 = uncx*uncx/dx/dx + uncy*uncy/dy/dy;
        dl = gwy_data_line_get_dx(line);

        for (i = 0; i < rangeres; i++) {
            z = i*dl;
            wp = gwy_data_line_get_dval_real(line, z + uncz + dl/2, GWY_INTERPOLATION_LINEAR);
            wm = gwy_data_line_get_dval_real(line, z - uncz + dl/2, GWY_INTERPOLATION_LINEAR);

            uSz = (wp - wm)/GWY_SQRT3;
            uSz /= l[i];
            p[i] *= sqrt(uSz*uSz + uSxy2);
        }
    }

    return TRUE;
}

static gdouble
get_zedge_max(GwyDataField *field)
{
    gdouble ztop, zbot, zleft, zright;
    gint xres, yres;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    ztop = gwy_data_field_area_get_max(field, NULL, 0, 0, xres, 1);
    zleft = gwy_data_field_area_get_max(field, NULL, 0, 0, 1, yres);
    zright = gwy_data_field_area_get_max(field, NULL, xres-1, 0, 1, yres);
    zbot = gwy_data_field_area_get_max(field, NULL, 0, yres-1, xres, 1);

    return fmax(fmax(ztop, zbot), fmax(zleft, zright));
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble rangemin = gwy_params_get_double(params, PARAM_RANGEMIN);
    gdouble rangemax = gwy_params_get_double(params, PARAM_RANGEMAX);

    if (rangemin > args->zedgemax)
        gwy_params_set_double(params, PARAM_RANGEMIN, (rangemin = 0.0));
    if (rangemax <= rangemin || rangemax > args->zedgemax)
        gwy_params_set_double(params, PARAM_RANGEMAX, (rangemax = args->zedgemax));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
