/*
 *  $Id: mfm_current.c 23811 2021-06-08 12:12:43Z yeti-dn $
 *  Copyright (C) 2017 David Necas (Yeti), Petr Klapetek.
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyrandgenset.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/mfm.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils-synth.h>
#include <app/gwyapp.h>
#include "preview.h"
#include "dimensions.h"
#include "mfmops.h"

#define MFM_CURRENT_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PAGE_DIMENSIONS = 0,
    PAGE_GENERATOR  = 1,
    PAGE_NPAGES
};

typedef enum {
    GWY_MFM_CURRENT_OUTPUT_HX   = 0,
    GWY_MFM_CURRENT_OUTPUT_HZ   = 1,
    GWY_MFM_CURRENT_OUTPUT_FORCE   = 2,
    GWY_MFM_CURRENT_OUTPUT_FORCE_DX = 3,
    GWY_MFM_CURRENT_OUTPUT_FORCE_DDX  = 4
} GwyMfmCurrentOutputType;

typedef struct {
    gint active_page;
    gboolean update;
    GwyMfmCurrentOutputType out;
    GwyMFMProbeType probe;
    gdouble height;
    gdouble current;
    gdouble width;
    gdouble position;
    gdouble mtip;
    gdouble bx;
    gdouble by;
    gdouble length;

} MfmCurrentArgs;

typedef struct {
    MfmCurrentArgs *args;
    GwyDimensions *dims;
    GtkWidget *dialog;
    GtkWidget *view;
    GtkWidget *update;
    GtkWidget *update_now;
    GtkWidget *out;
    GtkWidget *probe;
    GtkObject *height;
    GtkObject *current;
    GtkObject *mtip;
    GtkObject *bx;
    GtkObject *by;
    GtkObject *length;
    GtkObject *width;
    GtkObject *position;
    GtkObject *size_b;
    GtkObject *size_c;
    GtkTable *table;
    GwyContainer *mydata;
    GwyDataField *surface;
    gdouble pxsize;
    gdouble zscale;
    gboolean in_init;
    gulong sid;
} MfmCurrentControls;

static gboolean      module_register           (void);
static void          mfm_current               (GwyContainer *data,
                                                GwyRunType run);
static void          run_noninteractive        (MfmCurrentArgs *args,
                                                const GwyDimensionArgs *dimsargs,
                                                GwyContainer *data,
                                                GwyDataField *dfield,
                                                gint oldid,
                                                GQuark quark);
static gboolean      mfm_current_dialog        (MfmCurrentArgs *args,
                                                GwyDimensionArgs *dimsargs,
                                                GwyContainer *data,
                                                GwyDataField *dfield,
                                                gint id);
static void          update_controls           (MfmCurrentControls *controls,
                                                MfmCurrentArgs *args);
static void          page_switched             (MfmCurrentControls *controls,
                                                GtkNotebookPage *page,
                                                gint pagenum);
static void          update_values             (MfmCurrentControls *controls);
static void          mfm_current_invalidate    (MfmCurrentControls *controls);
static gboolean      preview_gsource           (gpointer user_data);
static void          preview                   (MfmCurrentControls *controls);
static void          mfm_current_do            (const MfmCurrentArgs *args,
                                                const GwyDimensionArgs *dimsargs,
                                                GwyDataField *dfield);
static void          mfm_current_load_args     (GwyContainer *container,
                                                MfmCurrentArgs *args,
                                                GwyDimensionArgs *dimsargs);
static void          mfm_current_save_args     (GwyContainer *container,
                                                const MfmCurrentArgs *args,
                                                const GwyDimensionArgs *dimsargs);
static void          probe_changed             (GtkComboBox *combo,
                                                MfmCurrentControls *controls);
static void          out_changed               (GtkComboBox *combo,
                                                MfmCurrentControls *controls);

static void          update_sensitivity        (MfmCurrentControls *controls);
static void          xyunits_changed           (MfmCurrentControls *controls);


#define GWY_SYNTH_CONTROLS MfmCurrentControls
#define GWY_SYNTH_INVALIDATE(controls) \
    mfm_current_invalidate(controls)

#include "synth.h"


static const MfmCurrentArgs mfm_current_defaults = {
    PAGE_DIMENSIONS, TRUE,
    GWY_MFM_CURRENT_OUTPUT_HZ,
    GWY_MFM_PROBE_CHARGE,
    100, 1, 100,
    50, 1,
    10, 10, 500,

};


static const GwyDimensionArgs dims_defaults = MFM_DIMENSION_ARGS_INIT;


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Simulation of current line magnetic field"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.2",
    "David Nečas (Yeti) & Petr Klapetek",
    "2017",
};

GWY_MODULE_QUERY2(module_info, mfm_current)

static gboolean
module_register(void)
{
    gwy_process_func_register("mfm_current",
                              (GwyProcessFunc)&mfm_current,
                              N_("/SPM M_odes/_Magnetic/_Current Line Field..."),
                              GWY_STOCK_MFM_CURRENT_LINE,
                              MFM_CURRENT_RUN_MODES,
                              0,
                              N_("Simulate stray field above current line"));

    return TRUE;
}


static void
mfm_current(GwyContainer *data, GwyRunType run)
{
    MfmCurrentArgs args;
    GwyDimensionArgs dimsargs;
    GwyDataField *dfield;
    GQuark quark;
    gint id;

    g_return_if_fail(run & MFM_CURRENT_RUN_MODES);
    mfm_current_load_args(gwy_app_settings_get(), &args, &dimsargs);

    //this should be always in meters at start
    dimsargs.xyunits = g_strdup("m");

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_DATA_FIELD_KEY, &quark,
                                     0);

    if (run == GWY_RUN_IMMEDIATE
        || mfm_current_dialog(&args, &dimsargs, data, dfield, id))
        run_noninteractive(&args, &dimsargs, data, dfield, id, quark);

    if (run == GWY_RUN_INTERACTIVE)
        mfm_current_save_args(gwy_app_settings_get(), &args, &dimsargs);

    gwy_dimensions_free_args(&dimsargs);
}

static void
run_noninteractive(MfmCurrentArgs *args,
                   const GwyDimensionArgs *dimsargs,
                   GwyContainer *data,
                   GwyDataField *dfield,
                   gint oldid,
                   GQuark quark)
{
    GwySIUnit *siunit;
    gboolean replace = dimsargs->replace && dfield;
    gboolean add = dimsargs->add && dfield;
    gint newid;

    if (replace) {
        /* Always take a reference so that we can always unref. */
        g_object_ref(dfield);

        gwy_app_undo_qcheckpointv(data, 1, &quark);
        if (!add)
            gwy_data_field_clear(dfield);

        gwy_app_channel_log_add_proc(data, oldid, oldid);
    }
    else {
        if (add)
            dfield = gwy_data_field_duplicate(dfield);
        else {
            gdouble mag = pow10(dimsargs->xypow10) * dimsargs->measure;
            dfield = gwy_data_field_new(dimsargs->xres, dimsargs->yres,
                                        mag*dimsargs->xres, mag*dimsargs->yres,
                                        TRUE);

            siunit = gwy_data_field_get_si_unit_xy(dfield);
            gwy_si_unit_set_from_string(siunit, dimsargs->xyunits);

            siunit = gwy_data_field_get_si_unit_z(dfield);
            gwy_si_unit_set_from_string(siunit, dimsargs->zunits);
        }
    }

    mfm_current_do(args, dimsargs, dfield);

    if (replace)
        gwy_data_field_data_changed(dfield);
    else {
        if (data) {
            newid = gwy_app_data_browser_add_data_field(dfield, data, TRUE);
            if (oldid != -1)
                gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                                        GWY_DATA_ITEM_GRADIENT,
                                        0);
        }
        else {
            newid = 0;
            data = gwy_container_new();
            gwy_container_set_object(data, gwy_app_get_data_key_for_id(newid),
                                     dfield);
            gwy_app_data_browser_add(data);
            gwy_app_data_browser_reset_visibility(data,
                                                  GWY_VISIBILITY_RESET_SHOW_ALL);
            g_object_unref(data);
        }

        gwy_app_set_data_field_title(data, newid, _("Simulated field"));
        gwy_app_channel_log_add_proc(data, add ? oldid : -1, newid);
    }
    g_object_unref(dfield);
}

static gboolean
mfm_current_dialog(MfmCurrentArgs *args,
                 GwyDimensionArgs *dimsargs,
                 GwyContainer *data,
                 GwyDataField *dfield_template,
                 gint id)
{
    GtkWidget *dialog, *table, *vbox, *hbox, *notebook;
    MfmCurrentControls controls;
    GwyDataField *dfield;
    gboolean finished;
    gint response;
    gint row;
    static const GwyEnum mfm_current_outputs[] = {
        {
            "H<sub>x</sub>",
            GWY_MFM_CURRENT_OUTPUT_HX,
        },
        {
            "H<sub>z</sub>",
            GWY_MFM_CURRENT_OUTPUT_HZ,
        },
        {
            "F<sub>z</sub>",
            GWY_MFM_CURRENT_OUTPUT_FORCE,
        },
        {
            "dF<sub>z</sub>/dz",
            GWY_MFM_CURRENT_OUTPUT_FORCE_DX,
        },
        {
            "d<sup>2</sup>F<sub>z</sub>/dz<sup>2</sup>",
            GWY_MFM_CURRENT_OUTPUT_FORCE_DDX,
        },
    };
    static const GwyEnum mfm_current_probes[] = {
        { N_("Point charge"), GWY_MFM_PROBE_CHARGE, },
        { N_("Bar"),          GWY_MFM_PROBE_BAR,   },
    };


    gwy_clear(&controls, 1);
    controls.in_init = TRUE;
    controls.args = args;
    dialog = gtk_dialog_new_with_buttons(_("Current Line Stray Field"),
                                         NULL, 0,
                                         _("_Reset"), RESPONSE_RESET,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    hbox = gtk_hbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    vbox = gtk_vbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 4);

    controls.mydata = gwy_container_new();
    dfield = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE,
                                pow10(dimsargs->xypow10)*dimsargs->measure*PREVIEW_SIZE,
                                pow10(dimsargs->xypow10)*dimsargs->measure*PREVIEW_SIZE,
                                TRUE);
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    if (dfield_template) {
        gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                                GWY_DATA_ITEM_PALETTE,
                                0);
        controls.surface = gwy_synth_make_preview_data_field(dfield_template,
                                                             PREVIEW_SIZE);
        controls.zscale = gwy_data_field_get_rms(dfield_template);
    }
    controls.view = gwy_create_preview(controls.mydata, 0, PREVIEW_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), controls.view, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox),
                       gwy_synth_instant_updates_new(&controls,
                                                     &controls.update_now,
                                                     &controls.update,
                                                     &args->update),
                       FALSE, FALSE, 0);
    g_signal_connect_swapped(controls.update_now, "clicked",
                             G_CALLBACK(preview), &controls);



    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 4);
    g_signal_connect_swapped(notebook, "switch-page",
                             G_CALLBACK(page_switched), &controls);

    controls.dims = gwy_dimensions_new(dimsargs, dfield_template);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             gwy_dimensions_get_widget(controls.dims),
                             gtk_label_new(_("Dimensions")));
    if (controls.dims->add)
        g_signal_connect_swapped(controls.dims->add, "toggled",
                                 G_CALLBACK(mfm_current_invalidate), &controls);

    g_signal_connect_swapped(controls.dims->xypow10, "changed",
                             G_CALLBACK(xyunits_changed), &controls);
    g_signal_connect_swapped(controls.dims->xreal, "value-changed",
                             G_CALLBACK(xyunits_changed), &controls);
    g_signal_connect_swapped(controls.dims->yreal, "value-changed",
                             G_CALLBACK(xyunits_changed), &controls);


    gtk_widget_set_no_show_all(controls.dims->xyunits, TRUE);
    gtk_widget_hide(controls.dims->xyunits);
    gtk_widget_set_no_show_all(controls.dims->zunits, TRUE);
    gtk_widget_hide(controls.dims->zunits);
    gtk_widget_set_no_show_all(controls.dims->zpow10, TRUE);
    gtk_widget_hide(controls.dims->zpow10);
    gtk_widget_set_no_show_all(controls.dims->unit_z_label, TRUE);
    gtk_widget_hide(controls.dims->unit_z_label);


    table = gtk_table_new(8 + (dfield_template ? 1 : 0), 3, FALSE);
    controls.table = GTK_TABLE(table);
    gtk_table_set_row_spacings(controls.table, 2);
    gtk_table_set_col_spacings(controls.table, 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table,
                             gtk_label_new(_("Generator")));
    row = 0;

    gtk_table_attach(GTK_TABLE(table), gwy_label_new_header(_("Output")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.height = gtk_adjustment_new(args->height, 1, 1000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("_Output plane height:"), "nm",
                            controls.height, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.height), "controls", &controls);
    g_signal_connect_swapped(controls.height, "value-changed",
                     G_CALLBACK(update_values), &controls);
    row++;

    controls.width = gtk_adjustment_new(args->width, 1, 1000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("_Stripe width:"), "nm",
                            controls.width, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.width), "controls", &controls);
    g_signal_connect_swapped(controls.width, "value-changed",
                     G_CALLBACK(update_values), &controls);
    row++;

    controls.current = gtk_adjustment_new(args->current, -1000, 1000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Stripe _current:"), "mA",
                            controls.current, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.current), "controls", &controls);
    g_signal_connect_swapped(controls.current, "value-changed",
                     G_CALLBACK(update_values), &controls);
    row++;

    controls.position = gtk_adjustment_new(args->position, 0, 100, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("_Position:"), "%",
                            controls.position, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.position), "controls", &controls);
    g_signal_connect_swapped(controls.position, "value-changed",
                     G_CALLBACK(update_values), &controls);
    row++;

    controls.out = gwy_enum_combo_box_new(mfm_current_outputs,
                                          G_N_ELEMENTS(mfm_current_outputs),
                                          G_CALLBACK(out_changed),
                                          &controls, args->out, TRUE);
    gwy_table_attach_adjbar(table, row, _("Output _type:"), NULL,
                            GTK_OBJECT(controls.out),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    gtk_table_attach(GTK_TABLE(table), gwy_label_new_header(_("Probe")),
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.probe = gwy_enum_combo_box_new(mfm_current_probes,
                                            G_N_ELEMENTS(mfm_current_probes),
                                            G_CALLBACK(probe_changed),
                                            &controls, args->probe, TRUE);
    gwy_table_attach_adjbar(table, row, _("P_robe type:"), NULL,
                            GTK_OBJECT(controls.probe),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    controls.mtip = gtk_adjustment_new(args->mtip, 1, 10000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Tip _magnetization:"), "kA/m",
                            controls.mtip, GWY_HSCALE_LOG);
    g_object_set_data(G_OBJECT(controls.mtip), "controls", &controls);
    g_signal_connect_swapped(controls.mtip, "value-changed",
                     G_CALLBACK(update_values), &controls);
    row++;


    controls.bx = gtk_adjustment_new(args->bx, 1, 1000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Bar width _x:"), "nm",
                            controls.bx, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.bx), "controls", &controls);
    g_signal_connect_swapped(controls.bx, "value-changed",
                     G_CALLBACK(update_values), &controls);
    row++;


    controls.by = gtk_adjustment_new(args->by, 1, 1000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Bar width _y:"), "nm",
                            controls.by, GWY_HSCALE_SQRT);
    g_object_set_data(G_OBJECT(controls.by), "controls", &controls);
    g_signal_connect_swapped(controls.by, "value-changed",
                     G_CALLBACK(update_values), &controls);
    row++;


    controls.length = gtk_adjustment_new(args->length, 1, 10000, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("Bar length (_z):"), "nm",
                            controls.length, GWY_HSCALE_LOG);
    g_object_set_data(G_OBJECT(controls.length), "controls", &controls);
    g_signal_connect_swapped(controls.length, "value-changed",
                     G_CALLBACK(update_values), &controls);
    row++;


    update_sensitivity(&controls);

    gtk_widget_show_all(dialog);
    controls.in_init = FALSE;
    /* Must be done when widgets are shown, see GtkNotebook docs */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), args->active_page);
    update_values(&controls);
    mfm_current_invalidate(&controls);

    finished = FALSE;
    while (!finished) {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            case GTK_RESPONSE_OK:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            finished = TRUE;
            break;

            case RESPONSE_RESET:
            {
                gboolean temp = args->update;
                gint temp2 = args->active_page;
                *args = mfm_current_defaults;
                args->active_page = temp2;
                args->update = temp;
            }
            controls.in_init = TRUE;
            update_controls(&controls, args);
            controls.in_init = FALSE;
            if (args->update)
                preview(&controls);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    }

    if (controls.sid) {
        g_source_remove(controls.sid);
        controls.sid = 0;
    }
    g_object_unref(controls.mydata);
    GWY_OBJECT_UNREF(controls.surface);
    gwy_dimensions_free(controls.dims);

    return response == GTK_RESPONSE_OK;
}


static void
update_controls(MfmCurrentControls *controls,
                MfmCurrentArgs *args)
{
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->update),
                                 args->update);

    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->out),
                                  args->out);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(controls->probe),
                                  args->probe);

    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->height), args->height);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->current), args->current);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->position), args->position);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->width), args->width);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->mtip), args->mtip);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->bx), args->bx);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->by), args->by);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->length), args->length);
}

static void
update_sensitivity(MfmCurrentControls *controls)
{
    gboolean is_force = TRUE, is_bar = FALSE;

    if (controls->args->out == GWY_MFM_CURRENT_OUTPUT_HZ
        || controls->args->out == GWY_MFM_CURRENT_OUTPUT_HX)
        is_force = FALSE;
    if (controls->args->probe == GWY_MFM_PROBE_BAR)
        is_bar = TRUE;
    if (!is_force)
        is_bar = FALSE;

    gwy_table_hscale_set_sensitive(GTK_OBJECT(controls->probe), is_force);
    gwy_table_hscale_set_sensitive(controls->mtip, is_bar);
    gwy_table_hscale_set_sensitive(controls->bx, is_bar);
    gwy_table_hscale_set_sensitive(controls->by, is_bar);
    gwy_table_hscale_set_sensitive(controls->length, is_bar);
}

static void
xyunits_changed(MfmCurrentControls *controls)
{
    gdouble mag;
    GwyDataField *dfield;

    update_values(controls);

    dfield = GWY_DATA_FIELD(gwy_container_get_object_by_name(controls->mydata,
                                                             "/0/data"));

    mag = pow10(controls->dims->args->xypow10) * controls->dims->args->measure;
    gwy_data_field_set_xreal(dfield, mag*controls->dims->args->xres);
    gwy_data_field_set_yreal(dfield, mag*controls->dims->args->yres);

    mfm_current_invalidate(controls);
}

static void
probe_changed(GtkComboBox *combo, MfmCurrentControls *controls)
{
    controls->args->probe = gwy_enum_combo_box_get_active(combo);
    update_sensitivity(controls);
    mfm_current_invalidate(controls);
}

static void
out_changed(GtkComboBox *combo, MfmCurrentControls *controls)
{
    controls->args->out = gwy_enum_combo_box_get_active(combo);
    update_sensitivity(controls);
    mfm_current_invalidate(controls);
}

static void
page_switched(MfmCurrentControls *controls,
              G_GNUC_UNUSED GtkNotebookPage *page,
              gint pagenum)
{
    if (controls->in_init)
        return;

    controls->args->active_page = pagenum;

    if (pagenum == PAGE_GENERATOR)
        update_values(controls);
}

static void
update_values(MfmCurrentControls *controls)
{
    GwyDimensions *dims = controls->dims;

    controls->pxsize = dims->args->measure * pow10(dims->args->xypow10);

    controls->args->current = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->current));
    controls->args->position = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->position));
    controls->args->width = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->width));
    controls->args->height = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->height));
    controls->args->mtip = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->mtip));
    controls->args->bx = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->bx));
    controls->args->by = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->by));
    controls->args->length = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->length));

    controls->args->probe = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(controls->probe));
    controls->args->out = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(controls->out));

    update_sensitivity(controls);
    mfm_current_invalidate(controls);
}

static void
mfm_current_invalidate(MfmCurrentControls *controls)
{
    /* create preview if instant updates are on */
    if (controls->args->update && !controls->in_init && !controls->sid) {
        controls->sid = g_idle_add_full(G_PRIORITY_LOW, preview_gsource,
                                        controls, NULL);
    }
}

static gboolean
preview_gsource(gpointer user_data)
{
    MfmCurrentControls *controls = (MfmCurrentControls*)user_data;
    controls->sid = 0;

    preview(controls);

    return FALSE;
}

static void
preview(MfmCurrentControls *controls)
{
    MfmCurrentArgs *args = controls->args;
    GwyDataField *dfield;

    dfield = GWY_DATA_FIELD(gwy_container_get_object_by_name(controls->mydata,
                                                             "/0/data"));
    if (controls->dims->args->add && controls->surface)
        gwy_data_field_copy(controls->surface, dfield, FALSE);
    else
        gwy_data_field_clear(dfield);

    mfm_current_do(args, controls->dims->args, dfield);

    gwy_data_field_data_changed(dfield);
}

static void
mfm_current_do(const MfmCurrentArgs *args,
               G_GNUC_UNUSED const GwyDimensionArgs *dimsargs,
               GwyDataField *dfield)
{
    GwyMFMComponentType component;
    GwyDataField *tmp;
    gdouble height = args->height*1e-9,
            width = args->width*1e-9,
            length = args->length*1e-9,
            bx = args->bx*1e-9,
            by = args->by*1e-9,
            current = args->current*1e-3,
            mtip = args->mtip*1e3,
            position = args->position*gwy_data_field_get_xreal(dfield)/100.0;

    if (args->out == GWY_MFM_CURRENT_OUTPUT_HX)
        component = GWY_MFM_COMPONENT_HX;
    else if (args->out == GWY_MFM_CURRENT_OUTPUT_HZ
             || args->out == GWY_MFM_CURRENT_OUTPUT_FORCE)
        component = GWY_MFM_COMPONENT_HZ;
    else if (args->out == GWY_MFM_CURRENT_OUTPUT_FORCE_DX)
        component = GWY_MFM_COMPONENT_DHZ_DZ;
    else if (args->out == GWY_MFM_CURRENT_OUTPUT_FORCE_DDX)
        component = GWY_MFM_COMPONENT_D2HZ_DZ2;
    else {
        g_return_if_reached();
    }

    gwy_data_field_mfm_current_line(dfield, height, width,
                                    position, current, component);

    if (args->out == GWY_MFM_CURRENT_OUTPUT_FORCE
        || args->out == GWY_MFM_CURRENT_OUTPUT_FORCE_DX
        || args->out == GWY_MFM_CURRENT_OUTPUT_FORCE_DDX) {
        tmp = gwy_data_field_duplicate(dfield);
        gwy_data_field_mfm_perpendicular_medium_force(tmp, dfield,
                                                      args->probe, mtip,
                                                      bx, by, length);
        g_object_unref(tmp);
    }
}

static const gchar prefix[]            = "/module/mfm_current";
static const gchar active_page_key[]   = "/module/mfm_current/active_page";
static const gchar update_key[]        = "/module/mfm_current/update";
static const gchar out_key[]           = "/module/mfm_current/out";
static const gchar probe_key[]         = "/module/mfm_current/probe";
static const gchar height_key[]        = "/module/mfm_current/height";
static const gchar current_key[]       = "/module/mfm_current/current";
static const gchar width_key[]         = "/module/mfm_current/width";
static const gchar position_key[]      = "/module/mfm_current/position";
static const gchar mtip_key[]          = "/module/mfm_current/mtip";
static const gchar bx_key[]            = "/module/mfm_current/bx";
static const gchar by_key[]            = "/module/mfm_current/by";
static const gchar length_key[]        = "/module/mfm_current/length";


static void
mfm_current_sanitize_args(MfmCurrentArgs *args)
{
    args->active_page = CLAMP(args->active_page,
                              PAGE_DIMENSIONS, PAGE_NPAGES-1);
    args->update = !!args->update;
    args->out = CLAMP(args->out, 0, GWY_MFM_CURRENT_OUTPUT_FORCE_DDX);
    args->probe = CLAMP(args->probe, 0, GWY_MFM_PROBE_BAR);
    args->position = CLAMP(args->position, 0, 100);
}

static void
mfm_current_load_args(GwyContainer *container,
                      MfmCurrentArgs *args,
                      GwyDimensionArgs *dimsargs)
{
    *args = mfm_current_defaults;

    gwy_container_gis_int32_by_name(container, active_page_key,
                                    &args->active_page);
    gwy_container_gis_boolean_by_name(container, update_key, &args->update);

    gwy_container_gis_enum_by_name(container, probe_key, &args->probe);
    gwy_container_gis_enum_by_name(container, out_key, &args->out);
    gwy_container_gis_double_by_name(container, height_key, &args->height);
    gwy_container_gis_double_by_name(container, current_key, &args->current);
    gwy_container_gis_double_by_name(container, width_key, &args->width);
    gwy_container_gis_double_by_name(container, position_key, &args->position);
    gwy_container_gis_double_by_name(container, mtip_key, &args->mtip);
    gwy_container_gis_double_by_name(container, bx_key, &args->bx);
    gwy_container_gis_double_by_name(container, by_key, &args->by);
    gwy_container_gis_double_by_name(container, length_key, &args->length);

    mfm_current_sanitize_args(args);

    gwy_clear(dimsargs, 1);
    gwy_dimensions_copy_args(&dims_defaults, dimsargs);
    gwy_dimensions_load_args(dimsargs, container, prefix);
}

static void
mfm_current_save_args(GwyContainer *container,
                      const MfmCurrentArgs *args,
                      const GwyDimensionArgs *dimsargs)
{
    gwy_container_set_int32_by_name(container, active_page_key,
                                    args->active_page);
    gwy_container_set_boolean_by_name(container, update_key, args->update);

    gwy_container_set_enum_by_name(container, probe_key, args->probe);
    gwy_container_set_enum_by_name(container, out_key, args->out);
    gwy_container_set_double_by_name(container, height_key, args->height);
    gwy_container_set_double_by_name(container, current_key, args->current);
    gwy_container_set_double_by_name(container, width_key, args->width);
    gwy_container_set_double_by_name(container, position_key, args->position);
    gwy_container_set_double_by_name(container, mtip_key, args->mtip);
    gwy_container_set_double_by_name(container, bx_key, args->bx);
    gwy_container_set_double_by_name(container, by_key, args->by);
    gwy_container_set_double_by_name(container, length_key, args->length);


    gwy_dimensions_save_args(dimsargs, container, prefix);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
