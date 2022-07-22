/*
 *  $Id: mfm_recalc.c 21471 2018-09-26 19:47:48Z klapetek $
 *  Copyright (C) 2018 David Necas (Yeti), Petr Klapetek.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyenum.h>
#include <libprocess/stats.h>
#include <libprocess/inttrans.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/mfm.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"
#include "mfmops.h"

#define MFM_RECALC_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum  {
    SIGNAL_PHASE_DEG = 0,
    SIGNAL_PHASE_RAD = 1,
    SIGNAL_FREQUENCY = 2,
    SIGNAL_AMPLITUDE_V = 3,
    SIGNAL_AMPLITUDE_M = 4
} MfmRecalcSignal;


typedef struct {
    MfmRecalcSignal signal;
    gdouble spring_constant;
    gdouble quality;
    gdouble base_frequency;
    gdouble base_amplitude;
    gboolean new_channel;
    GwyMFMGradientType result;
} MfmRecalcArgs;

typedef struct {
    MfmRecalcArgs *args;
    GSList *signal;
    GtkObject *spring_constant;
    GtkObject *quality;
    GtkObject *base_frequency;
    GtkObject *base_amplitude;
    GtkWidget *new_channel;
    GtkWidget *result;
} MfmRecalcControls;

static gboolean module_register                (void);
static void     mfm_recalc                     (GwyContainer *data,
                                                GwyRunType run);
static gboolean mfm_recalc_dialog              (MfmRecalcArgs *args,
                                                MfmRecalcSignal guess);
static void     mfm_recalc_dialog_update       (MfmRecalcControls *controls);
static void     mfm_recalc_load_args           (GwyContainer *container,
                                                MfmRecalcArgs *args);
static void     mfm_recalc_save_args           (GwyContainer *container,
                                                MfmRecalcArgs *args);
static void     mfm_recalc_sanitize_args       (MfmRecalcArgs *args);
static void     update_sensitivity             (MfmRecalcControls *controls);
static void     signal_changed                 (GtkToggleButton *toggle,
                                                MfmRecalcControls *controls);
static void     new_channel_changed            (GtkToggleButton *check,
                                                MfmRecalcControls *controls);
static void     mfm_recalc_dialog_update_values(MfmRecalcControls *controls,
                                                MfmRecalcArgs *args);

static const MfmRecalcArgs mfm_recalc_defaults = {
    SIGNAL_PHASE_DEG, 40, 1000, 150, 0.2, TRUE, GWY_MFM_GRADIENT_MFM,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Converts the MFM data to force gradient."),
    "Petr Klapetek <klapetek@gwyddion.net>, Robb Puttock <robb.puttock@npl.co.uk>",
    "1.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2018",
};

GWY_MODULE_QUERY2(module_info, mfm_recalc)

static gboolean
module_register(void)
{
    gwy_process_func_register("mfm_recalc",
                              (GwyProcessFunc)&mfm_recalc,
                              N_("/SPM M_odes/_Magnetic/_Recalculate to Force Gradient..."),
                              GWY_STOCK_MFM_CONVERT_TO_FORCE,
                              MFM_RECALC_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Recalculate to force gradient"));

    return TRUE;
}

static void
issue_warning(GtkWindow *window)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new(window,
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_OK,
                                    _("Data value units must be "
                                      "deg, rad, m, Hz or V "
                                      "for the recalculation"));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
mfm_recalc(GwyContainer *data, GwyRunType run)
{
    GwyDataField *dfield, *out;
    MfmRecalcArgs args;
    gboolean ok;
    gint newid, oldid;
    GwySIUnit *zunit;
    GQuark dquark;
    MfmRecalcSignal guess;

    g_return_if_fail(run & MFM_RECALC_RUN_MODES);
    mfm_recalc_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     GWY_APP_DATA_FIELD_KEY, &dquark,
                                     0);
    g_return_if_fail(dfield);

    //guess the units
    zunit = gwy_data_field_get_si_unit_z(dfield);
    if (gwy_si_unit_equal_string(zunit, "deg"))
        guess = SIGNAL_PHASE_DEG;
    else if (gwy_si_unit_equal_string(zunit, "rad"))
        guess = SIGNAL_PHASE_RAD;
    else if (gwy_si_unit_equal_string(zunit, "Hz"))
        guess = SIGNAL_FREQUENCY;
    else if (gwy_si_unit_equal_string(zunit, "V"))
        guess = SIGNAL_AMPLITUDE_V;
    else if (gwy_si_unit_equal_string(zunit, "m"))
        guess = SIGNAL_AMPLITUDE_M;
    else {
        issue_warning(gwy_app_find_window_for_channel(data, oldid));
        return;
    }

    args.signal = guess;

    if (run == GWY_RUN_INTERACTIVE) {
        ok = mfm_recalc_dialog(&args, guess);
        mfm_recalc_save_args(gwy_app_settings_get(), &args);
        if (!ok)
            return;
    }

    if (args.new_channel)
        out = gwy_data_field_duplicate(dfield);
    else {
        gwy_app_undo_qcheckpointv(data, 1, &dquark);
        out = dfield;
    }

    if (args.signal == SIGNAL_PHASE_DEG) {
        gwy_data_field_mfm_phase_to_force_gradient(out,
                                                   args.spring_constant,
                                                   args.quality,
                                                   args.result);
        gwy_data_field_multiply(out, M_PI/180.0);
    }
    else if (args.signal == SIGNAL_PHASE_RAD) {
        gwy_data_field_mfm_phase_to_force_gradient(out,
                                                   args.spring_constant,
                                                   args.quality,
                                                   args.result);
    }
    else if (args.signal == SIGNAL_FREQUENCY) {
        gwy_data_field_mfm_frequency_shift_to_force_gradient(out,
                                                             args.spring_constant,
                                                             args.base_frequency,
                                                             args.result);
    }
    else if (args.signal == SIGNAL_AMPLITUDE_M) {
        gwy_data_field_mfm_amplitude_shift_to_force_gradient(out,
                                                             args.spring_constant,
                                                             args.quality,
                                                             args.base_amplitude*1e-9,
                                                             args.result);
    }
    else {
        g_assert_not_reached();
    }

    if (args.new_channel) {
        newid = gwy_app_data_browser_add_data_field(out, data, TRUE);
        gwy_app_set_data_field_title(data, newid, _("Recalculated MFM data"));
        gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                                GWY_DATA_ITEM_PALETTE, 0);
        gwy_app_channel_log_add_proc(data, oldid, newid);
        g_object_unref(out);
    }
    else {
        gwy_data_field_data_changed(out);
        gwy_app_channel_log_add_proc(data, oldid, oldid);
    }
}

static gboolean
mfm_recalc_dialog(MfmRecalcArgs *args, MfmRecalcSignal guess)
{
    GtkWidget *dialog, *table;
    MfmRecalcControls controls;
    gint row, response;

    static const GwyEnum signals[] = {
        { N_("Phase (radians)"),   SIGNAL_PHASE_RAD, },
        { N_("Phase (degrees)"),   SIGNAL_PHASE_DEG, },
        { N_("Frequency shift"),   SIGNAL_FREQUENCY, },
        { N_("Amplitude (V)"),     SIGNAL_AMPLITUDE_V, },
        { N_("Amplitude (m)"),     SIGNAL_AMPLITUDE_M, },
    };
    static const GwyEnum results[] = {
        { N_("Force gradient"),                GWY_MFM_GRADIENT_FORCE, },
        { N_("MFM force gradient"),            GWY_MFM_GRADIENT_MFM, },
        { N_("Pixel area MFM force gradient"), GWY_MFM_GRADIENT_MFM_AREA, },
    };


    dialog = gtk_dialog_new_with_buttons(_("MFM Recalculate Data"), NULL, 0,
                                         _("_Reset"), RESPONSE_RESET,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);

    controls.args = args;

    table = gtk_table_new(10, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), table,
                       FALSE, FALSE, 4);
    row = 0;

    controls.signal
        = gwy_radio_buttons_create(signals, G_N_ELEMENTS(signals),
                                   G_CALLBACK(signal_changed),
                                   &controls, args->signal);
    row = gwy_radio_buttons_attach_to_table(controls.signal,
                                            GTK_TABLE(table), 2, row);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.spring_constant = gtk_adjustment_new(args->spring_constant,
                                                  0.01, 1000.0, 0.01, 1, 0);
    gwy_table_attach_adjbar(table, row, _("_Spring constant:"), "N/m",
                            controls.spring_constant, GWY_HSCALE_LOG);
    row++;

    controls.quality = gtk_adjustment_new(args->quality,
                                          0.01, 10000.0, 0.01, 1, 0);
    gwy_table_attach_adjbar(table, row, _("_Quality factor:"), NULL,
                            controls.quality, GWY_HSCALE_LOG);
    row++;

    controls.base_frequency = gtk_adjustment_new(args->base_frequency,
                                                 1, 1000000.0, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("_Base frequency:"), "Hz",
                            controls.base_frequency, GWY_HSCALE_LOG);
    row++;

    controls.base_amplitude = gtk_adjustment_new(args->base_amplitude,
                                                 0.01, 1000.0, 0.01, 1, 0);
    gwy_table_attach_adjbar(table, row, _("_Base amplitude:"), "V, nm",
                            controls.base_amplitude, GWY_HSCALE_LOG);
    row++;

    controls.result
        = gwy_enum_combo_box_new(results, G_N_ELEMENTS(results),
                                 G_CALLBACK(gwy_enum_combo_box_update_int),
                                 &args->result, args->result, TRUE);
    gwy_table_attach_adjbar(table, row, _("Result _type:"), NULL,
                            GTK_OBJECT(controls.result),
                            GWY_HSCALE_WIDGET_NO_EXPAND);

    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.new_channel
                  = gtk_check_button_new_with_mnemonic(_("Create new image"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.new_channel),
                                 args->new_channel);
    gtk_table_attach(GTK_TABLE(table), controls.new_channel,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(controls.new_channel, "toggled",
                     G_CALLBACK(new_channel_changed), &controls);
    row++;

    update_sensitivity(&controls);

    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            return FALSE;
            break;

            case GTK_RESPONSE_OK:
            mfm_recalc_dialog_update_values(&controls, args);
            break;

            case RESPONSE_RESET:
            *args = mfm_recalc_defaults;
            args->signal = guess;
            mfm_recalc_dialog_update(&controls);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);

    return TRUE;
}

static void
mfm_recalc_dialog_update(MfmRecalcControls *controls)
{
    MfmRecalcArgs *args = controls->args;

    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->spring_constant),
                             args->spring_constant);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->quality),
                             args->quality);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->base_frequency),
                             args->base_frequency);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->base_amplitude),
                             args->base_amplitude);
}

static void
mfm_recalc_dialog_update_values(MfmRecalcControls *controls,
                                MfmRecalcArgs *args)
{
    args->spring_constant
        = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->spring_constant));
    args->quality
        = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->quality));
    args->base_frequency
        = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->base_frequency));
    args->base_amplitude
        = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->base_amplitude));
}

static void
signal_changed(GtkToggleButton *toggle, MfmRecalcControls *controls)
{
    if (toggle && !gtk_toggle_button_get_active(toggle))
        return;

    controls->args->signal = gwy_radio_buttons_get_current(controls->signal);
    update_sensitivity(controls);
}

static void
new_channel_changed(GtkToggleButton *check,
                    MfmRecalcControls *controls)
{
    MfmRecalcArgs *args = controls->args;
    args->new_channel = gtk_toggle_button_get_active(check);
}

static void
update_sensitivity(MfmRecalcControls *controls)
{
    MfmRecalcArgs *args = controls->args;
    GtkWidget *label;
    gboolean is_phase, is_freq, is_amplitude;

    is_phase = (args->signal == SIGNAL_PHASE_DEG
                || args->signal == SIGNAL_PHASE_RAD);
    is_freq = (args->signal == SIGNAL_FREQUENCY);
    is_amplitude = (args->signal == SIGNAL_AMPLITUDE_V
                    || args->signal == SIGNAL_AMPLITUDE_M);

    gwy_table_hscale_set_sensitive(controls->base_frequency, is_freq);
    gwy_table_hscale_set_sensitive(controls->quality, is_phase || is_amplitude);
    gwy_table_hscale_set_sensitive(controls->base_amplitude, is_amplitude);

    /* Reset all to insensitive and then make the enabled buttons sensitive.
     * When there are more choices for one quantity type, this may become more
     * complex. */
    gwy_radio_buttons_set_sensitive(controls->signal, FALSE);
    gtk_widget_set_sensitive(gwy_radio_buttons_find(controls->signal,
                                                    args->signal), TRUE);

    label = gwy_table_hscale_get_units(controls->base_amplitude);
    /* This is correct, the signal is in [m], but the user enters base
     * amplitude in [nm]. */
    if (args->signal == SIGNAL_AMPLITUDE_M)
        gtk_label_set_text(GTK_LABEL(label), "nm");
    else
        gtk_label_set_text(GTK_LABEL(label), "V");
}

static const gchar base_amplitude_key[]  = "/module/mfm_recalc/base_amplitude";
static const gchar base_frequency_key[]  = "/module/mfm_recalc/base_frequency";
static const gchar new_channel_key[]     = "/module/mfm_recalc/new_channel";
static const gchar quality_key[]         = "/module/mfm_recalc/quality";
static const gchar signal_key[]          = "/module/mfm_recalc/signal";
static const gchar spring_constant_key[] = "/module/mfm_recalc/spring_constant";
static const gchar result_key[]          = "/module/mfm_recalc/result";


static void
mfm_recalc_sanitize_args(MfmRecalcArgs *args)
{
    args->signal = CLAMP(args->signal, 0, SIGNAL_AMPLITUDE_M);
    args->result = CLAMP(args->result, 0, GWY_MFM_GRADIENT_MFM_AREA);
    args->spring_constant = CLAMP(args->spring_constant, 0.01, 1000);
    args->quality = CLAMP(args->quality, 0.01, 10000);
    args->base_frequency = CLAMP(args->base_frequency, 1, 1000000);
    args->base_amplitude = CLAMP(args->base_amplitude, 0.1, 100);
    args->new_channel = !!args->new_channel;
}

static void
mfm_recalc_load_args(GwyContainer *container,
                     MfmRecalcArgs *args)
{
    *args = mfm_recalc_defaults;

    gwy_container_gis_enum_by_name(container, signal_key, &args->signal);
    gwy_container_gis_enum_by_name(container, result_key, &args->result);
    gwy_container_gis_double_by_name(container, spring_constant_key,
                                     &args->spring_constant);
    gwy_container_gis_double_by_name(container, quality_key, &args->quality);
    gwy_container_gis_double_by_name(container, base_frequency_key,
                                     &args->base_frequency);
    gwy_container_gis_double_by_name(container, base_amplitude_key,
                                     &args->base_amplitude);
    gwy_container_gis_boolean_by_name(container, new_channel_key,
                                      &args->new_channel);

    mfm_recalc_sanitize_args(args);
}

static void
mfm_recalc_save_args(GwyContainer *container,
                     MfmRecalcArgs *args)
{
    gwy_container_set_enum_by_name(container, signal_key, args->signal);
    gwy_container_set_enum_by_name(container, result_key, args->result);
    gwy_container_set_double_by_name(container, spring_constant_key,
                                     args->spring_constant);
    gwy_container_set_double_by_name(container, quality_key, args->quality);
    gwy_container_set_double_by_name(container, base_frequency_key,
                                     args->base_frequency);
    gwy_container_set_double_by_name(container, base_amplitude_key,
                                     args->base_amplitude);
    gwy_container_set_boolean_by_name(container, new_channel_key,
                                      args->new_channel);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
