/*
 *  $Id: polylevel.c 24707 2022-03-21 17:09:03Z yeti-dn $
 *  Copyright (C) 2004-2021 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    MAX_DEGREE = 11
};

enum {
    PARAM_COL_DEGREE,
    PARAM_ROW_DEGREE,
    PARAM_MAX_DEGREE,
    PARAM_DO_EXTRACT,
    PARAM_SAME_DEGREE,
    PARAM_INDEPENDENT,
    PARAM_MASKING,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
    GwyDataField *bg;
} ModuleArgs;

typedef struct {
    ModuleArgs args;    /* This is a copy with downscaled args for the preview. */
    GtkWidget *dialog;
    GwyParamTable *table;
    GtkListStore *coeffmodel;
    GtkWidget *coefflist;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register             (void);
static GwyParamDef*     define_module_params        (void);
static void             poly_level                  (GwyContainer *data,
                                                     GwyRunType runtype);
static void             execute                     (ModuleArgs *args,
                                                     GtkListStore *coeffmodel);
static GwyDialogOutcome run_gui                     (ModuleArgs *args,
                                                     GwyContainer *data,
                                                     gint id);
static void             param_changed               (ModuleGUI *gui,
                                                     gint id);
static void             preview                     (gpointer user_data);
static void             create_coeff_view           (ModuleGUI *gui,
                                                     GtkBox *hbox);
static void             render_coeff_name           (GtkTreeViewColumn *column,
                                                     GtkCellRenderer *renderer,
                                                     GtkTreeModel *model,
                                                     GtkTreeIter *iter,
                                                     gpointer user_data);
static void             render_coeff_value          (GtkTreeViewColumn *column,
                                                     GtkCellRenderer *renderer,
                                                     GtkTreeModel *model,
                                                     GtkTreeIter *iter,
                                                     gpointer user_data);
static void             convert_coefficients_to_real(GwyDataField *field,
                                                     GtkListStore *store);
static gchar*           format_coefficient          (ModuleGUI *gui,
                                                     gint j,
                                                     gint i,
                                                     gdouble v,
                                                     GwySIUnitFormatStyle style);
static void             save_coeffs                 (ModuleGUI *gui);
static void             copy_coeffs                 (ModuleGUI *gui);
static gchar*           create_report               (ModuleGUI *gui);
static void             sanitise_params             (ModuleArgs *args);

/* We have just two modes distinguished by TRUE/FALSE, but technically it is an enum and we could have more modes. */
static const GwyEnum types[] = {
    { N_("Independent degrees"),  TRUE,  },
    { N_("Limited total degree"), FALSE, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Subtracts polynomial background."),
    "Yeti <yeti@gwyddion.net>",
    "4.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, polylevel)

static gboolean
module_register(void)
{
    gwy_process_func_register("polylevel",
                              (GwyProcessFunc)&poly_level,
                              N_("/_Level/_Polynomial Background..."),
                              GWY_STOCK_POLYNOM_LEVEL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Remove polynomial background"));

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
    gwy_param_def_add_int(paramdef, PARAM_COL_DEGREE, "col_degree", _("_Horizontal polynomial degree"),
                          0, MAX_DEGREE, 3);
    gwy_param_def_add_int(paramdef, PARAM_ROW_DEGREE, "row_degree", _("_Vertical polynomial degree"),
                          0, MAX_DEGREE, 3);
    gwy_param_def_add_int(paramdef, PARAM_MAX_DEGREE, "max_degree", _("_Maximum polynomial degree"),
                          0, MAX_DEGREE, 3);
    gwy_param_def_add_boolean(paramdef, PARAM_DO_EXTRACT, "do_extract", _("E_xtract background"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_SAME_DEGREE, "same_degree", _("_Same degrees"), TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_INDEPENDENT, "independent", NULL, types, G_N_ELEMENTS(types), TRUE);
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    return paramdef;
}

static void
poly_level(GwyContainer *data, GwyRunType runtype)
{
    GQuark quark;
    ModuleArgs args;
    gboolean do_extract;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(args.field && quark);

    args.bg = args.result = NULL;
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }
    gwy_app_undo_qcheckpointv(data, 1, &quark);

    do_extract = gwy_params_get_boolean(args.params, PARAM_DO_EXTRACT);
    args.result = g_object_ref(args.field);
    if (do_extract)
        args.bg = gwy_data_field_new_alike(args.field, FALSE);

    execute(&args, NULL);
    gwy_data_field_data_changed(args.result);
    gwy_app_channel_log_add_proc(data, oldid, oldid);

    if (do_extract) {
        newid = gwy_app_data_browser_add_data_field(args.bg, data, TRUE);
        gwy_app_sync_data_items(data, data, oldid, newid, FALSE, GWY_DATA_ITEM_GRADIENT, 0);
        gwy_app_set_data_field_title(data, newid, _("Background"));
        gwy_app_channel_log_add(data, oldid, newid, NULL, NULL);
    }

end:
    GWY_OBJECT_UNREF(args.bg);
    g_object_unref(args.params);
}

static void
execute(ModuleArgs *args, GtkListStore *coeffmodel)
{
    GwyParams *params = args->params;
    GwyDataField *field = args->field, *mask = args->mask, *result = args->result, *bg = args->bg;
    GwyMaskingType masking = gwy_params_get_masking(params, PARAM_MASKING, &mask);
    gint max_degree = gwy_params_get_int(params, PARAM_MAX_DEGREE);
    gint col_degree = gwy_params_get_int(params, PARAM_COL_DEGREE);
    gint row_degree = gwy_params_get_int(params, PARAM_ROW_DEGREE);
    gboolean independent = gwy_params_get_enum(params, PARAM_INDEPENDENT);
    gint *term_powers;
    gdouble *coeffs;
    gint nterms, i, j, k;

    k = 0;
    if (independent) {
        nterms = (col_degree + 1)*(row_degree + 1);
        term_powers = g_new(gint, 2*nterms);
        for (i = 0; i <= col_degree; i++) {
            for (j = 0; j <= row_degree; j++) {
                term_powers[k++] = i;
                term_powers[k++] = j;
            }
        }
    }
    else {
        nterms = (max_degree + 1)*(max_degree + 2)/2;
        term_powers = g_new(gint, 2*nterms);
        for (i = 0; i <= max_degree; i++) {
            for (j = 0; j <= max_degree - i; j++) {
                term_powers[k++] = i;
                term_powers[k++] = j;
            }
        }
    }

    coeffs = gwy_data_field_fit_poly(field, mask, nterms, term_powers, masking == GWY_MASK_EXCLUDE, NULL);
    gwy_data_field_copy(field, result, FALSE);
    gwy_data_field_subtract_poly(result, nterms, term_powers, coeffs);

    if (bg) {
        gwy_data_field_clear(bg);
        gwy_data_field_subtract_poly(bg, nterms, term_powers, coeffs);
        gwy_data_field_multiply(bg, -1.0);
    }

    if (coeffmodel) {
        GtkTreeIter iter;

        gtk_list_store_clear(coeffmodel);
        for (k = 0; k < nterms; k++) {
            gtk_list_store_insert_with_values(coeffmodel, &iter, k,
                                              0, term_powers[2*k+1],
                                              1, term_powers[2*k],
                                              2, coeffs[k],
                                              -1);
        }
        convert_coefficients_to_real(field, coeffmodel);
    }

    g_free(coeffs);
    g_free(term_powers);
}

static GwyContainer*
create_preview_data(GwyContainer *data, gint id,
                    ModuleArgs *args, ModuleArgs *preview_args)
{
    GwyContainer *preview_data;
    gint xres, yres;
    gdouble zoomval;

    preview_args->params = args->params;
    preview_args->mask = NULL;
    preview_data = gwy_container_new();
    xres = gwy_data_field_get_xres(args->field);
    yres = gwy_data_field_get_yres(args->field);
    zoomval = (gdouble)PREVIEW_SIZE/MAX(xres, yres);
    if (zoomval <= 1.0) {
        xres = MAX(xres*zoomval, 3);
        yres = MAX(yres*zoomval, 3);
        preview_args->field = gwy_data_field_new_resampled(args->field, xres, yres, GWY_INTERPOLATION_ROUND);
        if (args->mask)
            preview_args->mask = gwy_data_field_new_resampled(args->mask, xres, yres, GWY_INTERPOLATION_ROUND);
    }
    else {
        preview_args->field = g_object_ref(args->field);
        if (args->mask)
            preview_args->mask = g_object_ref(args->mask);
    }

    preview_args->result = gwy_data_field_new_alike(preview_args->field, TRUE);
    gwy_container_set_object_by_name(preview_data, "/0/data", preview_args->result);
    g_object_unref(preview_args->result);
    gwy_app_sync_data_items(data, preview_data, id, 0, FALSE, GWY_DATA_ITEM_GRADIENT, 0);

    preview_args->bg = gwy_data_field_new_alike(preview_args->field, TRUE);
    gwy_container_set_object_by_name(preview_data, "/1/data", preview_args->bg);
    g_object_unref(preview_args->bg);
    gwy_app_sync_data_items(data, preview_data, id, 1, FALSE, GWY_DATA_ITEM_GRADIENT, 0);

    return preview_data;
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *preview_table, *label, *hbox, *vbox, *dataview;
    ModuleGUI gui;
    GwyDialogOutcome outcome;

    gwy_clear(&gui, 1);
    gui.data = create_preview_data(data, id, args, &gui.args);

    gui.dialog = gwy_dialog_new(_("Remove Polynomial Background"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    vbox = gwy_vbox_new(0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);
    create_coeff_view(&gui, GTK_BOX(hbox));

    hbox = gwy_hbox_new(0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    preview_table = gtk_table_new(2, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(preview_table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(preview_table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(preview_table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), preview_table, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_HALF_SIZE, FALSE);
    gtk_table_attach(GTK_TABLE(preview_table), dataview, 0, 1, 0, 1, 0, 0, 0, 0);

    dataview = gwy_create_preview(gui.data, 1, PREVIEW_HALF_SIZE, FALSE);
    gtk_table_attach(GTK_TABLE(preview_table), dataview, 1, 2, 0, 1, 0, 0, 0, 0);

    label = gtk_label_new(_("Leveled data"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(preview_table), label, 0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);

    label = gtk_label_new(_("Background"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(preview_table), label, 1, 2, 1, 2, GTK_FILL, GTK_FILL, 0, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio_item(table, PARAM_INDEPENDENT, TRUE);
    gwy_param_table_append_slider(table, PARAM_COL_DEGREE);
    gwy_param_table_append_slider(table, PARAM_ROW_DEGREE);
    gwy_param_table_append_checkbox(table, PARAM_SAME_DEGREE);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_INDEPENDENT, FALSE);
    gwy_param_table_append_slider(table, PARAM_MAX_DEGREE);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_DO_EXTRACT);
    if (args->mask)
        gwy_param_table_append_combo(table, PARAM_MASKING);

    gtk_box_pack_start(GTK_BOX(vbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.coeffmodel);

    return outcome;
}

static void
create_coeff_view(ModuleGUI *gui, GtkBox *hbox)
{
    GtkWidget *treeview, *button, *hbox2, *scwin, *label, *coeffvbox;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkTreeSelection *selection;

    coeffvbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(hbox, coeffvbox, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(coeffvbox), 4);

    label = gtk_label_new(_("Polynomial Coefficients"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(coeffvbox), label, FALSE, FALSE, 0);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(coeffvbox), scwin, TRUE, TRUE, 0);

    gui->coeffmodel = gtk_list_store_new(3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_DOUBLE);
    gui->coefflist = treeview = gtk_tree_view_new();
    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(gui->coeffmodel));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);
    gtk_container_add(GTK_CONTAINER(scwin), treeview);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(column, FALSE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 0.0, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer, render_coeff_name, gui, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer, render_coeff_value, gui, NULL);

    hbox2 = gwy_hbox_new(0);
    gtk_box_pack_start(GTK_BOX(coeffvbox), hbox2, FALSE, FALSE, 0);

    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(button, _("Save table to a file"));
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(GTK_STOCK_SAVE, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_box_pack_end(GTK_BOX(hbox2), button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(save_coeffs), gui);

    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(button, _("Copy table to clipboard"));
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(GTK_STOCK_COPY, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_box_pack_end(GTK_BOX(hbox2), button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(copy_coeffs), gui);
}

static void
render_coeff_name(G_GNUC_UNUSED GtkTreeViewColumn *column,
                  GtkCellRenderer *renderer,
                  GtkTreeModel *model,
                  GtkTreeIter *iter,
                  G_GNUC_UNUSED gpointer user_data)
{
    guint i, j;
    guchar buf[24];

    gtk_tree_model_get(model, iter, 0, &j, 1, &i, -1);
    g_snprintf(buf, sizeof(buf), "a<sub>%u,%u</sub>", j, i);
    g_object_set(renderer, "markup", buf, NULL);
}

static void
render_coeff_value(G_GNUC_UNUSED GtkTreeViewColumn *column,
                   GtkCellRenderer *renderer,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    guint i, j;
    gdouble v;
    gchar *buf;

    gtk_tree_model_get(model, iter, 0, &j, 1, &i, 2, &v, -1);
    buf = format_coefficient(gui, j, i, v, GWY_SI_UNIT_FORMAT_VFMARKUP);
    g_object_set(renderer, "markup", buf, NULL);
    g_free(buf);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table;
    GwyParams *params = gui->args.params;
    gboolean independent = gwy_params_get_enum(params, PARAM_INDEPENDENT);
    gboolean same_degree = gwy_params_get_boolean(params, PARAM_SAME_DEGREE);
    gint col_degree = gwy_params_get_int(params, PARAM_COL_DEGREE);
    gint row_degree = gwy_params_get_int(params, PARAM_ROW_DEGREE);

    if (id < 0 || id == PARAM_INDEPENDENT) {
        gwy_param_table_set_sensitive(table, PARAM_SAME_DEGREE, independent);
        gwy_param_table_set_sensitive(table, PARAM_ROW_DEGREE, independent);
        gwy_param_table_set_sensitive(table, PARAM_COL_DEGREE, independent);
        gwy_param_table_set_sensitive(table, PARAM_MAX_DEGREE, !independent);
    }
    if (id < 0 || id == PARAM_SAME_DEGREE) {
        if (same_degree && row_degree != col_degree) {
            row_degree = col_degree;
            gwy_param_table_set_int(table, PARAM_ROW_DEGREE, row_degree);
        }
    }
    if (id == PARAM_ROW_DEGREE && same_degree && row_degree != col_degree) {
        col_degree = row_degree;
        gwy_param_table_set_int(table, PARAM_COL_DEGREE, col_degree);
    }
    if (id == PARAM_COL_DEGREE && same_degree && row_degree != col_degree) {
        row_degree = col_degree;
        gwy_param_table_set_int(table, PARAM_ROW_DEGREE, row_degree);
    }

    if (id != PARAM_DO_EXTRACT)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    gtk_tree_view_set_model(GTK_TREE_VIEW(gui->coefflist), NULL);
    execute(&gui->args, gui->coeffmodel);
    gtk_tree_view_set_model(GTK_TREE_VIEW(gui->coefflist), GTK_TREE_MODEL(gui->coeffmodel));
    gwy_data_field_data_changed(gui->args.result);
    gwy_data_field_data_changed(gui->args.bg);
}

static void
convert_coefficients_to_real(GwyDataField *field,
                             GtkListStore *store)
{
    gdouble cx = field->xoff + 0.5*field->xreal,
            cy = field->yoff + 0.5*field->yreal,
            bx = 0.5*field->xreal*(1.0 - 1.0/field->xres),
            by = 0.5*field->yreal*(1.0 - 1.0/field->yres);
    GtkTreeModel *model = GTK_TREE_MODEL(store);
    guint n = gtk_tree_model_iter_n_children(model, NULL);
    gdouble *coeffs;
    guint *powermap;
    GtkTreeIter iter;
    guint k;

    if (!gtk_tree_model_get_iter_first(model, &iter))
        return;

    coeffs = g_new0(gdouble, n);
    powermap = g_new0(guint, 2*n);
    k = 0;

    do {
        guint i, j;
        gtk_tree_model_get(model, &iter, 0, &j, 1, &i, -1);
        powermap[k++] = j;
        powermap[k++] = i;
    } while (gtk_tree_model_iter_next(model, &iter));

    gtk_tree_model_get_iter_first(model, &iter);
    do {
        guint i, j, m, l;
        gdouble v, combjm = 1, cxpow = 1.0;

        gtk_tree_model_get(model, &iter, 0, &j, 1, &i, 2, &v, -1);
        v /= gwy_powi(bx, j) * gwy_powi(by, i);
        for (m = 0; m <= j; m++) {
            gdouble combil = 1, cypow = 1.0;
            for (l = 0; l <= i; l++) {
                gdouble vml = v*combjm*combil*cxpow*cypow;
                for (k = 0; k < n; k++) {
                    if (powermap[2*k] == j - m && powermap[2*k + 1] == i - l) {
                        coeffs[k] += vml;
                        break;
                    }
                }
                g_assert(k < n);
                cypow *= -cy;
                combil *= (i - l)/(l + 1.0);
            }
            cxpow *= -cx;
            combjm *= (j - m)/(m + 1.0);
        }
    } while (gtk_tree_model_iter_next(model, &iter));

    gtk_tree_model_get_iter_first(model, &iter);
    k = 0;
    do {
        gtk_list_store_set(store, &iter, 2, coeffs[k], -1);
        k++;
    } while (gtk_tree_model_iter_next(model, &iter));

    g_free(powermap);
    g_free(coeffs);
}

static gchar*
format_coefficient(ModuleGUI *gui,
                   gint j, gint i, gdouble v,
                   GwySIUnitFormatStyle style)
{
    GwySIUnit *zunit = gwy_data_field_get_si_unit_z(gui->args.field),
              *xyunit = gwy_data_field_get_si_unit_xy(gui->args.field);
    GwySIUnit *unit = gwy_si_unit_power_multiply(zunit, 1, xyunit, -(i + j), NULL);
    GwySIValueFormat *vf = gwy_si_unit_get_format_with_digits(unit, style, fabs(v), 4, NULL);
    gchar *retval = g_strdup_printf("%.*f%s%s", vf->precision, v/vf->magnitude, *vf->units ? " " : "", vf->units);
    gwy_si_unit_value_format_free(vf);
    g_object_unref(unit);

    return retval;
}

static void
save_coeffs(ModuleGUI *gui)
{
    gchar *text;

    text = create_report(gui);
    gwy_save_auxiliary_data(_("Save Table"), GTK_WINDOW(gui->dialog), -1, text);
    g_free(text);
}

static void
copy_coeffs(ModuleGUI *gui)
{
    GtkClipboard *clipboard;
    GdkDisplay *display;
    gchar *text;

    text = create_report(gui);
    display = gtk_widget_get_display(gui->dialog);
    clipboard = gtk_clipboard_get_for_display(display, GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    g_free(text);
}

static gchar*
create_report(ModuleGUI *gui)
{
    GtkTreeIter iter;
    GString *text;
    gchar *retval;
    GtkTreeModel *model = GTK_TREE_MODEL(gui->coeffmodel);

    if (!gtk_tree_model_get_iter_first(model, &iter))
        return g_strdup("");

    text = g_string_new(NULL);
    do {
        guint i, j;
        gdouble v;
        gchar *buf;
        gtk_tree_model_get(model, &iter, 0, &j, 1, &i, 2, &v, -1);
        buf = format_coefficient(gui, j, i, v, GWY_SI_UNIT_FORMAT_PLAIN);
        g_string_append_printf(text, "a[%u,%u] = %s\n", j, i, buf);
        g_free(buf);
    } while (gtk_tree_model_iter_next(model, &iter));

    retval = text->str;
    g_string_free(text, FALSE);

    return retval;
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    if (gwy_params_get_int(params, PARAM_ROW_DEGREE) != gwy_params_get_int(params, PARAM_COL_DEGREE))
        gwy_params_set_boolean(params, PARAM_SAME_DEGREE, FALSE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
