/*
 *  $Id: correct_affine.c 24626 2022-03-03 12:59:05Z yeti-dn $
 *  Copyright (C) 2013-2017 David Necas (Yeti).
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

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/correct.h>
#include <libprocess/elliptic.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define AFFINE_RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    USER_DEFINED_LATTICE = -1,
};

enum {
    SENS_USER_LATTICE = 1,
    SENS_DIFFERENT_LENGTHS = 2,
    SENS_VALID_LATTICE = 4,
};

enum {
    INVALID_A1 = 1,
    INVALID_A2 = 2,
    INVALID_PHI = 4,
    INVALID_SEL = 8,
};

typedef enum {
    IMAGE_DATA,
    IMAGE_ACF,
    IMAGE_CORRECTED,
} ImageMode;

/* Keep it simple and use a predefined set of zooms, these seem suitable. */
typedef enum {
    ZOOM_1 = 1,
    ZOOM_4 = 4,
    ZOOM_16 = 16
} ZoomType;

typedef struct {
    gdouble a1;
    gdouble a2;
    gdouble phi;
} LatticePreset;

typedef struct {
    gdouble a1;
    gdouble a2;
    gdouble phi;
    gboolean different_lengths;
    gboolean distribute;
    gboolean fix_hacf;
    GwyInterpolationType interp;
    GwyAffineScalingType scaling;
    gint preset;

    ZoomType zoom;
    ImageMode image_mode;
} AffcorArgs;

typedef struct {
    AffcorArgs *args;
    GwySensitivityGroup *sens;
    GtkWidget *dialog;
    GtkWidget *view;
    GwyVectorLayer *vlayer;
    GwySelection *selection;
    GwyContainer *mydata;
    GSList *zoom;
    GSList *image_mode;
    GtkWidget *acffield;
    GtkWidget *interp;
    GtkWidget *scaling;
    GtkWidget *distribute;
    GtkWidget *fix_hacf;
    GwySIValueFormat *vf;
    GwySIValueFormat *vfphi;
    /* Actual */
    GtkWidget *a1_x;
    GtkWidget *a1_y;
    GtkWidget *a1_len;
    GtkWidget *a1_phi;
    GtkWidget *a2_x;
    GtkWidget *a2_y;
    GtkWidget *a2_len;
    GtkWidget *a2_phi;
    GtkWidget *phi;
    GtkWidget *preset;
    gdouble xy[4];
    /* Correct (wanted) */
    GtkWidget *a1_corr;
    GtkWidget *different_lengths;
    GtkWidget *a2_corr;
    GtkWidget *phi_corr;
    GwySelection *selection_corr;
    guint invalid_corr;
    gboolean calculated;
    gulong recalculate_id;
} AffcorControls;

static gboolean      module_register          (void);
static void          correct_affine           (GwyContainer *data,
                                               GwyRunType run);
static gint          affcor_dialog            (AffcorArgs *args,
                                               GwyContainer *data,
                                               GwyDataField *dfield,
                                               gint id,
                                               gdouble *a1a2);
static GtkWidget*    make_lattice_table       (AffcorControls *controls);
static GtkWidget*    add_lattice_entry        (GtkTable *table,
                                               const gchar *name,
                                               gdouble value,
                                               GwySensitivityGroup *sens,
                                               guint flags,
                                               gint *row,
                                               GwySIValueFormat *vf);
static gboolean      filter_acffield          (GwyContainer *data,
                                               gint id,
                                               gpointer user_data);
static void          a1_changed_manually      (GtkEntry *entry,
                                               AffcorControls *controls);
static void          a2_changed_manually      (GtkEntry *entry,
                                               AffcorControls *controls);
static void          init_selection           (AffcorControls *controls);
static void          image_mode_changed       (GtkToggleButton *button,
                                               AffcorControls *controls);
static void          zoom_changed             (GtkRadioButton *button,
                                               AffcorControls *controls);
static void          preset_changed           (GtkComboBox *combo,
                                               AffcorControls *controls);
static void          a1_changed               (AffcorControls *controls,
                                               GtkEntry *entry);
static void          a2_changed               (AffcorControls *controls,
                                               GtkEntry *entry);
static void          phi_changed              (AffcorControls *controls,
                                               GtkEntry *entry);
static void          acffield_changed         (AffcorControls *controls,
                                               GwyDataChooser *chooser);
static void          calculate_acffield_full  (AffcorControls *controls,
                                               GwyDataField *dfield);
static GwyDataField* get_full_acffield        (AffcorControls *controls);
static void          calculate_acffield       (AffcorControls *controls);
static void          different_lengths_changed(AffcorControls *controls,
                                               GtkToggleButton *toggle);
static void          distribute_changed       (AffcorControls *controls,
                                               GtkToggleButton *toggle);
static void          fix_hacf_changed         (AffcorControls *controls,
                                               GtkToggleButton *toggle);
static void          refine                   (AffcorControls *controls);
static void          do_estimate              (AffcorControls *controls);
static void          selection_changed        (AffcorControls *controls);
static void          interp_changed           (GtkComboBox *combo,
                                               AffcorControls *controls);
static void          scaling_changed          (GtkComboBox *combo,
                                               AffcorControls *controls);
static void          invalidate               (AffcorControls *controls);
static gboolean      recalculate              (gpointer user_data);
static void          do_correction            (AffcorControls *controls);
static void          fill_correct_vectors     (const AffcorArgs *args,
                                               gdouble *a1a2);
static GwyDataField* create_corrected_dfield  (GwyDataField *dfield,
                                               const gdouble *a1a2,
                                               gdouble *a1a2_corr,
                                               GwyInterpolationType interp,
                                               GwyAffineScalingType scaling);
static void          affcor_load_args         (GwyContainer *container,
                                               AffcorArgs *args);
static void          affcor_save_args         (GwyContainer *container,
                                               AffcorArgs *args);
static void          affcor_sanitize_args     (AffcorArgs *args);

static const AffcorArgs affcor_defaults = {
    1.0, 1.0, 90.0, FALSE,
    FALSE, FALSE,
    GWY_INTERPOLATION_LINEAR, GWY_AFFINE_SCALING_AS_GIVEN,
    -1,
    ZOOM_1, IMAGE_DATA,
};

static const LatticePreset lattice_presets[] = {
    { 2.46e-10, 2.46e-10, G_PI/3.0 },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Corrects affine distortion of images by matching image Bravais "
       "lattice to the true one."),
    "Yeti <yeti@gwyddion.net>",
    "2.1",
    "David Nečas (Yeti)",
    "2013",
};

GWY_MODULE_QUERY2(module_info, correct_affine)

static gboolean
module_register(void)
{
    gwy_process_func_register("correct_affine",
                              (GwyProcessFunc)&correct_affine,
                              N_("/_Distortion/_Affine..."),
                              GWY_STOCK_CORRECT_AFFINE,
                              AFFINE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Correct affine distortion"));

    return TRUE;
}

static void
correct_affine(GwyContainer *data, GwyRunType run)
{
    const guint compat_flags = (GWY_DATA_COMPATIBILITY_RES
                                | GWY_DATA_COMPATIBILITY_REAL
                                | GWY_DATA_COMPATIBILITY_LATERAL);
    AffcorArgs args;
    GwyDataField *dfield, *ofield, *corrected;
    gint id, newid, corrid;
    gint *all_channels;
    gdouble a1a2_corr[4], a1a2[4];
    GType type;
    GwySelection *selection;
    gchar *s, *t;
    GQuark quark;
    guint i;

    g_return_if_fail(run & AFFINE_RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerLattice"));
    affcor_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(dfield);

    newid = affcor_dialog(&args, data, dfield, id, a1a2);
    affcor_save_args(gwy_app_settings_get(), &args);
    if (newid == -1)
        return;

    gwy_app_channel_log_add_proc(data, id, newid);

    if (!args.distribute)
        return;

    all_channels = gwy_app_data_browser_get_data_ids(data);
    type = g_type_from_name("GwySelectionLattice");
    for (i = 0; all_channels[i] != -1; i++) {
        if (all_channels[i] == id || all_channels[i] == newid)
            continue;

        quark = gwy_app_get_data_key_for_id(all_channels[i]);
        ofield = gwy_container_get_object(data, quark);
        if (gwy_data_field_check_compatibility(dfield, ofield, compat_flags))
            continue;

        fill_correct_vectors(&args, a1a2_corr);
        corrected = create_corrected_dfield(ofield, a1a2, a1a2_corr,
                                            args.interp, args.scaling);
        corrid = gwy_app_data_browser_add_data_field(corrected, data, FALSE);
        gwy_app_sync_data_items(data, data, all_channels[i], corrid, FALSE,
                                GWY_DATA_ITEM_RANGE_TYPE,
                                GWY_DATA_ITEM_RANGE,
                                GWY_DATA_ITEM_GRADIENT,
                                0);
        g_object_unref(corrected);

        selection = g_object_new(type, NULL);
        gwy_selection_set_data(selection, 1, a1a2_corr);
        s = g_strdup_printf("/%d/select/lattice", corrid);
        gwy_container_set_object_by_name(data, s, selection);
        g_object_unref(selection);
        g_free(s);

        s = gwy_app_get_data_field_title(data, all_channels[i]);
        t = g_strconcat(s, " ", _("Corrected"), NULL);
        quark = gwy_app_get_data_title_key_for_id(corrid);
        gwy_container_set_string(data, quark, (const guchar*)t);
        g_free(s);

        gwy_app_channel_log_add_proc(data, all_channels[i], corrid);
    }

    g_free(all_channels);
}

static gint
affcor_dialog(AffcorArgs *args,
              GwyContainer *data,
              GwyDataField *dfield,
              gint id,
              gdouble *a1a2)
{
    GtkWidget *hbox, *hbox2, *label, *button, *lattable, *alignment;
    GtkDialog *dialog;
    GtkTable *table;
    GSList *l;
    GwyDataField *corrected;
    AffcorControls controls;
    gint response, row, newid = -1;
    GObject *selection;
    gchar selkey[40];
    guint flags;
    gchar *s, *t;

    gwy_clear(&controls, 1);
    controls.args = args;
    controls.sens = gwy_sensitivity_group_new();

    controls.dialog = gtk_dialog_new_with_buttons(_("Affine Correction"),
                                                  NULL, 0, NULL);
    dialog = GTK_DIALOG(controls.dialog);
    gtk_dialog_add_button(dialog, _("_Reset"), RESPONSE_RESET);
    gtk_dialog_add_button(dialog, gwy_sgettext("verb|_Estimate"),
                          RESPONSE_ESTIMATE);
    gtk_dialog_add_button(dialog, _("_Refine"), RESPONSE_REFINE);
    gtk_dialog_add_button(dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    button = gtk_dialog_add_button(dialog, GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    gwy_sensitivity_group_add_widget(controls.sens, button,
                                     SENS_VALID_LATTICE);

    hbox = gtk_hbox_new(FALSE, 2);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    controls.mydata = gwy_container_new();
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    calculate_acffield_full(&controls, dfield);
    gwy_app_sync_data_items(data, controls.mydata, id, 1, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    alignment = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 4);

    controls.view = gwy_create_preview(controls.mydata, 0, PREVIEW_SIZE, FALSE);
    controls.selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(controls.view),
                                                         0, "Lattice", 1, TRUE);
    g_object_ref(controls.selection);
    controls.vlayer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(controls.view));
    g_object_ref(controls.vlayer);
    g_signal_connect_swapped(controls.selection, "changed",
                             G_CALLBACK(selection_changed), &controls);

    gtk_container_add(GTK_CONTAINER(alignment), controls.view);

    table = GTK_TABLE(gtk_table_new(20, 4, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(table), TRUE, TRUE, 0);
    row = 0;

    label = gwy_label_new_header(_("Preview Options"));
    gtk_table_attach(GTK_TABLE(table), label, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new(_("Display:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.image_mode
        = gwy_radio_buttons_createl(G_CALLBACK(image_mode_changed), &controls,
                                    args->image_mode,
                                    _("_Data"), IMAGE_DATA,
                                    _("2D _ACF"), IMAGE_ACF,
                                    _("Correc_ted data"), IMAGE_CORRECTED,
                                    NULL);
    row = gwy_radio_buttons_attach_to_table(controls.image_mode,
                                            table, 4, row);
    button = gwy_radio_buttons_find(controls.image_mode, IMAGE_CORRECTED);
    gwy_sensitivity_group_add_widget(controls.sens, button,
                                     SENS_VALID_LATTICE);

    hbox2 = gtk_hbox_new(FALSE, 8);
    gtk_table_attach(GTK_TABLE(table), hbox2, 0, 4, row, row+1,
                     GTK_FILL, 0, 0, 0);
    label = gtk_label_new(_("ACF zoom:"));
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, FALSE, 0);

    controls.zoom
        = gwy_radio_buttons_createl(G_CALLBACK(zoom_changed), &controls,
                                    args->zoom,
                                    "1×", ZOOM_1,
                                    "4×", ZOOM_4,
                                    "16×", ZOOM_16,
                                    NULL);
    for (l = controls.zoom; l; l = g_slist_next(l)) {
        GtkWidget *widget = GTK_WIDGET(l->data);
        gtk_box_pack_start(GTK_BOX(hbox2), widget, FALSE, FALSE, 0);
    }
    row++;

    controls.fix_hacf
        = gtk_check_button_new_with_mnemonic(_("Interpolate _horizontal ACF"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.fix_hacf),
                                 args->fix_hacf);
    gtk_table_attach(table, controls.fix_hacf,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.fix_hacf, "toggled",
                             G_CALLBACK(fix_hacf_changed), &controls);
    row++;

    gtk_table_set_row_spacing(table, row-1, 8);
    label = gwy_label_new_header(_("Lattice Vectors"));
    gtk_table_attach(table, label, 0, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.vf
        = gwy_data_field_get_value_format_xy(dfield,
                                             GWY_SI_UNIT_FORMAT_MARKUP, NULL);
    controls.vf->precision += 2;

    controls.vfphi = gwy_si_unit_value_format_new(G_PI/180.0, 2, _("deg"));

    lattable = make_lattice_table(&controls);
    gtk_table_attach(table, lattable, 0, 5, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_table_set_row_spacing(table, row, 8);
    row++;

    /* TRANSLATORS: Correct is an adjective here. */
    label = gwy_label_new_header(_("Correct Lattice"));
    gtk_table_attach(table, label, 0, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Lattice type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);

    controls.preset
        = gwy_enum_combo_box_newl(G_CALLBACK(preset_changed), &controls,
                                  args->preset,
                                  _("User defined"), USER_DEFINED_LATTICE,
                                  "HOPG", 0,
                                  NULL);
    gtk_table_attach(table, controls.preset,
                     2, 5, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.preset);
    row++;

    controls.a1_corr = add_lattice_entry(table, "a<sub>1</sub>:", args->a1,
                                         controls.sens, SENS_USER_LATTICE,
                                         &row, controls.vf);
    g_signal_connect_swapped(controls.a1_corr, "changed",
                             G_CALLBACK(a1_changed), &controls);

    controls.different_lengths
        = gtk_check_button_new_with_mnemonic(_("_Different lengths"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.different_lengths),
                                 args->different_lengths);
    gwy_sensitivity_group_add_widget(controls.sens, controls.different_lengths,
                                     SENS_USER_LATTICE);
    gtk_table_attach(table, controls.different_lengths,
                     3, 5, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.different_lengths, "toggled",
                             G_CALLBACK(different_lengths_changed), &controls);

    controls.a2_corr = add_lattice_entry(table, "a<sub>2</sub>:", args->a2,
                                         controls.sens,
                                         SENS_USER_LATTICE
                                         | SENS_DIFFERENT_LENGTHS,
                                         &row, controls.vf);
    g_signal_connect_swapped(controls.a2_corr, "changed",
                             G_CALLBACK(a2_changed), &controls);

    controls.phi_corr = add_lattice_entry(table, "ϕ:", args->phi,
                                          controls.sens, SENS_USER_LATTICE,
                                          &row, controls.vfphi);
    g_signal_connect_swapped(controls.phi_corr, "changed",
                             G_CALLBACK(phi_changed), &controls);
    gtk_table_set_row_spacing(table, row-1, 8);

    label = gwy_label_new_header(_("Options"));
    gtk_table_attach(table, label, 0, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("Image for _ACF:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);

    controls.acffield = gwy_data_chooser_new_channels();
    gwy_data_chooser_set_filter(GWY_DATA_CHOOSER(controls.acffield),
                                filter_acffield, &controls, NULL);
    gwy_data_chooser_set_active(GWY_DATA_CHOOSER(controls.acffield), data, id);
    gtk_table_attach(table, controls.acffield,
                     2, 5, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.acffield, "changed",
                             G_CALLBACK(acffield_changed), &controls);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Interpolation type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);

    controls.interp
        = gwy_enum_combo_box_new(gwy_interpolation_type_get_enum(), -1,
                                 G_CALLBACK(interp_changed), &controls,
                                 args->interp, TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.interp);
    gtk_table_attach(table, controls.interp,
                     2, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Scaling:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);

    controls.scaling
        = gwy_enum_combo_box_newl(G_CALLBACK(scaling_changed), &controls,
                                  args->scaling,
                                  _("Exactly as specified"),
                                  GWY_AFFINE_SCALING_AS_GIVEN,
                                  _("Preserve area"),
                                  GWY_AFFINE_SCALING_PRESERVE_AREA,
                                  _("Preserve X scale"),
                                  GWY_AFFINE_SCALING_PRESERVE_X,
                                  NULL);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.scaling);
    gtk_table_attach(table, controls.scaling,
                     2, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.distribute
        = gtk_check_button_new_with_mnemonic(_("_Apply to all "
                                               "compatible images"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.distribute),
                                 args->distribute);
    gtk_table_attach(table, controls.distribute,
                     0, 4, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.distribute, "toggled",
                             G_CALLBACK(distribute_changed), &controls);
    row++;

    g_snprintf(selkey, sizeof(selkey), "/%d/select/lattice", id);
    if (gwy_container_gis_object_by_name(data, selkey, &selection)
        && gwy_selection_get_data(GWY_SELECTION(selection), NULL) == 1)
        gwy_selection_assign(controls.selection, selection);
    else
        do_estimate(&controls);

    controls.selection_corr = gwy_selection_duplicate(controls.selection);

    flags = args->different_lengths ? SENS_DIFFERENT_LENGTHS : 0;
    gwy_sensitivity_group_set_state(controls.sens,
                                    SENS_DIFFERENT_LENGTHS, flags);
    preset_changed(GTK_COMBO_BOX(controls.preset), &controls);

    gtk_widget_show_all(controls.dialog);
    do {
        response = gtk_dialog_run(dialog);
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(controls.dialog);
            case GTK_RESPONSE_NONE:
            goto finalize;
            break;

            case GTK_RESPONSE_OK:
            break;

            case RESPONSE_RESET:
            init_selection(&controls);
            break;

            case RESPONSE_ESTIMATE:
            do_estimate(&controls);
            break;

            case RESPONSE_REFINE:
            refine(&controls);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    if (!controls.calculated)
        do_correction(&controls);

    gwy_selection_get_object(controls.selection, 0, a1a2);
    corrected = gwy_container_get_object_by_name(controls.mydata, "/2/data");
    newid = gwy_app_data_browser_add_data_field(corrected, data, TRUE);
    s = gwy_app_get_data_field_title(data, id);
    t = g_strconcat(s, " ", _("Corrected"), NULL);
    gwy_container_set_string(data, gwy_app_get_data_title_key_for_id(newid),
                             (const guchar*)t);
    g_free(s);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_GRADIENT,
                            0);

    g_snprintf(selkey, sizeof(selkey), "/%d/select/lattice", newid);
    gwy_container_set_object_by_name(data, selkey, controls.selection_corr);

    gtk_widget_destroy(controls.dialog);

finalize:
    g_snprintf(selkey, sizeof(selkey), "/%d/select/lattice", id);
    selection = gwy_serializable_duplicate(G_OBJECT(controls.selection));
    gwy_container_set_object_by_name(data, selkey, selection);
    g_object_unref(selection);
    g_object_unref(controls.selection);
    g_object_unref(controls.selection_corr);

    if (controls.recalculate_id)
        g_source_remove(controls.recalculate_id);
    g_object_unref(controls.vlayer);
    g_object_unref(controls.sens);
    gwy_si_unit_value_format_free(controls.vf);
    gwy_si_unit_value_format_free(controls.vfphi);
    g_object_unref(controls.mydata);

    return newid;
}

static GtkWidget*
make_lattice_table(AffcorControls *controls)
{
    GtkWidget *table, *label, *entry;
    GString *str = g_string_new(NULL);

    table = gtk_table_new(4, 5, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);

    /* header row */
    g_string_assign(str, "x");
    if (strlen(controls->vf->units))
        g_string_append_printf(str, " [%s]", controls->vf->units);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), str->str);
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, 0, 1, GTK_FILL, 0, 0, 0);

    g_string_assign(str, "y");
    if (strlen(controls->vf->units))
        g_string_append_printf(str, " [%s]", controls->vf->units);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), str->str);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1, GTK_FILL, 0, 0, 0);

    g_string_assign(str, _("length"));
    if (strlen(controls->vf->units))
        g_string_append_printf(str, " [%s]", controls->vf->units);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), str->str);
    gtk_table_attach(GTK_TABLE(table), label, 3, 4, 0, 1, GTK_FILL, 0, 0, 0);

    g_string_assign(str, _("angle"));
    if (strlen(controls->vfphi->units))
        g_string_append_printf(str, " [%s]", controls->vfphi->units);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), str->str);
    gtk_table_attach(GTK_TABLE(table), label, 4, 5, 0, 1, GTK_FILL, 0, 0, 0);

    /* a1 */
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "a<sub>1</sub>:");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2, GTK_FILL, 0, 0, 0);

    controls->a1_x = entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    g_object_set_data(G_OBJECT(entry), "id", (gpointer)"x");
    gwy_widget_set_activate_on_unfocus(entry, TRUE);
    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, 1, 2,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(a1_changed_manually), controls);

    controls->a1_y = entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    g_object_set_data(G_OBJECT(entry), "id", (gpointer)"y");
    gwy_widget_set_activate_on_unfocus(entry, TRUE);
    gtk_table_attach(GTK_TABLE(table), entry, 2, 3, 1, 2,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(a1_changed_manually), controls);

    controls->a1_len = entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    g_object_set_data(G_OBJECT(entry), "id", (gpointer)"len");
    gwy_widget_set_activate_on_unfocus(entry, TRUE);
    gtk_table_attach(GTK_TABLE(table), entry, 3, 4, 1, 2,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(a1_changed_manually), controls);

    controls->a1_phi = entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    g_object_set_data(G_OBJECT(entry), "id", (gpointer)"phi");
    gwy_widget_set_activate_on_unfocus(entry, TRUE);
    gtk_table_attach(GTK_TABLE(table), entry, 4, 5, 1, 2,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(a1_changed_manually), controls);

    /* a2 */
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "a<sub>2</sub>:");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3, GTK_FILL, 0, 0, 0);

    controls->a2_x = entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    g_object_set_data(G_OBJECT(entry), "id", (gpointer)"x");
    gwy_widget_set_activate_on_unfocus(entry, TRUE);
    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, 2, 3,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(a2_changed_manually), controls);

    controls->a2_y = entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    g_object_set_data(G_OBJECT(entry), "id", (gpointer)"y");
    gwy_widget_set_activate_on_unfocus(entry, TRUE);
    gtk_table_attach(GTK_TABLE(table), entry, 2, 3, 2, 3,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(a2_changed_manually), controls);

    controls->a2_len = entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    g_object_set_data(G_OBJECT(entry), "id", (gpointer)"len");
    gwy_widget_set_activate_on_unfocus(entry, TRUE);
    gtk_table_attach(GTK_TABLE(table), entry, 3, 4, 2, 3,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(a2_changed_manually), controls);

    controls->a2_phi = entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 8);
    g_object_set_data(G_OBJECT(entry), "id", (gpointer)"phi");
    gwy_widget_set_activate_on_unfocus(entry, TRUE);
    gtk_table_attach(GTK_TABLE(table), entry, 4, 5, 2, 3,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(entry, "activate",
                     G_CALLBACK(a2_changed_manually), controls);

    /* phi */
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), "ϕ:");
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 3, 4, 3, 4, GTK_FILL, 0, 0, 0);

    controls->phi = label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), .0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 4, 5, 3, 4, GTK_FILL, 0, 0, 0);

    g_string_free(str, TRUE);

    return table;
}

static GtkWidget*
add_lattice_entry(GtkTable *table,
                  const gchar *name,
                  gdouble value,
                  GwySensitivityGroup *sens,
                  guint flags,
                  gint *row,
                  GwySIValueFormat *vf)
{
    GtkWidget *label, *entry;
    gchar *buf;

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), name);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, *row, *row+1, GTK_FILL, 0, 0, 0);
    gwy_sensitivity_group_add_widget(sens, label, flags);

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), vf->units);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 2, 3, *row, *row+1, GTK_FILL, 0, 0, 0);
    gwy_sensitivity_group_add_widget(sens, label, flags);

    entry = gtk_entry_new();
    buf = g_strdup_printf("%g", value);
    gtk_entry_set_text(GTK_ENTRY(entry), buf);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 6);
    g_free(buf);
    gtk_table_attach(table, entry,
                     1, 2, *row, *row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gwy_sensitivity_group_add_widget(sens, entry, flags);

    (*row)++;

    return entry;
}

static gboolean
filter_acffield(GwyContainer *data, gint id, gpointer user_data)
{
    AffcorControls *controls = (AffcorControls*)user_data;
    GwyDataField *dfield, *acffield;
    gdouble r;

    dfield = gwy_container_get_object_by_name(controls->mydata, "/0/data");
    acffield = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));
    /* Do not check value, we may want to align channels of a different
     * physical quantity.  But check order-of-magnitude pixel size for
     * elementary sanity. */
    if (gwy_data_field_check_compatibility(dfield, acffield,
                                           GWY_DATA_COMPATIBILITY_LATERAL))
        return FALSE;

    r = (gwy_data_field_get_dx(dfield)
         /gwy_data_field_get_dx(acffield));
    if (r > 16.0 || r < 1.0/16.0)
        return FALSE;

    r = (gwy_data_field_get_dy(dfield)
         /gwy_data_field_get_dy(acffield));
    if (r > 16.0 || r < 1.0/16.0)
        return FALSE;

    return TRUE;
}

static void
a1_changed_manually(GtkEntry *entry,
                    AffcorControls *controls)
{
    GwySIValueFormat *vf = controls->vf;
    gdouble x, y, len, phi;
    const gchar *id, *text;
    gdouble value;

    id = g_object_get_data(G_OBJECT(entry), "id");
    text = gtk_entry_get_text(GTK_ENTRY(entry));
    value = g_strtod(text, NULL);

    x = controls->xy[0];
    y = -controls->xy[1];
    len = hypot(x, y);
    phi = atan2(y, x);
    if (gwy_strequal(id, "x"))
        controls->xy[0] = vf->magnitude * value;
    else if (gwy_strequal(id, "y"))
        controls->xy[1] = vf->magnitude * -value;
    else if (gwy_strequal(id, "len")) {
        controls->xy[0] = vf->magnitude * value * cos(phi);
        controls->xy[1] = vf->magnitude * value * -sin(phi);
    }
    else if (gwy_strequal(id, "phi")) {
        phi = G_PI/180.0 * value;
        controls->xy[0] = len * cos(phi);
        controls->xy[1] = len * -sin(phi);
    }

    /* This actually recalculates everything.  But it does not activate
     * entries so we will not recurse. */
    gwy_selection_set_data(controls->selection, 1, controls->xy);
}

static void
a2_changed_manually(GtkEntry *entry,
                    AffcorControls *controls)
{
    GwySIValueFormat *vf = controls->vf;
    gdouble x, y, len, phi;
    const gchar *id, *text;
    gdouble value;

    id = g_object_get_data(G_OBJECT(entry), "id");
    text = gtk_entry_get_text(GTK_ENTRY(entry));
    value = g_strtod(text, NULL);

    x = controls->xy[2];
    y = -controls->xy[3];
    len = hypot(x, y);
    phi = atan2(y, x);
    if (gwy_strequal(id, "x"))
        controls->xy[2] = vf->magnitude * value;
    else if (gwy_strequal(id, "y"))
        controls->xy[3] = vf->magnitude * -value;
    else if (gwy_strequal(id, "len")) {
        controls->xy[2] = vf->magnitude * value * cos(phi);
        controls->xy[3] = vf->magnitude * value * -sin(phi);
    }
    else if (gwy_strequal(id, "phi")) {
        phi = G_PI/180.0 * value;
        controls->xy[2] = len * cos(phi);
        controls->xy[3] = len * -sin(phi);
    }

    /* This actually recalculates everything.  But it does not activate
     * entries so we will not recurse. */
    gwy_selection_set_data(controls->selection, 1, controls->xy);
}

static void
init_selection(AffcorControls *controls)
{
    GwyDataField *dfield;
    gdouble xy[4] = { 0.0, 0.0, 0.0, 0.0 };

    dfield = gwy_container_get_object_by_name(controls->mydata, "/0/data");
    xy[0] = dfield->xreal/20;
    xy[3] = -dfield->yreal/20;
    gwy_selection_set_data(controls->selection, 1, xy);
}

static void
image_mode_changed(G_GNUC_UNUSED GtkToggleButton *button,
                   AffcorControls *controls)
{
    AffcorArgs *args = controls->args;
    GwyDataView *dataview;
    GwyPixmapLayer *layer;
    ImageMode mode;

    mode = gwy_radio_buttons_get_current(controls->image_mode);
    if (mode == args->image_mode)
        return;
    args->image_mode = mode;
    dataview = GWY_DATA_VIEW(controls->view);
    layer = gwy_data_view_get_base_layer(dataview);

    if (args->image_mode == IMAGE_DATA) {
        g_object_set(layer,
                     "data-key", "/0/data",
                     "range-type-key", "/0/base/range-type",
                     "min-max-key", "/0/base",
                     NULL);
        if (!gwy_data_view_get_top_layer(dataview))
            gwy_data_view_set_top_layer(dataview, controls->vlayer);
    }
    else if (args->image_mode == IMAGE_ACF) {
        /* There are no range-type and min-max keys, which is the point,
         * because we want full-colour-scale ACF, whatever the image has set. */
        g_object_set(layer,
                     "data-key", "/1/data",
                     "range-type-key", "/1/base/range-type",
                     "min-max-key", "/1/base",
                     NULL);
        if (!gwy_data_view_get_top_layer(dataview))
            gwy_data_view_set_top_layer(dataview, controls->vlayer);
    }
    else if (args->image_mode == IMAGE_CORRECTED) {
        if (!controls->calculated)
            do_correction(controls);
        g_object_set(layer,
                     "data-key", "/2/data",
                     "range-type-key", "/0/base/range-type",
                     "min-max-key", "/0/base",
                     NULL);
        gwy_data_view_set_top_layer(dataview, NULL);
    }

    gwy_set_data_preview_size(dataview, PREVIEW_SIZE);
}

static void
zoom_changed(GtkRadioButton *button,
             AffcorControls *controls)
{
    AffcorArgs *args = controls->args;
    ZoomType zoom = gwy_radio_buttons_get_current(controls->zoom);

    if (button && zoom == args->zoom)
        return;

    args->zoom = zoom;
    if (args->image_mode != IMAGE_ACF)
        return;

    calculate_acffield(controls);
}

static void
preset_changed(GtkComboBox *combo,
               AffcorControls *controls)
{
    AffcorArgs *args = controls->args;
    const LatticePreset *preset;
    gboolean different_lengths;
    GString *str;

    args->preset = gwy_enum_combo_box_get_active(combo);
    if (args->preset == USER_DEFINED_LATTICE) {
        gwy_sensitivity_group_set_state(controls->sens,
                                        SENS_USER_LATTICE, SENS_USER_LATTICE);
        return;
    }

    preset = lattice_presets + args->preset;
    different_lengths = (preset->a1 != preset->a2);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->different_lengths),
                                 different_lengths);

    str = g_string_new(NULL);
    g_string_printf(str, "%g", preset->a1/controls->vf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->a1_corr), str->str);
    g_string_printf(str, "%g", preset->a2/controls->vf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->a2_corr), str->str);
    g_string_printf(str, "%g", preset->phi/controls->vfphi->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->phi_corr), str->str);
    g_string_free(str, TRUE);

    gwy_sensitivity_group_set_state(controls->sens, SENS_USER_LATTICE, 0);
}

static void
a1_changed(AffcorControls *controls,
           GtkEntry *entry)
{
    AffcorArgs *args = controls->args;
    const gchar *buf;
    guint flags;

    buf = gtk_entry_get_text(entry);
    args->a1 = g_strtod(buf, NULL) * controls->vf->magnitude;
    if (args->a1 > 0.0)
        controls->invalid_corr &= ~INVALID_A1;
    else
        controls->invalid_corr |= INVALID_A1;

    if (!args->different_lengths)
        gtk_entry_set_text(GTK_ENTRY(controls->a2_corr), buf);

    flags = controls->invalid_corr ? 0 : SENS_VALID_LATTICE;
    gwy_sensitivity_group_set_state(controls->sens, SENS_VALID_LATTICE, flags);
    invalidate(controls);
}

static void
a2_changed(AffcorControls *controls,
           GtkEntry *entry)
{
    AffcorArgs *args = controls->args;
    const gchar *buf;
    guint flags;

    buf = gtk_entry_get_text(entry);
    args->a2 = g_strtod(buf, NULL) * controls->vf->magnitude;
    if (args->a2 > 0.0)
        controls->invalid_corr &= ~INVALID_A2;
    else
        controls->invalid_corr |= INVALID_A2;

    flags = controls->invalid_corr ? 0 : SENS_VALID_LATTICE;
    gwy_sensitivity_group_set_state(controls->sens, SENS_VALID_LATTICE, flags);
    invalidate(controls);
}

static void
phi_changed(AffcorControls *controls,
            GtkEntry *entry)
{
    AffcorArgs *args = controls->args;
    const gchar *buf;
    guint flags;

    buf = gtk_entry_get_text(entry);
    args->phi = g_strtod(buf, NULL)*G_PI/180.0;
    if (args->phi > 1e-3 && args->phi < G_PI - 1e-3)
        controls->invalid_corr &= ~INVALID_PHI;
    else
        controls->invalid_corr |= INVALID_PHI;

    flags = controls->invalid_corr ? 0 : SENS_VALID_LATTICE;
    gwy_sensitivity_group_set_state(controls->sens, SENS_VALID_LATTICE, flags);
    invalidate(controls);
}

static void
acffield_changed(AffcorControls *controls,
                 GwyDataChooser *chooser)
{
    GwyContainer *data;
    GwyDataField *dfield;
    gint id;

    data = gwy_data_chooser_get_active(chooser, &id);
    g_return_if_fail(data);
    dfield = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));
    calculate_acffield_full(controls, dfield);
}

static void
calculate_acffield_full(AffcorControls *controls,
                        GwyDataField *dfield)
{
    GwyDataField *acf, *mid, *mask;
    GwyDataLine *hacf;
    guint acfwidth, acfheight;

    dfield = gwy_data_field_duplicate(dfield);
    gwy_data_field_add(dfield, -gwy_data_field_get_avg(dfield));
    acf = gwy_data_field_new_alike(dfield, FALSE);
    acfwidth = MIN(MAX(dfield->xres/4, 64), dfield->xres/2);
    acfheight = MIN(MAX(dfield->yres/4, 64), dfield->yres/2);
    gwy_data_field_area_2dacf(dfield, acf, 0, 0, dfield->xres, dfield->yres,
                              acfwidth, acfheight);
    g_object_unref(dfield);
    gwy_container_set_object_by_name(controls->mydata, "/1/data/full", acf);
    g_object_unref(acf);

    /* Remember the middle row as we may replace it. */
    acfheight = gwy_data_field_get_yres(acf);
    acfwidth = gwy_data_field_get_xres(acf);
    hacf = gwy_data_line_new(acfwidth, 1.0, FALSE);
    gwy_data_field_get_row(acf, hacf, acfheight/2);
    gwy_container_set_object_by_name(controls->mydata, "/1/hacf", hacf);
    g_object_unref(hacf);

    /* Remember interpolated middle row. */
    mid = gwy_data_field_area_extract(acf, 0, acfheight/2-1, acfwidth, 3);
    mask = gwy_data_field_new(acfwidth, 3, acfwidth, 3, TRUE);
    gwy_data_field_area_fill(mask, 0, 1, acfwidth, 1, 1.0);
    gwy_data_field_set_val(mask, acfwidth/2, 1, 0.0);
    gwy_data_field_laplace_solve(mid, mask, -1, 1.0);
    hacf = gwy_data_line_new(acfwidth, 1.0, FALSE);
    gwy_data_field_get_row(mid, hacf, 1);
    gwy_container_set_object_by_name(controls->mydata, "/1/hacf-fixed", hacf);
    g_object_unref(hacf);
    g_object_unref(mask);
    g_object_unref(mid);

    calculate_acffield(controls);
}

static GwyDataField*
get_full_acffield(AffcorControls *controls)
{
    GwyDataField *acf;
    GwyDataLine *hacf;
    guint yres;
    const gchar *key;

    acf = GWY_DATA_FIELD(gwy_container_get_object_by_name(controls->mydata,
                                                          "/1/data/full"));
    yres = gwy_data_field_get_yres(acf);

    if (controls->args->fix_hacf)
        key = "/1/hacf-fixed";
    else
        key = "/1/hacf";

    hacf = GWY_DATA_LINE(gwy_container_get_object_by_name(controls->mydata,
                                                          key));
    gwy_data_field_set_row(acf, hacf, yres/2);

    return acf;
}

static void
calculate_acffield(AffcorControls *controls)
{
    ZoomType zoom = controls->args->zoom;
    GwyDataField *acf;
    guint xres, yres, width, height;

    acf = get_full_acffield(controls);
    xres = gwy_data_field_get_xres(acf);
    yres = gwy_data_field_get_yres(acf);

    if (zoom != ZOOM_1) {
        width = (xres/zoom) | 1;
        height = (yres/zoom) | 1;

        if (width < 17)
            width = MAX(width, MIN(17, xres));

        if (height < 17)
            height = MAX(height, MIN(17, yres));

        acf = gwy_data_field_area_extract(acf,
                                          (xres - width)/2, (yres - height)/2,
                                          width, height);
        gwy_data_field_set_xoffset(acf, -0.5*acf->xreal);
        gwy_data_field_set_yoffset(acf, -0.5*acf->yreal);
    }
    gwy_container_set_object_by_name(controls->mydata, "/1/data", acf);
    gwy_data_field_data_changed(acf);

    if (controls->args->image_mode == IMAGE_ACF)
        gwy_set_data_preview_size(GWY_DATA_VIEW(controls->view), PREVIEW_SIZE);
}

static void
different_lengths_changed(AffcorControls *controls,
                          GtkToggleButton *toggle)
{
    AffcorArgs *args = controls->args;
    guint flags;

    args->different_lengths = gtk_toggle_button_get_active(toggle);
    if (!args->different_lengths) {
        const gchar *buf = gtk_entry_get_text(GTK_ENTRY(controls->a1_corr));
        gtk_entry_set_text(GTK_ENTRY(controls->a2_corr), buf);
    }
    flags = args->different_lengths ? SENS_DIFFERENT_LENGTHS : 0;
    gwy_sensitivity_group_set_state(controls->sens,
                                    SENS_DIFFERENT_LENGTHS, flags);
}

static void
distribute_changed(AffcorControls *controls,
                   GtkToggleButton *toggle)
{
    AffcorArgs *args = controls->args;

    args->distribute = gtk_toggle_button_get_active(toggle);
}

static void
fix_hacf_changed(AffcorControls *controls,
                 GtkToggleButton *toggle)
{
    AffcorArgs *args = controls->args;

    args->fix_hacf = gtk_toggle_button_get_active(toggle);
    calculate_acffield(controls);
}

static void
refine(AffcorControls *controls)
{
    GwyDataField *acf;
    gdouble xy[4];

    if (!gwy_selection_get_object(controls->selection, 0, xy))
        return;

    acf = get_full_acffield(controls);
    if (gwy_data_field_measure_lattice_acf(acf, xy))
        gwy_selection_set_object(controls->selection, 0, xy);
}

static void
do_estimate(AffcorControls *controls)
{
    GwyDataField *acf;

    acf = get_full_acffield(controls);
    gwy_clear(controls->xy, 4);
    if (gwy_data_field_measure_lattice_acf(acf, controls->xy))
        gwy_selection_set_object(controls->selection, 0, controls->xy);
    else
        init_selection(controls);
}

static void
selection_changed(AffcorControls *controls)
{
    GwySIValueFormat *vf;
    GwyDataField *dfield;
    gdouble xy[4];
    gdouble a1, a2, phi1, phi2, phi;
    guint flags, i;
    GString *str = g_string_new(NULL);

    if (!gwy_selection_get_data(controls->selection, NULL)) {
        controls->invalid_corr |= INVALID_SEL;
        gwy_sensitivity_group_set_state(controls->sens, SENS_VALID_LATTICE, 0);
        invalidate(controls);
        return;
    }

    gwy_selection_get_object(controls->selection, 0, xy);
    for (i = 0; i < 4; i++)
        controls->xy[i] = xy[i];

    vf = controls->vf;
    g_string_printf(str, "%.*f", vf->precision, xy[0]/vf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->a1_x), str->str);

    g_string_printf(str, "%.*f", vf->precision, -xy[1]/vf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->a1_y), str->str);

    a1 = hypot(xy[0], xy[1]);
    g_string_printf(str, "%.*f", vf->precision, a1/vf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->a1_len), str->str);

    vf = controls->vfphi;
    phi1 = atan2(-xy[1], xy[0]);
    g_string_printf(str, "%.*f", vf->precision, phi1/vf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->a1_phi), str->str);

    vf = controls->vf;
    g_string_printf(str, "%.*f", vf->precision, xy[2]/vf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->a2_x), str->str);

    g_string_printf(str, "%.*f", vf->precision, -xy[3]/vf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->a2_y), str->str);

    a2 = hypot(xy[2], xy[3]);
    g_string_printf(str, "%.*f", vf->precision, a2/vf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->a2_len), str->str);

    vf = controls->vfphi;
    phi2 = atan2(-xy[3], xy[2]);
    g_string_printf(str, "%.*f", vf->precision, phi2/vf->magnitude);
    gtk_entry_set_text(GTK_ENTRY(controls->a2_phi), str->str);

    phi = gwy_canonicalize_angle(phi2 - phi1, TRUE, TRUE);
    g_string_printf(str, "%.*f", vf->precision, phi/vf->magnitude);
    gtk_label_set_text(GTK_LABEL(controls->phi), str->str);

    g_string_free(str, TRUE);

    dfield = GWY_DATA_FIELD(gwy_container_get_object_by_name(controls->mydata,
                                                             "/0/data"));
    if (hypot(xy[0]/gwy_data_field_get_dx(dfield),
              xy[1]/gwy_data_field_get_dy(dfield)) >= 0.9
        && hypot(xy[2]/gwy_data_field_get_dx(dfield),
                 xy[3]/gwy_data_field_get_dy(dfield)) >= 0.9
        && phi >= 1e-3
        && phi <= G_PI - 1e-3)
        controls->invalid_corr &= ~INVALID_SEL;
    else
        controls->invalid_corr |= INVALID_SEL;

    flags = controls->invalid_corr ? 0 : SENS_VALID_LATTICE;
    gwy_sensitivity_group_set_state(controls->sens, SENS_VALID_LATTICE, flags);
    invalidate(controls);
}

static void
interp_changed(GtkComboBox *combo,
               AffcorControls *controls)
{
    controls->args->interp = gwy_enum_combo_box_get_active(combo);
    invalidate(controls);
}

static void
scaling_changed(GtkComboBox *combo,
                AffcorControls *controls)
{
    controls->args->scaling = gwy_enum_combo_box_get_active(combo);
    invalidate(controls);
}

static void
invalidate(AffcorControls *controls)
{
    controls->calculated = FALSE;
    if (controls->invalid_corr
        || controls->args->image_mode != IMAGE_CORRECTED)
        return;

    if (controls->recalculate_id)
        return;

    controls->recalculate_id = g_idle_add(recalculate, controls);
}

static gboolean
recalculate(gpointer user_data)
{
    AffcorControls *controls = (AffcorControls*)user_data;
    do_correction(controls);
    controls->recalculate_id = 0;
    return FALSE;
}

static void
do_correction(AffcorControls *controls)
{
    AffcorArgs *args = controls->args;
    GwyDataField *dfield, *corrected;
    gdouble a1a2_corr[4], a1a2[4];

    dfield = GWY_DATA_FIELD(gwy_container_get_object_by_name(controls->mydata,
                                                             "/0/data"));
    gwy_selection_get_object(controls->selection, 0, a1a2);
    fill_correct_vectors(args, a1a2_corr);
    corrected = create_corrected_dfield(dfield, a1a2, a1a2_corr,
                                        args->interp, args->scaling);
    gwy_container_set_object_by_name(controls->mydata, "/2/data", corrected);
    g_object_unref(corrected);

    /* Now save the corrected lattice selection on result. */
    gwy_selection_set_data(controls->selection_corr, 1, a1a2_corr);

    controls->calculated = TRUE;
}

static void
fill_correct_vectors(const AffcorArgs *args, gdouble *a1a2)
{
    a1a2[0] = args->a1;
    a1a2[1] = 0.0;
    a1a2[2] = args->a2 * cos(args->phi);
    a1a2[3] = -args->a2 * sin(args->phi);
}

/* NB: a1a2_corr is modified according to scaling to be correct for the
 * returned data field. */
static GwyDataField*
create_corrected_dfield(GwyDataField *dfield,
                        const gdouble *a1a2,
                        gdouble *a1a2_corr,
                        GwyInterpolationType interp,
                        GwyAffineScalingType scaling)
{
    GwyDataField *corrected;
    gdouble invtrans[6];

    corrected = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    gwy_data_field_affine_prepare(dfield, corrected, a1a2, a1a2_corr, invtrans,
                                  scaling, TRUE, 1.0);
    gwy_data_field_affine(dfield, corrected, invtrans, interp,
                          GWY_EXTERIOR_FIXED_VALUE,
                          gwy_data_field_get_avg(dfield));

    return corrected;
}

static const gchar a1_key[]                = "/module/correct_affine/a1";
static const gchar a2_key[]                = "/module/correct_affine/a2";
static const gchar different_lengths_key[] = "/module/correct_affine/different-lengths";
static const gchar distribute_key[]        = "/module/correct_affine/distribute";
static const gchar fix_hacf_key[]          = "/module/correct_affine/fix_hacf";
static const gchar interp_key[]            = "/module/correct_affine/interpolation";
static const gchar phi_key[]               = "/module/correct_affine/phi";
static const gchar preset_key[]            = "/module/correct_affine/preset";
static const gchar scaling_key[]           = "/module/correct_affine/scaling";
static const gchar zoom_key[]              = "/module/correct_affine/zoom";

static void
affcor_sanitize_args(AffcorArgs *args)
{
    args->interp = gwy_enum_sanitize_value(args->interp,
                                           GWY_TYPE_INTERPOLATION_TYPE);
    args->scaling = MIN(args->scaling, GWY_AFFINE_SCALING_PRESERVE_X);
    args->preset = CLAMP(args->preset,
                         USER_DEFINED_LATTICE,
                         (gint)G_N_ELEMENTS(lattice_presets)-1);
    args->fix_hacf = !!args->fix_hacf;
    args->distribute = !!args->distribute;
    if (args->zoom != ZOOM_1 && args->zoom != ZOOM_4 && args->zoom != ZOOM_16)
        args->zoom = affcor_defaults.zoom;

    if (args->preset == USER_DEFINED_LATTICE) {
        args->different_lengths = !!args->different_lengths;

        if (!(args->a1 > 0.0))
            args->a1 = 1.0;

        if (args->different_lengths) {
            if (!(args->a2 > 0.0))
                args->a2 = 1.0;
        }
        else
            args->a2 = args->a1;

        args->phi = gwy_canonicalize_angle(args->phi, TRUE, FALSE);
        if (args->phi < 1e-3 || args->phi > G_PI - 1e-3)
            args->phi = 0.5*G_PI;
    }
}

static void
affcor_load_args(GwyContainer *container,
                 AffcorArgs *args)
{
    *args = affcor_defaults;

    gwy_container_gis_double_by_name(container, a1_key, &args->a1);
    gwy_container_gis_double_by_name(container, a2_key, &args->a2);
    gwy_container_gis_double_by_name(container, phi_key, &args->phi);
    gwy_container_gis_boolean_by_name(container, different_lengths_key,
                                      &args->different_lengths);
    gwy_container_gis_enum_by_name(container, interp_key, &args->interp);
    gwy_container_gis_enum_by_name(container, scaling_key, &args->scaling);
    gwy_container_gis_int32_by_name(container, preset_key, &args->preset);
    gwy_container_gis_enum_by_name(container, zoom_key, &args->zoom);
    gwy_container_gis_boolean_by_name(container, fix_hacf_key, &args->fix_hacf);
    gwy_container_gis_boolean_by_name(container, distribute_key,
                                      &args->distribute);

    affcor_sanitize_args(args);
}

static void
affcor_save_args(GwyContainer *container,
                 AffcorArgs *args)
{
    gwy_container_set_double_by_name(container, a1_key, args->a1);
    gwy_container_set_double_by_name(container, a2_key, args->a2);
    gwy_container_set_double_by_name(container, phi_key, args->phi);
    gwy_container_set_boolean_by_name(container, different_lengths_key,
                                      args->different_lengths);
    gwy_container_set_enum_by_name(container, interp_key, args->interp);
    gwy_container_set_enum_by_name(container, scaling_key, args->scaling);
    gwy_container_set_int32_by_name(container, preset_key, args->preset);
    gwy_container_set_enum_by_name(container, zoom_key, args->zoom);
    gwy_container_set_boolean_by_name(container, fix_hacf_key, args->fix_hacf);
    gwy_container_set_boolean_by_name(container, distribute_key,
                                      args->distribute);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
