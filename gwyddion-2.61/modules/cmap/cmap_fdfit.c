/*
 *  $Id: cmap_fdfit.c 24531 2021-11-15 20:09:29Z klapetek $
 *  Copyright (C) 2021 David Necas (Yeti).
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libgwyddion/gwyfdcurvepreset.h>
#include <libprocess/lawn.h>
#include <libprocess/stats.h>
#include <libprocess/correct.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-cmap.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE      = 360,
    RESPONSE_ESTIMATE = 100,
    RESPONSE_FIT      = 101,
};

enum {
    PARAM_RANGE_FROM,
    PARAM_RANGE_TO,
    PARAM_ABSCISSA,
    PARAM_ORDINATE,
    PARAM_SEGMENT,
    PARAM_ENABLE_SEGMENT,
    PARAM_XPOS,
    PARAM_YPOS,
    PARAM_FUNCTION,
    WIDGET_FIT_PARAMETERS,
    PARAM_INFO,
    PARAM_ESTIMATE,
    PARAM_ADHESION,
    PARAM_SEGMENT_ADHESION,
    PARAM_SEGMENT_BASELINE,
    PARAM_BASELINE_RANGE,

};

typedef struct {
    GwyParams *params;
    GwyLawn *lawn;
    GwyDataField *field;
    gint nsegments;
    gdouble *fit_parameters;
    gboolean *param_fixed;
    GwyDataField **result;
    GwyDataField *mask;
    gint adhesion_index;
    gdouble xmin;
    gdouble xmax;
} ModuleArgs;

typedef struct {
    GtkWidget *fix;          /* Unused for secondary */
    GtkWidget *name;
    GtkWidget *equals;
    GtkWidget *value;
    GtkWidget *value_unit;
    GtkWidget *pm;
    GtkWidget *error;
    GtkWidget *error_unit;
    gdouble magnitude;       /* Unused for secondary */
} FitParamControl;


typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyParamTable *table_fit;
    GwyParamTable *table_optimize;
    GtkWidget *fit_param_table;
    GwyContainer *data;
    GwySelection *selection;
    GwySelection *graph_selection;
    GwyGraphModel *gmodel;
    GArray *param_controls;
    GwyNLFitPreset *preset;
    GtkWidget *fitok;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             fdfit                   (GwyContainer *data,
                                                 GwyRunType runtype);
static void             execute                 (ModuleArgs *args,
                                                 GtkWindow *window);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static void             dialog_response         (ModuleGUI *gui,
                                                 gint response);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             param_fit_changed       (ModuleGUI *gui,
                                                 gint id);
static void             param_optimize_changed  (ModuleGUI *gui,
                                                 gint id);
static void             preview                 (gpointer user_data);
static void             set_selection           (ModuleGUI *gui);
static GtkWidget*       create_fit_table        (gpointer user_data);
static void             point_selection_changed (ModuleGUI *gui,
                                                 gint id,
                                                 GwySelection *selection);
static void             graph_selected          (GwySelection* selection, 
                                                 gint i, 
                                                 ModuleGUI *gui);
static void             extract_one_curve       (GwyLawn *lawn,
                                                 GwyGraphCurveModel *gcmodel,
                                                 gint col,
                                                 gint row,
                                                 GwyParams *params);
static void             estimate_one_curve      (GwyGraphCurveModel *gcmodel,
                                                 GwyParams *params,
                                                 GwyNLFitPreset *preset,
                                                 gdouble *fitparams,
                                                 gint nsegments,
                                                 const gint *segments);
static void             fit_one_curve           (GwyGraphCurveModel *gcmodel,
                                                 GwyParams *params,
                                                 GwyNLFitPreset *preset,
                                                 gdouble *fitparams,
                                                 gboolean *fix,
                                                 gdouble *error,
                                                 gint nsegments,
                                                 const gint *segments,
                                                 gint adhesion_index,
                                                 gboolean *fitok);
static void             do_fdfit                (const gdouble *xdata,
                                                 const gdouble *ydata,
                                                 gint ndata,
                                                 GwyNLFitPreset *preset,
                                                 const gint *segments,
                                                 gint segment,
                                                 gboolean segment_enabled,
                                                 gboolean adhesion,
                                                 gint adhesion_segment,
                                                 gint baseline_segment,
                                                 gdouble adhesion_range,
                                                 gint adhesion_index,
                                                 gdouble from,
                                                 gdouble to,
                                                 gdouble *retparam,
                                                 gboolean *fix,
                                                 gdouble *error,
                                                 gboolean *fitok);
static void             do_fdestimate           (const gdouble *xdata,
                                                 const gdouble *ydata,
                                                 gint ndata,
                                                 GwyNLFitPreset *preset,
                                                 const gint *segments,
                                                 gint segment,
                                                 gboolean segment_enabled,
                                                 gdouble from,
                                                 gdouble to,
                                                 gdouble *retparam);
static void             fit_param_table_resize  (ModuleGUI *gui);
static void             update_graph_model_props(GwyGraphModel *gmodel,
                                                 ModuleArgs *args);
static void             sanitise_params         (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Fit FD curves."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2021",
};

GWY_MODULE_QUERY2(module_info, cmap_fdfit)

static gboolean
module_register(void)
{
    gwy_curve_map_func_register("cmap_fdfit",
                                (GwyCurveMapFunc)&fdfit,
                                N_("/Fit _FD Curves..."),
                                NULL,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Fit FD curves by a function"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_curve_map_func_current());

    gwy_param_def_add_resource(paramdef, PARAM_FUNCTION, "function", _("_Function"),
                               gwy_fd_curve_presets(), "Hertz: spherical");
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ABSCISSA, "abscissa", _("Abscissa"));
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ORDINATE, "ordinate", _("Ordinate"));
    gwy_param_def_add_int(paramdef, PARAM_XPOS, "xpos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_YPOS, "ypos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_double(paramdef, PARAM_RANGE_FROM, "from", _("_From"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_RANGE_TO, "to", _("_To"), 0.0, 1.0, 1.0);
    gwy_param_def_add_lawn_segment(paramdef, PARAM_SEGMENT, "segment", NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_ENABLE_SEGMENT, "enable_segment", NULL, FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_ESTIMATE, "estimate", _("Run _estimate at each point"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_ADHESION, "adhesion", _("Get adhesion directly"), FALSE);
    gwy_param_def_add_lawn_segment(paramdef, PARAM_SEGMENT_ADHESION, "segment_adhesion", _("Adhesion data"));
    gwy_param_def_add_lawn_segment(paramdef, PARAM_SEGMENT_BASELINE, "segment_baseline", _("Baseline data"));
    gwy_param_def_add_double(paramdef, PARAM_BASELINE_RANGE, "baseline", _("Baseline _range"), 0.0, 0.5, 0.2);

    return paramdef;
}

static void
fdfit(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyLawn *lawn = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint id, newid, nparams, i;
    GwyNLFitPreset *preset;
    GwyDataField *newmask;
    const guchar *gradient;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_LAWN, &lawn,
                                     GWY_APP_LAWN_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_LAWN(lawn));

    args.lawn = lawn;
    args.nsegments = gwy_lawn_get_n_segments(lawn);
    args.params = gwy_params_new_from_settings(define_module_params());
    args.adhesion_index = -1;
    args.xmin = 0;
    args.xmax = 0;
    sanitise_params(&args);

    args.field = gwy_data_field_new(gwy_lawn_get_xres(lawn), gwy_lawn_get_yres(lawn),
                                    gwy_lawn_get_xreal(lawn), gwy_lawn_get_yreal(lawn), TRUE);
    gwy_data_field_set_xoffset(args.field, gwy_lawn_get_xoffset(lawn));
    gwy_data_field_set_yoffset(args.field, gwy_lawn_get_yoffset(lawn));
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(args.field), gwy_lawn_get_si_unit_xy(lawn));

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        execute(&args, gwy_app_find_window_for_curve_map(data, id));

        preset = gwy_inventory_get_item(gwy_fd_curve_presets(),
                                        gwy_params_get_string(args.params, PARAM_FUNCTION));
        nparams = gwy_nlfit_preset_get_nparams(preset);


        for (i = 0; i < nparams; i++) {
            newid = gwy_app_data_browser_add_data_field(args.result[i], data, TRUE);
            gwy_app_set_data_field_title(data, newid,
                                         gwy_nlfit_preset_get_param_name(preset, i));

            if (gwy_data_field_get_max(args.mask) > 0.0) {
                newmask = gwy_data_field_duplicate(args.mask);
                gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), newmask);
                g_object_unref(newmask);
            }

            if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), &gradient)) {
                gwy_container_set_const_string(data,
                                               gwy_app_get_data_palette_key_for_id(newid), gradient);
            }
        }
    }


end:
    //GWY_OBJECT_UNREF(args.result); //do this better?
    g_object_unref(args.field);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *graph, *dataview, *align, *area;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDataField *field;
    GwyDialogOutcome outcome;
    GwyGraphCurveModel *gcmodel;
    GwyVectorLayer *vlayer = NULL;
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    field = gwy_container_get_object(data, gwy_app_get_lawn_preview_key_for_id(id));
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Fit FD Curves"));
    dialog = GWY_DIALOG(gui.dialog);
    gtk_dialog_add_button(GTK_DIALOG(dialog), gwy_sgettext("verb|_Estimate single"), RESPONSE_ESTIMATE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), gwy_sgettext("verb|_Fit single"), RESPONSE_FIT);

    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 0);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gtk_container_add(GTK_CONTAINER(align), dataview);
    vlayer = g_object_new(g_type_from_name("GwyLayerPoint"), NULL);
    gwy_vector_layer_set_selection_key(vlayer, "/0/select/pointer");
    gwy_data_view_set_top_layer(GWY_DATA_VIEW(dataview), vlayer);
    gui.selection = gwy_vector_layer_ensure_selection(vlayer);
    set_selection(&gui);

    gui.gmodel = gwy_graph_model_new();
    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(0),
                 "description", g_strdup(_("data")),
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(1),
                 "description", g_strdup(_("fit")),
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);



    graph = gwy_graph_new(gui.gmodel);
    area = gwy_graph_get_area(GWY_GRAPH(graph));
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    gwy_graph_area_set_selection_editable(GWY_GRAPH_AREA(area), TRUE);
    gui.graph_selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);


    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 4);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_lawn_curve(table, PARAM_ABSCISSA, args->lawn);
    gwy_param_table_append_lawn_curve(table, PARAM_ORDINATE, args->lawn);
    if (args->nsegments) {
        gwy_param_table_append_lawn_segment(table, PARAM_SEGMENT, args->lawn);
        gwy_param_table_add_enabler(table, PARAM_ENABLE_SEGMENT, PARAM_SEGMENT);
    }
    gwy_param_table_append_slider(table, PARAM_RANGE_FROM);
    gwy_param_table_slider_set_factor(table, PARAM_RANGE_FROM, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_RANGE_FROM, "%");
    gwy_param_table_append_slider(table, PARAM_RANGE_TO);
    gwy_param_table_slider_set_factor(table, PARAM_RANGE_TO, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_RANGE_TO, "%");

    /* TODO: Replace with something more fitting. */
    /* TRANSLATORS: This is really outcome, like OK/failed, not the actual results. */
    gwy_param_table_append_info(table, PARAM_INFO, _("Fitting result"));

    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    table = gui.table_fit = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_FUNCTION);
    gwy_param_table_append_foreign(table, WIDGET_FIT_PARAMETERS, create_fit_table, &gui, NULL);

    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
   
    table = gui.table_optimize = gwy_param_table_new(args->params);
    gwy_param_table_append_checkbox(table, PARAM_ESTIMATE);
    gwy_param_table_append_checkbox(table, PARAM_ADHESION);
    if (args->nsegments) {
        gwy_param_table_append_lawn_segment(table, PARAM_SEGMENT_ADHESION, args->lawn);
        gwy_param_table_append_lawn_segment(table, PARAM_SEGMENT_BASELINE, args->lawn);
    }
    gwy_param_table_append_slider(table, PARAM_BASELINE_RANGE);
    gwy_param_table_slider_set_factor(table, PARAM_BASELINE_RANGE, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_BASELINE_RANGE, "%");
  
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
 
    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_fit, "param-changed", G_CALLBACK(param_fit_changed), &gui);
    g_signal_connect_swapped(gui.table_optimize, "param-changed", G_CALLBACK(param_optimize_changed), &gui);
    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(point_selection_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    g_signal_connect(gui.graph_selection, "changed", G_CALLBACK(graph_selected), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    gwy_param_table_param_changed(gui.table_fit, PARAM_FUNCTION);
    gwy_param_table_param_changed(gui.table_optimize, PARAM_ADHESION);
    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.gmodel);
    g_object_unref(gui.data);
    g_array_free(gui.param_controls, TRUE);

    return outcome;
}

static void
plot_result(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gdouble from = gwy_params_get_double(params, PARAM_RANGE_FROM);
    gdouble to = gwy_params_get_double(params, PARAM_RANGE_TO);
    gdouble sel[2];
    GwyGraphCurveModel *gc;

    gdouble xfrom, xto;
    gdouble *xfit, *yfit;
    gint i, nfit = 100;
    gboolean fres;

    gc = gwy_graph_model_get_curve(gui->gmodel, 0);
    gwy_graph_curve_model_get_x_range(gc, &xfrom, &xto);
    sel[0] = xfrom + from*(xto-xfrom);
    sel[1] = xfrom + to*(xto-xfrom);

    xfit = g_new(gdouble, nfit);
    yfit = g_new(gdouble, nfit);

    gwy_selection_set_data(gui->graph_selection, 1, sel);

    gc = gwy_graph_model_get_curve(gui->gmodel, 1);
    for (i = 0; i < nfit; i++) {
        xfit[i] = xfrom + (gdouble)i*(xto-xfrom)/(gdouble)nfit;
        yfit[i] = gwy_nlfit_preset_get_value(gui->preset, xfit[i], args->fit_parameters, &fres);
    }
    gwy_graph_curve_model_set_data(gc, xfit, yfit, nfit);

    g_free(xfit);
    g_free(yfit);
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gint col = gwy_params_get_int(params, PARAM_XPOS);
    gint row = gwy_params_get_int(params, PARAM_YPOS);
    GwyGraphCurveModel *gc;
    FitParamControl *cntrl;
    gboolean fitok;

    gint i, nparams;
    gchar buf[50];
    gdouble *error;

    if (response == RESPONSE_ESTIMATE) {
        //printf("estimate single\n");

        nparams = gwy_nlfit_preset_get_nparams(gui->preset);
        gc = gwy_graph_model_get_curve(gui->gmodel, 0);
        extract_one_curve(args->lawn, gc, col, row, params);
        estimate_one_curve(gc, params, gui->preset, args->fit_parameters, args->nsegments,
                           gwy_lawn_get_segments(args->lawn, col, row, NULL));

        for (i = 0; i < nparams; i++) {
           cntrl = &g_array_index(gui->param_controls, FitParamControl, i);
           g_snprintf(buf, sizeof(buf), "%0.6g", args->fit_parameters[i]);
           gtk_entry_set_text(GTK_ENTRY(cntrl->value), buf);
        }
        plot_result(gui);

        gwy_param_table_info_set_valuestr(gui->table, PARAM_INFO, _("N.A."));
    }
    else if (response == RESPONSE_FIT) {
        //printf("fit single\n");

        nparams = gwy_nlfit_preset_get_nparams(gui->preset);
        nparams = CLAMP(nparams, 0, 1024);  // prevent silly GCC warning

        error = g_new(gdouble, nparams);
        for (i = 0; i < nparams; i++) {
            error[i] = 0;
        }

        gc = gwy_graph_model_get_curve(gui->gmodel, 0);
        extract_one_curve(args->lawn, gc, col, row, params);
        fit_one_curve(gc, params, gui->preset, args->fit_parameters, args->param_fixed, error, args->nsegments,
                      gwy_lawn_get_segments(args->lawn, col, row, NULL), args->adhesion_index, &fitok);

        for (i = 0; i < nparams; i++) {
           cntrl = &g_array_index(gui->param_controls, FitParamControl, i);
           g_snprintf(buf, sizeof(buf), "%0.6g", args->fit_parameters[i]);
           gtk_entry_set_text(GTK_ENTRY(cntrl->value), buf);
           g_snprintf(buf, sizeof(buf), "%0.6g", error[i]);
           gtk_label_set_text(GTK_LABEL(cntrl->error), buf);
        }

        if (fitok)
            gwy_param_table_info_set_valuestr(gui->table, PARAM_INFO, _("OK"));
        else
            gwy_param_table_info_set_valuestr(gui->table, PARAM_INFO, _("failed"));

        plot_result(gui);
        g_free(error);
    }
    else {
        gwy_param_table_info_set_valuestr(gui->table, PARAM_INFO, _("N.A."));
    }
}

static void
graph_selected(GwySelection* selection, gint i, ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gdouble range[2];
    gdouble xfrom, xto, pfrom, pto;
    gboolean have_range = TRUE;

    g_return_if_fail(i <= 0);

    if (gwy_selection_get_data(selection, NULL) <= 0)
        have_range = FALSE;
    else {
        gwy_selection_get_object(selection, 0, range);
        if (range[0] == range[1])
            have_range = FALSE;
    }
    if (have_range) {
        xfrom = MIN(range[0], range[1]);
        xto = MAX(range[0], range[1]);
    }
    else {
        xfrom = args->xmin;
        xto = args->xmax;
    }

    pfrom = CLAMP((xfrom - args->xmin)/(args->xmax - args->xmin), 0.0, 1.0);
    pto = CLAMP((xto - args->xmin)/(args->xmax - args->xmin), 0.0, 1.0);

    gwy_param_table_set_double(gui->table, PARAM_RANGE_FROM, pfrom);
    gwy_param_table_set_double(gui->table, PARAM_RANGE_TO, pto);
}


static void
param_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id)
{
    gwy_param_table_info_set_valuestr(gui->table, PARAM_INFO, _("N.A."));
    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
param_fit_changed(ModuleGUI *gui, gint id)
{
    gint i, nparams;
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_FUNCTION) {
        //printf("function was changed\n");
        gui->preset = gwy_inventory_get_item(gwy_fd_curve_presets(), gwy_params_get_string(params, PARAM_FUNCTION));
        nparams = gwy_nlfit_preset_get_nparams(gui->preset);

        gui->args->fit_parameters = g_renew(gdouble, gui->args->fit_parameters, nparams);
        gui->args->param_fixed = g_renew(gboolean, gui->args->param_fixed, nparams);

        gui->args->adhesion_index = -1;
        for (i = 0; i < nparams; i++) {
            gui->args->param_fixed[i] = 0;
            
            if (strcmp(gwy_nlfit_preset_get_param_name(gui->preset, i), "Fad") == 0) {
                gui->args->adhesion_index = i;
            }
        }

        fit_param_table_resize(gui);
    }

    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
fix_changed(GtkToggleButton *button, ModuleGUI *gui)
{
    gboolean fixed = gtk_toggle_button_get_active(button);
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "id"));

    //printf("fix %d changed to %d\n", i, fixed);
    gui->args->param_fixed[i] = fixed;
}

static void
param_optimize_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    FitParamControl *cntrl;
    gboolean use_adhesion;

    if (id == PARAM_ADHESION && args->nsegments) {
        use_adhesion = gwy_params_get_boolean(params, PARAM_ADHESION);

        gwy_param_table_set_sensitive(gui->table_optimize, PARAM_SEGMENT_ADHESION, use_adhesion);
        gwy_param_table_set_sensitive(gui->table_optimize, PARAM_SEGMENT_BASELINE, use_adhesion);
        gwy_param_table_set_sensitive(gui->table_optimize, PARAM_BASELINE_RANGE, use_adhesion);

        if (gui->args->adhesion_index >= 0) {
           cntrl = &g_array_index(gui->param_controls, FitParamControl, gui->args->adhesion_index);
           gtk_widget_set_sensitive(cntrl->value, !use_adhesion);
           if (!use_adhesion) fix_changed(GTK_TOGGLE_BUTTON(cntrl->fix), gui);
        }
    }
}

static void
param_value_edited(GtkEntry *entry, ModuleGUI *gui)
{
    gdouble value = g_strtod(gtk_entry_get_text(entry), NULL);
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(entry), "id"));

    //printf("value %d changed to %g\n", i, value);
    gui->args->fit_parameters[i] = value;
}


static void
fit_param_table_resize(ModuleGUI *gui)
{
    GtkTable *table;
    guint i, row, old_nparams, nparams;

    old_nparams = gui->param_controls->len;
    nparams = gwy_nlfit_preset_get_nparams(gui->preset);
    gwy_debug("%u -> %u", old_nparams, nparams);
    for (i = old_nparams; i > nparams; i--) {
        FitParamControl *cntrl = &g_array_index(gui->param_controls, FitParamControl, i-1);
        gtk_widget_destroy(cntrl->fix);
        gtk_widget_destroy(cntrl->name);
        gtk_widget_destroy(cntrl->equals);
        gtk_widget_destroy(cntrl->value);
        gtk_widget_destroy(cntrl->value_unit);
        gtk_widget_destroy(cntrl->pm);
        gtk_widget_destroy(cntrl->error);
        gtk_widget_destroy(cntrl->error_unit);
        g_array_set_size(gui->param_controls, i-1);
    }

    table = GTK_TABLE(gui->fit_param_table);
    gtk_table_resize(table, 1+nparams, 8);
    row = old_nparams + 1;
    for (i = old_nparams; i < nparams; i++) {
        FitParamControl cntrl;

        cntrl.fix = gtk_check_button_new();
        gtk_table_attach(table, cntrl.fix, 0, 1, row, row+1, 0, 0, 0, 0);
        g_object_set_data(G_OBJECT(cntrl.fix), "id", GUINT_TO_POINTER(i));
        g_signal_connect(cntrl.fix, "toggled", G_CALLBACK(fix_changed), gui);

        cntrl.name = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.name), 1.0, 0.5);
        gtk_table_attach(table, cntrl.name, 1, 2, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.equals = gtk_label_new("=");
        gtk_table_attach(table, cntrl.equals, 2, 3, row, row+1, 0, 0, 0, 0);

        cntrl.value = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(cntrl.value), 12);
        gtk_table_attach(table, cntrl.value, 3, 4, row, row+1, GTK_FILL, 0, 0, 0);
        g_object_set_data(G_OBJECT(cntrl.value), "id", GUINT_TO_POINTER(i));
        g_signal_connect(cntrl.value, "changed", G_CALLBACK(param_value_edited), gui);
        gwy_widget_set_activate_on_unfocus(cntrl.value, TRUE);

        cntrl.value_unit = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.value_unit), 0.0, 0.5);
        gtk_table_attach(table, cntrl.value_unit, 4, 5, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.pm = gtk_label_new("±");
        gtk_table_attach(table, cntrl.pm, 5, 6, row, row+1, 0, 0, 0, 0);

        cntrl.error = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.error), 1.0, 0.5);
        gtk_table_attach(table, cntrl.error, 6, 7, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.error_unit = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.error_unit), 0.0, 0.5);
        gtk_table_attach(table, cntrl.error_unit, 7, 8, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.magnitude = 1.0;
        g_array_append_val(gui->param_controls, cntrl);
        row++;
    }

    for (i = 0; i < nparams; i++) {
        FitParamControl *cntrl = &g_array_index(gui->param_controls, FitParamControl, i);
        const gchar *name = gwy_nlfit_preset_get_param_name(gui->preset, i);

        gtk_label_set_markup(GTK_LABEL(cntrl->name), name);
    }

    gtk_widget_show_all(gui->fit_param_table);
}

static void
set_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gint col = gwy_params_get_int(args->params, PARAM_XPOS);
    gint row = gwy_params_get_int(args->params, PARAM_YPOS);
    gdouble xy[2];

    xy[0] = (col + 0.5)*gwy_lawn_get_dx(args->lawn);
    xy[1] = (row + 0.5)*gwy_lawn_get_dy(args->lawn);
    gwy_selection_set_object(gui->selection, 0, xy);
}

static void
point_selection_changed(ModuleGUI *gui, gint id, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GwyLawn *lawn = args->lawn;
    gint i, xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    gdouble xy[2];

    gwy_selection_get_object(selection, id, xy);
    i = GWY_ROUND(floor(xy[0]/gwy_lawn_get_dx(lawn)));
    gwy_params_set_int(args->params, PARAM_XPOS, CLAMP(i, 0, xres-1));
    i = GWY_ROUND(floor(xy[1]/gwy_lawn_get_dy(lawn)));
    gwy_params_set_int(args->params, PARAM_YPOS, CLAMP(i, 0, yres-1));

    gwy_param_table_param_changed(gui->table, PARAM_XPOS);
    gwy_param_table_param_changed(gui->table, PARAM_YPOS);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gint col = gwy_params_get_int(params, PARAM_XPOS);
    gint row = gwy_params_get_int(params, PARAM_YPOS);
    gdouble from = gwy_params_get_double(params, PARAM_RANGE_FROM);
    gdouble to = gwy_params_get_double(params, PARAM_RANGE_TO);
    gdouble sel[2];
    GwyGraphCurveModel *gc;
    gdouble xfrom, xto;

    gc = gwy_graph_model_get_curve(gui->gmodel, 0);
    extract_one_curve(args->lawn, gc, col, row, params);
    update_graph_model_props(gui->gmodel, args);
    gwy_graph_curve_model_get_x_range(gc, &xfrom, &xto);
    args->xmin = xfrom;
    args->xmax = xto;
    sel[0] = xfrom + from*(xto-xfrom);
    sel[1] = xfrom + to*(xto-xfrom);

    gc = gwy_graph_model_get_curve(gui->gmodel, 1);
    gwy_graph_curve_model_set_data(gc, NULL, NULL, 0);

    gwy_selection_set_data(gui->graph_selection, 1, sel);
}

static void
execute(ModuleArgs *args, GtkWindow *window)
{
    GwyParams *params = args->params;
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    gdouble from = gwy_params_get_double(params, PARAM_RANGE_FROM);
    gdouble to = gwy_params_get_double(params, PARAM_RANGE_TO);
    gboolean segment_enabled = args->nsegments ? gwy_params_get_boolean(params, PARAM_ENABLE_SEGMENT) : FALSE;
    gint segment = segment_enabled ? gwy_params_get_int(params, PARAM_SEGMENT) : -1;
    gint adhesion = gwy_params_get_boolean(params, PARAM_ADHESION);
    gint adhesion_segment = adhesion ? gwy_params_get_int(params, PARAM_SEGMENT_ADHESION) : -1;
    gint baseline_segment = adhesion ? gwy_params_get_int(params, PARAM_SEGMENT_BASELINE) : -1;
    gdouble baseline_range = gwy_params_get_double(params, PARAM_BASELINE_RANGE);
    gboolean estimate = gwy_params_get_boolean(params, PARAM_ESTIMATE);

    GwyNLFitPreset *preset = gwy_inventory_get_item(gwy_fd_curve_presets(),
                                                    gwy_params_get_string(params, PARAM_FUNCTION));

    gint nparams = gwy_nlfit_preset_get_nparams(preset);
    GwyLawn *lawn = args->lawn;
    gdouble **rdata, *mdata;
    gdouble *inits;
    gint xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    gboolean fitok;

    gint nsegments = gwy_lawn_get_n_segments(lawn);
    gint *segments = g_new(gint, 2*nsegments);

    const gdouble *cd, *cdx, *cdy;
    gint ndata, i, j, k, col, row;

    rdata = g_new(gdouble*, nparams);
    inits = g_new(gdouble, nparams);
    gwy_assign(inits, args->fit_parameters, nparams);

    args->result = g_new(GwyDataField *, nparams);
    for (i = 0; i < nparams; i++) { //FIXME: set the units!
        args->result[i] = gwy_data_field_new(gwy_lawn_get_xres(lawn), gwy_lawn_get_yres(lawn),
                                             gwy_lawn_get_xreal(lawn), gwy_lawn_get_yreal(lawn),
                                             TRUE);
        gwy_data_field_set_xoffset(args->result[i], gwy_lawn_get_xoffset(lawn));
        gwy_data_field_set_yoffset(args->result[i], gwy_lawn_get_yoffset(lawn));
        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(args->result[i]),
                           gwy_lawn_get_si_unit_xy(lawn));

        rdata[i] = gwy_data_field_get_data(args->result[i]);
    }
    args->mask = gwy_data_field_new(gwy_lawn_get_xres(lawn), gwy_lawn_get_yres(lawn),
                                    gwy_lawn_get_xreal(lawn), gwy_lawn_get_yreal(lawn),
                                    TRUE);
    mdata = gwy_data_field_get_data(args->mask);

    gwy_app_wait_start(window, _("Fitting..."));

    for (k = 0; k < xres*yres; k++) {
        if (!gwy_app_wait_set_fraction((gdouble)k/(xres*yres)))
            break;//what would happen next?

        col = k % xres;
        row = k/xres;
        gwy_assign(segments, gwy_lawn_get_segments(lawn, col, row, NULL), 2*nsegments);

        cd = gwy_lawn_get_curves_data_const(lawn, col, row, &ndata);
        cdx = cd + ndata*abscissa;
        cdy = cd + ndata*ordinate;

        if (estimate) 
            do_fdestimate(cdx, cdy, ndata, preset,
                          gwy_lawn_get_segments(lawn, col, row, NULL),
                          segment, segment_enabled,
                          from, to, inits);

        do_fdfit(cdx, cdy, ndata, preset,
                 gwy_lawn_get_segments(lawn, col, row, NULL),
                 segment, segment_enabled,
                 adhesion, adhesion_segment, baseline_segment, baseline_range,
                 args->adhesion_index,
                 from, to, inits,
                 args->param_fixed,
                 NULL,
                 &fitok);

        for (j = 0; j < nparams; j++)
             rdata[j][k] = inits[j];

        if (!fitok) mdata[k] = 1.0;
    }


    for (i = 0; i < nparams; i++) {
        if (gwy_data_field_get_max(args->mask) > 0.0)
            gwy_data_field_laplace_solve(args->result[i], args->mask, -1, 1.0);
    }

    gwy_app_wait_finish();

    g_free(inits);
    g_free(segments);
}

static void
extract_one_curve(GwyLawn *lawn, GwyGraphCurveModel *gcmodel,
                  gint col, gint row,
                  GwyParams *params)
{
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    const gdouble *xdata, *ydata;
    gint ndata;

    ydata = gwy_lawn_get_curve_data_const(lawn, col, row, ordinate, &ndata);
    xdata = gwy_lawn_get_curve_data_const(lawn, col, row, abscissa, NULL);
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ndata);
}

static void
do_fdestimate(const gdouble *xdata, const gdouble *ydata,
              gint ndata, GwyNLFitPreset *preset,
              const gint *segments,
              gint segment, gboolean segment_enabled,
              gdouble from, gdouble to,
              gdouble *fitparams)
{
    gboolean fres;
    gint i, j, n;
    gdouble startval, endval, xmin, xmax, ymin, ymax;
    gdouble *xf, *yf;
    gint segment_from, segment_to;

    //get the total range and fit range
    xmin = G_MAXDOUBLE;
    xmax = -G_MAXDOUBLE;
    ymin = G_MAXDOUBLE;
    ymax = -G_MAXDOUBLE;
    for (i = 0; i < ndata; i++) {
       if (xdata[i] < xmin)
           xmin = xdata[i];
       if (xdata[i] > xmax)
           xmax = xdata[i];
       if (ydata[i] < ymin)
           ymin = ydata[i];
       if (ydata[i] > ymax)
           ymax = ydata[i];
    }
    startval = xmin + from*(xmax-xmin);
    endval = xmin + to*(xmax-xmin);

    if (segment_enabled) {
        segment_from = segments[2*segment];
        segment_to = segments[2*segment + 1];
    } else {
        segment_from = 0;
        segment_to = G_MAXINT;
    }

    //determine number of points to fit,  FIXME: add segment here
    n = 0;
    for (i = 0; i < ndata; i++) {
        if (xdata[i] >= startval && xdata[i] < endval
            && i >= segment_from && i < segment_to)
            n++;
    }

    //fill the data to fit
    xf = g_new(gdouble, n);
    yf = g_new(gdouble, n);
    j = 0;
    for (i = 0; i < ndata; i++) {
        if (xdata[i] >= startval && xdata[i] < endval
            && i >= segment_from && i < segment_to) {
            xf[j] = xdata[i];
            yf[j] = ydata[i];
            j++;
        }
    }

    gwy_nlfit_preset_guess(preset,
                           n,
                           xf,
                           yf,
                           fitparams,
                           &fres);

    g_free(xf);
    g_free(yf);
}

static void
do_fdfit(const gdouble *xdata, const gdouble *ydata,
         gint ndata, GwyNLFitPreset *preset,
         const gint *segments,
         gint segment, gboolean segment_enabled,
         gboolean use_adhesion, gint adhesion_segment, gint baseline_segment, gdouble baseline_range,
         gint adhesion_index,
         gdouble from, gdouble to,
         gdouble *fitparams, gboolean *fix, gdouble *error,
         gboolean *fitok)
{
    gint i, j, n;
    gdouble startval, endval, xmin, xmax, ymin, ymax;
    gdouble *xf, *yf;
    GwyNLFitter *fitter;
    gint segment_from, segment_to;
    gint baseline_from, baseline_to, adhesion_from, adhesion_to, nbaseline;
    gdouble bfrom, bto;
    const gdouble *yadata, *xbdata, *ybdata;
    gdouble adhesion, baseline;

    //get the total range and fit range
    xmin = G_MAXDOUBLE;
    xmax = -G_MAXDOUBLE;
    ymin = G_MAXDOUBLE;
    ymax = -G_MAXDOUBLE;
    for (i = 0; i < ndata; i++) {
       if (xdata[i] < xmin)
           xmin = xdata[i];
       if (xdata[i] > xmax)
           xmax = xdata[i];
       if (ydata[i] < ymin)
           ymin = ydata[i];
       if (ydata[i] > ymax)
           ymax = ydata[i];
    }
    startval = xmin + from*(xmax-xmin);
    endval = xmin + to*(xmax-xmin);

    if (segment_enabled) {
        segment_from = segments[2*segment];
        segment_to = segments[2*segment + 1];
    } else {
        segment_from = 0;
        segment_to = G_MAXINT;
    }

    if (use_adhesion) {
       //get baseline curve range
       baseline_from = segments[2*baseline_segment];
       baseline_to = segments[2*baseline_segment + 1];
       adhesion_from = segments[2*adhesion_segment];
       adhesion_to = segments[2*adhesion_segment + 1];

       xbdata = xdata + baseline_from;
       ybdata = ydata + baseline_from;
       yadata = ydata + adhesion_from;

       bfrom = G_MAXDOUBLE;
       bto = -G_MAXDOUBLE;
       for (i = 0; i < (baseline_to - baseline_from); i++) {
           if (xbdata[i] < bfrom)
               bfrom = xbdata[i];
           if (xbdata[i] > bto)
               bto = xbdata[i];
       }

       //fit baseline - average value on the curve flat part
       baseline = 0;
       nbaseline = 0;
       for (i = 0; i < (baseline_to - baseline_from); i++) {
           if (xbdata[i] > (bto - baseline_range*(bto - bfrom))) {
               baseline += ybdata[i];
               nbaseline++;
           }
       }
       if (nbaseline>0) baseline /= nbaseline;
       else {
           baseline = ybdata[baseline_to-baseline_from-1];
       }

       //get adhesion as minimum on the curve
       adhesion = G_MAXDOUBLE;
       for (i = 0; i < (adhesion_to - adhesion_from); i++) {
           if (adhesion > yadata[i]) { 
               adhesion = yadata[i]; 
           }
       }       
       adhesion -= baseline;

       if (adhesion_index >=0) {
           fitparams[adhesion_index] = adhesion;
           fix[adhesion_index] = 1;
       }

    }

    //determine number of points to fit
    n = 0;
    for (i = 0; i < ndata; i++) {
        if (xdata[i] >= startval && xdata[i] < endval
            && i >= segment_from && i < segment_to)
                n++;
    }

    //fill the data to fit
    xf = g_new(gdouble, n);
    yf = g_new(gdouble, n);
    j = 0;
    for (i = 0; i < ndata; i++) {
        if (xdata[i] >= startval && xdata[i] < endval
            && i >= segment_from && i < segment_to) {
            xf[j] = xdata[i];
            yf[j] = ydata[i];
            j++;
        }
    }

    fitter = gwy_nlfit_preset_fit(preset,
                                  NULL,
                                  n,
                                  xf,
                                  yf,
                                  fitparams,
                                  error,
                                  fix);

    *fitok = gwy_math_nlfit_succeeded(fitter);

    //if (error)
    //    printf("err: %g %g %g %g\n", error[0], error[1], error[2], error[3]);

    g_free(xf);
    g_free(yf);
    gwy_math_nlfit_free(fitter);
}

static void
fit_one_curve(GwyGraphCurveModel *gcmodel, GwyParams *params, GwyNLFitPreset *preset,
              gdouble *fitparams, gboolean *fix, gdouble *error, gint nsegments, const gint *segments,
              gint adhesion_index,
              gboolean *fitok)
{
    gdouble from = gwy_params_get_double(params, PARAM_RANGE_FROM);
    gdouble to = gwy_params_get_double(params, PARAM_RANGE_TO);
    gboolean segment_enabled = nsegments ? gwy_params_get_boolean(params, PARAM_ENABLE_SEGMENT) : FALSE;
    gint segment = segment_enabled ? gwy_params_get_int(params, PARAM_SEGMENT) : -1;
    gint adhesion = gwy_params_get_boolean(params, PARAM_ADHESION);
    gint adhesion_segment = adhesion ? gwy_params_get_int(params, PARAM_SEGMENT_ADHESION) : -1;
    gint baseline_segment = adhesion ? gwy_params_get_int(params, PARAM_SEGMENT_BASELINE) : -1;
    gdouble baseline_range = gwy_params_get_double(params, PARAM_BASELINE_RANGE);

    const gdouble *xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    const gdouble *ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    gint ndata = gwy_graph_curve_model_get_ndata(gcmodel);

    do_fdfit(xdata, ydata, ndata, preset, segments, segment, segment_enabled, 
             adhesion, adhesion_segment, baseline_segment, baseline_range, adhesion_index,
             from, to, 
             fitparams, fix, error, fitok);
}


static void
estimate_one_curve(GwyGraphCurveModel *gcmodel, GwyParams *params, GwyNLFitPreset *preset,
                   gdouble *fitparams, gint nsegments, const gint *segments)
{
    gdouble from = gwy_params_get_double(params, PARAM_RANGE_FROM);
    gdouble to = gwy_params_get_double(params, PARAM_RANGE_TO);
    gboolean segment_enabled = nsegments ? gwy_params_get_boolean(params, PARAM_ENABLE_SEGMENT) : FALSE;
    gint segment = segment_enabled ? gwy_params_get_int(params, PARAM_SEGMENT) : -1;


    const gdouble *xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    const gdouble *ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    gint ndata = gwy_graph_curve_model_get_ndata(gcmodel);

    do_fdestimate(xdata, ydata, ndata, preset, segments, segment, segment_enabled, from, to, fitparams);
}

static void
update_graph_model_props(GwyGraphModel *gmodel, ModuleArgs *args)
{
    GwyLawn *lawn = args->lawn;
    GwyParams *params = args->params;
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    GwySIUnit *xunit, *yunit;
    const gchar *xlabel, *ylabel;

    xunit = gwy_lawn_get_si_unit_curve(lawn, abscissa);
    xlabel = gwy_lawn_get_curve_label(lawn, abscissa);
    yunit = gwy_lawn_get_si_unit_curve(lawn, ordinate);
    ylabel = gwy_lawn_get_curve_label(lawn, ordinate);

    g_object_set(gmodel,
                 "si-unit-x", xunit,
                 "si-unit-y", yunit,
                 "axis-label-bottom", xlabel ? xlabel : _("Untitled"),
                 "axis-label-left", ylabel ? ylabel : _("Untitled"),
                 NULL);
}

static GtkWidget*
create_fit_table(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    gui->fit_param_table = gtk_table_new(10, 10, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(gui->fit_param_table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(gui->fit_param_table), 8);

    gtk_table_attach(GTK_TABLE(gui->fit_param_table), gwy_label_new_header(_("Fix")),
                     0, 1, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(GTK_TABLE(gui->fit_param_table), gwy_label_new_header(_("Parameter")),
                     1, 5, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(GTK_TABLE(gui->fit_param_table), gwy_label_new_header(_("Error")),
                     6, 8, 0, 1, GTK_FILL, 0, 0, 0);

    gui->param_controls = g_array_new(FALSE, FALSE, sizeof(FitParamControl));

    return gui->fit_param_table;
}


static void
sanitise_one_param(GwyParams *params, gint id, gint min, gint max, gint defval)
{
    gint v;

    v = gwy_params_get_int(params, id);
    if (v >= min && v <= max) {
        gwy_debug("param #%d is %d, i.e. within range [%d..%d]", id, v, min, max);
        return;
    }
    gwy_debug("param #%d is %d, setting it to the default %d", id, v, defval);
    gwy_params_set_int(params, id, defval);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyLawn *lawn = args->lawn;

    sanitise_one_param(params, PARAM_XPOS, 0, gwy_lawn_get_xres(lawn)-1, gwy_lawn_get_xres(lawn)/2);
    sanitise_one_param(params, PARAM_YPOS, 0, gwy_lawn_get_yres(lawn)-1, gwy_lawn_get_yres(lawn)/2);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
