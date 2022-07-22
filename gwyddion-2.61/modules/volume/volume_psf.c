/*
 *  $Id: volume_psf.c 24683 2022-03-15 18:41:03Z yeti-dn $
 *  Copyright (C) 2017-2019 David Necas (Yeti), Petr Klapetek.
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
#include <fftw3.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwycheckboxes.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"
#include "../process/preview.h"
#include "../process/mfmops.h"

#define PSF_RUN_MODES (GWY_RUN_INTERACTIVE)

#define field_convolve_default(field, kernel) \
    gwy_data_field_area_ext_convolve((field), \
                                     0, 0, \
                                     gwy_data_field_get_xres(field), \
                                     gwy_data_field_get_yres(field), \
                                     (field), (kernel), \
                                     GWY_EXTERIOR_BORDER_EXTEND, 0.0, TRUE)

#define gwycreal(x) ((x)[0])
#define gwycimag(x) ((x)[1])

/* Do not require FFTW 3.3 just for a couple of trivial macros. */
#define gwy_fftw_new_real(n) \
    (gdouble*)fftw_malloc((n)*sizeof(gdouble))
#define gwy_fftw_new_complex(n) \
    (fftw_complex*)fftw_malloc((n)*sizeof(fftw_complex))

typedef enum {
    PSF_METHOD_REGULARISED   = 0,
    PSF_METHOD_LEAST_SQUARES = 1,
    PSF_METHOD_PSEUDO_WIENER = 2,
    PSF_NMETHODS
} PSFMethodType;

typedef enum {
    PSF_DISPLAY_DATA       = 0,
    PSF_DISPLAY_PSF        = 1,
    PSF_DISPLAY_CONVOLVED  = 2,
    PSF_DISPLAY_DIFFERENCE = 3,
    PSF_NDISPLAYS
} PSFDisplayType;

typedef enum {
    PSF_OUTPUT_PSF       = (1 << 0),
    PSF_OUTPUT_TF_WIDTH  = (1 << 1),
    PSF_OUTPUT_TF_HEIGHT = (1 << 2),
    PSF_OUTPUT_TF_NORM   = (1 << 3),
    PSF_OUTPUT_DIFF_NORM = (1 << 4),
    PSF_OUTPUT_SIGMA     = (1 << 5),
    PSF_OUTPUTS_MASK     = (1 << 6) - 1,
} PSFOutputType;

typedef struct {
    PSFMethodType method;
    gdouble sigma;                /* Log10 */
    GwyWindowingType windowing;
    GwyAppDataId op1;
    GwyAppDataId op2;
    gint zlevel;
    gint txres;
    gint tyres;
    gint border;
    PSFDisplayType display;
    gboolean as_integral;
    gboolean estimate_sigma;
    gboolean estimate_tres;
    PSFOutputType output_type;
} PSFArgs;

static const gchar *guivalues[] = {
    "width", "height", "l2norm", "residuum",
};

typedef struct {
    PSFArgs *args;
    GwyBrick *brick;
    GtkWidget *method;
    GtkObject *sigma;
    GtkWidget *windowing;
    GtkWidget *chooser_op2;
    GtkWidget *display;
    GtkObject *zlevel;
    GtkWidget *zlevelfit;
    GtkWidget *zlevelpx;
    GtkObject *txres;
    GtkObject *tyres;
    GtkObject *border;
    GtkWidget *guess_tres;
    GtkWidget *full_tres;
    GtkWidget *tf_size_header;
    GSList *output_type;
    GtkWidget *as_integral;
    GtkWidget *estimate_sigma;
    GtkWidget *estimate_tres;
    GwyContainer *mydata;
    GwyDataField *image;
    GtkWidget *dialog;
    GtkWidget *view;
    gboolean in_update;
    GwyDataField *resultfield;
    GwyResults *results;
    GtkWidget *guivalues[G_N_ELEMENTS(guivalues)];
} PSFControls;

static GwyAppDataId op2_id = GWY_APP_DATA_ID_NONE;

static gboolean module_register                (void);
static void     volume_psf                     (GwyContainer *data,
                                                GwyRunType run);
static gboolean psf_dialog                     (PSFArgs *args,
                                                GwyBrick *brick,
                                                GwyContainer *data);
static void     psf_do                         (PSFArgs *args,
                                                GwyBrick *brick,
                                                GwyContainer *data);
static void     prepare_field                  (GwyDataField *field,
                                                GwyDataField *wfield,
                                                GwyWindowingType window);
static void     calculate_tf                   (GwyDataField *measured_buf,
                                                GwyDataField *wideal,
                                                GwyDataField *psf,
                                                PSFArgs *args);
static void     display_changed                (GtkComboBox *combo,
                                                PSFControls *controls);
static void     zlevel_changed                 (PSFControls *controls,
                                                GtkAdjustment *adj);
static void     update_zlevel_fit              (PSFControls *controls,
                                                gboolean fitted);
static void     sigma_changed                  (GtkAdjustment *adj,
                                                PSFControls *controls);
static void     windowing_changed              (GtkComboBox *combo,
                                                PSFControls *controls);
static void     estimate_sigma_changed         (GtkToggleButton *toggle,
                                                PSFControls *controls);
static void     estimate_tres_changed          (GtkToggleButton *toggle,
                                                PSFControls *controls);
static void     as_integral_changed            (GtkToggleButton *toggle,
                                                PSFControls *controls);
static void     method_changed                 (GtkComboBox *combo,
                                                PSFControls *controls);
static void     txres_changed                  (GtkAdjustment *adj,
                                                PSFControls *controls);
static void     tyres_changed                  (GtkAdjustment *adj,
                                                PSFControls *controls);
static void     border_changed                 (GtkAdjustment *adj,
                                                PSFControls *controls);
static void     guess_tres_clicked             (GtkButton *button,
                                                PSFControls *controls);
static void     full_tres_clicked              (GtkButton *button,
                                                PSFControls *controls);
static void     output_type_changed            (GtkToggleButton *toggle,
                                                PSFControls *controls);
static void     update_sensitivity             (PSFControls *controls);
static void     update_tres_for_method         (PSFControls *controls);
static gboolean psf_data_filter                (GwyContainer *data,
                                                gint id,
                                                gpointer user_data);
static void     psf_data_changed               (GwyDataChooser *chooser,
                                                PSFControls *controls);
static void     preview                        (PSFControls *controls,
                                                PSFArgs *args);
static void     estimate_sigma                 (PSFArgs *args,
                                                GwyBrick *brick);
static gdouble  find_regularization_sigma      (GwyDataField *dfield,
                                                GwyDataField *ideal,
                                                const PSFArgs *args);
static void     adjust_tf_field_to_non_integral(GwyDataField *psf);
static void     adjust_tf_brick_to_non_integral(GwyBrick *psf);
static gdouble  measure_tf_width               (GwyDataField *psf);
static void     psf_deconvolve_wiener          (GwyDataField *dfield,
                                                GwyDataField *operand,
                                                GwyDataField *out,
                                                gdouble sigma);
static gboolean method_is_full_sized           (PSFMethodType method);
static void     estimate_tf_region             (GwyDataField *wmeas,
                                                GwyDataField *wideal,
                                                GwyDataField *psf,
                                                gint *col,
                                                gint *row,
                                                gint *width,
                                                gint *height);
static void     symmetrise_tf_region           (gint pos,
                                                gint len,
                                                gint res,
                                                gint *tres);
static gboolean clamp_psf_size                 (GwyBrick *brick,
                                                PSFArgs *args);
static void     psf_sanitize_args              (PSFArgs *args);
static void     psf_load_args                  (GwyContainer *container,
                                                PSFArgs *args);
static void     psf_save_args                  (GwyContainer *container,
                                                PSFArgs *args);

static const GwyEnum output_types[] = {
    { N_("Transfer function"), PSF_OUTPUT_PSF,       },
    { N_("TF width"),          PSF_OUTPUT_TF_WIDTH,  },
    { N_("TF height"),         PSF_OUTPUT_TF_HEIGHT, },
    { N_("TF norm"),           PSF_OUTPUT_TF_NORM,   },
    { N_("Difference norm"),   PSF_OUTPUT_DIFF_NORM, },
    { N_("Sigma"),             PSF_OUTPUT_SIGMA,     },
};

enum {
    OUTPUT_NTYPES = G_N_ELEMENTS(output_types)
};

static const PSFArgs psf_defaults = {
    PSF_METHOD_REGULARISED, 1.0, GWY_WINDOWING_WELCH,
    GWY_APP_DATA_ID_NONE, GWY_APP_DATA_ID_NONE,
    0,
    41, 41, 2,
    PSF_DISPLAY_PSF,
    TRUE, FALSE, FALSE,
    PSF_OUTPUT_PSF | PSF_OUTPUT_TF_WIDTH,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates the volume PSF."),
    "Petr Klapetek <pklapetek@gwyddion.net>",
    "2.2",
    "Petr Klapetek, Robb Puttock & David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, volume_psf)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_psf",
                             (GwyVolumeFunc)&volume_psf,
                             N_("/_Transfer Function Guess..."),
                             NULL,
                             PSF_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Estimate transfer function "
                                "from known data and ideal images"));

    return TRUE;
}

static void
volume_psf(GwyContainer *data, GwyRunType run)
{
    PSFArgs args;
    GwyBrick *brick = NULL;

    g_return_if_fail(run & PSF_RUN_MODES);

    psf_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_CONTAINER_ID, &args.op1.datano,
                                     GWY_APP_BRICK_ID, &args.op1.id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));

    if (CLAMP(args.zlevel, 0, brick->zres-1) != args.zlevel)
        args.zlevel = 0;

    if (!clamp_psf_size(brick, &args)) {
        if (run == GWY_RUN_INTERACTIVE) {
            GtkWidget *dialog;

            dialog = gtk_message_dialog_new
                           (gwy_app_find_window_for_channel(data, args.op1.id),
                            GTK_DIALOG_DESTROY_WITH_PARENT,
                            GTK_MESSAGE_ERROR,
                            GTK_BUTTONS_OK,
                            _("Image is too small."));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }
        return;
    }

    if (psf_dialog(&args, brick, data))
        psf_do(&args, brick, data);

    psf_save_args(gwy_app_settings_get(), &args);
}

static gboolean
psf_dialog(PSFArgs *args, GwyBrick *brick, GwyContainer *data)
{
    static const GwyEnum psf_methods[] = {
        { N_("Regularized filter"), PSF_METHOD_REGULARISED,   },
        { N_("Least squares"),      PSF_METHOD_LEAST_SQUARES, },
        { N_("Wiener filter"),      PSF_METHOD_PSEUDO_WIENER, },
    };
    static const GwyEnum psf_displays[] = {
        { N_("Data"),              PSF_DISPLAY_DATA,       },
        { N_("Transfer function"), PSF_DISPLAY_PSF,        },
        { N_("Convolved"),         PSF_DISPLAY_CONVOLVED,  },
        { N_("Difference"),        PSF_DISPLAY_DIFFERENCE, },
    };

    GtkWidget *dialog, *table, *hbox, *label, *align, *notebook, *hbox2;
    GwyDataChooser *chooser;
    PSFControls controls;
    GwyDataField *dfield;
    gint response, row, xres, yres;
    GQuark quark;
    const guchar *gradient;
    GwyResults *results;
    GString *str;
    guint i;

    gwy_clear(&controls, 1);
    controls.args = args;
    controls.in_update = TRUE;
    controls.brick = brick;

    controls.results = results = gwy_results_new();
    gwy_results_add_value_x(results, "width", N_("TF width"));
    gwy_results_add_value_z(results, "height", N_("TF height"));
    gwy_results_add_value(results, "l2norm", N_("TF norm"),
                          "power-u", 1,
                          NULL);
    gwy_results_add_value(results, "residuum", N_("Difference norm"),
                          "power-v", 1,
                          NULL);

    dialog = gtk_dialog_new_with_buttons(_("Estimate Transfer Function"), NULL, 0,
                                         _("_Fit Sigma"), RESPONSE_ESTIMATE,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    controls.dialog = dialog;
    controls.resultfield = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    dfield = controls.resultfield;
    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);

    /* This sets pixel size, real dimensions, units, etc. */
    gwy_brick_extract_xy_plane(brick, dfield, 0);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    controls.mydata = gwy_container_new();
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    g_object_unref(dfield);

    quark = gwy_app_get_brick_palette_key_for_id(args->op1.id);
    if (gwy_container_gis_string(data, quark, &gradient)) {
        gwy_container_set_const_string_by_name(controls.mydata,
                                               "/0/base/palette", gradient);
    }
    controls.view = gwy_create_preview(controls.mydata, 0, PREVIEW_SIZE, FALSE);
    align = gtk_alignment_new(0.5, 0.0, 0, 0);
    gtk_container_add(GTK_CONTAINER(align), controls.view);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 4);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 4);

    table = gtk_table_new(16 + G_N_ELEMENTS(guivalues), 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table,
                             gtk_label_new(_("Parameters")));
    row = 0;

    controls.chooser_op2 = gwy_data_chooser_new_channels();
    chooser = GWY_DATA_CHOOSER(controls.chooser_op2);
    gwy_data_chooser_set_active_id(chooser, &args->op2);
    gwy_data_chooser_set_filter(chooser, psf_data_filter, &args->op1, NULL);
    g_signal_connect(chooser, "changed",
                     G_CALLBACK(psf_data_changed), &controls);
    gwy_table_attach_adjbar(table, row, _("_Ideal response:"), NULL,
                            GTK_OBJECT(chooser), GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.method
        = gwy_enum_combo_box_new(psf_methods, G_N_ELEMENTS(psf_methods),
                                 G_CALLBACK(method_changed), &controls,
                                 args->method, TRUE);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Method:"), NULL,
                            GTK_OBJECT(controls.method),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.sigma = gtk_adjustment_new(args->sigma, -8.0, 3.0, 0.001, 1.0, 0);
    gwy_table_attach_adjbar(table, row, _("_Sigma:"), "log<sub>10</sub>",
                            controls.sigma, GWY_HSCALE_LINEAR);
    g_object_set_data(G_OBJECT(controls.sigma), "controls", &controls);
    g_signal_connect(controls.sigma, "value-changed",
                     G_CALLBACK(sigma_changed), &controls);
    row++;

    controls.estimate_sigma
        = gtk_check_button_new_with_mnemonic(_("_Estimate sigma "
                                               "for each level"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.estimate_sigma),
                                 args->estimate_sigma);
    gtk_table_attach(GTK_TABLE(table), controls.estimate_sigma,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls.estimate_sigma, "toggled",
                     G_CALLBACK(estimate_sigma_changed), &controls);
    row++;

    controls.estimate_tres
        = gtk_check_button_new_with_mnemonic(_("_Estimate size "
                                               "for each level"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.estimate_tres),
                                 args->estimate_tres);
    gtk_table_attach(GTK_TABLE(table), controls.estimate_tres,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls.estimate_tres, "toggled",
                     G_CALLBACK(estimate_tres_changed), &controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.windowing
        = gwy_enum_combo_box_new(gwy_windowing_type_get_enum(), -1,
                                 G_CALLBACK(windowing_changed), &controls,
                                 args->windowing, TRUE);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Windowing type:"), NULL,
                            GTK_OBJECT(controls.windowing),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    controls.zlevel = gtk_adjustment_new(args->zlevel, 0.0, brick->zres-1.0,
                                         1.0, 10.0, 0);
    gwy_table_attach_adjbar(table, row, _("_Z level:"), _("px"),
                            controls.zlevel,
                            GWY_HSCALE_LINEAR | GWY_HSCALE_SNAP);
    g_signal_connect_swapped(controls.zlevel, "value-changed",
                             G_CALLBACK(zlevel_changed), &controls);

    row++;

    label = gtk_label_new(_("Sigma fitted at Z level:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    controls.zlevelfit = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.zlevelfit), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.zlevelfit,
                     1, 2, row, row+1, GTK_FILL, 0, 0, 0);

    controls.zlevelpx = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.zlevelpx), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.zlevelpx,
                     2, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.tf_size_header = gwy_label_new_header(_("Transfer Function Size"));
    gtk_table_attach(GTK_TABLE(table), controls.tf_size_header,
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.txres = gtk_adjustment_new(args->txres, 3, xres, 2, 10, 0);
    gwy_table_attach_adjbar(table, row, _("_Horizontal size:"), _("px"),
                            controls.txres, GWY_HSCALE_SQRT | GWY_HSCALE_SNAP);
    g_signal_connect(controls.txres, "value-changed",
                     G_CALLBACK(txres_changed), &controls);
    row++;

    controls.tyres = gtk_adjustment_new(args->tyres, 3, yres, 2, 10, 0);
    gwy_table_attach_adjbar(table, row, _("_Vertical size:"), _("px"),
                            controls.tyres, GWY_HSCALE_SQRT | GWY_HSCALE_SNAP);
    g_signal_connect(controls.tyres, "value-changed",
                     G_CALLBACK(tyres_changed), &controls);
    row++;

    controls.border = gtk_adjustment_new(args->border,
                                         0, MIN(xres, yres)/8, 1, 5, 0);
    gwy_table_attach_adjbar(table, row, _("_Border:"), _("px"),
                            controls.border, GWY_HSCALE_SQRT | GWY_HSCALE_SNAP);
    g_signal_connect(controls.border, "value-changed",
                     G_CALLBACK(border_changed), &controls);
    row++;

    hbox2 = gtk_hbox_new(FALSE, 2);
    gtk_table_attach(GTK_TABLE(table), hbox2,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    controls.guess_tres = gtk_button_new_with_mnemonic("_Estimate Size");
    gtk_box_pack_end(GTK_BOX(hbox2), controls.guess_tres, FALSE, FALSE, 0);
    g_signal_connect(controls.guess_tres, "clicked",
                     G_CALLBACK(guess_tres_clicked), &controls);
    controls.full_tres = gtk_button_new_with_mnemonic("_Full Size");
    gtk_box_pack_end(GTK_BOX(hbox2), controls.full_tres, FALSE, FALSE, 0);
    g_signal_connect(controls.full_tres, "clicked",
                     G_CALLBACK(full_tres_clicked), &controls);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    label = gwy_label_new_header(_("Preview Options"));
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.display
        = gwy_enum_combo_box_new(psf_displays, G_N_ELEMENTS(psf_displays),
                                 G_CALLBACK(display_changed),
                                 &controls, args->display, TRUE);
    gwy_table_attach_adjbar(table, row, _("_Display:"), NULL,
                            GTK_OBJECT(controls.display),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    label = gwy_label_new_header(_("Result"));
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 3, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    str = g_string_new(NULL);
    for (i = 0; i < G_N_ELEMENTS(guivalues); i++) {
        g_string_assign(str, gwy_results_get_label_with_symbol(results,
                                                               guivalues[i]));
        g_string_append_c(str, ':');
        label = gtk_label_new(str->str);
        gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
        gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row+1,
                         GTK_FILL, 0, 0, 0);

        label = controls.guivalues[i] = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_label_set_selectable(GTK_LABEL(label), TRUE);
        gtk_table_attach(GTK_TABLE(table), label, 1, 2, row, row+1,
                         GTK_EXPAND | GTK_FILL, 0, 0, 0);
        row++;
    }
    g_string_free(str, TRUE);

    table = gtk_table_new(3 + OUTPUT_NTYPES, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table,
                             gtk_label_new(_("Output Options")));
    row = 0;

    label = gtk_label_new(_("Output type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);
    row++;

    controls.output_type
        = gwy_check_boxes_create(output_types, OUTPUT_NTYPES,
                                 G_CALLBACK(output_type_changed), &controls,
                                 args->output_type);
    row = gwy_check_boxes_attach_to_table(controls.output_type,
                                          GTK_TABLE(table), 2, row);

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.as_integral
        = gtk_check_button_new_with_mnemonic(_("Normalize as _integral"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.as_integral),
                                 args->as_integral);
    g_signal_connect(controls.as_integral, "toggled",
                     G_CALLBACK(as_integral_changed), &controls);
    gtk_table_attach(GTK_TABLE(table), controls.as_integral,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.in_update = FALSE;
    update_tres_for_method(&controls);
    psf_data_changed(chooser, &controls);

    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            g_object_unref(controls.mydata);
            g_object_unref(controls.results);
            return FALSE;
            break;

            case GTK_RESPONSE_OK:
            break;

            case RESPONSE_ESTIMATE:
            gwy_app_wait_cursor_start(GTK_WINDOW(dialog));
            estimate_sigma(args, brick);
            gwy_app_wait_cursor_finish(GTK_WINDOW(dialog));
            gtk_adjustment_set_value(GTK_ADJUSTMENT(controls.sigma),
                                     args->sigma);
            update_zlevel_fit(&controls, TRUE);
            preview(&controls, args);
            break;


            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);
    g_object_unref(controls.mydata);
    g_object_unref(controls.results);

    return TRUE;
}

static void
sigma_changed(GtkAdjustment *adj, PSFControls *controls)
{
    controls->args->sigma = gtk_adjustment_get_value(adj);
    update_zlevel_fit(controls, FALSE);
    preview(controls, controls->args);
}

static void
windowing_changed(GtkComboBox *combo, PSFControls *controls)
{
    controls->args->windowing = gwy_enum_combo_box_get_active(combo);
    preview(controls, controls->args);
}

static void
display_changed(GtkComboBox *combo, PSFControls *controls)
{
    PSFArgs *args = controls->args;

    args->display = gwy_enum_combo_box_get_active(combo);
    preview(controls, controls->args);
}

static void
zlevel_changed(PSFControls *controls, GtkAdjustment *adj)
{
    controls->args->zlevel = gwy_adjustment_get_int(adj);
    preview(controls, controls->args);
}

static void
estimate_sigma_changed(GtkToggleButton *toggle, PSFControls *controls)
{
    controls->args->estimate_sigma = gtk_toggle_button_get_active(toggle);
    /* Might want to make sigma adjbar insensitive?  Let the user play with
     * it.  We do not want to run immediate sigma estimate for each change in
     * the dialogue. */
}

static void
estimate_tres_changed(GtkToggleButton *toggle, PSFControls *controls)
{
    controls->args->estimate_tres = gtk_toggle_button_get_active(toggle);
}

static void
as_integral_changed(GtkToggleButton *toggle, PSFControls *controls)
{
    controls->args->as_integral = gtk_toggle_button_get_active(toggle);
    /* PSF height changes, unfortunately. */
    preview(controls, controls->args);
}

static void
method_changed(GtkComboBox *combo, PSFControls *controls)
{
    controls->args->method = gwy_enum_combo_box_get_active(combo);
    update_sensitivity(controls);
    update_tres_for_method(controls);
    preview(controls, controls->args);
}

static void
txres_changed(GtkAdjustment *adj, PSFControls *controls)
{
    controls->args->txres = gwy_adjustment_get_int(adj);
    if (!controls->in_update)
        preview(controls, controls->args);
}

static void
tyres_changed(GtkAdjustment *adj, PSFControls *controls)
{
    controls->args->tyres = gwy_adjustment_get_int(adj);
    if (!controls->in_update)
        preview(controls, controls->args);
}

static void
border_changed(GtkAdjustment *adj, PSFControls *controls)
{
    controls->args->border = gwy_adjustment_get_int(adj);
    if (!controls->in_update)
        preview(controls, controls->args);
}

static void
update_zlevel_fit(PSFControls *controls, gboolean fitted)
{
     gchar *s;

     if (!fitted) {
         gtk_label_set_text(GTK_LABEL(controls->zlevelfit), "");
         gtk_label_set_text(GTK_LABEL(controls->zlevelpx), "");
         return;
     }
     s = g_strdup_printf("%d", controls->args->zlevel);
     gtk_label_set_text(GTK_LABEL(controls->zlevelfit), s);
     g_free(s);
     gtk_label_set_text(GTK_LABEL(controls->zlevelpx), _("px"));
}

static void
guess_tres_clicked(G_GNUC_UNUSED GtkButton *button,
                   PSFControls *controls)
{
    PSFArgs *args = controls->args;
    GwyContainer *data2;
    GQuark quark;
    GwyDataField *wmeas, *ideal, *wideal, *psf;
    GwyBrick *brick = controls->brick;
    gint col, row, width, height;

    wmeas = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    gwy_brick_extract_xy_plane(brick, wmeas, controls->args->zlevel);
    psf = gwy_data_field_new_alike(wmeas, FALSE);

    data2 = gwy_app_data_browser_get(args->op2.datano);
    quark = gwy_app_get_data_key_for_id(args->op2.id);
    ideal = GWY_DATA_FIELD(gwy_container_get_object(data2, quark));
    wideal = gwy_data_field_duplicate(ideal);
    prepare_field(ideal, wideal, args->windowing);
    prepare_field(wmeas, wmeas, args->windowing);

    estimate_tf_region(wmeas, wideal, psf, &col, &row, &width, &height);
    g_object_unref(wideal);
    g_object_unref(wmeas);
    g_object_unref(psf);
    symmetrise_tf_region(col, width, gwy_data_field_get_xres(ideal),
                         &args->txres);
    symmetrise_tf_region(row, height, gwy_data_field_get_yres(ideal),
                         &args->tyres);
    args->border = GWY_ROUND(0.5*log(fmax(args->txres, args->tyres)) + 0.5);

    controls->in_update = TRUE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->border), args->border);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->txres), args->txres);
    controls->in_update = FALSE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->tyres), args->tyres);
}

static void
full_tres_clicked(G_GNUC_UNUSED GtkButton *button,
                  PSFControls *controls)
{
    PSFArgs *args = controls->args;

    args->txres = gwy_brick_get_xres(controls->brick);
    args->tyres = gwy_brick_get_yres(controls->brick);

    controls->in_update = TRUE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->txres), args->txres);
    controls->in_update = FALSE;
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->tyres), args->tyres);
}

static void
update_sensitivity(PSFControls *controls)
{
    PSFArgs *args = controls->args;
    gboolean have_ideal = args->op2.datano;
    gboolean out_is_psf = (args->output_type & PSF_OUTPUT_PSF);
    gboolean method_is_lsq = (args->method == PSF_METHOD_LEAST_SQUARES);
    gboolean any_output = args->output_type;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      GTK_RESPONSE_OK,
                                      have_ideal && any_output);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      RESPONSE_ESTIMATE, have_ideal);
    gtk_widget_set_sensitive(controls->as_integral, out_is_psf);
    gtk_widget_set_sensitive(controls->guess_tres, method_is_lsq);
    gtk_widget_set_sensitive(controls->full_tres, !method_is_lsq);
    gwy_table_hscale_set_sensitive(controls->border, method_is_lsq);
    gtk_widget_set_sensitive(controls->estimate_tres, method_is_lsq);
    gtk_widget_set_sensitive(controls->tf_size_header, method_is_lsq);
}

static void
update_tres_for_method(PSFControls *controls)
{
    PSFArgs *args = controls->args;
    gint xres, yres, wantxupper, wantyupper;
    gdouble xupper, yupper;

    xres = gwy_brick_get_xres(controls->brick);
    yres = gwy_brick_get_yres(controls->brick);

    g_object_get(controls->txres, "upper", &xupper, NULL);
    g_object_get(controls->tyres, "upper", &yupper, NULL);

    controls->in_update = TRUE;
    if (method_is_full_sized(args->method)) {
        wantxupper = xres;
        wantyupper = yres;
    }
    else {
        wantxupper = (xres/3) | 1;
        wantyupper = (yres/3) | 1;
    }
    if (args->txres > wantxupper)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->txres), wantxupper);
    if (xupper > wantxupper)
        g_object_set(controls->txres, "upper", (gdouble)wantxupper, NULL);
    if (args->tyres > wantyupper)
        gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->tyres), wantyupper);
    if (yupper > wantyupper)
        g_object_set(controls->tyres, "upper", (gdouble)wantyupper, NULL);
    controls->in_update = FALSE;
}

static void
output_type_changed(G_GNUC_UNUSED GtkToggleButton *toggle,
                    PSFControls *controls)
{
    PSFArgs *args = controls->args;

    args->output_type = gwy_check_boxes_get_selected(controls->output_type);
    update_sensitivity(controls);
}

static void
psf_data_changed(GwyDataChooser *chooser, PSFControls *controls)
{
    GwyAppDataId *object = &controls->args->op2;

    gwy_data_chooser_get_active_id(chooser, object);
    update_sensitivity(controls);
    if (!controls->in_update)
        preview(controls, controls->args);
}

static gboolean
psf_data_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyAppDataId *object = (GwyAppDataId*)user_data;
    GwyBrick *op1;
    GwyDataField *op2;
    GQuark quark;

    quark = gwy_app_get_data_key_for_id(id);
    op2 = GWY_DATA_FIELD(gwy_container_get_object(data, quark));

    data = gwy_app_data_browser_get(object->datano);

    quark = gwy_app_get_brick_key_for_id(object->id);
    op1 = GWY_BRICK(gwy_container_get_object(data, quark));

    return !gwy_data_field_check_compatibility_with_brick_xy
                                          (op2, op1,
                                           GWY_DATA_COMPATIBILITY_RES
                                           | GWY_DATA_COMPATIBILITY_REAL
                                           | GWY_DATA_COMPATIBILITY_LATERAL);
}

static void
estimate_sigma(PSFArgs *args, GwyBrick *brick)
{
    GwyDataField *measured, *ideal;
    GQuark quark;
    GwyContainer *data2;
    gint zlevel = args->zlevel;

    if (zlevel == -1)
        zlevel = 0;

    data2 = gwy_app_data_browser_get(args->op2.datano);
    quark = gwy_app_get_data_key_for_id(args->op2.id);
    ideal = GWY_DATA_FIELD(gwy_container_get_object(data2, quark));

    measured = gwy_data_field_new_alike(ideal, FALSE);
    gwy_brick_extract_xy_plane(brick, measured, zlevel);

    args->sigma = log(find_regularization_sigma(measured, ideal, args))/G_LN2;

    g_object_unref(measured);
}

static gdouble
calculate_l2_norm(GwyDataField *dfield, gboolean as_integral,
                  GwySIUnit *unit)
{
    gdouble l2norm, q;

    l2norm = gwy_data_field_get_mean_square(dfield);

    /* In the integral formulation, we calculate the integral of squared
     * values and units of dx dy are reflected in the result.  In non-integral,
     * we calculate a mere sum of squared values and the result has the same
     * units as the field values. */
    if (as_integral) {
        q = gwy_data_field_get_xreal(dfield) * gwy_data_field_get_yreal(dfield);
        if (unit) {
            gwy_si_unit_multiply(gwy_data_field_get_si_unit_xy(dfield),
                                 gwy_data_field_get_si_unit_z(dfield),
                                 unit);
        }
    }
    else {
        q = gwy_data_field_get_xres(dfield) * gwy_data_field_get_yres(dfield);
        if (unit)
            gwy_si_unit_assign(unit, gwy_data_field_get_si_unit_z(dfield));
    }

    return sqrt(q*l2norm);
}

static void
preview(PSFControls *controls, PSFArgs *args)
{
    GwyLayerBasicRangeType range_type = GWY_LAYER_BASIC_RANGE_FULL;
    GwyDataField *dfield1, *dfield2, *wfield2, *psf, *convolved;
    GQuark quark;
    GwyContainer *data2;
    GwyBrick *brick = controls->brick;
    gint zlevel = args->zlevel;
    gdouble min, max, l2norm, resid;
    GwyResults *results;
    GwySIUnit *unit;
    guint i;

    if (zlevel == -1)
        zlevel = 0;

    if (args->op2.datano < 0 || args->op2.id < 0) {
        if (args->display == PSF_DISPLAY_DATA)
            gwy_brick_extract_xy_plane(brick, controls->resultfield, zlevel);
        else
            gwy_data_field_clear(controls->resultfield);
        gwy_data_field_data_changed(controls->resultfield);
        return;
    }

    data2 = gwy_app_data_browser_get(args->op2.datano);
    quark = gwy_app_get_data_key_for_id(args->op2.id);
    dfield2 = GWY_DATA_FIELD(gwy_container_get_object(data2, quark));
    wfield2 = gwy_data_field_duplicate(dfield2);
    prepare_field(wfield2, wfield2, args->windowing);

    dfield1 = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    gwy_brick_extract_xy_plane(brick, dfield1, zlevel);
    psf = gwy_data_field_new_alike(dfield1, TRUE);
    calculate_tf(dfield1, wfield2, psf, args);
    g_object_unref(wfield2);

    convolved = gwy_data_field_duplicate(dfield2);
    gwy_data_field_add(convolved, -gwy_data_field_get_avg(convolved));
    field_convolve_default(convolved, psf);
    gwy_data_field_add(convolved, gwy_data_field_get_avg(dfield1));

    if (args->display == PSF_DISPLAY_DATA)
        gwy_data_field_assign(controls->resultfield, dfield1);
    else if (args->display == PSF_DISPLAY_PSF)
        gwy_data_field_assign(controls->resultfield, psf);
    else if (args->display == PSF_DISPLAY_CONVOLVED)
        gwy_data_field_assign(controls->resultfield, convolved);
    else if (args->display == PSF_DISPLAY_DIFFERENCE) {
        gwy_data_field_assign(controls->resultfield, convolved);
        gwy_data_field_subtract_fields(controls->resultfield,
                                       dfield1, controls->resultfield);
        range_type = GWY_LAYER_BASIC_RANGE_AUTO;
    }
    g_object_unref(dfield1);
    gwy_data_field_data_changed(controls->resultfield);
    gwy_container_set_enum_by_name(controls->mydata, "/0/base/range-type",
                                   range_type);
    gwy_set_data_preview_size(GWY_DATA_VIEW(controls->view), PREVIEW_SIZE);
    /* Prevent the size changing wildly the moment someone touches the size
     * adjbars. */
    gtk_widget_set_size_request(controls->view, PREVIEW_SIZE, PREVIEW_SIZE);

    /* Change the normalisation to the discrete (i.e. wrong) one after all
     * calculations are done. */
    if (!args->as_integral)
        adjust_tf_field_to_non_integral(psf);

    results = controls->results;
    gwy_results_set_unit(results, "x", gwy_data_field_get_si_unit_xy(psf));
    gwy_results_set_unit(results, "y", gwy_data_field_get_si_unit_xy(psf));
    gwy_results_set_unit(results, "z", gwy_data_field_get_si_unit_z(psf));
    gwy_data_field_get_min_max(psf, &min, &max);
    unit = gwy_si_unit_new(NULL);
    l2norm = calculate_l2_norm(psf, args->as_integral, unit);
    gwy_results_set_unit(results, "u", unit);
    resid = calculate_l2_norm(convolved, args->as_integral, unit);
    gwy_results_set_unit(results, "v", unit);
    g_object_unref(unit);
    gwy_results_fill_values(results,
                           "width", measure_tf_width(psf),
                           "height", fmax(fabs(min), fabs(max)),
                           "l2norm", l2norm,
                           "residuum", resid,
                           NULL);
    for (i = 0; i < G_N_ELEMENTS(guivalues); i++) {
        gtk_label_set_markup(GTK_LABEL(controls->guivalues[i]),
                             gwy_results_get_full(results, guivalues[i]));
    }

    g_object_unref(convolved);
    g_object_unref(psf);
}

static void
psf_do(PSFArgs *args, GwyBrick *brick, GwyContainer *data)
{
    static const PSFOutputType graph_outputs[] = {
        PSF_OUTPUT_TF_WIDTH,
        PSF_OUTPUT_TF_HEIGHT,
        PSF_OUTPUT_TF_NORM,
        PSF_OUTPUT_DIFF_NORM,
        PSF_OUTPUT_SIGMA,
    };
    enum { NGRAPH_OUTPUTS = G_N_ELEMENTS(graph_outputs) };

    GwyDataField *ideal, *wideal;
    GwyContainer *data2;
    GtkWindow *window;
    GwyBrick *result = NULL;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwyDataLine *plots[NGRAPH_OUTPUTS];
    GwyDataLine *zcal;
    GQuark quark;
    gint i, newid, xres, yres, zres, txres, tyres;
    gdouble dx, dy, zreal, min, max;
    const gchar *name;
    gboolean cancelled = FALSE, *pcancelled = &cancelled;

    window = gwy_app_find_window_for_volume(data, args->op1.id);
    gwy_app_wait_start(window, _("Calculating volume transfer function..."));

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);

    data2 = gwy_app_data_browser_get(args->op2.datano);
    quark = gwy_app_get_data_key_for_id(args->op2.id);
    ideal = GWY_DATA_FIELD(gwy_container_get_object(data2, quark));
    wideal = gwy_data_field_duplicate(ideal);
    prepare_field(wideal, wideal, args->windowing);

    txres = args->txres;
    tyres = args->tyres;
    dx = gwy_brick_get_dx(brick);
    dy = gwy_brick_get_dy(brick);
    zreal = gwy_brick_get_zreal(brick);
    if (args->output_type & PSF_OUTPUT_PSF) {
        result = gwy_brick_new(txres, tyres, zres, txres*dx, tyres*dy, zreal,
                               FALSE);
        gwy_brick_copy_units(brick, result);
        gwy_brick_copy_zcalibration(brick, result);
    }
    for (i = 0; i < NGRAPH_OUTPUTS; i++) {
        if (args->output_type & graph_outputs[i]) {
            plots[i] = gwy_data_line_new(zres, zreal, FALSE);
            gwy_si_unit_assign(gwy_data_line_get_si_unit_x(plots[i]),
                               gwy_brick_get_si_unit_z(brick));
        }
        else
            plots[i] = NULL;
    }

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(brick,result,ideal,wideal,plots,args,xres,yres,zres,txres,tyres,min,max,pcancelled)
#endif
    {
        gint kfrom = gwy_omp_chunk_start(zres), kto = gwy_omp_chunk_end(zres);
        GwyDataField *measured, *psf, *buf, *convolved = NULL, *wmeas = NULL;
        GwySIUnit *unit;
        PSFArgs targs = *args;
        gint k;

        /* measured does not have correct units here yet. */
        psf = gwy_data_field_new_alike(ideal, FALSE);

        measured = gwy_data_field_new(xres, yres,
                                      gwy_brick_get_xreal(brick),
                                      gwy_brick_get_yreal(brick),
                                      FALSE);

        for (k = kfrom; k < kto; k++) {
            gwy_brick_extract_xy_plane(brick, measured, k);
            if (targs.estimate_tres) {
                gint col, row, width, height;

                if (!wmeas)
                    wmeas = gwy_data_field_new_alike(measured, FALSE);
                prepare_field(measured, wmeas, targs.windowing);
                estimate_tf_region(wmeas, wideal, psf,
                                   &col, &row, &width, &height);
                symmetrise_tf_region(col, width, xres, &targs.txres);
                symmetrise_tf_region(row, height, yres, &targs.tyres);
                targs.txres = MIN(targs.txres, args->txres);
                targs.tyres = MIN(targs.tyres, args->tyres);
                /* find_regularization_sigma() does its own windowing */
                if (targs.estimate_sigma) {
                    targs.sigma = find_regularization_sigma(measured, ideal,
                                                            &targs);
                    targs.sigma = log(targs.sigma)/G_LN2;
                }
                calculate_tf(measured, wideal, psf, &targs);
                width = gwy_data_field_get_xres(psf);
                height = gwy_data_field_get_yres(psf);
                buf = gwy_data_field_extend(psf,
                                            (txres - width)/2,
                                            (tyres - height)/2,
                                            (txres - width)/2,
                                            (tyres - height)/2,
                                            GWY_EXTERIOR_FIXED_VALUE, 0.0,
                                            FALSE);
                gwy_data_field_assign(psf, buf);
                g_object_unref(buf);
            }
            else if (targs.estimate_sigma) {
                /* find_regularization_sigma() does its own windowing */
                targs.sigma = find_regularization_sigma(measured, ideal,
                                                        &targs);
                targs.sigma = log(targs.sigma)/G_LN2;
                calculate_tf(measured, wideal, psf, &targs);
            }
            else
                calculate_tf(measured, wideal, psf, &targs);

            if (result) {
                gwy_brick_set_xy_plane(result, psf, k);
                if (!k) {
                    gwy_si_unit_assign(gwy_brick_get_si_unit_w(result),
                                       gwy_data_field_get_si_unit_z(psf));
                    gwy_brick_set_xoffset(result,
                                          gwy_data_field_get_xoffset(psf));
                    gwy_brick_set_yoffset(result,
                                          gwy_data_field_get_yoffset(psf));
                }
            }

            /* PSF_OUTPUT_TF_WIDTH */
            if (plots[0])
                gwy_data_line_set_val(plots[0], k, measure_tf_width(psf));
            /* Calculate this first because we may need to adjust psf to
             * non-integral for height and norm. */
            /* PSF_OUTPUT_DIFF_NORM */
            if (plots[3]) {
                if (!k)
                    unit = gwy_si_unit_new(NULL);
                if (!convolved)
                    convolved = gwy_data_field_new_alike(ideal, FALSE);
                else
                    gwy_data_field_copy(ideal, convolved, TRUE);
                gwy_data_field_add(convolved,
                                   -gwy_data_field_get_avg(convolved));
                field_convolve_default(convolved, psf);
                gwy_data_field_subtract_fields(convolved, measured, convolved);
                gwy_data_field_add(convolved, gwy_data_field_get_avg(measured));
                gwy_data_line_set_val(plots[3], k,
                                      calculate_l2_norm(convolved,
                                                        targs.as_integral,
                                                        unit));
                if (unit) {
                    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(plots[3]),
                                       unit);
                    gwy_object_unref(unit);
                }
            }
            if ((plots[1] || plots[2]) && !targs.as_integral)
                adjust_tf_field_to_non_integral(psf);
            /* PSF_OUTPUT_TF_HEIGHT */
            if (plots[1]) {
                if (!k) {
                    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(plots[1]),
                                       gwy_data_field_get_si_unit_z(psf));
                }
                gwy_data_field_get_min_max(psf, &min, &max);
                gwy_data_line_set_val(plots[1], k, fmax(fabs(min), fabs(max)));
            }
            /* PSF_OUTPUT_TF_NORM */
            if (plots[2]) {
                if (!k)
                    unit = gwy_si_unit_new(NULL);
                gwy_data_line_set_val(plots[2], k,
                                      calculate_l2_norm(psf, targs.as_integral,
                                                        unit));
                if (unit) {
                    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(plots[2]),
                                       unit);
                    gwy_object_unref(unit);
                }
            }
            /* PSF_OUTPUT_SIGMA */
            if (plots[4]) {
                if (!k)
                    unit = gwy_si_unit_new(NULL);
                gwy_data_line_set_val(plots[4], k, pow10(targs.sigma));
                if (unit) {
                    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(plots[4]),
                                       unit);
                    gwy_object_unref(unit);
                }
            }

            if (gwy_omp_set_fraction_check_cancel(gwy_app_wait_set_fraction,
                                                  k, kfrom, kto, pcancelled))
                break;
        }

        g_object_unref(measured);
        g_object_unref(psf);
        gwy_object_unref(convolved);
        gwy_object_unref(wmeas);
    }

    if (cancelled)
        goto fail;

    if (plots[0]) {
        gwy_si_unit_assign(gwy_data_line_get_si_unit_y(plots[0]),
                           gwy_brick_get_si_unit_x(brick));
    }

    if (result) {
        if (!args->as_integral)
            adjust_tf_brick_to_non_integral(result);

        newid = gwy_app_data_browser_add_brick(result, NULL, data, TRUE);
        gwy_app_set_brick_title(data, newid, _("Volume TF"));
        gwy_app_volume_log_add_volume(data, args->op1.id, newid);
        gwy_app_sync_volume_items(data, data, args->op1.id, newid,
                                  GWY_DATA_ITEM_GRADIENT,
                                  0);
    }

    zcal = gwy_brick_get_zcalibration(brick);
    for (i = 0; i < NGRAPH_OUTPUTS; i++) {
        if (!plots[i])
            continue;

        gmodel = gwy_graph_model_new();
        gwy_graph_model_set_units_from_data_line(gmodel, plots[i]);
        name = gwy_enum_to_string(graph_outputs[i],
                                  output_types, OUTPUT_NTYPES);
        g_object_set(gmodel,
                     "title", _(name),
                     "axis-label-left", _(name),
                     "axis-label-bottom", _("z level"),
                     NULL);

        gcmodel = gwy_graph_curve_model_new();
        if (zcal) {
            gwy_graph_curve_model_set_data(gcmodel,
                                           gwy_data_line_get_data(zcal),
                                           gwy_data_line_get_data(plots[i]),
                                           zres);
            g_object_set(gmodel,
                         "si-unit-x", gwy_data_line_get_si_unit_y(zcal),
                         NULL);
        }
        else {
            gwy_graph_curve_model_set_data_from_dataline(gcmodel, plots[i],
                                                         -1, -1);
        }
        g_object_set(gcmodel,
                     "description", _(name),
                     "mode", GWY_GRAPH_CURVE_LINE,
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
        gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);
        g_object_unref(gmodel);
    }

fail:
    gwy_app_wait_finish();
    g_object_unref(wideal);
    for (i = 0; i < NGRAPH_OUTPUTS; i++)
        gwy_object_unref(plots[i]);
    gwy_object_unref(result);
}

static void
prepare_field(GwyDataField *field, GwyDataField *wfield,
              GwyWindowingType window)
{
    /* Prepare field in place if requested. */
    if (wfield != field) {
        gwy_data_field_resample(wfield,
                                gwy_data_field_get_xres(field),
                                gwy_data_field_get_yres(field),
                                GWY_INTERPOLATION_NONE);
        gwy_data_field_copy(field, wfield, TRUE);
    }
    gwy_data_field_add(wfield, -gwy_data_field_get_avg(wfield));
    gwy_fft_window_data_field(wfield, GWY_ORIENTATION_HORIZONTAL, window);
    gwy_fft_window_data_field(wfield, GWY_ORIENTATION_VERTICAL, window);
}

static void
calculate_tf(GwyDataField *measured, GwyDataField *wideal,
             GwyDataField *psf, PSFArgs *args)
{
    GwyDataField *wmeas;
    gdouble r, sigma = pow10(args->sigma);
    gint xres, yres, xborder, yborder;

    wmeas = gwy_data_field_new_alike(measured, FALSE);
    prepare_field(measured, wmeas, args->windowing);
    if (args->method == PSF_METHOD_REGULARISED)
        gwy_data_field_deconvolve_regularized(wmeas, wideal, psf, sigma);
    else if (args->method == PSF_METHOD_PSEUDO_WIENER)
        psf_deconvolve_wiener(wmeas, wideal, psf, sigma);
    else {
        gwy_data_field_resample(psf, args->txres, args->tyres,
                                GWY_INTERPOLATION_NONE);
        gwy_data_field_deconvolve_psf_leastsq(wmeas, wideal, psf,
                                              sigma, args->border);
    }
    g_object_unref(wmeas);

    if (!method_is_full_sized(args->method))
        return;

    xres = gwy_data_field_get_xres(psf);
    yres = gwy_data_field_get_yres(psf);
    xborder = (xres - args->txres + 1)/2;
    yborder = (yres - args->tyres + 1)/2;
    if (!xborder && !yborder)
        return;

    gwy_data_field_resize(psf,
                          xborder, yborder,
                          xborder + args->txres, yborder + args->tyres);
    r = (args->txres + 1 - args->txres % 2)/2.0;
    gwy_data_field_set_xoffset(psf, -gwy_data_field_jtor(psf, r));
    r = (args->tyres + 1 - args->tyres % 2)/2.0;
    gwy_data_field_set_yoffset(psf, -gwy_data_field_itor(psf, r));
}

static void
adjust_tf_field_to_non_integral(GwyDataField *psf)
{
    GwySIUnit *xyunit, *zunit;
    gdouble hxhy;

    xyunit = gwy_data_field_get_si_unit_xy(psf);
    zunit = gwy_data_field_get_si_unit_z(psf);
    gwy_si_unit_power_multiply(zunit, 1, xyunit, 2, zunit);

    hxhy = gwy_data_field_get_dx(psf) * gwy_data_field_get_dy(psf);
    gwy_data_field_multiply(psf, hxhy);
    gwy_data_field_data_changed(psf);
}

static void
adjust_tf_brick_to_non_integral(GwyBrick *psf)
{
    GwySIUnit *xunit, *yunit, *wunit;
    gdouble hxhy;

    xunit = gwy_brick_get_si_unit_x(psf);
    yunit = gwy_brick_get_si_unit_y(psf);
    wunit = gwy_brick_get_si_unit_w(psf);
    gwy_si_unit_multiply(wunit, xunit, wunit);
    gwy_si_unit_multiply(wunit, yunit, wunit);

    hxhy = gwy_brick_get_dx(psf) * gwy_brick_get_dy(psf);
    gwy_brick_multiply(psf, hxhy);
    gwy_brick_data_changed(psf);
}

static gdouble
measure_tf_width(GwyDataField *psf)
{
    GwyDataField *mask, *abspsf;
    gint xres, yres;
    gdouble s2;

    xres = gwy_data_field_get_xres(psf);
    yres = gwy_data_field_get_yres(psf);
    mask = gwy_data_field_duplicate(psf);
    gwy_data_field_threshold(mask, 0.15*gwy_data_field_get_max(mask), 0.0, 1.0);
    if (gwy_data_field_get_val(mask, xres/2, yres/2) == 0.0) {
        g_object_unref(mask);
        return 0.0;
    }

    gwy_data_field_grains_extract_grain(mask, xres/2, yres/2);
    gwy_data_field_grains_grow(mask, 0.5*log(xres*yres),
                               GWY_DISTANCE_TRANSFORM_EUCLIDEAN, FALSE);
    abspsf = gwy_data_field_duplicate(psf);
    gwy_data_field_abs(abspsf);
    s2 = gwy_data_field_area_get_dispersion(abspsf, mask, GWY_MASK_INCLUDE,
                                            0, 0, xres, yres, NULL, NULL);
    g_object_unref(mask);
    g_object_unref(abspsf);

    return sqrt(s2);
}

static gboolean
method_is_full_sized(PSFMethodType method)
{
    return (method == PSF_METHOD_REGULARISED
            || method == PSF_METHOD_PSEUDO_WIENER);
}

static void
estimate_tf_region(GwyDataField *wmeas, GwyDataField *wideal,
                   GwyDataField *psf,  /* scratch buffer */
                   gint *col, gint *row, gint *width, gint *height)
{
    gint xres, yres, i, j, imin, jmin, imax, jmax, ext;
    const gdouble *d;
    gdouble m;

    xres = gwy_data_field_get_xres(wmeas);
    yres = gwy_data_field_get_yres(wmeas);
    *col = xres/3;
    *row = yres/3;
    *width = xres - 2*(*col);
    *height = yres - 2*(*row);
    /* Use a fairly large but not yet insane sigma value 4.0 to estimate the
     * width.  We want to err on the side of size overestimation here.
     * XXX: We might want to use a proportional to 1/sqrt(xres*yres) here. */
    gwy_data_field_deconvolve_regularized(wmeas, wideal, psf, 4.0);
    d = gwy_data_field_get_data_const(psf);

    /* FIXME: From here it the same as to libprocess/filter.c
     * psf_sigmaopt_estimate_size(). */
    imax = yres/2;
    jmax = xres/2;
    m = 0.0;
    for (i = *row; i < *row + *height; i++) {
        for (j = *col; j < *col + *width; j++) {
            if (d[i*xres + j] > m) {
                m = d[i*xres + j];
                imax = i;
                jmax = j;
            }
        }
    }
    gwy_debug("maximum %g at (%d,%d)", m, imax, jmax);
    gwy_data_field_threshold(psf, 0.05*m, 0.0, 1.0);
    g_return_if_fail(d[imax*xres + jmax] > 0.0);
    gwy_data_field_grains_extract_grain(psf, jmax, imax);

    imin = imax;
    jmin = jmax;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            if (d[i*xres + j] > 0.0) {
                if (i < imin)
                    imin = i;
                if (i > imax)
                    imax = i;
                if (j < jmin)
                    jmin = j;
                if (j > jmax)
                    jmax = j;
            }
        }
    }

    ext = GWY_ROUND(0.5*log(xres*yres)) + 1;
    *col = jmin - ext;
    *row = imin - ext;
    *width = jmax+1 - jmin + 2*ext;
    *height = imax+1 - imin + 2*ext;
    if (*col < 0) {
        *width += *col;
        *col = 0;
    }
    if (*row < 0) {
        *height += *row;
        *row = 0;
    }
    if (*col + *width > xres)
        *width = xres - *col;
    if (*row + *height > yres)
        *height = yres - *row;

    gwy_debug("estimated region: %dx%d centered at (%d,%d)",
              *width, *height, *col + *width/2, *row + *height/2);

    /* Use some default reasonable size when things get out of hand... */
    *width = MIN(*width, xres/4);
    *height = MIN(*height, yres/4);
}

static void
symmetrise_tf_region(gint pos, gint len, gint res, gint *tres)
{
    gint epos = pos + len-1;
    len = MAX(epos, res-1 - pos) - MIN(pos, res-1 - epos) + 1;
    *tres = len | 1;
}

typedef struct {
    const PSFArgs *args;
    GwyDataField *psf;         /* Real-space PSF */
    GwyDataField *wideal;      /* Windowed ideal. */
    GwyDataField *wmeas;       /* Windowed measured. */
    gint col, row;
    gint width, height;
} PSFSigmaOptData;

static void
psf_sigmaopt_prepare(GwyDataField *measured, GwyDataField *ideal,
                     const PSFArgs *args,
                     PSFSigmaOptData *sodata)
{
    gwy_clear(sodata, 1);
    sodata->args = args;
    sodata->wideal = gwy_data_field_new_alike(ideal, FALSE);
    sodata->wmeas = gwy_data_field_new_alike(measured, FALSE);
    prepare_field(measured, sodata->wmeas, args->windowing);
    prepare_field(ideal, sodata->wideal, args->windowing);
    if (args->method == PSF_METHOD_PSEUDO_WIENER) {
        sodata->psf = gwy_data_field_new_alike(measured, FALSE);
        estimate_tf_region(sodata->wmeas, sodata->wideal, sodata->psf,
                           &sodata->col, &sodata->row,
                           &sodata->width, &sodata->height);
    }
}

static void
psf_sigmaopt_free(PSFSigmaOptData *sodata)
{
    GWY_OBJECT_UNREF(sodata->psf);
    g_object_unref(sodata->wmeas);
    g_object_unref(sodata->wideal);
}

static gdouble
psf_sigmaopt_evaluate(gdouble logsigma, gpointer user_data)
{
    PSFSigmaOptData *sodata = (PSFSigmaOptData*)user_data;
    const PSFArgs *args = sodata->args;
    GwyDataField *psf = sodata->psf;
    gdouble sigma, w;

    g_assert(args->method == PSF_METHOD_PSEUDO_WIENER);
    sigma = exp(logsigma);
    psf_deconvolve_wiener(sodata->wmeas, sodata->wideal, psf, sigma);
    gwy_data_field_area_abs(psf,
                            sodata->col, sodata->row,
                            sodata->width, sodata->height);
    w = gwy_data_field_area_get_dispersion(psf, NULL, GWY_MASK_IGNORE,
                                           sodata->col, sodata->row,
                                           sodata->width, sodata->height,
                                           NULL, NULL);
    return sqrt(w);
}

static gdouble
find_regularization_sigma(GwyDataField *dfield,
                          GwyDataField *ideal,
                          const PSFArgs *args)
{
    PSFSigmaOptData sodata;
    gdouble logsigma, sigma;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(dfield), 0.0);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(ideal), 0.0);
    g_return_val_if_fail(!gwy_data_field_check_compatibility
                                          (dfield, ideal,
                                           GWY_DATA_COMPATIBILITY_RES
                                           | GWY_DATA_COMPATIBILITY_REAL
                                           | GWY_DATA_COMPATIBILITY_LATERAL),
                         0.0);

    psf_sigmaopt_prepare(dfield, ideal, args, &sodata);
    if (args->method == PSF_METHOD_REGULARISED) {
        sigma = gwy_data_field_find_regularization_sigma_for_psf(sodata.wmeas,
                                                                 sodata.wideal);
    }
    else if (args->method == PSF_METHOD_LEAST_SQUARES) {
        sigma = gwy_data_field_find_regularization_sigma_leastsq(sodata.wmeas,
                                                                 sodata.wideal,
                                                                 args->txres,
                                                                 args->tyres,
                                                                 args->border);
    }
    else {
        logsigma = gwy_math_find_minimum_1d(psf_sigmaopt_evaluate,
                                            log(1e-8), log(1e3), &sodata);
        /* Experimentally determined fudge factor from large-scale
         * simulations. */
        sigma = 0.375*exp(logsigma);
    }
    psf_sigmaopt_free(&sodata);

    return sigma;
}

static void
set_transfer_function_units(GwyDataField *ideal, GwyDataField *measured, GwyDataField *transferfunc)
{
    GwySIUnit *sunit, *iunit, *tunit, *xyunit;

    xyunit = gwy_data_field_get_si_unit_xy(measured);
    sunit = gwy_data_field_get_si_unit_z(ideal);
    iunit = gwy_data_field_get_si_unit_z(measured);
    tunit = gwy_data_field_get_si_unit_z(transferfunc);
    gwy_si_unit_divide(iunit, sunit, tunit);
    gwy_si_unit_power_multiply(tunit, 1, xyunit, -2, tunit);
}


/* This is an exact replica of gwy_data_field_deconvolve_regularized().
 * The only difference is that instead of σ² the regularisation term is
 * σ²/|P|², corresponding to pseudo-Wiener filter with the assumption of
 * uncorrelated point noise. */
static void
psf_deconvolve_wiener(GwyDataField *dfield,
                      GwyDataField *operand,
                      GwyDataField *out,
                      gdouble sigma)
{
    gint xres, yres, i, cstride;
    gdouble lambda, q, r, frms, orms;
    fftw_complex *ffield, *foper;
    fftw_plan fplan, bplan;

    g_return_if_fail(GWY_IS_DATA_FIELD(dfield));
    g_return_if_fail(GWY_IS_DATA_FIELD(operand));
    g_return_if_fail(GWY_IS_DATA_FIELD(out));

    xres = dfield->xres;
    yres = dfield->yres;
    cstride = xres/2 + 1;
    g_return_if_fail(operand->xres == xres);
    g_return_if_fail(operand->yres == yres);
    gwy_data_field_resample(out, xres, yres, GWY_INTERPOLATION_NONE);

    orms = gwy_data_field_get_rms(operand);
    frms = gwy_data_field_get_rms(dfield);
    if (!orms) {
        g_warning("Deconvolution by zero.");
        gwy_data_field_clear(out);
        return;
    }
    if (!frms) {
        gwy_data_field_clear(out);
        return;
    }

    ffield = gwy_fftw_new_complex(cstride*yres);
    foper = gwy_fftw_new_complex(cstride*yres);
#if defined(_OPENMP) && defined(HAVE_FFTW_WITH_OPENMP)
    fftw_plan_with_nthreads(1);
#endif
    fplan = fftw_plan_dft_r2c_2d(yres, xres, out->data, ffield,
                                 FFTW_DESTROY_INPUT);
    bplan = fftw_plan_dft_c2r_2d(yres, xres, ffield, out->data,
                                 FFTW_DESTROY_INPUT);

    gwy_data_field_copy(operand, out, FALSE);
    fftw_execute(fplan);
    gwy_assign(foper, ffield, cstride*yres);

    gwy_data_field_copy(dfield, out, FALSE);
    fftw_execute(fplan);
    fftw_destroy_plan(fplan);

    /* This seems wrong, but we just compensate the FFT. */
    orms *= sqrt(xres*yres);
    /* XXX: Is this correct now? */
    frms *= sqrt(xres*yres);
    lambda = sigma*sigma * orms*orms * frms*frms;
    /* NB: We normalize it as an integral.  So one recovers the convolution
     * with TRUE in ext-convolve! */
    q = 1.0/(dfield->xreal * dfield->yreal);
    for (i = 1; i < cstride*yres; i++) {
        gdouble fre = gwycreal(ffield[i]), fim = gwycimag(ffield[i]);
        gdouble ore = gwycreal(foper[i]), oim = gwycimag(foper[i]);
        gdouble inorm = ore*ore + oim*oim;
        gdouble fnorm = fre*fre + fim*fim;
        gdouble f = fnorm/(inorm*fnorm + lambda);
        gwycreal(ffield[i]) = (fre*ore + fim*oim)*f;
        gwycimag(ffield[i]) = (-fre*oim + fim*ore)*f;
    }
    fftw_free(foper);
    gwycreal(ffield[0]) = gwycimag(ffield[0]) = 0.0;
    fftw_execute(bplan);
    fftw_destroy_plan(bplan);
    fftw_free(ffield);

    gwy_data_field_multiply(out, q);
    gwy_data_field_2dfft_humanize(out);

    out->xreal = dfield->xreal;
    out->yreal = dfield->yreal;

    r = (xres + 1 - xres % 2)/2.0;
    gwy_data_field_set_xoffset(out, -gwy_data_field_jtor(out, r));

    r = (xres + 1 - xres % 2)/2.0;
    gwy_data_field_set_yoffset(out, -gwy_data_field_itor(out, r));

    gwy_data_field_invalidate(out);
    set_transfer_function_units(operand, dfield, out);
}

static gboolean
clamp_psf_size(GwyBrick *brick, PSFArgs *args)
{
    gint xres, yres;

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    if (MIN(xres, yres) < 24)
        return FALSE;

    if (method_is_full_sized(args->method)) {
        args->txres = CLAMP(args->txres, 3, xres);
        args->tyres = CLAMP(args->tyres, 3, yres);
    }
    else {
        args->txres = CLAMP(args->txres, 3, xres/3 | 1);
        args->tyres = CLAMP(args->tyres, 3, yres/3 | 1);
    }
    args->border = CLAMP(args->border, 0, MIN(xres, yres)/8);
    return TRUE;
}

static const gchar as_integral_key[]    = "/module/volume_psf/as_integral";
static const gchar border_key[]         = "/module/volume_psf/border";
static const gchar display_key[]        = "/module/volume_psf/display";
static const gchar estimate_sigma_key[] = "/module/volume_psf/estimate_sigma";
static const gchar estimate_tres_key[]  = "/module/volume_psf/estimate_tres";
static const gchar method_key[]         = "/module/volume_psf/method";
static const gchar output_type_key[]    = "/module/volume_psf/output_type";
static const gchar sigma_key[]          = "/module/volume_psf/sigma";
static const gchar txres_key[]          = "/module/volume_psf/txres";
static const gchar tyres_key[]          = "/module/volume_psf/tyres";
static const gchar windowing_key[]      = "/module/volume_psf/windowing";
static const gchar zlevel_key[]         = "/module/volume_psf/zlevel";

static void
psf_sanitize_args(PSFArgs *args)
{
    gwy_app_data_id_verify_channel(&args->op2);
    args->output_type &= PSF_OUTPUTS_MASK;
    args->sigma = CLAMP(args->sigma, -8.0, 3.0);
    args->as_integral = !!args->as_integral;
    args->estimate_sigma = !!args->estimate_sigma;
    args->estimate_tres = !!args->estimate_tres;
    args->display = CLAMP(args->display, 0, PSF_NDISPLAYS-1);
    args->method = CLAMP(args->method, 0, PSF_NMETHODS-1);
    args->windowing = gwy_enum_sanitize_value(args->windowing,
                                              GWY_TYPE_WINDOWING_TYPE);
}

static void
psf_load_args(GwyContainer *container, PSFArgs *args)
{
    *args = psf_defaults;

    gwy_container_gis_enum_by_name(container, method_key, &args->method);
    gwy_container_gis_enum_by_name(container, display_key, &args->display);
    gwy_container_gis_enum_by_name(container, output_type_key,
                                   &args->output_type);
    gwy_container_gis_double_by_name(container, sigma_key, &args->sigma);
    gwy_container_gis_enum_by_name(container, windowing_key, &args->windowing);
    gwy_container_gis_int32_by_name(container, zlevel_key, &args->zlevel);
    gwy_container_gis_boolean_by_name(container, as_integral_key,
                                      &args->as_integral);
    gwy_container_gis_boolean_by_name(container, estimate_sigma_key,
                                      &args->estimate_sigma);
    gwy_container_gis_boolean_by_name(container, estimate_tres_key,
                                      &args->estimate_tres);
    gwy_container_gis_int32_by_name(container, txres_key, &args->txres);
    gwy_container_gis_int32_by_name(container, tyres_key, &args->tyres);
    gwy_container_gis_int32_by_name(container, border_key, &args->border);
    args->op2 = op2_id;

    psf_sanitize_args(args);
}

static void
psf_save_args(GwyContainer *container, PSFArgs *args)
{
    op2_id = args->op2;

    gwy_container_set_enum_by_name(container, method_key, args->method);
    gwy_container_set_enum_by_name(container, display_key, args->display);
    gwy_container_set_enum_by_name(container, output_type_key,
                                   args->output_type);
    gwy_container_set_double_by_name(container, sigma_key, args->sigma);
    gwy_container_set_enum_by_name(container, windowing_key, args->windowing);
    gwy_container_set_int32_by_name(container, zlevel_key, args->zlevel);
    gwy_container_set_boolean_by_name(container, as_integral_key,
                                      args->as_integral);
    gwy_container_set_boolean_by_name(container, estimate_sigma_key,
                                      args->estimate_sigma);
    gwy_container_set_boolean_by_name(container, estimate_tres_key,
                                      args->estimate_tres);
    gwy_container_set_int32_by_name(container, txres_key, args->txres);
    gwy_container_set_int32_by_name(container, tyres_key, args->tyres);
    gwy_container_set_int32_by_name(container, border_key, args->border);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
