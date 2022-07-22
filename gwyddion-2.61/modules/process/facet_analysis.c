/*
 *  $Id: facet_analysis.c 24626 2022-03-03 12:59:05Z yeti-dn $
 *  Copyright (C) 2003-2017 David Necas (Yeti), Petr Klapetek.
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

/**
 * Facet (angle) view uses a zoomed area-preserving projection of north
 * hemisphere normal.  Coordinates on hemisphere are labeled (theta, phi),
 * coordinates on the projection (x, y)
 **/

/* TODO:
 * - Selecting a point recalls the corresponding mask on the image (if
 *   marked previously)???
 * - Create multiple masks on output?  How exactly?
 * - Add "Make (001)/(010)/(100)" which sets the selected point to direction
 *   of a crystal plane (NOT along an axis!).
 */

#include "config.h"
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/stats.h>
#include <libprocess/level.h>
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libprocess/elliptic.h>
#include <libgwydgets/gwyshader.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define FACETS_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define FVIEW_GRADIENT "DFit"

enum {
    MAX_PLANE_SIZE = 7,  /* this is actually half */
    FACETVIEW_SIZE = PREVIEW_HALF_SIZE | 1,
    IMAGEVIEW_SIZE = (PREVIEW_SIZE + PREVIEW_SMALL_SIZE)/2,
};

typedef enum {
    LATTICE_CUBIC        = 0,
    LATTICE_RHOMBOHEDRAL = 1,
    LATTICE_HEXAGONAL    = 2,
    LATTICE_TETRAGONAL   = 3,
    LATTICE_ORTHORHOMBIC  = 4,
    LATTICE_MONOCLINIC   = 5,
    LATTICE_TRICLINIC    = 6,
    LATTICE_NTYPES,
} LatticeType;

typedef enum {
    LATTICE_PARAM_A     = 0,
    LATTICE_PARAM_B     = 1,
    LATTICE_PARAM_C     = 2,
    LATTICE_PARAM_ALPHA = 3,
    LATTICE_PARAM_BETA  = 4,
    LATTICE_PARAM_GAMMA = 5,
    LATTICE_PARAM_NPARAMS,
} LatticeParameterType;

static const guint lattice_indep_params[LATTICE_NTYPES] = {
    (1 << LATTICE_PARAM_A),
    (1 << LATTICE_PARAM_A) | (1 << LATTICE_PARAM_GAMMA),
    (1 << LATTICE_PARAM_A) | (1 << LATTICE_PARAM_C),
    (1 << LATTICE_PARAM_A) | (1 << LATTICE_PARAM_C),
    (1 << LATTICE_PARAM_A) | (1 << LATTICE_PARAM_B) | (1 << LATTICE_PARAM_C),
    (1 << LATTICE_PARAM_A) | (1 << LATTICE_PARAM_C) | (1 << LATTICE_PARAM_BETA),
    (1 << LATTICE_PARAM_A) | (1 << LATTICE_PARAM_B) | (1 << LATTICE_PARAM_C)
        | (1 << LATTICE_PARAM_ALPHA) | (1 << LATTICE_PARAM_BETA)
        | (1 << LATTICE_PARAM_GAMMA),
};

enum {
    FACET_COLUMN_N,
    FACET_COLUMN_THETA,
    FACET_COLUMN_PHI,
    FACET_COLUMN_X,
    FACET_COLUMN_Y,
    FACET_COLUMN_Z,
};

typedef struct {
    gdouble tolerance;
    gint kernel_size;
    gboolean combine;
    gboolean number_points;
    GwyMergeType combine_type;
    LatticeType lattice_type;
    GwyResultsReportType report_style;
    gdouble lattice_params[LATTICE_PARAM_NPARAMS];
    /* We store here the last angles used for any marking. */
    gdouble theta0;
    gdouble phi0;
    /* Rotation, dynamic state only */
    gdouble rot_theta;
    gdouble rot_phi;
    gdouble rot_omega;
} FacetsArgs;

typedef struct {
    FacetsArgs *args;
    GtkWidget *dialog;
    GtkWidget *view;
    GtkWidget *fview;
    GwySelection *fselection;
    GwySelection *fselection0;
    GwySelection *iselection;
    GwyNullStore *store;
    GtkWidget *pointlist;
    GtkWidget *rexport;
    GtkWidget *number_points;
    GtkWidget *clear;
    GtkWidget *delete;
    GtkWidget *refine;
    GtkWidget *mark;
    GtkWidget *theta_min_label;
    GtkWidget *theta_0_label;
    GtkWidget *theta_max_label;
    GtkWidget *mangle_label;
    GtkObject *tolerance;
    GtkObject *kernel_size;
    GtkWidget *shader;
    GtkObject *rot_theta;
    GtkObject *rot_phi;
    GtkObject *rot_omega;
    GtkWidget *reset_rotation;
    GtkWidget *combine;
    GSList *combine_type;
    GtkWidget *color_button;
    GtkWidget *lattice_type;
    GtkWidget *lattice_label[LATTICE_PARAM_NPARAMS];
    GtkWidget *lattice_entry[LATTICE_PARAM_NPARAMS];
    GtkWidget *lattice_units[LATTICE_PARAM_NPARAMS];
    GtkWidget *create;
    GwyContainer *mydata;
    GwyContainer *fdata;
    gdouble q;
    gint selid;
    gboolean in_update;
    gboolean is_rotating;
} FacetsControls;

static gboolean   module_register                  (void);
static void       facets_analyse                   (GwyContainer *data,
                                                    GwyRunType run);
static void       facets_dialog                    (FacetsArgs *args,
                                                    GwyContainer *data,
                                                    GwyDataField *dfield,
                                                    GwyDataField *mfield,
                                                    gint id,
                                                    GQuark mquark);
static GtkWidget* create_facets_controls           (FacetsControls *controls,
                                                    GwyDataField *mfield);
static GtkWidget* create_rotation_controls         (FacetsControls *controls);
static GtkWidget* create_lattice_controls          (FacetsControls *controls);
static void       create_point_list                (FacetsControls *controls);
static void       point_list_selection_changed     (GtkTreeSelection *treesel,
                                                    FacetsControls *controls);
static void       render_id                        (GtkCellLayout *layout,
                                                    GtkCellRenderer *renderer,
                                                    GtkTreeModel *model,
                                                    GtkTreeIter *iter,
                                                    gpointer user_data);
static void       render_facet_parameter           (GtkCellLayout *layout,
                                                    GtkCellRenderer *renderer,
                                                    GtkTreeModel *model,
                                                    GtkTreeIter *iter,
                                                    gpointer user_data);
static void       clear_facet_selection            (FacetsControls *controls);
static void       delete_facet_selection           (FacetsControls *controls);
static void       refine_facet_selection           (FacetsControls *controls);
static void       mark_facet_selection             (FacetsControls *controls);
static gboolean   point_list_key_pressed           (FacetsControls *controls,
                                                    GdkEventKey *event);
static void       run_noninteractive               (FacetsArgs *args,
                                                    GwyContainer *data,
                                                    GwyDataField *dtheta,
                                                    GwyDataField *dphi,
                                                    GwyDataField *dfield,
                                                    GwyDataField *mfield,
                                                    GQuark mquark,
                                                    gdouble theta,
                                                    gdouble phi);
static void       report_style_changed             (FacetsControls *controls,
                                                    GwyResultsExport *rexport);
static void       number_points_changed            (FacetsControls *controls,
                                                    GtkToggleButton *toggle);
static void       copy_facet_table                 (FacetsControls *controls);
static void       save_facet_table                 (FacetsControls *controls);
static void       kernel_size_changed              (GtkAdjustment *adj,
                                                    FacetsControls *controls);
static void       update_theta_range               (FacetsControls *controls);
static void       facet_view_select_angle          (FacetsControls *controls,
                                                    gdouble theta,
                                                    gdouble phi);
static void       facet_view_selection_updated     (GwySelection *selection,
                                                    gint hint,
                                                    FacetsControls *controls);
static void       update_average_angle             (FacetsControls *controls,
                                                    gboolean clearme);
static void       preview_selection_updated        (GwySelection *selection,
                                                    gint id,
                                                    FacetsControls *controls);
static void       combine_changed                  (FacetsControls *controls,
                                                    GtkToggleButton *toggle);
static void       combine_type_changed             (FacetsControls *controls);
static void       lattice_type_changed             (GtkComboBox *combo,
                                                    FacetsControls *controls);
static void       lattice_parameter_changed        (GtkEntry *entry,
                                                    FacetsControls *controls);
static void       create_lattice                   (FacetsControls *controls);
static void       rot_shader_changed               (FacetsControls *controls,
                                                    GwyShader *shader);
static void       rot_theta_changed                (FacetsControls *controls,
                                                    GtkAdjustment *adj);
static void       rot_phi_changed                  (FacetsControls *controls,
                                                    GtkAdjustment *adj);
static void       rot_omega_changed                (FacetsControls *controls,
                                                    GtkAdjustment *adj);
static void       reset_rotation                   (FacetsControls *controls);
static void       gwy_data_field_mark_facets       (GwyDataField *dtheta,
                                                    GwyDataField *dphi,
                                                    gdouble theta0,
                                                    gdouble phi0,
                                                    gdouble tolerance,
                                                    GwyDataField *mask);
static void       calculate_average_angle          (GwyDataField *dtheta,
                                                    GwyDataField *dphi,
                                                    GwyDataField *mask,
                                                    gdouble *theta,
                                                    gdouble *phi);
static gdouble    gwy_data_field_facet_distribution(GwyDataField *dfield,
                                                    gint half_size,
                                                    GwyContainer *container);
static void       compute_slopes                   (GwyDataField *dfield,
                                                    gint kernel_size,
                                                    GwyDataField *xder,
                                                    GwyDataField *yder);
static void       facets_tolerance_changed         (FacetsControls *controls,
                                                    GtkAdjustment *adj);
static void       add_mask_field                   (GwyDataView *view,
                                                    const GwyRGBA *color);
static void       facets_mark_fdata                (FacetsArgs *args,
                                                    GwyContainer *fdata,
                                                    gdouble q);
static void       facets_load_args                 (GwyContainer *container,
                                                    FacetsArgs *args);
static void       facets_save_args                 (GwyContainer *container,
                                                    FacetsArgs *args);
static void       make_unit_vector                 (GwyXYZ *v,
                                                    gdouble theta,
                                                    gdouble phi);
static void       vector_angles                    (const GwyXYZ *v,
                                                    gdouble *theta,
                                                    gdouble *phi);
static void       conform_to_lattice_type          (gdouble *params,
                                                    LatticeType type);
static void       make_lattice_vectors             (const gdouble *params,
                                                    GwyXYZ *a,
                                                    GwyXYZ *b,
                                                    GwyXYZ *c);
static void       make_inverse_lattice             (const GwyXYZ *a,
                                                    const GwyXYZ *b,
                                                    const GwyXYZ *c,
                                                    GwyXYZ *ia,
                                                    GwyXYZ *ib,
                                                    GwyXYZ *ic);
static void       rotate_vector                    (GwyXYZ *v,
                                                    gdouble omega,
                                                    gdouble theta,
                                                    gdouble phi);

static const FacetsArgs facets_defaults = {
    3.0*G_PI/180.0,
    3,
    FALSE, FALSE, GWY_MERGE_UNION, LATTICE_CUBIC,
    GWY_RESULTS_REPORT_TABSEP,
    { 1.0, 1.0, 1.0, 0.5*G_PI, 0.5*G_PI, 0.5*G_PI },
    0.0, 0.0,
    0.0, 0.0, 0.0,
};

static const GwyRGBA mask_color = { 0.56, 0.39, 0.07, 0.5 };

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Visualizes, marks and measures facet orientation."),
    "Yeti <yeti@gwyddion.net>",
    "2.3",
    "David Nečas (Yeti) & Petr Klapetek",
    "2005",
};

GWY_MODULE_QUERY2(module_info, facet_analysis)

static gboolean
module_register(void)
{
    gwy_process_func_register("facet_analysis",
                              (GwyProcessFunc)&facets_analyse,
                              N_("/Measure _Features/Facet _Analysis..."),
                              GWY_STOCK_FACET_ANALYSIS,
                              FACETS_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Mark areas by 2D slope"));

    return TRUE;
}


static void
facets_analyse(GwyContainer *data, GwyRunType run)
{
    FacetsArgs args;
    GwyContainer *fdata;
    GwyDataField *dfield, *mfield, *dtheta, *dphi;
    GQuark mquark;
    gint id;

    g_return_if_fail(run & FACETS_RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(dfield && mquark);

    if (!gwy_require_image_same_units(dfield, data, id, _("Facet Analysis")))
        return;

    facets_load_args(gwy_app_settings_get(), &args);
    if (run == GWY_RUN_IMMEDIATE) {
        /* FIXME: Refactor for more meaningful non-interactive mode? */
        fdata = gwy_container_new();
        gwy_data_field_facet_distribution(dfield, args.kernel_size, fdata);
        dtheta = gwy_container_get_object_by_name(fdata, "/theta");
        dphi = gwy_container_get_object_by_name(fdata, "/phi");
        run_noninteractive(&args, data, dtheta, dphi, dfield, mfield, mquark,
                           args.theta0, args.phi0);
        gwy_app_channel_log_add_proc(data, id, id);
        g_object_unref(fdata);
    }
    else {
        facets_dialog(&args, data, dfield, mfield, id, mquark);
        facets_save_args(gwy_app_settings_get(), &args);
    }
}

static void
facets_dialog(FacetsArgs *args,
              GwyContainer *data,
              GwyDataField *dfield,
              GwyDataField *mfield,
              gint id,
              GQuark mquark)
{
    GtkWidget *dialog, *table, *hbox, *vbox, *label, *thetabox, *scwin,
              *button, *notebook;
    FacetsControls controls;
    GwyResultsExport *rexport;
    GwyDataField *dtheta, *dphi;
    gint response;
    GtkTreeView *treeview;
    GwySelection *selection;
    GtkTreeSelection *treesel;
    GtkTreeIter iter;
    gchar *selkey;
    gboolean restored_selection = FALSE;

    gwy_clear(&controls, 1);
    controls.args = args;
    controls.selid = -1;
    dialog = gtk_dialog_new_with_buttons(_("Facet Analysis"),
                                         NULL,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    /* Shallow-copy stuff to temporary container */
    controls.fdata = gwy_container_new();
    controls.mydata = gwy_container_new();
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    controls.q = gwy_data_field_facet_distribution(dfield, args->kernel_size,
                                                   controls.fdata);

    /**** First row: Image + point list *****/
    hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 2);

    controls.view = gwy_create_preview(controls.mydata, 0, IMAGEVIEW_SIZE,
                                   TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), controls.view, FALSE, FALSE, 4);
    selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(controls.view),
                                                0, "Point", 1, TRUE);
    controls.iselection = selection;
    g_signal_connect(selection, "changed",
                     G_CALLBACK(preview_selection_updated), &controls);

    /* Info table */
    vbox = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    create_point_list(&controls);
    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scwin), controls.pointlist);
    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    controls.rexport = gwy_results_export_new(args->report_style);
    rexport = GWY_RESULTS_EXPORT(controls.rexport);
    gwy_results_export_set_style(rexport, GWY_RESULTS_EXPORT_TABULAR_DATA);
    gtk_box_pack_start(GTK_BOX(vbox), controls.rexport, FALSE, FALSE, 0);
    g_signal_connect_swapped(rexport, "format-changed",
                             G_CALLBACK(report_style_changed), &controls);
    g_signal_connect_swapped(controls.rexport, "copy",
                             G_CALLBACK(copy_facet_table), &controls);
    g_signal_connect_swapped(controls.rexport, "save",
                             G_CALLBACK(save_facet_table), &controls);

    controls.number_points
    /* TRANSLATORS: Number is verb here. */
        = gtk_check_button_new_with_mnemonic(_("_Number points"));
    gtk_box_pack_start(GTK_BOX(controls.rexport), controls.number_points,
                       FALSE, FALSE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.number_points),
                                 args->number_points);
    g_signal_connect_swapped(controls.number_points, "toggled",
                             G_CALLBACK(number_points_changed), &controls);

    hbox = gtk_hbox_new(TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    button = gwy_stock_like_button_new(_("_Clear"), GTK_STOCK_CLEAR);
    controls.clear = button;
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(clear_facet_selection), &controls);

    button = gwy_stock_like_button_new(_("_Delete"), GTK_STOCK_DELETE);
    controls.delete = button;
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(delete_facet_selection), &controls);

    button = gtk_button_new_with_mnemonic(_("_Refine"));
    controls.refine = button;
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(refine_facet_selection), &controls);

    button = gtk_button_new_with_mnemonic(_("_Mark"));
    controls.mark = button;
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(mark_facet_selection), &controls);

    hbox = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 8);

    label = gtk_label_new(_("Mean normal:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    controls.mangle_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.mangle_label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), controls.mangle_label, FALSE, FALSE, 0);

    /**** Second row: Facet view + point controls *****/
    hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 2);

    vbox = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 4);

    controls.fview = gwy_create_preview(controls.fdata, 0, FACETVIEW_SIZE, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), controls.fview, FALSE, FALSE, 0);
    selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(controls.fview),
                                                0, "Point", 1024, TRUE);
    controls.fselection = selection;
    selkey = g_strdup_printf("/%d/select/_facets", id);
    /* XXX XXX XXX: This is bogus. The selection depends on q, which changes
     * with plane size. We have to remember the selection as derivatives, not
     * facet view coordinates – in sync with facet_measure. */
    if (gwy_container_gis_object_by_name(data, selkey, &selection)) {
        gwy_selection_assign(controls.fselection, selection);
        selection = controls.fselection;
        restored_selection = TRUE;
    }
    controls.fselection0 = gwy_selection_duplicate(selection);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(facet_view_selection_updated), &controls);

    thetabox = gtk_hbox_new(TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), thetabox, FALSE, FALSE, 0);

    label = controls.theta_min_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(thetabox), label, TRUE, TRUE, 0);

    label = controls.theta_0_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
    gtk_box_pack_start(GTK_BOX(thetabox), label, TRUE, TRUE, 0);

    label = controls.theta_max_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_box_pack_start(GTK_BOX(thetabox), label, TRUE, TRUE, 0);

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 4);

    table = create_facets_controls(&controls, mfield);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table,
                             gtk_label_new(_("Facets")));

    table = create_rotation_controls(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table,
                             gtk_label_new(_("Rotation")));

    table = create_lattice_controls(&controls);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table,
                             gtk_label_new(_("Lattice")));
    lattice_type_changed(GTK_COMBO_BOX(controls.lattice_type), &controls);

    gtk_widget_show_all(dialog);
    update_theta_range(&controls);
    number_points_changed(&controls, GTK_TOGGLE_BUTTON(controls.number_points));

    if (restored_selection) {
        facet_view_selection_updated(selection, -1, &controls);
        if (gwy_null_store_get_n_rows(controls.store)) {
            treeview = GTK_TREE_VIEW(controls.pointlist);
            treesel = gtk_tree_view_get_selection(treeview);
            gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(controls.store),
                                          &iter, NULL, 0);
            gtk_tree_selection_select_iter(treesel, &iter);
        }
    }
    else
        facet_view_select_angle(&controls, args->theta0, args->phi0);

    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            g_object_unref(controls.mydata);
            g_object_unref(controls.fdata);
            g_object_unref(controls.fselection0);
            g_free(selkey);
            return;
            break;

            case GTK_RESPONSE_OK:
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gwy_app_sync_data_items(controls.mydata, data, 0, id, FALSE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    gwy_container_set_object_by_name(data, selkey, controls.fselection);
    gtk_widget_destroy(dialog);

    g_object_unref(controls.mydata);
    dtheta = gwy_container_get_object_by_name(controls.fdata, "/theta");
    dphi = gwy_container_get_object_by_name(controls.fdata, "/phi");
    run_noninteractive(args, data, dtheta, dphi, dfield, mfield, mquark,
                       args->theta0, args->phi0);
    g_object_unref(controls.fdata);
    g_object_unref(controls.fselection0);
    g_free(selkey);
    gwy_app_channel_log_add_proc(data, id, id);
}

static GtkWidget*
create_facets_controls(FacetsControls *controls, GwyDataField *mfield)
{
    FacetsArgs *args = controls->args;
    GtkWidget *table, *scale;
    gint row;

    table = gtk_table_new(3 + 1*(!!mfield), 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    row = 0;

    controls->kernel_size = gtk_adjustment_new(args->kernel_size,
                                               0.0, MAX_PLANE_SIZE, 1.0, 1.0,
                                               0);
    gwy_table_attach_adjbar(table, row, _("Facet plane size:"), _("px"),
                            controls->kernel_size,
                            GWY_HSCALE_LINEAR | GWY_HSCALE_SNAP);
    g_signal_connect(controls->kernel_size, "value-changed",
                     G_CALLBACK(kernel_size_changed), controls);
    row++;

    controls->tolerance = gtk_adjustment_new(args->tolerance*180.0/G_PI,
                                             0.0, 30.0, 0.01, 0.1, 0);
    scale = gwy_table_attach_adjbar(table, row++, _("_Tolerance:"), _("deg"),
                                    controls->tolerance, GWY_HSCALE_SQRT);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(scale), 3);
    g_signal_connect_swapped(controls->tolerance, "value-changed",
                             G_CALLBACK(facets_tolerance_changed), controls);

    if (mfield) {
        gwy_container_set_object_by_name(controls->fdata, "/1/mask", mfield);
        create_mask_merge_buttons(table, row, NULL,
                                  args->combine,
                                  G_CALLBACK(combine_changed),
                                  args->combine_type,
                                  G_CALLBACK(combine_type_changed),
                                  controls,
                                  &controls->combine, &controls->combine_type);
        row++;
    }

    controls->color_button = create_mask_color_button(controls->mydata,
                                                      controls->dialog, 0);
    gwy_table_attach_adjbar(table, row, _("_Mask color:"), NULL,
                            GTK_OBJECT(controls->color_button),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    return table;
}

static GtkWidget*
create_rotation_controls(FacetsControls *controls)
{
    FacetsArgs *args = controls->args;
    GtkWidget *vbox, *hbox, *table, *label;
    gint row;

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

    label = gtk_label_new(_("Rotate all points"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 2);

    hbox = gtk_hbox_new(FALSE, 12);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    controls->shader = gwy_shader_new(FVIEW_GRADIENT);
    gwy_shader_set_angle(GWY_SHADER(controls->shader),
                         args->rot_theta, args->rot_phi);
    gtk_widget_set_size_request(controls->shader, 80, 80);
    g_signal_connect_swapped(controls->shader, "angle_changed",
                             G_CALLBACK(rot_shader_changed), controls);
    gtk_box_pack_start(GTK_BOX(hbox), controls->shader, FALSE, TRUE, 0);

    table = gtk_table_new(3, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 0);
    row = 0;

    controls->rot_theta = gtk_adjustment_new(args->rot_theta*180.0/G_PI,
                                             0.0, 90.0, 1.0, 15.0, 0.0);
    gwy_table_attach_adjbar(table, row, _("θ:"), _("deg"),
                            controls->rot_theta, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls->rot_theta, "value-changed",
                             G_CALLBACK(rot_theta_changed), controls);
    row++;

    controls->rot_phi = gtk_adjustment_new(args->rot_phi*180.0/G_PI,
                                           -180.0, 180.0, 1.0, 30.0, 0.0);
    gwy_table_attach_adjbar(table, row, _("φ:"), _("deg"),
                            controls->rot_phi, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls->rot_phi, "value-changed",
                             G_CALLBACK(rot_phi_changed), controls);
    row++;

    controls->rot_omega = gtk_adjustment_new(args->rot_omega*180.0/G_PI,
                                             -180.0, 180.0, 1.0, 30.0, 0.0);
    gwy_table_attach_adjbar(table, row, _("α:"), _("deg"),
                            controls->rot_omega, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls->rot_omega, "value-changed",
                             G_CALLBACK(rot_omega_changed), controls);
    row++;

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 4);

    controls->reset_rotation
        = gtk_button_new_with_mnemonic(_("Re_set Rotation"));
    gtk_box_pack_start(GTK_BOX(hbox), controls->reset_rotation,
                       FALSE, FALSE, 0);
    g_signal_connect_swapped(controls->reset_rotation, "clicked",
                             G_CALLBACK(reset_rotation), controls);

    return vbox;
}

static void
attach_lattice_parameter(GtkTable *table, gint row, gint col,
                         LatticeParameterType paramtype,
                         const gchar *name, gboolean is_angle,
                         FacetsControls *controls)
{
    GtkWidget *label, *entry;

    label = gtk_label_new(name);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, col, col+1, row, row+1, GTK_FILL, 0, 0, 0);
    controls->lattice_label[paramtype] = label;

    entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    gtk_table_attach(table, entry, col+1, col+2, row, row+1, GTK_FILL, 0, 0, 0);
    controls->lattice_entry[paramtype] = entry;

    label = gtk_label_new(is_angle ? _("deg") : NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, col+2, col+3, row, row+1,
                     (is_angle ? GTK_EXPAND : 0) | GTK_FILL, 0, 0, 0);
    controls->lattice_units[paramtype] = label;

    g_object_set_data(G_OBJECT(entry), "id", GUINT_TO_POINTER(paramtype));
    g_signal_connect(entry, "activate",
                     G_CALLBACK(lattice_parameter_changed), controls);
    gwy_widget_set_activate_on_unfocus(entry, TRUE);
}

static GtkWidget*
create_lattice_controls(FacetsControls *controls)
{
    FacetsArgs *args = controls->args;
    GtkWidget *label, *combo, *vbox, *hbox;
    GtkTable *table;
    gint row;

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

    hbox = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic(_("_Lattice type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    combo = gwy_enum_combo_box_newl(G_CALLBACK(lattice_type_changed), controls,
                                    args->lattice_type,
                                    gwy_sgettext("lattice|Cubic"),
                                    LATTICE_CUBIC,
                                    /* FIXME: correct spelling */
                                    gwy_sgettext("lattice|Rhomohedral"),
                                    LATTICE_RHOMBOHEDRAL,
                                    gwy_sgettext("lattice|Hexagonal"),
                                    LATTICE_HEXAGONAL,
                                    gwy_sgettext("lattice|Tetragonal"),
                                    LATTICE_TETRAGONAL,
                                    gwy_sgettext("lattice|Orthorhombic"),
                                    LATTICE_ORTHORHOMBIC,
                                    gwy_sgettext("lattice|Monoclinic"),
                                    LATTICE_MONOCLINIC,
                                    gwy_sgettext("lattice|Triclinic"),
                                    LATTICE_TRICLINIC,
                                    NULL);
    controls->lattice_type = combo;
    gtk_box_pack_start(GTK_BOX(hbox), combo, FALSE, FALSE, 0);

    table = GTK_TABLE(gtk_table_new(4, 6, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(table), FALSE, FALSE, 8);
    row = 0;

    label = gtk_label_new(_("Length"));
    gtk_table_attach(table, label, 1, 2, row, row+1, GTK_FILL, 0, 0, 0);
    label = gtk_label_new(_("Angle"));
    gtk_table_attach(table, label, 4, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    attach_lattice_parameter(table, row, 0, LATTICE_PARAM_A,
                             "a:", FALSE, controls);
    attach_lattice_parameter(table, row, 3, LATTICE_PARAM_ALPHA,
                             "α:", TRUE, controls);
    row++;

    attach_lattice_parameter(table, row, 0, LATTICE_PARAM_B,
                             "b:", FALSE, controls);
    attach_lattice_parameter(table, row, 3, LATTICE_PARAM_BETA,
                             "β:", TRUE, controls);
    row++;

    attach_lattice_parameter(table, row, 0, LATTICE_PARAM_C,
                             "c:", FALSE, controls);
    attach_lattice_parameter(table, row, 3, LATTICE_PARAM_GAMMA,
                             "γ:", TRUE, controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    hbox = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    controls->create = gtk_button_new_with_mnemonic(_("Create _Points"));
    gtk_box_pack_start(GTK_BOX(hbox), controls->create, FALSE, FALSE, 0);
    g_signal_connect_swapped(controls->create, "clicked",
                             G_CALLBACK(create_lattice), controls);
    row++;

    return vbox;
}

static void
create_point_list_column(GtkTreeView *treeview, GtkCellRenderer *renderer,
                         FacetsControls *controls,
                         const gchar *name, const gchar *units,
                         guint facet_column)
{
    GtkTreeViewColumn *column;
    GtkCellLayoutDataFunc cellfunc;
    GtkWidget *label;
    gchar *s;

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_column_set_alignment(column, 0.5);
    g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(facet_column));

    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    if (facet_column == FACET_COLUMN_N)
        cellfunc = render_id;
    else
        cellfunc = render_facet_parameter;
    gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column), renderer,
                                       cellfunc, controls, NULL);

    label = gtk_label_new(NULL);
    if (units && strlen(units))
        s = g_strdup_printf("<b>%s</b> [%s]", name, units);
    else
        s = g_strdup_printf("<b>%s</b>", name);
    gtk_label_set_markup(GTK_LABEL(label), s);
    g_free(s);
    gtk_tree_view_column_set_widget(column, label);
    gtk_widget_show(label);
    gtk_tree_view_append_column(treeview, column);
}

static void
create_point_list(FacetsControls *controls)
{
    GtkTreeView *treeview;
    GtkCellRenderer *renderer;
    GtkTreeSelection *treesel;

    controls->store = gwy_null_store_new(0);
    controls->pointlist
        = gtk_tree_view_new_with_model(GTK_TREE_MODEL(controls->store));
    treeview = GTK_TREE_VIEW(controls->pointlist);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);

    create_point_list_column(treeview, renderer, controls,
                             "n", NULL, FACET_COLUMN_N);
    create_point_list_column(treeview, renderer, controls,
                             "θ", _("deg"), FACET_COLUMN_THETA);
    create_point_list_column(treeview, renderer, controls,
                             "φ", _("deg"), FACET_COLUMN_PHI);
    create_point_list_column(treeview, renderer, controls,
                             "x", NULL, FACET_COLUMN_X);
    create_point_list_column(treeview, renderer, controls,
                             "y", NULL, FACET_COLUMN_Y);
    create_point_list_column(treeview, renderer, controls,
                             "z", NULL, FACET_COLUMN_Z);

    treesel = gtk_tree_view_get_selection(treeview);
    gtk_tree_selection_set_mode(treesel, GTK_SELECTION_BROWSE);
    g_signal_connect(treesel, "changed",
                     G_CALLBACK(point_list_selection_changed), controls);

    g_signal_connect_swapped(treeview, "key-press-event",
                             G_CALLBACK(point_list_key_pressed), controls);

    g_object_unref(controls->store);
}

static void
point_list_selection_changed(GtkTreeSelection *treesel,
                             FacetsControls *controls)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean sens;

    if ((sens = gtk_tree_selection_get_selected(treesel, &model, &iter)))
        gtk_tree_model_get(model, &iter, 0, &controls->selid, -1);
    else
        controls->selid = -1;

    gtk_widget_set_sensitive(controls->delete, sens);
    gtk_widget_set_sensitive(controls->refine, sens);
    gtk_widget_set_sensitive(controls->mark, sens);
}

static void
render_id(G_GNUC_UNUSED GtkCellLayout *layout,
          GtkCellRenderer *renderer,
          GtkTreeModel *model,
          GtkTreeIter *iter,
          G_GNUC_UNUSED gpointer user_data)
{
    gchar buf[16];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    g_snprintf(buf, sizeof(buf), "%d", i + 1);
    g_object_set(renderer, "text", buf, NULL);
}

static inline void
slopes_to_angles(gdouble xder, gdouble yder,
                 gdouble *theta, gdouble *phi)
{
    *phi = atan2(yder, -xder);
    *theta = atan(hypot(xder, yder));
}

/* Transforms (ϑ, φ) to Cartesian selection coordinates [-q,q], which is
 * [-1,1] for the full range of angles. */
static inline void
angles_to_xy(gdouble theta, gdouble phi,
             gdouble *x, gdouble *y)
{
    gdouble rho = G_SQRT2*sin(theta/2.0);
    gdouble c = cos(phi), s = sin(phi);

    *x = rho*c;
    *y = -rho*s;
}

static inline void
xy_to_angles(gdouble x, gdouble y,
             gdouble *theta, gdouble *phi)
{
    gdouble s = hypot(x, y)/G_SQRT2;

    *phi = atan2(-y, x);
    if (s <= 1.0)
        *theta = 2.0*asin(s);
    else
        *theta = G_PI - 2.0*asin(2.0 - s);
}

static void
render_facet_parameter(GtkCellLayout *layout,
                       GtkCellRenderer *renderer,
                       GtkTreeModel *model,
                       GtkTreeIter *iter,
                       gpointer user_data)
{
    FacetsControls *controls = (FacetsControls*)user_data;
    gdouble theta, phi;
    GwyXYZ v;
    gchar buf[16];
    gdouble point[2];
    guint i, id;
    gdouble u;

    id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(layout), "id"));
    gtk_tree_model_get(model, iter, 0, &i, -1);
    gwy_selection_get_object(controls->fselection, i, point);

    xy_to_angles(point[0] - controls->q, point[1] - controls->q, &theta, &phi);
    if (id == FACET_COLUMN_THETA || id == FACET_COLUMN_PHI) {
        u = (id == FACET_COLUMN_THETA) ? theta : phi;
        g_snprintf(buf, sizeof(buf), "%.2f", 180.0/G_PI*u);
    }
    else {
        make_unit_vector(&v, theta, phi);
        if (id == FACET_COLUMN_X)
            u = v.x;
        else if (id == FACET_COLUMN_Y)
            u = v.y;
        else
            u = v.z;
        g_snprintf(buf, sizeof(buf), "%.3f", u);
    }
    g_object_set(renderer, "text", buf, NULL);
}

static void
clear_facet_selection(FacetsControls *controls)
{
    gwy_selection_clear(controls->fselection);
}

static void
delete_facet_selection(FacetsControls *controls)
{
    if (controls->fselection && controls->selid > -1)
        gwy_selection_delete_object(controls->fselection, controls->selid);
}

static void
refine_facet_selection(FacetsControls *controls)
{
    GwyDataField *dist;
    gdouble xy[2], theta, phi, x, y, h;
    gint fres, range;

    if (controls->selid == -1)
        return;
    if (!gwy_selection_get_object(controls->fselection, controls->selid, xy))
        return;

    xy_to_angles(xy[0] - controls->q, xy[1] - controls->q, &theta, &phi);

    dist = gwy_container_get_object_by_name(controls->fdata, "/0/data");
    fres = gwy_data_field_get_xres(dist);
    h = gwy_data_field_get_dx(dist);
    range = GWY_ROUND(fres/controls->q * 0.5/G_SQRT2 * cos(0.5*theta)
                      * controls->args->tolerance);
    x = xy[0]/h;
    y = xy[1]/h;
    gwy_data_field_local_maximum(dist, &x, &y, range, range);
    xy[0] = x*h;
    xy[1] = y*h;

    gwy_selection_set_object(controls->fselection, controls->selid, xy);
}

static void
mark_facet_selection(FacetsControls *controls)
{
    FacetsArgs *args = controls->args;
    GwyDataField *dtheta, *dphi, *mask, *mfield = NULL;
    GwyContainer *data, *fdata;
    gdouble xy[2], theta, phi;

    if (controls->selid == -1)
        return;
    if (!gwy_selection_get_object(controls->fselection, controls->selid, xy))
        return;

    xy_to_angles(xy[0] - controls->q, xy[1] - controls->q, &theta, &phi);
    args->theta0 = theta;
    args->phi0 = phi;

    data = controls->mydata;
    fdata = controls->fdata;

    add_mask_field(GWY_DATA_VIEW(controls->view), NULL);
    add_mask_field(GWY_DATA_VIEW(controls->fview), &mask_color);

    mask = gwy_container_get_object_by_name(data, "/0/mask");
    dtheta = gwy_container_get_object_by_name(fdata, "/theta");
    dphi = gwy_container_get_object_by_name(fdata, "/phi");
    gwy_container_gis_object_by_name(fdata, "/1/mask", (GObject**)&mfield);

    gwy_data_field_mark_facets(dtheta, dphi, theta, phi, args->tolerance, mask);
    if (mfield && args->combine) {
        if (args->combine_type == GWY_MERGE_UNION)
            gwy_data_field_grains_add(mask, mfield);
        else if (args->combine_type == GWY_MERGE_INTERSECTION)
            gwy_data_field_grains_intersect(mask, mfield);
    }
    gwy_data_field_data_changed(mask);
    facets_mark_fdata(args, fdata, controls->q);
    update_average_angle(controls, FALSE);
}

static gboolean
point_list_key_pressed(FacetsControls *controls,
                       GdkEventKey *event)
{
    if (event->keyval == GDK_Delete) {
        delete_facet_selection(controls);
        return TRUE;
    }
    return FALSE;
}

static void
report_style_changed(FacetsControls *controls, GwyResultsExport *rexport)
{
    controls->args->report_style = gwy_results_export_get_format(rexport);
}

static void
number_points_changed(FacetsControls *controls, GtkToggleButton *toggle)
{
    FacetsArgs *args = controls->args;
    GwyVectorLayer *layer;

    args->number_points = gtk_toggle_button_get_active(toggle);
    layer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(controls->fview));
    g_object_set(layer, "point-numbers", args->number_points, NULL);
}

static gchar*
format_facet_table(FacetsControls *controls)
{
    GwyResultsReportType report_style = controls->args->report_style;
    gdouble theta, phi, q, point[2];
    GString *str;
    guint n, i;
    GwyXYZ v;

    if (!(n = gwy_null_store_get_n_rows(controls->store)))
        return NULL;

    q = controls->q;
    str = g_string_new(NULL);

    if (!(report_style & GWY_RESULTS_REPORT_MACHINE)) {
        gwy_format_result_table_strings(str, report_style, 5,
                                        "ϑ [deg]", "φ [deg]", "x", "y", "z");
    }
    else {
        gwy_format_result_table_strings(str, report_style, 5,
                                        "ϑ", "φ", "x", "y", "z");
    }

    for (i = 0; i < n; i++) {
        gwy_selection_get_object(controls->fselection, i, point);
        xy_to_angles(point[0] - q, point[1] - q, &theta, &phi);
        make_unit_vector(&v, theta, phi);
        if (!(report_style & GWY_RESULTS_REPORT_MACHINE)) {
            theta *= 180.0/G_PI;
            phi *= 180.0/G_PI;
        }
        gwy_format_result_table_row(str, report_style, 5,
                                    theta, phi, v.x, v.y, v.z);
    }
    return g_string_free(str, FALSE);
}

static void
copy_facet_table(FacetsControls *controls)
{
    GdkDisplay *display;
    GtkClipboard *clipboard;
    gchar *report;

    if (!(report = format_facet_table(controls)))
        return;
    display = gtk_widget_get_display(controls->dialog);
    clipboard = gtk_clipboard_get_for_display(display,
                                              GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, report, -1);
    g_free(report);
}

static void
save_facet_table(FacetsControls *controls)
{
    gchar *report;

    if (!(report = format_facet_table(controls)))
        return;
    gwy_save_auxiliary_data(_("Save Facet Vectors"),
                            GTK_WINDOW(controls->dialog), -1, report);
    g_free(report);
}

static void
kernel_size_changed(GtkAdjustment *adj, FacetsControls *controls)
{
    GwyDataField *dfield;
    GwySelection *selection;
    gdouble *xy;
    gdouble q;
    guint i, n;

    selection = controls->fselection;
    q = controls->q;
    n = gwy_selection_get_data(selection, NULL);
    xy = g_new(gdouble, 2*n);
    gwy_selection_get_data(selection, xy);
    for (i = 0; i < n; i++)
        xy_to_angles(xy[2*i] - q, xy[2*i+1] - q, xy + 2*i, xy + 2*i+1);

    controls->args->kernel_size = gwy_adjustment_get_int(adj);
    gwy_app_wait_cursor_start(GTK_WINDOW(controls->dialog));
    dfield = gwy_container_get_object_by_name(controls->mydata, "/0/data");
    controls->q = gwy_data_field_facet_distribution(dfield,
                                                    controls->args->kernel_size,
                                                    controls->fdata);
    q = controls->q;

    /* TODO: Handle mask combining options to show the correct mask on the
     * image. */
    if (gwy_container_gis_object_by_name(controls->mydata, "/0/mask",
                                         &dfield)) {
        gwy_data_field_clear(dfield);
        gwy_data_field_data_changed(dfield);
    }
    if (gwy_container_gis_object_by_name(controls->fdata, "/0/mask",
                                         &dfield)) {
        gwy_data_field_clear(dfield);
        gwy_data_field_data_changed(dfield);
    }

    update_theta_range(controls);
    update_average_angle(controls, TRUE);
    if (gwy_selection_get_data(controls->iselection, NULL))
        gwy_selection_clear(controls->iselection);

    for (i = 0; i < n; i++) {
        angles_to_xy(xy[2*i], xy[2*i+1], xy + 2*i, xy + 2*i+1);
        xy[2*i] += q;
        xy[2*i+1] += q;
    }
    gwy_selection_set_data(selection, n, xy);
    g_free(xy);
    gwy_app_wait_cursor_finish(GTK_WINDOW(controls->dialog));
}

static void
update_theta_range(FacetsControls *controls)
{
    gdouble x, y, theta, phi;
    gchar buf[32];

    x = controls->q;
    y = 0.0;
    xy_to_angles(x, y, &theta, &phi);
    g_snprintf(buf, sizeof(buf), "%.1f %s", -180.0/G_PI*theta, _("deg"));
    gtk_label_set_text(GTK_LABEL(controls->theta_min_label), buf);
    g_snprintf(buf, sizeof(buf), "0 %s", _("deg"));
    gtk_label_set_text(GTK_LABEL(controls->theta_0_label), buf);
    g_snprintf(buf, sizeof(buf), "%.1f %s", 180.0/G_PI*theta, _("deg"));
    gtk_label_set_text(GTK_LABEL(controls->theta_max_label), buf);
}

static void
facet_view_select_angle(FacetsControls *controls,
                        gdouble theta,
                        gdouble phi)
{
    gdouble xy[2];
    gint n, i;

    controls->in_update = TRUE;
    angles_to_xy(theta, phi, xy+0, xy+1);
    xy[0] += controls->q;
    xy[1] += controls->q;
    n = gwy_selection_get_data(controls->fselection, NULL);
    i = (!n || controls->selid == -1) ? n : controls->selid;
    gwy_selection_set_object(controls->fselection, i, xy);
    controls->in_update = FALSE;
}

static void
facet_view_selection_updated(GwySelection *selection,
                             gint hint,
                             FacetsControls *controls)
{
    GtkTreeSelection *treesel;
    GtkTreeIter iter;
    gint i, n, nold;

    n = gwy_selection_get_data(selection, NULL);
    nold = gwy_null_store_get_n_rows(controls->store);
    if (hint == -1 || n != nold) {
        gwy_null_store_set_n_rows(controls->store, n);
        if (n == nold+1)
            hint = n-1;
        n = MIN(n, nold);
        for (i = 0; i < n; i++)
            gwy_null_store_row_changed(controls->store, i);
    }
    else {
        g_return_if_fail(hint >= 0);
        gwy_null_store_row_changed(controls->store, hint);
    }

    treesel = gtk_tree_view_get_selection(GTK_TREE_VIEW(controls->pointlist));
    if (hint != controls->selid) {
        if (hint >= 0) {
            gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(controls->store),
                                          &iter, NULL, hint);
            gtk_tree_selection_select_iter(treesel, &iter);
        }
        else
            gtk_tree_selection_unselect_all(treesel);
    }

    if (!controls->in_update) {
        controls->in_update = TRUE;
        if (gwy_selection_get_data(controls->iselection, NULL))
            gwy_selection_clear(controls->iselection);
        controls->in_update = FALSE;
    }

    /* The user can either control the points using the shader (rotation) or by
     * moving the points.  These are exclusive. If we are not rotating, always
     * save the current selection as the base for the rotation.  When we are
     * rotating, do not touch fselection0. */
    if (!controls->is_rotating) {
        gwy_selection_assign(controls->fselection0, controls->fselection);
        controls->in_update = TRUE;
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->rot_theta), 0.0);
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->rot_phi), 0.0);
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->rot_omega), 0.0);
        controls->in_update = FALSE;
    }
}

static void
update_average_angle(FacetsControls *controls, gboolean clearme)
{
    GwyDataField *dtheta, *dphi, *mask;
    gdouble theta, phi;
    gchar *s;

    if (!clearme && controls->selid > -1) {
        dtheta = gwy_container_get_object_by_name(controls->fdata, "/theta");
        dphi = gwy_container_get_object_by_name(controls->fdata, "/phi");
        mask = gwy_container_get_object_by_name(controls->mydata, "/0/mask");
        calculate_average_angle(dtheta, dphi, mask, &theta, &phi);

        s = g_strdup_printf(_("θ = %.2f deg, φ = %.2f deg"),
                            180.0/G_PI*theta, 180.0/G_PI*phi);
        gtk_label_set_text(GTK_LABEL(controls->mangle_label), s);
        g_free(s);
    }
    else {
        gtk_label_set_text(GTK_LABEL(controls->mangle_label), "");
    }
}

static void
preview_selection_updated(GwySelection *selection,
                          G_GNUC_UNUSED gint id,
                          FacetsControls *controls)
{
    GwyDataField *dfield;
    gdouble theta, phi, xy[2];
    gint i, j;

    if (controls->in_update)
        return;

    dfield = gwy_container_get_object_by_name(controls->mydata, "/0/data");
    if (!gwy_selection_get_object(selection, 0, xy))
        return;

    j = gwy_data_field_rtoj(dfield, xy[0]);
    i = gwy_data_field_rtoi(dfield, xy[1]);
    dfield = gwy_container_get_object_by_name(controls->fdata, "/theta");
    theta = gwy_data_field_get_val(dfield, j, i);
    dfield = gwy_container_get_object_by_name(controls->fdata, "/phi");
    phi = gwy_data_field_get_val(dfield, j, i);
    facet_view_select_angle(controls, theta, phi);
}

static void
run_noninteractive(FacetsArgs *args,
                   GwyContainer *data,
                   GwyDataField *dtheta,
                   GwyDataField *dphi,
                   GwyDataField *dfield,
                   GwyDataField *mfield,
                   GQuark mquark,
                   gdouble theta,
                   gdouble phi)
{
    GwyDataField *mask;

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    mask = gwy_data_field_new_alike(dfield, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mask), NULL);

    gwy_data_field_mark_facets(dtheta, dphi, theta, phi, args->tolerance, mask);
    if (mfield && args->combine) {
        if (args->combine_type == GWY_MERGE_UNION)
            gwy_data_field_grains_add(mfield, mask);
        else if (args->combine_type == GWY_MERGE_INTERSECTION)
            gwy_data_field_grains_intersect(mfield, mask);
        gwy_data_field_data_changed(mfield);
    }
    else if (mfield) {
        gwy_data_field_copy(mask, mfield, FALSE);
        gwy_data_field_data_changed(mfield);
    }
    else {
        gwy_container_set_object(data, mquark, mask);
    }
    g_object_unref(mask);
}

static void
gwy_data_field_mark_facets(GwyDataField *dtheta,
                           GwyDataField *dphi,
                           gdouble theta0,
                           gdouble phi0,
                           gdouble tolerance,
                           GwyDataField *mask)
{
    gdouble cr, cth0, sth0, cro;
    const gdouble *td, *fd;
    gdouble *md;
    guint i, n;

    cr = cos(tolerance);
    cth0 = cos(theta0);
    sth0 = sin(theta0);

    td = gwy_data_field_get_data_const(dtheta);
    fd = gwy_data_field_get_data_const(dphi);
    md = gwy_data_field_get_data(mask);
    n = gwy_data_field_get_xres(dtheta)*gwy_data_field_get_yres(dtheta);
    for (i = n; i; i--, td++, fd++, md++) {
        cro = cth0*cos(*td) + sth0*sin(*td)*cos(*fd - phi0);
        *md = (cro >= cr);
    }
}

static void
calculate_average_angle(GwyDataField *dtheta,
                        GwyDataField *dphi,
                        GwyDataField *mask,
                        gdouble *theta,
                        gdouble *phi)
{
    GwyXYZ s, v;
    const gdouble *td, *pd, *md;
    gint i;

    td = gwy_data_field_get_data_const(dtheta);
    pd = gwy_data_field_get_data_const(dphi);
    md = gwy_data_field_get_data_const(mask);
    gwy_clear(&s, 1);
    for (i = gwy_data_field_get_xres(dtheta)*gwy_data_field_get_yres(dtheta);
         i;
         i--, td++, pd++, md++) {
        if (!*md)
            continue;

        make_unit_vector(&v, *td, *pd);
        s.x += v.x;
        s.y += v.y;
        s.z += v.z;
    }

    vector_angles(&s, theta, phi);
}

static gdouble
gwy_data_field_facet_distribution(GwyDataField *dfield,
                                  gint half_size,
                                  GwyContainer *container)
{
    GwyDataField *dtheta, *dphi, *dist;
    gdouble *xd, *yd, *data;
    const gdouble *xdc, *ydc;
    gdouble q, x, y;
    gint hres, i, xres, yres, n, fres;

    if (gwy_container_gis_object_by_name(container, "/theta", &dtheta))
        g_object_ref(dtheta);
    else
        dtheta = gwy_data_field_new_alike(dfield, FALSE);

    if (gwy_container_gis_object_by_name(container, "/phi", &dphi))
        g_object_ref(dphi);
    else
        dphi = gwy_data_field_new_alike(dfield, FALSE);

    compute_slopes(dfield, 2*half_size + 1, dtheta, dphi);
    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    xd = gwy_data_field_get_data(dtheta);
    yd = gwy_data_field_get_data(dphi);
    n = xres*yres;

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(xd,yd,n)
#endif
    for (i = 0; i < n; i++) {
        gdouble theta, phi;

        slopes_to_angles(xd[i], yd[i], &theta, &phi);
        xd[i] = theta;
        yd[i] = phi;
    }
    q = gwy_data_field_get_max(dtheta);
    q = MIN(q*1.05, 1.001*G_PI/2.0);
    q = G_SQRT2*sin(q/2.0);

    hres = GWY_ROUND(cbrt(3.49*n));
    fres = 2*hres + 1;
    dist = gwy_data_field_new(fres, fres, 2.0*q, 2.0*q, TRUE);
    gwy_data_field_set_xoffset(dist, -q);
    gwy_data_field_set_yoffset(dist, -q);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dist), NULL);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dist), NULL);

    data = gwy_data_field_get_data(dist);
    xdc = gwy_data_field_get_data_const(dtheta);
    ydc = gwy_data_field_get_data_const(dphi);
    for (i = n; i; i--, xdc++, ydc++) {
        gint xx, yy;

        angles_to_xy(*xdc, *ydc, &x, &y);
        x = (x + q)/q*hres;
        y = (y + q)/q*hres;
        xx = (gint)floor(x - 0.5);
        yy = (gint)floor(y - 0.5);

        if (G_UNLIKELY(xx < 0)) {
            xx = 0;
            x = 0.0;
        }
        else if (G_UNLIKELY(xx >= fres-1)) {
            xx = fres-2;
            x = 1.0;
        }
        else
            x = x - (xx + 0.5);

        if (G_UNLIKELY(yy < 0)) {
            yy = 0;
            y = 0.0;
        }
        else if (G_UNLIKELY(yy >= fres-1)) {
            yy = fres-2;
            y = 1.0;
        }
        else
            y = y - (yy + 0.5);

        data[yy*fres + xx] += (1.0 - x)*(1.0 - y);
        data[yy*fres + xx+1] += x*(1.0 - y);
        data[yy*fres+fres + xx] += (1.0 - x)*y;
        data[yy*fres+fres + xx+1] += x*y;
    }

    /* Transform values for visualisation. */
    for (i = fres*fres; i; i--, data++)
        *data = cbrt(*data);

    gwy_container_set_object_by_name(container, "/0/data", dist);
    g_object_unref(dist);
    gwy_container_set_object_by_name(container, "/theta", dtheta);
    g_object_unref(dtheta);
    gwy_container_set_object_by_name(container, "/phi", dphi);
    g_object_unref(dphi);
    gwy_container_set_const_string_by_name(container, "/0/base/palette",
                                           FVIEW_GRADIENT);

    return q;
}

static void
compute_slopes(GwyDataField *dfield,
               gint kernel_size,
               GwyDataField *xder,
               GwyDataField *yder)
{
    gint xres, yres;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    if (kernel_size > 1) {
        GwyPlaneFitQuantity quantites[] = {
            GWY_PLANE_FIT_BX, GWY_PLANE_FIT_BY
        };
        GwyDataField *fields[2];

        fields[0] = xder;
        fields[1] = yder;
        gwy_data_field_fit_local_planes(dfield, kernel_size,
                                        2, quantites, fields);

        gwy_data_field_multiply(xder, xres/gwy_data_field_get_xreal(dfield));
        gwy_data_field_multiply(yder, yres/gwy_data_field_get_yreal(dfield));
    }
    else
        gwy_data_field_filter_slope(dfield, xder, yder);
}

static void
facets_tolerance_changed(FacetsControls *controls, GtkAdjustment *adj)
{
    FacetsArgs *args = controls->args;
    args->tolerance = G_PI/180.0 * gtk_adjustment_get_value(adj);
}

static void
combine_changed(FacetsControls *controls, GtkToggleButton *toggle)
{
    controls->args->combine = gtk_toggle_button_get_active(toggle);
}

static void
combine_type_changed(FacetsControls *controls)
{
    FacetsArgs *args = controls->args;
    args->combine_type = gwy_radio_buttons_get_current(controls->combine_type);
}

static void
update_latice_params(FacetsControls *controls)
{
    FacetsArgs *args = controls->args;
    LatticeType lattice_type = args->lattice_type;
    gchar buf[24];
    gdouble v;
    guint i;

    conform_to_lattice_type(args->lattice_params, lattice_type);
    g_assert(!controls->in_update);
    controls->in_update = TRUE;
    for (i = 0; i < LATTICE_PARAM_NPARAMS; i++) {
        /* Update all because we need to normalise the nonsense the user
         * entered as well. */
        v = args->lattice_params[i];
        if (i >= LATTICE_PARAM_ALPHA)
            v *= 180.0/G_PI;
        g_snprintf(buf, sizeof(buf), "%g", v);
        gtk_entry_set_text(GTK_ENTRY(controls->lattice_entry[i]), buf);
    }
    controls->in_update = FALSE;
}

static void
lattice_type_changed(GtkComboBox *combo, FacetsControls *controls)
{
    LatticeType lattice_type = gwy_enum_combo_box_get_active(combo);
    guint i, indep_params = lattice_indep_params[lattice_type];
    FacetsArgs *args = controls->args;
    gboolean sens;

    args->lattice_type = lattice_type;
    for (i = 0; i < LATTICE_PARAM_NPARAMS; i++) {
        sens = (indep_params & (1 << i));
        gtk_widget_set_sensitive(controls->lattice_label[i], sens);
        gtk_widget_set_sensitive(controls->lattice_entry[i], sens);
        gtk_widget_set_sensitive(controls->lattice_units[i], sens);
    }
    update_latice_params(controls);
}

static void
lattice_parameter_changed(GtkEntry *entry, FacetsControls *controls)
{
    FacetsArgs *args = controls->args;
    guint indep_params = lattice_indep_params[args->lattice_type];
    LatticeParameterType paramtype;
    const gchar *value;
    gchar *endp;
    gdouble v;

    if (controls->in_update)
        return;

    paramtype = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(entry), "id"));
    if (indep_params & (1 << paramtype)) {
        value = gtk_entry_get_text(entry);
        v = g_strtod(value, &endp);
        if (v != 0.0 && endp != value) {
            if (paramtype >= LATTICE_PARAM_ALPHA) {
                v *= G_PI/180.0;
                v = CLAMP(v, 0.001, G_PI-0.001);
            }
            else {
                v = CLAMP(v, 1e-38, 1e38);
            }
            args->lattice_params[paramtype] = v;
            update_latice_params(controls);
        }
    }
}

static gint
gcd(gint a, gint b)
{
    a = ABS(a);
    b = ABS(b);
    if (a < b)
        GWY_SWAP(gint, a, b);

    /* This also handles that gcd(x, 0) = x, by definition. */
    while (b) {
        a %= b;
        GWY_SWAP(gint, a, b);
    }

    return a;
}

static gint
gcd3(gint a, gint b, gint c)
{
    return gcd(gcd(a, b), c);
}

static void
create_lattice(FacetsControls *controls)
{
    FacetsArgs *args = controls->args;
    GwyXYZ a, b, c, ia, ib, ic, v;
    gint i, j, k, f;
    GArray *array;
    gdouble q, theta, phi, xy[2];

    q = controls->q;
    make_lattice_vectors(args->lattice_params, &a, &b, &c);
    make_inverse_lattice(&a, &b, &c, &ia, &ib, &ic);
    array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    /* FIXME: Let the user control this somehow.  Also the default rules which
     * points to include may not be always useful...
     * We may also want to special-case hexagonal lattices. */
    for (i = -2; i <= 2; i++) {
        for (j = -2; j <= 2; j++) {
            for (k = -2; k <= 2; k++) {
                f = ABS(i) + ABS(j) + ABS(k);
                /* Omit zero vector. */
                if (!f)
                    continue;
                /* Omit planes with too high indices. */
                if (f > 2)
                    continue;
                /* Omit planes clearly from below. */
                if (i < 0)
                    continue;
                /* Omit planes with the same direction as other planes. */
                if (gcd3(i, j, k) != 1)
                    continue;

                v.x = i*ia.x + j*ib.x + k*ic.x;
                v.y = i*ia.y + j*ib.y + k*ic.y;
                v.z = i*ia.z + j*ib.z + k*ic.z;
                vector_angles(&v, &theta, &phi);
                angles_to_xy(theta, phi, xy+0, xy+1);
                xy[0] += q;
                xy[1] += q;
                g_array_append_vals(array, xy, 2);
            }
        }
    }

    gwy_selection_set_data(controls->fselection,
                           array->len/2, (gdouble*)array->data);
    g_array_free(array, TRUE);
}

static void
apply_facet_selection_rotation(FacetsControls *controls)
{
    FacetsArgs *args = controls->args;
    gdouble rot_theta, rot_phi, rot_omega, theta, phi, q;
    guint n, i;
    gdouble *xy;
    GwyXYZ v;

    n = gwy_selection_get_data(controls->fselection0, NULL);
    if (!n)
        return;

    g_return_if_fail(gwy_selection_get_data(controls->fselection, NULL) == n);
    controls->is_rotating = TRUE;

    rot_theta = args->rot_theta;
    rot_phi = args->rot_phi;
    rot_omega = args->rot_omega;
    q = controls->q;
    xy = g_new(gdouble, 2*n);
    gwy_selection_get_data(controls->fselection0, xy);

    for (i = 0; i < n; i++ ) {
        xy_to_angles(xy[2*i] - q, xy[2*i+1] - q, &theta, &phi);
        make_unit_vector(&v, theta, phi);
        rotate_vector(&v, rot_omega, rot_theta, rot_phi);
        vector_angles(&v, &theta, &phi);
        angles_to_xy(theta, phi, xy + 2*i, xy + 2*i+1);
        xy[2*i] += q;
        xy[2*i+1] += q;
    }
    gwy_selection_set_data(controls->fselection, n, xy);
    g_free(xy);

    controls->is_rotating = FALSE;
}

static void
rot_shader_changed(FacetsControls *controls, GwyShader *shader)
{
    gdouble theta, phi;

    if (controls->in_update)
        return;

    theta = 180.0/G_PI*gwy_shader_get_theta(shader);
    phi = 180.0/G_PI*gwy_shader_get_phi(shader);
    if (phi > 180.0)
        phi -= 360.0;

    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->rot_theta), theta);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->rot_phi), phi);
}

static void
rot_theta_changed(FacetsControls *controls, GtkAdjustment *adj)
{
    FacetsArgs *args = controls->args;

    args->rot_theta = G_PI/180.0*gtk_adjustment_get_value(adj);
    if (controls->in_update)
        return;
    controls->in_update = TRUE;
    gwy_shader_set_theta(GWY_SHADER(controls->shader), args->rot_theta);
    controls->in_update = FALSE;
    apply_facet_selection_rotation(controls);
}

static void
rot_phi_changed(FacetsControls *controls, GtkAdjustment *adj)
{
    FacetsArgs *args = controls->args;

    args->rot_phi = G_PI/180.0*gtk_adjustment_get_value(adj);
    if (controls->in_update)
        return;
    controls->in_update = TRUE;
    gwy_shader_set_phi(GWY_SHADER(controls->shader), args->rot_phi);
    controls->in_update = FALSE;
    apply_facet_selection_rotation(controls);
}

static void
rot_omega_changed(FacetsControls *controls, GtkAdjustment *adj)
{
    FacetsArgs *args = controls->args;

    args->rot_omega = G_PI/180.0*gtk_adjustment_get_value(adj);
    if (controls->in_update)
        return;
    apply_facet_selection_rotation(controls);
}

static void
reset_rotation(FacetsControls *controls)
{
    controls->in_update = TRUE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->rot_theta), 0.0);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->rot_phi), 0.0);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->rot_omega), 0.0);
    controls->in_update = FALSE;
    apply_facet_selection_rotation(controls);
}

static void
add_mask_field(GwyDataView *view,
               const GwyRGBA *color)
{
    GwyContainer *data;
    GwyDataField *mfield, *dfield;

    data = gwy_data_view_get_data(view);
    if (gwy_container_gis_object_by_name(data, "/0/mask", &mfield))
        return;

    gwy_container_gis_object_by_name(data, "/0/data", &dfield);
    mfield = gwy_data_field_new_alike(dfield, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mfield), NULL);
    gwy_container_set_object_by_name(data, "/0/mask", mfield);
    g_object_unref(mfield);
    if (color)
        gwy_rgba_store_to_container(color, data, "/0/mask");
}

static void
facets_mark_fdata(FacetsArgs *args,
                  GwyContainer *fdata,
                  gdouble q)
{
    GwyDataField *mask;
    gdouble r, r2, cr, cro, cth0, sth0, cphi0, sphi0;
    gint fres, hres, i, j;
    gdouble *m;

    cr = cos(args->tolerance);
    cth0 = cos(args->theta0);
    sth0 = sin(args->theta0);
    cphi0 = cos(args->phi0);
    sphi0 = sin(args->phi0);
    mask = gwy_container_get_object_by_name(fdata, "/0/mask");
    fres = gwy_data_field_get_xres(mask);
    hres = (fres - 1)/2;
    m = gwy_data_field_get_data(mask);

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i,j,r2,r,cro) \
            shared(m,fres,hres,cth0,sth0,cphi0,sphi0,q,cr)
#endif
    for (i = 0; i < fres; i++) {
        gdouble y = -q*(i/(gdouble)hres - 1.0);

        for (j = 0; j < fres; j++) {
            gdouble x = q*(j/(gdouble)hres - 1.0);

            /**
             * Orthodromic distance computed directly from x, y:
             * cos(theta) = 1 - r^2
             * sin(theta) = r*sqrt(1 - r^2/2)
             * cos(phi) = x/r
             * sin(phi) = y/r
             * where r = hypot(x, y)
             **/
            r2 = x*x + y*y;
            r = sqrt(r2);
            cro = cth0*(1.0 - r2)
                  + sth0*G_SQRT2*r*sqrt(1.0 - r2/2.0)*(x/r*cphi0 + y/r*sphi0);
            m[i*fres + j] = (cro >= cr);
        }
    }
    gwy_data_field_data_changed(mask);
}

static void
conform_to_lattice_type(gdouble *params, LatticeType type)
{
    if (type == LATTICE_CUBIC) {
        params[LATTICE_PARAM_B] = params[LATTICE_PARAM_A];
        params[LATTICE_PARAM_C] = params[LATTICE_PARAM_A];
        params[LATTICE_PARAM_ALPHA] = 0.5*G_PI;
        params[LATTICE_PARAM_BETA] = 0.5*G_PI;
        params[LATTICE_PARAM_GAMMA] = 0.5*G_PI;
    }
    else if (type == LATTICE_RHOMBOHEDRAL) {
        params[LATTICE_PARAM_B] = params[LATTICE_PARAM_A];
        params[LATTICE_PARAM_C] = params[LATTICE_PARAM_A];
        params[LATTICE_PARAM_ALPHA] = 0.5*G_PI;
        params[LATTICE_PARAM_BETA] = 0.5*G_PI;
    }
    else if (type == LATTICE_HEXAGONAL) {
        params[LATTICE_PARAM_B] = params[LATTICE_PARAM_A];
        params[LATTICE_PARAM_ALPHA] = 0.5*G_PI;
        params[LATTICE_PARAM_BETA] = 0.5*G_PI;
        params[LATTICE_PARAM_GAMMA] = 2.0*G_PI/3.0;
    }
    else if (type == LATTICE_TETRAGONAL) {
        params[LATTICE_PARAM_B] = params[LATTICE_PARAM_A];
        params[LATTICE_PARAM_ALPHA] = 0.5*G_PI;
        params[LATTICE_PARAM_BETA] = 0.5*G_PI;
        params[LATTICE_PARAM_GAMMA] = 0.5*G_PI;
    }
    else if (type == LATTICE_ORTHORHOMBIC) {
        params[LATTICE_PARAM_ALPHA] = 0.5*G_PI;
        params[LATTICE_PARAM_BETA] = 0.5*G_PI;
        params[LATTICE_PARAM_GAMMA] = 0.5*G_PI;
    }
    else if (type == LATTICE_MONOCLINIC) {
        params[LATTICE_PARAM_B] = params[LATTICE_PARAM_A];
        params[LATTICE_PARAM_ALPHA] = 0.5*G_PI;
        params[LATTICE_PARAM_GAMMA] = 0.5*G_PI;
    }
    else {
        g_assert(type == LATTICE_TRICLINIC);
    }
}

/* Make lattice vectors with @a oriented along the z axis.  Maybe we want @c
 * along the z axis.  Maybe we want to choose -- this can be done by cyclic
 * rotations of (A, B, C) and (γ, α, β). */
static void
make_lattice_vectors(const gdouble *params, GwyXYZ *a, GwyXYZ *b, GwyXYZ *c)
{
    gdouble calpha = cos(params[LATTICE_PARAM_ALPHA]);
    gdouble cbeta = cos(params[LATTICE_PARAM_BETA]);
    gdouble sbeta = sin(params[LATTICE_PARAM_BETA]);
    gdouble cgamma = cos(params[LATTICE_PARAM_GAMMA]);
    gdouble sgamma = sin(params[LATTICE_PARAM_GAMMA]);
    gdouble cphi, sphi;

    a->x = a->y = b->y = 0.0;
    a->z = 1.0;
    b->x = sgamma;
    b->z = cgamma;
    cphi = (calpha - cgamma*cbeta)/(sgamma*sbeta);
    /* FIXME: Check sign acording to handeness. */
    sphi = sqrt(CLAMP(1.0 - cphi*cphi, 0.0, 1.0));
    c->x = cphi*sbeta;
    c->y = sphi*sbeta;
    c->z = cbeta;

    a->x *= params[LATTICE_PARAM_A];
    a->y *= params[LATTICE_PARAM_A];
    a->z *= params[LATTICE_PARAM_A];
    b->x *= params[LATTICE_PARAM_B];
    b->y *= params[LATTICE_PARAM_B];
    b->z *= params[LATTICE_PARAM_B];
    c->x *= params[LATTICE_PARAM_C];
    c->y *= params[LATTICE_PARAM_C];
    c->z *= params[LATTICE_PARAM_C];
}

static inline void
vector_product(const GwyXYZ *a, const GwyXYZ *b, GwyXYZ *r)
{
    gdouble x = a->y*b->z - a->z*b->y;
    gdouble y = a->z*b->x - a->x*b->z;
    gdouble z = a->x*b->y - a->y*b->x;

    r->x = x;
    r->y = y;
    r->z = z;
}

/* NB: We do not care about absolute length because at the end we reduce
 * the vectors to directions.  So we can avoid the 2π/Det(a,b,c) factor
 * and hence never produce infinities. */
static void
make_inverse_lattice(const GwyXYZ *a, const GwyXYZ *b, const GwyXYZ *c,
                     GwyXYZ *ia, GwyXYZ *ib, GwyXYZ *ic)
{
    vector_product(a, b, ic);
    vector_product(b, c, ia);
    vector_product(c, a, ib);
}

/* Rotate coordinate system around the z axis by ω, then rotate the z axis
 * straight to the direction given by ϑ and φ. */
static inline void
rotate_vector(GwyXYZ *v, gdouble omega, gdouble theta, gdouble phi)
{
    gdouble c, s, v1, v2;

    c = cos(omega - phi);
    s = sin(omega - phi);
    v1 = c*v->x - s*v->y;
    v2 = s*v->x + c*v->y;
    v->x = v1;
    v->y = v2;

    c = cos(theta);
    s = sin(theta);
    v1 = c*v->x + s*v->z;
    v2 = -s*v->x + c*v->z;
    v->x = v1;
    v->z = v2;

    c = cos(phi);
    s = sin(phi);
    v1 = c*v->x - s*v->y;
    v2 = s*v->x + c*v->y;
    v->x = v1;
    v->y = v2;
}

static inline void
make_unit_vector(GwyXYZ *v, gdouble theta, gdouble phi)
{
    v->x = sin(theta)*cos(phi);
    v->y = sin(theta)*sin(phi);
    v->z = cos(theta);
}

static inline void
vector_angles(const GwyXYZ *v, gdouble *theta, gdouble *phi)
{
    *theta = atan2(sqrt(v->x*v->x + v->y*v->y), v->z);
    *phi = atan2(v->y, v->x);
}

static const gchar combine_key[]       = "/module/facet_analysis/combine";
static const gchar combine_type_key[]  = "/module/facet_analysis/combine_type";
static const gchar kernel_size_key[]   = "/module/facet_analysis/kernel-size";
static const gchar lattice_a_key[]     = "/module/facet_analysis/lattice_a";
static const gchar lattice_alpha_key[] = "/module/facet_analysis/lattice_alpha";
static const gchar lattice_beta_key[]  = "/module/facet_analysis/lattice_beta";
static const gchar lattice_b_key[]     = "/module/facet_analysis/lattice_b";
static const gchar lattice_c_key[]     = "/module/facet_analysis/lattice_c";
static const gchar lattice_gamma_key[] = "/module/facet_analysis/lattice_gamma";
static const gchar lattice_type_key[]  = "/module/facet_analysis/lattice_type";
static const gchar number_points_key[] = "/module/facet_analysis/number_points";
static const gchar phi0_key[]          = "/module/facet_analysis/phi0";
static const gchar report_style_key[]  = "/module/facet_analysis/report_style";
static const gchar theta0_key[]        = "/module/facet_analysis/theta0";
static const gchar tolerance_key[]     = "/module/facet_analysis/tolerance";

static void
facets_sanitize_args(FacetsArgs *args)
{
    args->combine = !!args->combine;
    args->number_points = !!args->number_points;
    args->tolerance = CLAMP(args->tolerance, 0.0, 30.0*G_PI/180.0);
    args->phi0 = fmod(args->phi0, 2.0*G_PI);
    args->theta0 = CLAMP(args->theta0, 0.0, 0.5*G_PI);
    args->kernel_size = CLAMP(args->kernel_size, 0, MAX_PLANE_SIZE);
    args->combine_type = MIN(args->combine_type, GWY_MERGE_INTERSECTION);
    args->lattice_type = MIN(args->lattice_type, LATTICE_NTYPES-1);
    args->lattice_params[LATTICE_PARAM_A]
        = CLAMP(args->lattice_params[LATTICE_PARAM_A], 1e-38, 1e38);
    args->lattice_params[LATTICE_PARAM_B]
        = CLAMP(args->lattice_params[LATTICE_PARAM_B], 1e-38, 1e38);
    args->lattice_params[LATTICE_PARAM_C]
        = CLAMP(args->lattice_params[LATTICE_PARAM_C], 1e-38, 1e38);
    args->lattice_params[LATTICE_PARAM_ALPHA]
        = CLAMP(args->lattice_params[LATTICE_PARAM_ALPHA], 0.001, G_PI-0.001);
    args->lattice_params[LATTICE_PARAM_BETA]
        = CLAMP(args->lattice_params[LATTICE_PARAM_BETA], 0.001, G_PI-0.001);
    args->lattice_params[LATTICE_PARAM_GAMMA]
        = CLAMP(args->lattice_params[LATTICE_PARAM_GAMMA], 0.001, G_PI-0.001);
}

static void
facets_load_args(GwyContainer *container, FacetsArgs *args)
{
    gdouble *lattice_params = args->lattice_params;
    *args = facets_defaults;

    gwy_container_gis_boolean_by_name(container, combine_key, &args->combine);
    gwy_container_gis_boolean_by_name(container, number_points_key,
                                      &args->number_points);
    gwy_container_gis_double_by_name(container, tolerance_key,
                                     &args->tolerance);
    gwy_container_gis_double_by_name(container, phi0_key, &args->phi0);
    gwy_container_gis_double_by_name(container, theta0_key, &args->theta0);
    gwy_container_gis_int32_by_name(container, kernel_size_key,
                                    &args->kernel_size);
    gwy_container_gis_enum_by_name(container, combine_type_key,
                                   &args->combine_type);
    gwy_container_gis_enum_by_name(container, lattice_type_key,
                                   &args->lattice_type);
    gwy_container_gis_double_by_name(container, lattice_a_key,
                                     lattice_params + LATTICE_PARAM_A);
    gwy_container_gis_double_by_name(container, lattice_b_key,
                                     lattice_params + LATTICE_PARAM_B);
    gwy_container_gis_double_by_name(container, lattice_c_key,
                                     lattice_params + LATTICE_PARAM_C);
    gwy_container_gis_double_by_name(container, lattice_alpha_key,
                                     lattice_params + LATTICE_PARAM_ALPHA);
    gwy_container_gis_double_by_name(container, lattice_beta_key,
                                     lattice_params + LATTICE_PARAM_BETA);
    gwy_container_gis_double_by_name(container, lattice_gamma_key,
                                     lattice_params + LATTICE_PARAM_GAMMA);
    gwy_container_gis_enum_by_name(container, report_style_key,
                                   &args->report_style);
    facets_sanitize_args(args);
}

static void
facets_save_args(GwyContainer *container, FacetsArgs *args)
{
    gdouble *lattice_params = args->lattice_params;

    gwy_container_set_boolean_by_name(container, combine_key, args->combine);
    gwy_container_set_boolean_by_name(container, number_points_key,
                                      args->number_points);
    gwy_container_set_double_by_name(container, tolerance_key,
                                     args->tolerance);
    gwy_container_set_double_by_name(container, phi0_key, args->phi0);
    gwy_container_set_double_by_name(container, theta0_key, args->theta0);
    gwy_container_set_int32_by_name(container, kernel_size_key,
                                    args->kernel_size);
    gwy_container_set_enum_by_name(container, combine_type_key,
                                   args->combine_type);
    gwy_container_set_enum_by_name(container, lattice_type_key,
                                   args->lattice_type);
    gwy_container_set_double_by_name(container, lattice_a_key,
                                     lattice_params[LATTICE_PARAM_A]);
    gwy_container_set_double_by_name(container, lattice_b_key,
                                     lattice_params[LATTICE_PARAM_B]);
    gwy_container_set_double_by_name(container, lattice_c_key,
                                     lattice_params[LATTICE_PARAM_C]);
    gwy_container_set_double_by_name(container, lattice_alpha_key,
                                     lattice_params[LATTICE_PARAM_ALPHA]);
    gwy_container_set_double_by_name(container, lattice_beta_key,
                                     lattice_params[LATTICE_PARAM_BETA]);
    gwy_container_set_double_by_name(container, lattice_gamma_key,
                                     lattice_params[LATTICE_PARAM_GAMMA]);
    gwy_container_set_enum_by_name(container, report_style_key,
                                   args->report_style);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
