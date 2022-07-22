/*
 *  $Id: cmap_cutter.c 24458 2021-11-03 13:59:05Z yeti-dn $
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
#include <libgwyddion/gwynlfit.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/lawn.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-cmap.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

/* Lower symmetric part indexing */
/* i MUST be greater or equal than j */
#define SLi(a, i, j) a[(i)*((i) + 1)/2 + (j)]

enum {
    PREVIEW_SIZE = 360,
};

typedef enum {
    OUTPUT_MARK    = 0,
    OUTPUT_EXTRACT = 1,
} CutterOutput;

typedef enum {
    MODE_ZCUT_AR  = 0,
    MODE_ZCUT_AHR = 1,
} CutterMode;

typedef enum {
    ZCUT_APPROACH = 0,
    ZCUT_CONTACT  = 1,
    ZCUT_RETRACT  = 2,
} CutterZCutSegment;

enum {
    PARAM_OUTPUT,
    PARAM_MODE,
    PARAM_CURVE,
    PARAM_KEEP_CURVES,
    PARAM_XPOS,
    PARAM_YPOS,

    PARAM_ZCUT_SEGMENTS,
};

typedef struct {
    gint from;
    gint end;   /* Exclusive. */
} CutInterval;

typedef struct {
    gdouble one;
    gdouble x;
    gdouble xx;
    gdouble y;
    gdouble xy;
} LinearSum;

typedef struct {
    GwyParams *params;
    GwyLawn *lawn;
    GwyLawn **result;
    const GwyEnum *segnames;
    guint nresults;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyParamTable *table_mode;
    GSList *keep_curves;
    GtkWidget *keep_curves_label;
    GwyContainer *data;
    GwySelection *selection;
    GwyGraphModel *gmodel;
    GwyEnum *curve_enum;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             cmap_cutter             (GwyContainer *data,
                                                 GwyRunType runtype);
static gboolean         execute                 (ModuleArgs *args,
                                                 GtkWindow *wait_window);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static GtkWidget*       create_keep_curves      (ModuleGUI *gui);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             keep_curves_changed     (GtkRadioButton *button,
                                                 ModuleGUI *gui);
static void             preview                 (gpointer user_data);
static void             set_selection           (ModuleGUI *gui);
static void             point_selection_changed (ModuleGUI *gui,
                                                 gint id,
                                                 GwySelection *selection);
static void             extract_curve           (ModuleGUI *gui);
static void             update_graph_model_props(ModuleGUI *gui);
static gboolean         cutter_zcut_ahr         (const gdouble *data,
                                                 gint ndata,
                                                 CutInterval *cuts,
                                                 GArray *sums);
static gboolean         cutter_zcut_ar          (const gdouble *data,
                                                 gint ndata,
                                                 CutInterval *cuts);
static void             sanitise_params         (ModuleArgs *args);

static const GwyEnum zcut_segments_ahr[] = {
    { N_("Approach"), (1 << ZCUT_APPROACH), },
    { N_("Contact"),  (1 << ZCUT_CONTACT),  },
    { N_("Retract"),  (1 << ZCUT_RETRACT),  },
};

static const GwyEnum zcut_segments_ar[] = {
    { N_("Approach"), (1 << ZCUT_APPROACH), },
    { N_("Retract"),  (1 << ZCUT_RETRACT),  },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Cuts a curve map to segments."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, cmap_cutter)

static gboolean
module_register(void)
{
    gwy_curve_map_func_register("cmap_cutter",
                                (GwyCurveMapFunc)&cmap_cutter,
                                N_("/_Cut to Segments..."),
                                NULL,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Cut curves to segments"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum outputs[] = {
        { N_("Mark"),    OUTPUT_MARK,    },
        { N_("Extract"), OUTPUT_EXTRACT, },
    };
    static const GwyEnum modes[] = {
        { N_("Approach/Retract"),      MODE_ZCUT_AR  },
        { N_("Approach/Hold/Retract"), MODE_ZCUT_AHR },
    };

    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_curve_map_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT, "output", _("Output _type"),
                              outputs, G_N_ELEMENTS(outputs), OUTPUT_MARK);
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", _("Mode"), modes, G_N_ELEMENTS(modes), MODE_ZCUT_AR);
    gwy_param_def_add_lawn_curve(paramdef, PARAM_CURVE, "curve", NULL);
    gwy_param_def_add_int(paramdef, PARAM_KEEP_CURVES, "keep_curves", _("Keep curves"), 0, G_MAXINT, 1);
    gwy_param_def_add_int(paramdef, PARAM_XPOS, "xpos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_YPOS, "ypos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_gwyflags(paramdef, PARAM_ZCUT_SEGMENTS, "zcut_segments", _("Extract segments"),
                               zcut_segments_ahr, G_N_ELEMENTS(zcut_segments_ahr),
                               (1 << ZCUT_APPROACH) | (1 << ZCUT_RETRACT));
    return paramdef;
}

static void
cmap_cutter(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyLawn *lawn = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint i, oldid, newid;
    CutterOutput output;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_LAWN, &lawn,
                                     GWY_APP_LAWN_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_LAWN(lawn));
    args.lawn = lawn;
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (execute(&args, gwy_app_find_window_for_curve_map(data, oldid))) {
        output = gwy_params_get_enum(args.params, PARAM_OUTPUT);
        if (output == OUTPUT_EXTRACT) {
            for (i = 0; i < args.nresults; i++) {
                if (!(lawn = args.result[i]))
                    continue;

                newid = gwy_app_data_browser_add_lawn(lawn, NULL, data, TRUE);
                gwy_container_set_const_string(data, gwy_app_get_lawn_title_key_for_id(newid), args.segnames[i].name);
            }
            for (i = 0; i < args.nresults; i++)
                GWY_OBJECT_UNREF(args.result[i]);
            g_free(args.result);
        }
        else if (output == OUTPUT_MARK) {
            gwy_app_curve_map_log_add_curve_map(data, oldid, oldid);
        }
    }

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *graph, *dataview, *align, *kclist;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDataField *field;
    GwyDialogOutcome outcome;
    GwyVectorLayer *vlayer = NULL;
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gui.gmodel = gwy_graph_model_new();
    field = gwy_container_get_object(data, gwy_app_get_lawn_preview_key_for_id(id));
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Cut to Segments"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

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

    graph = gwy_graph_new(gui.gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 4);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_MODE);
    gwy_param_table_append_lawn_curve(table, PARAM_CURVE, args->lawn);
    gwy_param_table_append_combo(table, PARAM_OUTPUT);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    table = gui.table_mode = gwy_param_table_new(args->params);
    gwy_param_table_append_checkboxes(table, PARAM_ZCUT_SEGMENTS);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    kclist = create_keep_curves(&gui);
    gtk_box_pack_start(GTK_BOX(hbox), kclist, FALSE, FALSE, 0);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_mode, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(point_selection_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.gmodel);
    g_object_unref(gui.data);
    g_free(gui.curve_enum);

    return outcome;
}

static GtkWidget*
create_keep_curves(ModuleGUI *gui)
{
    GtkTable *table;
    GwyLawn *lawn = gui->args->lawn;
    gint keep_curves = gwy_params_get_int(gui->args->params, PARAM_KEEP_CURVES);
    const gchar *name;
    gint ncurves, i;

    ncurves = gwy_lawn_get_n_curves(lawn);
    gui->curve_enum = g_new(GwyEnum, ncurves);
    for (i = 0; i < ncurves; i++) {
        name = gwy_lawn_get_curve_label(lawn, i);
        gui->curve_enum[i].value = (1 << i);
        gui->curve_enum[i].name = name ? name : _("Untitled");
    }
    gui->keep_curves = gwy_check_boxes_create(gui->curve_enum, ncurves, G_CALLBACK(keep_curves_changed), gui,
                                              keep_curves);

    table = GTK_TABLE(gtk_table_new(ncurves+1, 2, FALSE));
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gui->keep_curves_label = gtk_label_new(_("Keep curves:"));
    gtk_misc_set_alignment(GTK_MISC(gui->keep_curves_label), 0.0, 0.5);
    gtk_table_attach(table, gui->keep_curves_label, 0, 2, 0, 1, 0, 0, 0, 0);
    gwy_check_boxes_attach_to_table(gui->keep_curves, table, 2, 1);

    return GTK_WIDGET(table);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_OUTPUT) {
        CutterOutput output = gwy_params_get_enum(params, PARAM_OUTPUT);
        gboolean extract_sens = (output == OUTPUT_EXTRACT);

        gwy_param_table_set_sensitive(gui->table_mode, PARAM_ZCUT_SEGMENTS, extract_sens);
        gwy_check_boxes_set_sensitive(gui->keep_curves, extract_sens);
        gtk_widget_set_sensitive(gui->keep_curves_label, extract_sens);
        if (output == OUTPUT_EXTRACT)
            gwy_param_table_set_label(gui->table, PARAM_CURVE, _("Z curve"));
    }
    if (id < 0 || id == PARAM_MODE) {
        CutterMode mode = gwy_params_get_enum(params, PARAM_MODE);
        gwy_param_table_checkboxes_set_sensitive(gui->table_mode, PARAM_ZCUT_SEGMENTS,
                                                 (1 << ZCUT_CONTACT), (mode == MODE_ZCUT_AHR));
    }
    if (id < 0 || id == PARAM_CURVE)
        update_graph_model_props(gui);
    if (id == PARAM_MODE || id == PARAM_CURVE || id == PARAM_XPOS || id == PARAM_YPOS)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
keep_curves_changed(G_GNUC_UNUSED GtkRadioButton *button, ModuleGUI *gui)
{
    gwy_params_set_int(gui->args->params, PARAM_KEEP_CURVES, gwy_check_boxes_get_selected(gui->keep_curves));
    gwy_param_table_param_changed(gui->table, PARAM_KEEP_CURVES);
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

    extract_curve(gui);
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyLawn *lawn = args->lawn, *result;
    GwyParams *params = args->params;
    gint curve = gwy_params_get_int(params, PARAM_CURVE);
    CutterMode mode = gwy_params_get_enum(params, PARAM_MODE);
    CutterOutput output = gwy_params_get_enum(params, PARAM_OUTPUT);
    guint keep_curves = gwy_params_get_int(params, PARAM_KEEP_CURVES);
    gint xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    gint ncurves = gwy_lawn_get_n_curves(lawn);
    GArray *rdata;
    const gdouble *adata;
    CutInterval *cuts;
    gboolean is_first = TRUE, cancelled = FALSE;
    gint i, j, k, m, n, ndata, nc, nsegments, from, end;
    guint segflag;

    /* Find segment cut points. */
    gwy_app_wait_start(wait_window, _("Fitting in progress..."));

    if (mode == MODE_ZCUT_AHR) {
        args->nresults = nsegments = G_N_ELEMENTS(zcut_segments_ahr);
        args->segnames = zcut_segments_ahr;
        segflag = gwy_params_get_flags(params, PARAM_ZCUT_SEGMENTS);
    }
    else if (mode == MODE_ZCUT_AR) {
        args->nresults = nsegments = G_N_ELEMENTS(zcut_segments_ar);
        args->segnames = zcut_segments_ar;
        segflag = gwy_params_get_flags(params, PARAM_ZCUT_SEGMENTS);
    }
    else {
        g_return_val_if_reached(FALSE);
    }

    nc = 0;
    for (m = 0; m < ncurves; m++) {
        if (keep_curves & (1 << m))
            nc++;
    }
    if (!nc) {
        args->result = NULL;
        return FALSE;
    }

    result = gwy_lawn_new(xres, yres, gwy_lawn_get_xreal(lawn), gwy_lawn_get_yreal(lawn), nc, 0);
    gwy_lawn_set_xoffset(result, gwy_lawn_get_xoffset(lawn));
    gwy_lawn_set_yoffset(result, gwy_lawn_get_yoffset(lawn));
    gwy_si_unit_assign(gwy_lawn_get_si_unit_xy(result), gwy_lawn_get_si_unit_xy(lawn));
    for (m = n = 0; m < ncurves; m++) {
        if (keep_curves & (1 << m)) {
            gwy_si_unit_assign(gwy_lawn_get_si_unit_curve(result, n), gwy_lawn_get_si_unit_curve(lawn, m));
            gwy_lawn_set_curve_label(result, n, gwy_lawn_get_curve_label(lawn, m));
            n++;
        }
    }

    cuts = g_new0(CutInterval, xres*yres*nsegments);
    rdata = g_array_new(FALSE, FALSE, sizeof(gdouble));

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(lawn,xres,yres,nsegments,curve,cuts,mode,cancelled)
#endif
    {
        GArray *sums = g_array_new(FALSE, FALSE, sizeof(LinearSum));
        guint kfrom = gwy_omp_chunk_start(xres*yres);
        guint kto = gwy_omp_chunk_end(xres*yres);
        const gdouble *tadata;
        guint tk, col, row;
        gint nd;

        for (tk = kfrom; tk < kto; tk++) {
            col = tk % xres;
            row = tk/xres;
            tadata = gwy_lawn_get_curve_data_const(lawn, col, row, curve, &nd);
            if (mode == MODE_ZCUT_AHR) {
                if (!cutter_zcut_ahr(tadata, nd, cuts + tk*nsegments, sums))
                    continue;
            }
            else if (mode == MODE_ZCUT_AR) {
                if (!cutter_zcut_ar(tadata, nd, cuts + tk*nsegments))
                    continue;
            }

            if (tk % 1000)
                continue;
            if (gwy_omp_set_fraction_check_cancel(gwy_app_wait_set_fraction, tk, kfrom, kto, &cancelled))
                break;
        }

        g_array_free(sums, TRUE);
    }
    if (cancelled)
        goto end;

    /* Create new lawns or just re-mark the current one, depending on the mode. */
    if (output == OUTPUT_EXTRACT) {
        args->result = g_new0(GwyLawn*, nsegments);
        for (k = 0; k < nsegments; k++) {
            if (segflag & args->segnames[k].value) {
                if (is_first) {
                    args->result[k] = result;
                    is_first = FALSE;
                }
                else
                    args->result[k] = gwy_lawn_new_alike(result);
            }
        }

        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++) {
                adata = gwy_lawn_get_curve_data_const(lawn, j, i, curve, &ndata);
                for (k = 0; k < nsegments; k++) {
                    if (!(result = args->result[k]))
                        continue;
                    from = CLAMP(cuts[k].from, 0, ndata-1);
                    end = CLAMP(cuts[k].end, 1, ndata);
                    if (from >= end)
                        continue;

                    g_array_set_size(rdata, 0);
                    for (m = n = 0; m < ncurves; m++) {
                        if (keep_curves & (1 << m)) {
                            adata = gwy_lawn_get_curve_data_const(lawn, j, i, m, NULL);
                            g_array_append_vals(rdata, adata + from, end - from);
                            n++;
                        }
                    }
                    gwy_lawn_set_curves(result, j, i, end - from, &g_array_index(rdata, gdouble, 0), NULL);
                }
            }
        }
    }
    else if (output == OUTPUT_MARK) {
        // FIXME: We should create an undo level here.  Unfortunately, that would copy the entire lawn. */
        gwy_lawn_set_segments(lawn, nsegments, (gint*)cuts);
        for (i = 0; i < nsegments; i++)
            gwy_lawn_set_segment_label(lawn, i, args->segnames[i].name);
        gwy_lawn_data_changed(lawn);
    }

end:
    gwy_app_wait_finish();
    g_array_free(rdata, TRUE);
    g_free(cuts);

    return !cancelled;
}

static void
extract_curve(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyLawn *lawn = args->lawn;
    GwyParams *params = args->params;
    gint curve = gwy_params_get_int(params, PARAM_CURVE);
    CutterMode mode = gwy_params_get_enum(params, PARAM_MODE);
    gint col = gwy_params_get_int(params, PARAM_XPOS);
    gint row = gwy_params_get_int(params, PARAM_YPOS);
    GwyGraphModel *gmodel = gui->gmodel;
    GwyGraphCurveModel *gcmodel;
    const gdouble *adata;
    gdouble *xdata;
    CutInterval *cuts = NULL;
    GArray *sums;
    const GwyEnum *segments;
    gint i, k, ndata, nsegments, from, end;

    gwy_graph_model_remove_all_curves(gmodel);
    adata = gwy_lawn_get_curve_data_const(lawn, col, row, curve, &ndata);

    sums = g_array_new(FALSE, FALSE, sizeof(LinearSum));
    xdata = g_new(gdouble, ndata);
    for (i = 0; i < ndata; i++)
        xdata[i] = i;

    if (mode == MODE_ZCUT_AHR) {
        segments = zcut_segments_ahr;
        nsegments = G_N_ELEMENTS(zcut_segments_ahr);
        cuts = g_new(CutInterval, nsegments);
        if (!(cutter_zcut_ahr(adata, ndata, cuts, sums)))
            goto end;
    }
    else if (mode == MODE_ZCUT_AR) {
        segments = zcut_segments_ar;
        nsegments = G_N_ELEMENTS(zcut_segments_ar);
        cuts = g_new(CutInterval, nsegments);
        if (!(cutter_zcut_ar(adata, ndata, cuts)))
            goto end;
    }
    else {
        g_return_if_reached();
    }

    for (k = 0; k < nsegments; k++) {
        from = CLAMP(cuts[k].from, 0, ndata-1);
        end = CLAMP(cuts[k].end, 1, ndata);
        if (from >= end)
            continue;

        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data(gcmodel, xdata + from, adata + from, end - from);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(k),
                     "description", segments[k].name,
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

end:
    g_array_free(sums, TRUE);
    g_free(cuts);
    g_free(xdata);
}

static void
update_graph_model_props(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyLawn *lawn = args->lawn;
    GwyParams *params = args->params;
    gint curve = gwy_params_get_int(params, PARAM_CURVE);
    GwyGraphModel *gmodel = gui->gmodel;
    GwySIUnit *yunit;
    const gchar *ylabel;

    yunit = gwy_lawn_get_si_unit_curve(lawn, curve);
    ylabel = gwy_lawn_get_curve_label(lawn, curve);
    g_object_set(gmodel,
                 "si-unit-y", yunit,
                 "axis-label-bottom", _("sample"),
                 "axis-label-left", ylabel ? ylabel : _("Untitled"),
                 NULL);
}

static void
set_segments_from_xpos(const gdouble *xpos, gint nseg, CutInterval *cuts, gint ndata)
{
    gint i;

    cuts[0].from = 0;
    for (i = 1; i < nseg; i++) {
        cuts[i-1].end = (gint)ceil(xpos[i-1]);
        cuts[i-1].end = CLAMP(cuts[i-1].end, 0, ndata);
        cuts[i].from = (gint)floor(xpos[i-1]);
        cuts[i].from = CLAMP(cuts[i].from, 0, ndata);
    }
    cuts[nseg-1].end = ndata;
}

static void
negate_sums(const LinearSum *src, LinearSum *dest)
{
    dest->one = -src->one;
    dest->x = -src->x;
    dest->xx = -src->xx;
    dest->y = -src->y;
    dest->xy = -src->xy;
}

static void
make_cummulative_sums(const gdouble *data, gint n, GArray *sums)
{
    LinearSum *s;
    gint i;

    g_array_set_size(sums, n+1);
    s = &g_array_index(sums, LinearSum, 0);
    gwy_clear(s, 1);
    for (i = 0; i < n; i++) {
        gdouble x = i, y = data[i];

        s[i+1].one = i+1;
        s[i+1].x = s[i].x + x;
        s[i+1].xx = s[i].xx + x*x;
        s[i+1].y = s[i].y + y;
        s[i+1].xy = s[i].xy + x*y;
    }
}

/* Calculate sum_j sgn(j+ε - i)*whatever[j], correct even for i outside the range.
 * The caller must know the split index i (not just the x-value).  */
static void
split_sums_one(const LinearSum *sums, gint n, gint i, LinearSum *s)
{
    const LinearSum *full = sums + n;

    if (i <= 0)
        *s = *full;
    else if (i > n)
        negate_sums(full, s);
    else {
        /* sums[i] is the sum up to, but not including i. */
        s->one = full->one - 2*sums[i].one;
        s->x = full->x - 2*sums[i].x;
        s->xx = full->xx - 2*sums[i].xx;
        s->y = full->y - 2*sums[i].y;
        s->xy = full->xy - 2*sums[i].xy;
    }
}

/* Calculate sum_j sgn(j+ε - i1)*sgn(j+ε - i2)*whatever[j], correct even for i1 or i2 outside the range.
 * The caller must know the split indices i1 and i2 (not just the x-values).  */
static void
split_sums_two(const LinearSum *sums, gint n, gint i1, gint i2, LinearSum *s)
{
    const LinearSum *full = sums + n;

    if (i1 == i2) {
        *s = *full;
        return;
    }

    if (i2 < i1)
        GWY_SWAP(gint, i1, i2);

    if (i1 <= 0) {
        split_sums_one(sums, n, i2, s);
        return;
    }
    if (i2 > n) {
        split_sums_one(sums, n, i1, s);
        negate_sums(s, s);
        return;
    }

    /* Now i1 and i2 are both within range, ordered and different. */
    s->one = full->one + 2*(sums[i1].one - sums[i2].one);
    s->x = full->x + 2*(sums[i1].x - sums[i2].x);
    s->xx = full->xx + 2*(sums[i1].xx - sums[i2].xx);
    s->y = full->y + 2*(sums[i1].y - sums[i2].y);
    s->xy = full->xy + 2*(sums[i1].xy - sums[i2].xy);
}

/* Sums of |x-ix|, |x-ix|y, |x-ix|x, given both the split index i and value ix. */
static void
absval_sums(const LinearSum *sums, gint n, gint i, gdouble ix,
            gdouble *ax, gdouble *axy, gdouble *axx)
{
    LinearSum s;

    split_sums_one(sums, n, i, &s);
    *ax = s.x - ix*s.one;
    *axy = s.xy - ix*s.y;
    *axx = s.xx - ix*s.x;
}

/* Sum of |x-ix1||x-ix2|, given both the split indices i and values ix. */
static gdouble
absval_sum_mixed(const LinearSum *sums, gint n, gint i1, gdouble ix1, gint i2, gdouble ix2)
{
    LinearSum s;

    split_sums_two(sums, n, i1, i2, &s);
    return s.xx - (ix1 + ix2)*s.x + ix1*ix2*s.one;
}

#if 0
/* p[] contains a0, b0, ix1, b1; the split point must correspond to i. */
static gdouble
piecewise_linear_residuum_one(const LinearSum *sums, gint n, gint i, const gdouble *p)
{
    const LinearSum *full = sums + n;
    gdouble a0 = p[0], b0 = p[1], ix1 = p[2], b1 = p[3];
    gdouble sax1, saxx1, saxy1, sax1x1;

    absval_sums(sums, n, i, ix1, &sax1, &saxy1, &saxx1);
    sax1x1 = full->xx - 2*ix1*full->x + n*ix1*ix1;

    /* Omit the constant sum of squared y. */
    return (b1*b1*sax1x1 + n*a0*a0 + b0*b0*full->xx
            + 2*a0*b1*sax1 + 2*b0*b1*saxx1 - 2*b1*saxy1 - 2*a0*full->y - 2*b0*full->xy + 2*a0*b0*full->x);
}

/* Solve the restricted problem of fitting a0*x + b0 + b1|x - ix| with fixed ix. */
static gboolean
piecewise_linear_fixed_solve_one(const LinearSum *sums, gint n, gint i, gdouble ix,
                                 gdouble *p)
{
    const LinearSum *full = sums + n;
    gdouble sax1, saxx1, saxy1, sax1x1;
    gdouble mat[6], rhs[3];

    absval_sums(sums, n, i, ix, &sax1, &saxy1, &saxx1);
    sax1x1 = full->xx - 2*ix*full->x + n*ix*ix;

    SLi(mat, 0, 0) = full->one;
    SLi(mat, 1, 0) = full->x;
    SLi(mat, 1, 1) = full->xx;
    SLi(mat, 2, 0) = sax1;
    SLi(mat, 2, 1) = saxx1;
    SLi(mat, 2, 2) = sax1x1;
    if (!gwy_math_choleski_decompose(3, mat))
        return FALSE;

    rhs[0] = full->y;
    rhs[1] = full->xy;
    rhs[2] = saxy1;
    gwy_math_choleski_solve(3, mat, rhs);

    p[0] = rhs[0];
    p[1] = rhs[1];
    p[2] = ix;
    p[3] = rhs[2];
    return TRUE;
}
#endif

/* p[] contains a0, b0, ix1, b1, ix2, b2; the split points must correspond to i1 and i2. */
static gdouble
piecewise_linear_residuum_two(const LinearSum *sums, gint n, gint i1, gint i2, const gdouble *p)
{
    const LinearSum *full = sums + n;
    gdouble a0 = p[0], b0 = p[1], ix1 = p[2], b1 = p[3], ix2 = p[4], b2 = p[5];
    gdouble sax1, saxx1, saxy1, sax1x1;
    gdouble sax2, saxx2, saxy2, sax2x2;
    gdouble sax1x2;

    absval_sums(sums, n, i1, ix1, &sax1, &saxy1, &saxx1);
    absval_sums(sums, n, i2, ix2, &sax2, &saxy2, &saxx2);
    sax1x1 = full->xx - 2*ix1*full->x + n*ix1*ix1;
    sax2x2 = full->xx - 2*ix2*full->x + n*ix2*ix2;
    sax1x2 = absval_sum_mixed(sums, n, i1, ix1, i2, ix2);

    /* Omit the constant sum of squared y. */
    return (b1*b1*sax1x1 + b2*b2*sax2x2 + n*a0*a0 + b0*b0*full->xx
            + 2*b1*b2*sax1x2 + 2*a0*(b1*sax1 + b2*sax2) + 2*b0*(b1*saxx1 + b2*saxx2) + 2*a0*b0*full->x
            - 2*b1*saxy1 - 2*b2*saxy2 - 2*a0*full->y - 2*b0*full->xy);
}

/* Solve the restricted problem of fitting a0*x + b0 + b1|x - ix1| + b2|x - ix2| with fixed ix1 and ix2. */
static gboolean
piecewise_linear_fixed_solve_two(const LinearSum *sums, gint n, gint i1, gdouble ix1, gint i2, gdouble ix2,
                                 gdouble *p)
{
    const LinearSum *full = sums + n;
    gdouble sax1, saxx1, saxy1, sax1x1;
    gdouble sax2, saxx2, saxy2, sax2x2;
    gdouble sax1x2;
    gdouble mat[10], rhs[4];

    absval_sums(sums, n, i1, ix1, &sax1, &saxy1, &saxx1);
    absval_sums(sums, n, i2, ix2, &sax2, &saxy2, &saxx2);
    sax1x1 = full->xx - 2*ix1*full->x + n*ix1*ix1;
    sax2x2 = full->xx - 2*ix2*full->x + n*ix2*ix2;
    sax1x2 = absval_sum_mixed(sums, n, i1, ix1, i2, ix2);

    SLi(mat, 0, 0) = full->one;
    SLi(mat, 1, 0) = full->x;
    SLi(mat, 1, 1) = full->xx;
    SLi(mat, 2, 0) = sax1;
    SLi(mat, 2, 1) = saxx1;
    SLi(mat, 2, 2) = sax1x1;
    SLi(mat, 3, 0) = sax2;
    SLi(mat, 3, 1) = saxx2;
    SLi(mat, 3, 2) = sax1x2;
    SLi(mat, 3, 3) = sax2x2;
    if (!gwy_math_choleski_decompose(4, mat))
        return FALSE;

    rhs[0] = full->y;
    rhs[1] = full->xy;
    rhs[2] = saxy1;
    rhs[3] = saxy2;
    gwy_math_choleski_solve(4, mat, rhs);

    p[0] = rhs[0];
    p[1] = rhs[1];
    p[2] = ix1;
    p[3] = rhs[2];
    p[4] = ix2;
    p[5] = rhs[3];
    return TRUE;
}

static gboolean
piecewise_estimate_two(const LinearSum *sums, gint n, gdouble *p)
{
    gint k1, k2, i1, i2, nsplit;
    gdouble r, rbest = G_MAXDOUBLE;
    gboolean any_good = FALSE;
    gdouble pp[6];

    nsplit = GWY_ROUND(1.5*sqrt(n));
    nsplit = MAX(nsplit, 3);
    for (k1 = 0; k1 < nsplit; k1++) {
        i1 = (n + 1)*(k1 + 1)/(nsplit + 1);
        for (k2 = k1+1; k2 < nsplit; k2++) {
            i2 = (n + 1)*(k2 + 1)/(nsplit + 1);
            /* XXX: Here we assume xdata are just integers.  Otherwise we would need some map-back function. */
            if (!piecewise_linear_fixed_solve_two(sums, n, i1, i1 - 0.5, i2, i2 - 0.5, pp))
                continue;
            r = piecewise_linear_residuum_two(sums, n, i1, i2, pp);
            if (r < rbest) {
                any_good = TRUE;
                rbest = r;
                gwy_assign(p, pp, 6);
            }
        }
    }

    return any_good;
}

static gint
zcut_simple(const gdouble *data, gint ifrom, gint ito)
{
    gint i, ibest = (ifrom + ito)/2;
    gdouble b, y, m = G_MAXDOUBLE;

    if (ito <= ifrom)
        return ibest;

    b = (data[ito-1] - data[ifrom])/(ito - ifrom);
    for (i = ifrom; i < ito; i++) {
        y = data[i] - b*i;
        if (y < m) {
            ibest = i;
            m = y;
        }
    }
    return ibest;
}

static gboolean
cutter_zcut_ar(const gdouble *data, gint ndata, CutInterval *cuts)
{
    gdouble xpos;

    if (ndata < 4)
        return FALSE;

    xpos = zcut_simple(data, 0, ndata) + 0.5;
    set_segments_from_xpos(&xpos, 2, cuts, ndata);

    return TRUE;
}

static gboolean
cutter_zcut_ahr(const gdouble *data, gint ndata, CutInterval *cuts, GArray *sums)
{
    gdouble p[6], xpos[2];

    if (ndata < 6)
        return FALSE;

    make_cummulative_sums(data, ndata, sums);
    if (!piecewise_estimate_two(&g_array_index(sums, LinearSum, 0), ndata, p))
        return FALSE;

    xpos[0] = zcut_simple(data, 0, CLAMP(GWY_ROUND(p[4]), 2, ndata)) + 0.5;
    xpos[1] = zcut_simple(data, CLAMP(GWY_ROUND(p[2]), 0, ndata-2), ndata) + 0.5;
    if (xpos[0] > xpos[1])
        GWY_SWAP(gdouble, xpos[0], xpos[1]);
    set_segments_from_xpos(xpos, 3, cuts, ndata);

    return TRUE;
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
    gint ncurves, keep_curves;

    sanitise_one_param(params, PARAM_XPOS, 0, gwy_lawn_get_xres(lawn)-1, gwy_lawn_get_xres(lawn)/2);
    sanitise_one_param(params, PARAM_YPOS, 0, gwy_lawn_get_yres(lawn)-1, gwy_lawn_get_yres(lawn)/2);
    ncurves = gwy_lawn_get_n_curves(lawn);
    keep_curves = gwy_params_get_int(params, PARAM_KEEP_CURVES);
    gwy_params_set_int(params, PARAM_KEEP_CURVES, keep_curves & ((1 << ncurves) - 1));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
