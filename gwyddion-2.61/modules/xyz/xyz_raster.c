/*
 *  $Id: xyz_raster.c 22564 2019-10-10 15:48:53Z yeti-dn $
 *  Copyright (C) 2016-2019 David Necas (Yeti).
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libprocess/triangulation.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-xyz.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"

#define XYZRAS_RUN_MODES (GWY_RUN_INTERACTIVE | GWY_RUN_IMMEDIATE)

#define EPSREL 1e-8

/* Use smaller cell sides than the triangulation algorithm as we only need them
 * for identical point detection and border extension. */
#define CELL_SIDE 1.6

enum {
    PREVIEW_SIZE = 400,
    UNDEF = G_MAXUINT
};

enum {
    GWY_INTERPOLATION_FIELD = -1,
    GWY_INTERPOLATION_AVERAGE = -2,
};

enum {
    LAST_UPDATED_X,
    LAST_UPDATED_Y
};

typedef struct {
    /* XXX: Not all values of interpolation and exterior are possible. */
    GwyInterpolationType interpolation;
    GwyExteriorType exterior;
    gint xres;
    gint yres;
    gboolean mask_empty;
    /* Interface only. */
    gdouble xmin;
    gdouble xmax;
    gdouble ymin;
    gdouble ymax;
} XYZRasArgs;

typedef struct {
    GwySurface *surface;
    GwyTriangulation *triangulation;
    GwyDataField *regular;
    GwyDataField *raster;
    GwyDataField *nilmask;
    GArray *points;
    guint norigpoints;
    guint nbasepoints;
    gdouble step;
    gdouble xymag;
} XYZRasData;

typedef struct {
    XYZRasArgs *args;
    XYZRasData *rdata;
    GwyContainer *mydata;
    GtkWidget *dialog;
    GtkWidget *directbox;
    GtkWidget *xmin;
    GtkWidget *xmax;
    GtkWidget *ymin;
    GtkWidget *ymax;
    GtkObject *xres;
    GtkObject *yres;
    GtkWidget *interpolation;
    GtkWidget *exterior;
    GtkWidget *mask_empty;
    GtkWidget *view;
    GtkWidget *do_preview;
    GtkWidget *error;
    gboolean in_update;
    gboolean in_selection_update;
    gint last_updated;
} XYZRasControls;

typedef struct {
    guint *id;
    guint pos;
    guint len;
    guint size;
} WorkQueue;

static gboolean      module_register        (void);
static void          xyzras                 (GwyContainer *data,
                                             GwyRunType run);
static gboolean      xyzras_dialog          (XYZRasArgs *arg,
                                             XYZRasData *rdata,
                                             GwyContainer *data,
                                             gint id);
static void          add_dfield_to_data     (GwyDataField *dfield,
                                             GwyDataField *mask,
                                             GwyContainer *data,
                                             gint id);
static gint          construct_physical_dims(XYZRasControls *controls,
                                             GtkTable *table,
                                             gint row);
static gint          construct_options      (XYZRasControls *controls,
                                             GtkTable *table,
                                             gint row);
static void          make_pixels_square     (XYZRasControls *controls);
static void          xres_changed           (XYZRasControls *controls,
                                             GtkAdjustment *adj);
static void          yres_changed           (XYZRasControls *controls,
                                             GtkAdjustment *adj);
static void          xmin_changed           (XYZRasControls *controls,
                                             GtkEntry *entry);
static void          xmax_changed           (XYZRasControls *controls,
                                             GtkEntry *entry);
static void          ymin_changed           (XYZRasControls *controls,
                                             GtkEntry *entry);
static void          ymax_changed           (XYZRasControls *controls,
                                             GtkEntry *entry);
static void          interpolation_changed  (XYZRasControls *controls,
                                             GtkComboBox *combo);
static void          exterior_changed       (XYZRasControls *controls,
                                             GtkComboBox *combo);
static void          mask_empty_changed     (XYZRasControls *controls,
                                             GtkToggleButton *button);
static void          reset_ranges           (XYZRasControls *controls);
static void          update_selection       (XYZRasControls *controls);
static void          selection_changed      (XYZRasControls *controls,
                                             gint hint,
                                             GwySelection *selection);
static void          clear_selection        (XYZRasControls *controls);
static void          preview                (XYZRasControls *controls);
static void          triangulation_info     (XYZRasControls *controls);
static void          render_regular_directly(XYZRasControls *controls);
static GwyDataField* xyzras_do              (XYZRasData *rdata,
                                             const XYZRasArgs *args,
                                             GwyDataField **mask,
                                             GtkWindow *dialog,
                                             gchar **error);
static gboolean      interpolate_field      (guint npoints,
                                             const GwyXYZ *points,
                                             GwyDataField *dfield,
                                             GwySetFractionFunc set_fraction,
                                             GwySetMessageFunc set_message);
static gboolean      extend_borders         (XYZRasData *rdata,
                                             const XYZRasArgs *args,
                                             gboolean check_for_changes,
                                             gdouble epsrel);
static void          xyzras_free            (XYZRasData *rdata);
static void          initialize_ranges      (const XYZRasData *rdata,
                                             XYZRasArgs *args);
static void          invalidate_raster      (XYZRasData *rdata);
static void          analyse_points         (XYZRasData *rdata,
                                             double epsrel);
static GwyDataField* check_regular_grid     (GwySurface *surface);
static void          xyzras_load_args       (GwyContainer *container,
                                             XYZRasArgs *args);
static void          xyzras_save_args       (GwyContainer *container,
                                             XYZRasArgs *args);

static const XYZRasArgs xyzras_defaults = {
    GWY_INTERPOLATION_AVERAGE, GWY_EXTERIOR_MIRROR_EXTEND,
    512, 512, TRUE,
    /* Interface only. */
    0.0, 0.0, 0.0, 0.0,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Rasterizes XYZ data to images."),
    "Yeti <yeti@gwyddion.net>",
    "1.4",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, xyz_raster)

static gboolean
module_register(void)
{
    gwy_xyz_func_register("xyz_raster",
                          (GwyXYZFunc)&xyzras,
                          N_("/_Rasterize..."),
                          GWY_STOCK_RASTERIZE,
                          XYZRAS_RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Rasterize to image"));

    return TRUE;
}

static void
xyzras(GwyContainer *data, GwyRunType run)
{
    XYZRasArgs args;
    XYZRasData rdata;

    GwyContainer *settings;
    GwySurface *surface = NULL;
    GwyDataField *dfield, *mask;
    gboolean ok = TRUE;
    gint id;

    g_return_if_fail(run & XYZRAS_RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerRectangle"));

    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &surface,
                                     GWY_APP_SURFACE_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(surface));

    dfield = check_regular_grid(surface);
    if (dfield && run == GWY_RUN_IMMEDIATE) {
        add_dfield_to_data(dfield, NULL, data, id);
        return;
    }

    settings = gwy_app_settings_get();
    xyzras_load_args(settings, &args);
    gwy_clear(&rdata, 1);
    rdata.surface = surface;
    rdata.regular = dfield;
    rdata.points = g_array_new(FALSE, FALSE, sizeof(GwyXYZ));
    analyse_points(&rdata, EPSREL);
    initialize_ranges(&rdata, &args);

    if (run == GWY_RUN_INTERACTIVE)
        ok = xyzras_dialog(&args, &rdata, data, id);

    xyzras_save_args(settings, &args);

    if (ok) {
        gchar *error = NULL;

        if (rdata.raster) {
            dfield = g_object_ref(rdata.raster);
            mask = rdata.nilmask ? g_object_ref(rdata.nilmask) : NULL;
        }
        else {
            GtkWindow *window = gwy_app_find_window_for_xyz(data, id);
            dfield = xyzras_do(&rdata, &args, &mask, window, &error);
        }

        if (dfield) {
            add_dfield_to_data(dfield, mask, data, id);
        }
        else if (run == GWY_RUN_INTERACTIVE) {
            GtkWidget *dialog;
            dialog = gtk_message_dialog_new
                                    (gwy_app_find_window_for_channel(data, id),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_ERROR,
                                     GTK_BUTTONS_OK,
                                     "%s", error);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            g_free(error);
        }
        else {
            g_free(error);
        }
    }

    xyzras_free(&rdata);
}

static void
add_dfield_to_data(GwyDataField *dfield, GwyDataField *mask,
                   GwyContainer *data, gint id)
{
    GQuark qsrc, qdest;
    const guchar *s;
    gint newid;

    newid = gwy_app_data_browser_add_data_field(dfield, data, TRUE);
    if (mask) {
        qdest = gwy_app_get_mask_key_for_id(newid);
        gwy_container_set_object(data, qdest, mask);
        g_object_unref(mask);
    }
    gwy_app_channel_log_add(data, -1, newid, "xyz::xyz_raster", NULL);

    qsrc = gwy_app_get_surface_palette_key_for_id(id);
    qdest = gwy_app_get_data_palette_key_for_id(newid);
    if (gwy_container_gis_string(data, qsrc, &s))
        gwy_container_set_const_string(data, qdest, s);

    qsrc = gwy_app_get_surface_title_key_for_id(id);
    qdest = gwy_app_get_data_title_key_for_id(newid);
    if (gwy_container_gis_string(data, qsrc, &s))
        gwy_container_set_const_string(data, qdest, s);
}

static gboolean
xyzras_dialog(XYZRasArgs *args,
              XYZRasData *rdata,
              GwyContainer *data,
              gint id)
{
    GtkWidget *dialog, *vbox, *label, *hbox, *button;
    GwyPixmapLayer *player;
    GwyVectorLayer *vlayer;
    GwyDataField *dfield;
    GtkTable *table;
    XYZRasControls controls;
    gint row, response;
    const guchar *gradient;
    GwySelection *selection;
    gdouble xmin, xmax, ymin, ymax;
    GType gtype;
    GQuark quark;

    gwy_clear(&controls, 1);
    controls.args = args;
    controls.rdata = rdata;
    controls.mydata = gwy_container_new();
    controls.last_updated = LAST_UPDATED_X;

    dialog = gtk_dialog_new_with_buttons(_("Rasterize XYZ Data"), NULL, 0,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_xyz_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    if (rdata->regular) {
        hbox = controls.directbox = gtk_hbox_new(FALSE, 8);
        gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                           FALSE, FALSE, 0);

        button = gtk_button_new_with_mnemonic(_("Create Image _Directly"));
        gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
        g_signal_connect_swapped(button, "clicked",
                                 G_CALLBACK(render_regular_directly),
                                 &controls);

        label = gtk_label_new(_("XY points form a regular grid "
                                "so interpolation is not necessary."));
        gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    }

    hbox = gtk_hbox_new(FALSE, 20);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 0);

    /* Left column */
    vbox = gtk_vbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    table = GTK_TABLE(gtk_table_new(4, 5, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(table), FALSE, FALSE, 0);
    row = 0;

    gtk_table_attach(table, gwy_label_new_header(_("Resolution")),
                     0, 4, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    controls.xres = gtk_adjustment_new(args->xres, 2, 16384, 1, 100, 0);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row,
                            _("_Horizontal size:"), _("px"),
                            controls.xres, GWY_HSCALE_LOG | GWY_HSCALE_SNAP);
    row++;

    controls.yres = gtk_adjustment_new(args->yres, 2, 16384, 1, 100, 0);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row,
                            _("_Vertical size:"), _("px"),
                            controls.yres, GWY_HSCALE_LOG | GWY_HSCALE_SNAP);
    row++;

    button = gtk_button_new_with_mnemonic(_("Make Pixels S_quare"));
    gtk_table_attach(table, button, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(make_pixels_square), &controls);
    row++;

    table = GTK_TABLE(gtk_table_new(7, 5, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(table), FALSE, FALSE, 0);
    row = 0;

    row = construct_physical_dims(&controls, table, row);

    button = gtk_button_new_with_mnemonic(_("Reset Ran_ges"));
    gtk_table_attach(table, button, 0, 4, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(reset_ranges), &controls);
    row++;

    gtk_table_set_row_spacing(table, row-1, 8);
    row = construct_options(&controls, table, row);

    /* Right column */
    vbox = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    label = gtk_label_new(_("Preview"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    quark = gwy_app_get_surface_palette_key_for_id(id);
    if (gwy_container_gis_string(data, quark, &gradient)) {
        gwy_container_set_const_string_by_name(controls.mydata,
                                               "/0/base/palette", gradient);
    }
    gwy_surface_get_xrange(rdata->surface, &xmin, &xmax);
    gwy_surface_get_yrange(rdata->surface, &ymin, &ymax);
    dfield = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE,
                                xmax - xmin, ymax - ymin, TRUE);
    gwy_data_field_set_xoffset(dfield, xmin);
    gwy_data_field_set_yoffset(dfield, ymin);
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    g_object_unref(dfield);

    controls.view = gwy_data_view_new(controls.mydata);
    gtk_box_pack_start(GTK_BOX(vbox), controls.view, FALSE, FALSE, 0);

    player = gwy_layer_basic_new();
    g_object_set(player,
                 "data-key", "/0/data",
                 "gradient-key", "/0/base/palette",
                 NULL);
    gwy_data_view_set_data_prefix(GWY_DATA_VIEW(controls.view), "/0/data");
    gwy_data_view_set_base_layer(GWY_DATA_VIEW(controls.view), player);

    gtype = g_type_from_name("GwyLayerRectangle");
    vlayer = GWY_VECTOR_LAYER(g_object_new(gtype, NULL));
    gwy_vector_layer_set_selection_key(vlayer, "/0/select/rectangle");
    gwy_data_view_set_top_layer(GWY_DATA_VIEW(controls.view), vlayer);
    selection = gwy_vector_layer_ensure_selection(vlayer);
    g_object_set(selection, "max-objects", 1, NULL);
    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(selection_changed), &controls);

    controls.do_preview = gtk_button_new_with_mnemonic(_("_Update"));
    gtk_box_pack_start(GTK_BOX(vbox), controls.do_preview, FALSE, FALSE, 4);

    controls.error = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.error), 0.0, 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(controls.error), TRUE);
    gtk_widget_set_size_request(controls.error, PREVIEW_SIZE, -1);
    gtk_box_pack_start(GTK_BOX(vbox), controls.error, FALSE, FALSE, 0);
    triangulation_info(&controls);

    g_signal_connect_swapped(controls.do_preview, "clicked",
                             G_CALLBACK(preview), &controls);
    g_signal_connect_swapped(controls.xres, "value-changed",
                             G_CALLBACK(xres_changed), &controls);
    g_signal_connect_swapped(controls.yres, "value-changed",
                             G_CALLBACK(yres_changed), &controls);
    g_signal_connect_swapped(controls.xmin, "activate",
                             G_CALLBACK(xmin_changed), &controls);
    g_signal_connect_swapped(controls.xmax, "activate",
                             G_CALLBACK(xmax_changed), &controls);
    g_signal_connect_swapped(controls.ymin, "activate",
                             G_CALLBACK(ymin_changed), &controls);
    g_signal_connect_swapped(controls.ymax, "activate",
                             G_CALLBACK(ymax_changed), &controls);
    g_signal_connect_swapped(controls.interpolation, "changed",
                             G_CALLBACK(interpolation_changed), &controls);
    g_signal_connect_swapped(controls.exterior, "changed",
                             G_CALLBACK(exterior_changed), &controls);
    g_signal_connect_swapped(controls.mask_empty, "toggled",
                             G_CALLBACK(mask_empty_changed), &controls);

    controls.in_update = FALSE;
    reset_ranges(&controls);

    if (rdata->regular) {
        gwy_container_set_object_by_name(controls.mydata, "/0/data",
                                         rdata->regular);
        gwy_set_data_preview_size(GWY_DATA_VIEW(controls.view), PREVIEW_SIZE);
    }

    gtk_widget_show_all(dialog);

    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            g_object_unref(controls.mydata);
            return FALSE;
            break;

            case GTK_RESPONSE_OK:
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);
    g_object_unref(controls.mydata);

    return TRUE;
}

static gint
construct_physical_dims(XYZRasControls *controls,
                        GtkTable *table,
                        gint row)
{
    GwySurface *surface = controls->rdata->surface;
    GwySIValueFormat *vf;
    GtkWidget *label;

    vf = gwy_surface_get_value_format_xy(surface, GWY_SI_UNIT_FORMAT_VFMARKUP,
                                         NULL);

    gtk_table_attach(table, gwy_label_new_header(_("Physical Dimensions")),
                     0, 4, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_X-range:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->xmin = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(controls->xmin), 7);
    gwy_widget_set_activate_on_unfocus(controls->xmin, TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls->xmin);
    gtk_table_attach(table, controls->xmin, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gtk_label_new("–"), 2, 3, row, row+1, 0, 0, 0, 0);
    controls->xmax = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(controls->xmax), 7);
    gwy_widget_set_activate_on_unfocus(controls->xmax, TRUE);
    gtk_table_attach(table, controls->xmax, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_label_set_markup(GTK_LABEL(label), vf->units);
    gtk_table_attach(table, label, 4, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Y-range:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    controls->ymin = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(controls->ymin), 7);
    gwy_widget_set_activate_on_unfocus(controls->ymin, TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls->ymin);
    gtk_table_attach(table, controls->ymin, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gtk_label_new("–"), 2, 3, row, row+1, 0, 0, 0, 0);
    controls->ymax = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(controls->ymax), 7);
    gwy_widget_set_activate_on_unfocus(controls->ymax, TRUE);
    gtk_table_attach(table, controls->ymax, 3, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_label_set_markup(GTK_LABEL(label), vf->units);
    gtk_table_attach(table, label, 4, 5, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls->rdata->xymag = vf->magnitude;
    gwy_si_unit_value_format_free(vf);

    return row;
}

static gint
construct_options(XYZRasControls *controls,
                  GtkTable *table,
                  gint row)
{
    XYZRasArgs *args = controls->args;
    GtkWidget *label;

    gtk_table_attach(table, gwy_label_new_header(_("Options")),
                     0, 4, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Interpolation type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->interpolation
        = gwy_enum_combo_box_newl(NULL, NULL,
                                  args->interpolation,
                                  _("Round"), GWY_INTERPOLATION_ROUND,
                                  _("NNA"), GWY_INTERPOLATION_NNA,
                                  _("Linear"), GWY_INTERPOLATION_LINEAR,
                                  _("Field"), GWY_INTERPOLATION_FIELD,
                                  _("Average"), GWY_INTERPOLATION_AVERAGE,
                                  NULL);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls->interpolation);
    gtk_table_attach(table, controls->interpolation, 1, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new_with_mnemonic(_("_Exterior type:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    controls->exterior
        = gwy_enum_combo_box_newl(NULL, NULL,
                                  args->exterior,
                                  gwy_sgettext("exterior|Border"),
                                  GWY_EXTERIOR_BORDER_EXTEND,
                                  gwy_sgettext("exterior|Mirror"),
                                  GWY_EXTERIOR_MIRROR_EXTEND,
                                  gwy_sgettext("exterior|Periodic"),
                                  GWY_EXTERIOR_PERIODIC,
                                  NULL);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls->exterior);
    gtk_table_attach(table, controls->exterior, 1, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    controls->mask_empty
        = gtk_check_button_new_with_mnemonic(_("_Mask empty regions"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->mask_empty),
                                 args->mask_empty);
    gtk_widget_set_sensitive(controls->mask_empty,
                             (gint)args->interpolation
                             == GWY_INTERPOLATION_AVERAGE);
    gtk_table_attach(table, controls->mask_empty, 0, 4, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    return row;
}

static void
set_adjustment_in_update(XYZRasControls *controls,
                         GtkAdjustment *adj,
                         gdouble value)
{
    controls->in_update = TRUE;
    gtk_adjustment_set_value(adj, value);
    controls->in_update = FALSE;
}

static void
set_physical_dimension(XYZRasControls *controls,
                       GtkEntry *entry,
                       gdouble value)
{
    gchar buf[24];

    g_return_if_fail(!controls->in_update);
    controls->in_update = TRUE;

    g_snprintf(buf, sizeof(buf), "%g", value/controls->rdata->xymag);
    gtk_entry_set_text(entry, buf);

    controls->in_update = FALSE;
}

static void
make_pixels_square(XYZRasControls *controls)
{
    XYZRasArgs *args = controls->args;
    gdouble h;
    gint res;

    if (controls->last_updated == LAST_UPDATED_X) {
        h = (args->xmax - args->xmin)/args->xres;
        res = GWY_ROUND((args->ymax - args->ymin)/h);
        res = CLAMP(res, 2, 16384);
        set_adjustment_in_update(controls, GTK_ADJUSTMENT(controls->yres), res);
        controls->last_updated = LAST_UPDATED_X;
    }
    else {
        h = (args->ymax - args->ymin)/args->yres;
        res = GWY_ROUND((args->xmax - args->xmin)/h);
        res = CLAMP(res, 2, 16384);
        set_adjustment_in_update(controls, GTK_ADJUSTMENT(controls->xres), res);
        controls->last_updated = LAST_UPDATED_Y;
    }
    invalidate_raster(controls->rdata);
}

static void
xres_changed(XYZRasControls *controls,
             GtkAdjustment *adj)
{
    controls->args->xres = gwy_adjustment_get_int(adj);
    controls->last_updated = LAST_UPDATED_X;
    invalidate_raster(controls->rdata);
}

static void
yres_changed(XYZRasControls *controls,
             GtkAdjustment *adj)
{
    controls->args->yres = gwy_adjustment_get_int(adj);
    controls->last_updated = LAST_UPDATED_Y;
    invalidate_raster(controls->rdata);
}

static void
xmin_changed(XYZRasControls *controls,
             GtkEntry *entry)
{
    XYZRasArgs *args = controls->args;
    gdouble val = g_strtod(gtk_entry_get_text(entry), NULL);

    val *= controls->rdata->xymag;
    if (val == args->xmin)
        return;

    args->xmin = val;
    update_selection(controls);
    invalidate_raster(controls->rdata);
}

static void
xmax_changed(XYZRasControls *controls,
             GtkEntry *entry)
{
    XYZRasArgs *args = controls->args;
    gdouble val = g_strtod(gtk_entry_get_text(entry), NULL);

    val *= controls->rdata->xymag;
    if (val == args->xmax)
        return;

    args->xmax = val;
    update_selection(controls);
    invalidate_raster(controls->rdata);
}

static void
ymin_changed(XYZRasControls *controls,
             GtkEntry *entry)
{
    XYZRasArgs *args = controls->args;
    gdouble val = g_strtod(gtk_entry_get_text(entry), NULL);

    val *= controls->rdata->xymag;
    if (val == args->ymin)
        return;

    args->ymin = val;
    update_selection(controls);
    invalidate_raster(controls->rdata);
}

static void
ymax_changed(XYZRasControls *controls,
             GtkEntry *entry)
{
    XYZRasArgs *args = controls->args;
    gdouble val = g_strtod(gtk_entry_get_text(entry), NULL);

    val *= controls->rdata->xymag;
    if (val == args->ymax)
        return;

    args->ymax = val;
    update_selection(controls);
    invalidate_raster(controls->rdata);
}

static void
interpolation_changed(XYZRasControls *controls,
                      GtkComboBox *combo)
{
    controls->args->interpolation = gwy_enum_combo_box_get_active(combo);
    gtk_widget_set_sensitive(controls->mask_empty,
                             (gint)controls->args->interpolation
                             == GWY_INTERPOLATION_AVERAGE);
    invalidate_raster(controls->rdata);
}

static void
exterior_changed(XYZRasControls *controls,
                 GtkComboBox *combo)
{
    controls->args->exterior = gwy_enum_combo_box_get_active(combo);
    invalidate_raster(controls->rdata);
}

static void
mask_empty_changed(XYZRasControls *controls,
                   GtkToggleButton *button)
{
    controls->args->mask_empty = gtk_toggle_button_get_active(button);
}

static void
set_all_physical_dimensions(XYZRasControls *controls)
{
    XYZRasArgs *args = controls->args;

    set_physical_dimension(controls, GTK_ENTRY(controls->ymin), args->ymin);
    set_physical_dimension(controls, GTK_ENTRY(controls->ymax), args->ymax);
    set_physical_dimension(controls, GTK_ENTRY(controls->xmin), args->xmin);
    set_physical_dimension(controls, GTK_ENTRY(controls->xmax), args->xmax);
    invalidate_raster(controls->rdata);
}

static void
reset_ranges(XYZRasControls *controls)
{
    initialize_ranges(controls->rdata, controls->args);
    set_all_physical_dimensions(controls);
    clear_selection(controls);
}

static void
update_selection(XYZRasControls *controls)
{
    XYZRasArgs *args = controls->args;
    GwyVectorLayer *vlayer;
    GwySelection *selection;
    GwyDataField *dfield;
    gdouble xoff, yoff;
    gdouble xy[4];

    if (controls->in_selection_update)
        return;

    controls->in_selection_update = TRUE;
    dfield = gwy_container_get_object_by_name(controls->mydata, "/0/data");
    xoff = gwy_data_field_get_xoffset(dfield);
    yoff = gwy_data_field_get_yoffset(dfield);
    xy[0] = args->xmin - xoff;
    xy[1] = args->ymin - yoff;
    xy[2] = args->xmax - xoff;
    xy[3] = args->ymax - yoff;
    vlayer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(controls->view));
    selection = gwy_vector_layer_ensure_selection(vlayer);
    gwy_selection_set_data(selection, 1, xy);
    controls->in_selection_update = FALSE;
}

static void
selection_changed(XYZRasControls *controls,
                  G_GNUC_UNUSED gint hint, GwySelection *selection)
{
    XYZRasArgs *args = controls->args;
    GwyDataField *dfield;
    gdouble xoff, yoff;
    guint n;
    gdouble xy[4];

    if (controls->in_selection_update)
        return;

    n = gwy_selection_get_data(selection, NULL);
    if (n != 1)
        return;

    controls->in_selection_update = TRUE;
    dfield = gwy_container_get_object_by_name(controls->mydata, "/0/data");
    gwy_selection_get_data(selection, xy);
    xoff = gwy_data_field_get_xoffset(dfield);
    yoff = gwy_data_field_get_yoffset(dfield);
    args->xmin = xy[0] + xoff;
    args->ymin = xy[1] + yoff;
    args->xmax = xy[2] + xoff;
    args->ymax = xy[3] + yoff;
    set_all_physical_dimensions(controls);
    controls->in_selection_update = FALSE;
}

static void
clear_selection(XYZRasControls *controls)
{
    GwyVectorLayer *vlayer;
    GwySelection *selection;

    vlayer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(controls->view));
    selection = gwy_vector_layer_ensure_selection(vlayer);
    gwy_selection_clear(selection);
}

static void
preview(XYZRasControls *controls)
{
    XYZRasArgs *args = controls->args;
    GwyDataField *dfield, *mask;
    GtkWidget *entry;
    gchar *error = NULL;

    entry = gtk_window_get_focus(GTK_WINDOW(controls->dialog));
    if (entry && GTK_IS_ENTRY(entry))
        gtk_widget_activate(entry);

    GWY_OBJECT_UNREF(controls->rdata->raster);
    GWY_OBJECT_UNREF(controls->rdata->nilmask);
    dfield = xyzras_do(controls->rdata, args, &mask,
                       GTK_WINDOW(controls->dialog), &error);
    if (dfield) {
        triangulation_info(controls);
        controls->rdata->raster = g_object_ref(dfield);
        controls->rdata->nilmask = mask;
    }
    else {
        gtk_label_set_text(GTK_LABEL(controls->error), error);
        g_free(error);
        dfield = gwy_data_field_new(args->xres, args->yres,
                                    args->xres, args->yres, TRUE);
    }

    gwy_container_set_object_by_name(controls->mydata, "/0/data", dfield);
    gwy_set_data_preview_size(GWY_DATA_VIEW(controls->view), PREVIEW_SIZE);
    g_object_unref(dfield);

    /* After doing preview the selection always covers the full data and thus
     * is not useful. */
    clear_selection(controls);

    /* When user starts messing with the controls, remove the direct
     * rendering option. */
    if (controls->directbox)
        gtk_widget_hide(controls->directbox);
}

static void
triangulation_info(XYZRasControls *controls)
{
    XYZRasData *rdata;
    gchar *s;

    rdata = controls->rdata;
    s = g_strdup_printf(_("Number of points: %u\n"
                          "Merged as too close: %u\n"
                          "Added on the boundaries: %u"),
                        rdata->norigpoints,
                        rdata->norigpoints - rdata->nbasepoints,
                        rdata->points->len - rdata->nbasepoints);
    gtk_label_set_text(GTK_LABEL(controls->error), s);
    g_free(s);
}

static void
render_regular_directly(XYZRasControls *controls)
{
    gwy_object_unref(controls->rdata->raster);
    controls->rdata->raster = g_object_ref(controls->rdata->regular);
    gtk_dialog_response(GTK_DIALOG(controls->dialog), GTK_RESPONSE_OK);
}

static GwyDataField*
xyzras_do(XYZRasData *rdata,
          const XYZRasArgs *args,
          GwyDataField **mask,
          GtkWindow *window,
          gchar **error)
{
    GwyTriangulation *triangulation = rdata->triangulation;
    GArray *points = rdata->points;
    GwyDataField *dfield;
    GwySurface *surface = rdata->surface;
    GwySetMessageFunc set_message = (window ? gwy_app_wait_set_message : NULL);
    GwySetFractionFunc set_fraction = (window ? gwy_app_wait_set_fraction : NULL);
    gboolean ok = TRUE, extended;

    *mask = NULL;
    gwy_debug("%g %g :: %g %g", args->xmin, args->xmax, args->ymin, args->ymax);
    if (!(args->xmax > args->xmin) || !(args->ymax > args->ymin)) {
        *error = g_strdup(_("Physical dimensions are invalid."));
        return NULL;
    }
    dfield = gwy_data_field_new(args->xres, args->yres,
                                args->xmax - args->xmin,
                                args->ymax - args->ymin,
                                FALSE);
    gwy_data_field_set_xoffset(dfield, args->xmin);
    gwy_data_field_set_yoffset(dfield, args->ymin);
    gwy_surface_copy_units_to_data_field(surface, dfield);

    if ((gint)args->interpolation == GWY_INTERPOLATION_FIELD) {
        if (window)
            gwy_app_wait_start(window, _("Initializing..."));

        extend_borders(rdata, args, FALSE, EPSREL);
        ok = interpolate_field(points->len, (const GwyXYZ*)points->data, dfield,
                               set_fraction, set_message);
        if (window)
            gwy_app_wait_finish();
    }
    else if ((gint)args->interpolation == GWY_INTERPOLATION_AVERAGE) {
        extend_borders(rdata, args, FALSE, EPSREL);
        if (args->mask_empty) {
            *mask = gwy_data_field_new_alike(dfield, FALSE);
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(*mask),
                                        NULL);
            gwy_data_field_average_xyz(dfield, *mask,
                                       (const GwyXYZ*)points->data,
                                       points->len);
            gwy_data_field_threshold(*mask, G_MINDOUBLE, 1.0, 0.0);
        }
        else {
            gwy_data_field_average_xyz(dfield, NULL,
                                       (const GwyXYZ*)points->data,
                                       points->len);
        }
        ok = TRUE;
    }
    else {
        if (window)
            gwy_app_wait_start(window, _("Initializing..."));
        /* [Try to] perform triangulation if either there is none yet or
         * extend_borders() reports the points have changed. */
        gwy_debug("have triangulation: %d", !!triangulation);
        extended = extend_borders(rdata, args, TRUE, EPSREL);
        if (!triangulation || extended) {
            gwy_debug("must triangulate");
            if (!triangulation)
                rdata->triangulation = triangulation = gwy_triangulation_new();
            /* This can fail for two different reasons:
             * 1) numerical failure
             * 2) cancellation */
            ok = gwy_triangulation_triangulate_iterative(triangulation,
                                                         points->len,
                                                         points->data,
                                                         sizeof(GwyXYZ),
                                                         set_fraction,
                                                         set_message);
        }
        else {
            gwy_debug("points did not change, recycling triangulation");
        }

        if (triangulation && ok) {
            if (window)
                ok = set_message(_("Interpolating..."));
            if (ok)
                ok = gwy_triangulation_interpolate(triangulation,
                                                   args->interpolation, dfield);
        }
        if (window)
            gwy_app_wait_finish();
    }

    if (!ok) {
        gwy_object_unref(rdata->triangulation);
        g_object_unref(dfield);
        GWY_OBJECT_UNREF(*mask);
        *error = g_strdup(_("XYZ data regularization failed due to "
                            "numerical instability or was interrupted."));
        return NULL;
    }

    return dfield;
}

static gboolean
interpolate_field(guint npoints,
                  const GwyXYZ *points,
                  GwyDataField *dfield,
                  GwySetFractionFunc set_fraction,
                  GwySetMessageFunc set_message)
{
    gboolean cancelled = FALSE, *pcancelled = &cancelled;
    gdouble xoff, yoff, qx, qy;
    guint xres, yres;
    gdouble *d;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    xoff = gwy_data_field_get_xoffset(dfield);
    yoff = gwy_data_field_get_yoffset(dfield);
    qx = gwy_data_field_get_xreal(dfield)/xres;
    qy = gwy_data_field_get_yreal(dfield)/yres;
    d = gwy_data_field_get_data(dfield);

    if (set_message)
        set_message(_("Interpolating..."));

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(d,xres,yres,points,npoints,xoff,yoff,qx,qy,set_fraction,pcancelled)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gint i, j, k;

        for (i = ifrom; i < ito; i++) {
            gdouble y = yoff + qy*(i + 0.5);
            gdouble *drow = d + i*xres;

            for (j = 0; j < xres; j++) {
                gdouble x = xoff + qx*(j + 0.5);
                gdouble w = 0.0;
                gdouble s = 0.0;

                for (k = 0; k < npoints; k++) {
                    const GwyXYZ *pt = points + k;
                    gdouble dx = x - pt->x;
                    gdouble dy = y - pt->y;
                    gdouble r2 = dx*dx + dy*dy;

                    r2 *= r2;
                    if (G_UNLIKELY(r2 == 0.0)) {
                        s = pt->z;
                        w = 1.0;
                        break;
                    }

                    r2 = 1.0/r2;
                    w += r2;
                    s += r2*pt->z;
                }
                drow[j] = s/w;
            }

            if (gwy_omp_set_fraction_check_cancel(set_fraction,
                                                  i, ifrom, ito, pcancelled))
                break;
        }
    }

    return !cancelled;
}

/* Return TRUE if extpoints have changed. */
static gboolean
extend_borders(XYZRasData *rdata,
               const XYZRasArgs *args,
               gboolean check_for_changes,
               gdouble epsrel)
{
    GwySurface *surface = rdata->surface;
    gdouble xmin, xmax, ymin, ymax, xreal, yreal, eps;
    gdouble sxmin, sxmax, symin, symax;
    gdouble *oldextpoints = NULL;
    guint i, nbase, noldext;
    gboolean extchanged;

    /* Remember previous extpoints.  If they do not change we do not need to
     * repeat the triangulation. */
    nbase = rdata->nbasepoints;
    noldext = rdata->points->len - nbase;
    gwy_debug("check for changes: %d", check_for_changes);
    if (check_for_changes) {
        gwy_debug("copying %u old extpoints", noldext);
        oldextpoints = g_memdup(&g_array_index(rdata->points, GwyXYZ, nbase),
                                noldext*sizeof(GwyXYZ));
    }
    g_array_set_size(rdata->points, nbase);

    if (args->exterior == GWY_EXTERIOR_BORDER_EXTEND) {
        gwy_debug("exterior is BORDER, just reducing points to base");
        g_free(oldextpoints);
        return noldext > 0 || !check_for_changes;
    }

    gwy_surface_get_xrange(surface, &sxmin, &sxmax);
    gwy_surface_get_yrange(surface, &symin, &symax);
    xreal = sxmax - sxmin;
    yreal = symax - symin;

    xmin = args->xmin - 2*rdata->step;
    xmax = args->xmax + 2*rdata->step;
    ymin = args->ymin - 2*rdata->step;
    ymax = args->ymax + 2*rdata->step;
    eps = epsrel*rdata->step;

    /* Extend the field according to requester boder extension, however,
     * create at most 3 full copies (4 halves and 4 quarters) of the base set.
     * Anyone asking for more is either clueless or malicious. */
    for (i = 0; i < nbase; i++) {
        const GwyXYZ pt = g_array_index(rdata->points, GwyXYZ, i);
        GwyXYZ pt2;
        gdouble txl, txr, tyt, tyb;
        gboolean txlok, txrok, tytok, tybok;

        pt2.z = pt.z;
        if (args->exterior == GWY_EXTERIOR_MIRROR_EXTEND) {
            txl = 2.0*sxmin - pt.x;
            tyt = 2.0*symin - pt.y;
            txr = 2.0*sxmax - pt.x;
            tyb = 2.0*symax - pt.y;
            txlok = pt.x - sxmin < 0.5*xreal;
            tytok = pt.y - symin < 0.5*yreal;
            txrok = sxmax - pt.x < 0.5*xreal;
            tybok = symax - pt.y < 0.5*yreal;
        }
        else if (args->exterior == GWY_EXTERIOR_PERIODIC) {
            txl = pt.x - xreal;
            tyt = pt.y - yreal;
            txr = pt.x + xreal;
            tyb = pt.y + yreal;
            txlok = sxmax - pt.x < 0.5*xreal;
            tytok = symax - pt.y < 0.5*yreal;
            txrok = pt.x - sxmin < 0.5*xreal;
            tybok = pt.y - symin < 0.5*yreal;
        }
        else {
            g_assert_not_reached();
        }

        txlok = txlok && (txl >= xmin && txl <= xmax
                          && fabs(txl - sxmin) > eps);
        tytok = tytok && (tyt >= ymin && tyt <= ymax
                          && fabs(tyt - symin) > eps);
        txrok = txrok && (txr >= ymin && txr <= xmax
                          && fabs(txr - sxmax) > eps);
        tybok = tybok && (tyb >= ymin && tyb <= xmax
                          && fabs(tyb - symax) > eps);

        if (txlok) {
            pt2.x = txl;
            pt2.y = pt.y - eps;
            g_array_append_val(rdata->points, pt2);
        }
        if (txlok && tytok) {
            pt2.x = txl + eps;
            pt2.y = tyt - eps;
            g_array_append_val(rdata->points, pt2);
        }
        if (tytok) {
            pt2.x = pt.x + eps;
            pt2.y = tyt;
            g_array_append_val(rdata->points, pt2);
        }
        if (txrok && tytok) {
            pt2.x = txr + eps;
            pt2.y = tyt + eps;
            g_array_append_val(rdata->points, pt2);
        }
        if (txrok) {
            pt2.x = txr;
            pt2.y = pt.y + eps;
            g_array_append_val(rdata->points, pt2);
        }
        if (txrok && tybok) {
            pt2.x = txr - eps;
            pt2.y = tyb + eps;
            g_array_append_val(rdata->points, pt2);
        }
        if (tybok) {
            pt2.x = pt.x - eps;
            pt2.y = tyb;
            g_array_append_val(rdata->points, pt2);
        }
        if (txlok && tybok) {
            pt2.x = txl - eps;
            pt2.y = tyb - eps;
            g_array_append_val(rdata->points, pt2);
        }
    }
    gwy_debug("after extension we have %u extpoints",
              rdata->points->len - nbase);

    if (!check_for_changes) {
        gwy_debug("do not check for changes, so just state expoints changed");
        g_assert(!oldextpoints);
        return TRUE;
    }

    extchanged = (noldext != rdata->points->len - nbase
                  || memcmp(&g_array_index(rdata->points, GwyXYZ, nbase),
                            oldextpoints,
                            noldext*sizeof(GwyXYZ)));
    g_free(oldextpoints);
    gwy_debug("comparison says extchanged = %d", extchanged);
    return extchanged;
}

static void
xyzras_free(XYZRasData *rdata)
{
    GWY_OBJECT_UNREF(rdata->triangulation);
    GWY_OBJECT_UNREF(rdata->raster);
    GWY_OBJECT_UNREF(rdata->nilmask);
    GWY_OBJECT_UNREF(rdata->regular);
    g_array_free(rdata->points, TRUE);
}

static gdouble
round_with_base(gdouble x, gdouble base)
{
    gint s;

    s = (x < 0) ? -1 : 1;
    x = fabs(x)/base;
    if (x <= 1.0)
        return GWY_ROUND(10.0*x)/10.0*s*base;
    else if (x <= 2.0)
        return GWY_ROUND(5.0*x)/5.0*s*base;
    else if (x <= 5.0)
        return GWY_ROUND(2.0*x)/2.0*s*base;
    else
        return GWY_ROUND(x)*s*base;
}

static void
round_to_nice(gdouble *minval, gdouble *maxval)
{
    gdouble range = *maxval - *minval;
    gdouble base = pow10(floor(log10(range) - 2.0));

    *minval = round_with_base(*minval, base);
    *maxval = round_with_base(*maxval, base);
}

static void
initialize_ranges(const XYZRasData *rdata,
                  XYZRasArgs *args)
{
    GwySurface *surface = rdata->surface;

    gwy_surface_get_xrange(surface, &args->xmin, &args->xmax);
    gwy_surface_get_yrange(surface, &args->ymin, &args->ymax);

    round_to_nice(&args->xmin, &args->xmax);
    round_to_nice(&args->ymin, &args->ymax);

    gwy_debug("%g %g :: %g %g", args->xmin, args->xmax, args->ymin, args->ymax);
}

static void
invalidate_raster(XYZRasData *rdata)
{
    gwy_object_unref(rdata->raster);
}

static inline guint
coords_to_grid_index(guint xres,
                     guint yres,
                     gdouble step,
                     gdouble x,
                     gdouble y)
{
    guint ix, iy;

    ix = (guint)floor(x/step);
    if (G_UNLIKELY(ix >= xres))
        ix--;

    iy = (guint)floor(y/step);
    if (G_UNLIKELY(iy >= yres))
        iy--;

    return iy*xres + ix;
}

static inline void
index_accumulate(guint *index_array,
                 guint n)
{
    guint i;

    for (i = 1; i <= n; i++)
        index_array[i] += index_array[i-1];
}

static inline void
index_rewind(guint *index_array,
             guint n)
{
    guint i;

    for (i = n; i; i--)
        index_array[i] = index_array[i-1];
    index_array[0] = 0;
}

static void
work_queue_init(WorkQueue *queue)
{
    queue->size = 64;
    queue->len = 0;
    queue->id = g_new(guint, queue->size);
}

static void
work_queue_destroy(WorkQueue *queue)
{
    g_free(queue->id);
}

static void
work_queue_add(WorkQueue *queue,
               guint id)
{
    if (G_UNLIKELY(queue->len == queue->size)) {
        queue->size *= 2;
        queue->id = g_renew(guint, queue->id, queue->size);
    }
    queue->id[queue->len] = id;
    queue->len++;
}

static void
work_queue_ensure(WorkQueue *queue,
                  guint id)
{
    guint i;

    for (i = 0; i < queue->len; i++) {
        if (queue->id[i] == id)
            return;
    }
    work_queue_add(queue, id);
}

static inline gdouble
point_dist2(const GwyXYZ *p,
            const GwyXYZ *q)
{
    gdouble dx = p->x - q->x;
    gdouble dy = p->y - q->y;

    return dx*dx + dy*dy;
}

static gboolean
maybe_add_point(WorkQueue *pointqueue,
                const GwyXYZ *newpoints,
                guint ii,
                gdouble eps2)
{
    const GwyXYZ *pt;
    guint i;

    pt = newpoints + pointqueue->id[ii];
    for (i = 0; i < pointqueue->pos; i++) {
        if (point_dist2(pt, newpoints + pointqueue->id[i]) < eps2) {
            GWY_SWAP(guint,
                     pointqueue->id[ii], pointqueue->id[pointqueue->pos]);
            pointqueue->pos++;
            return TRUE;
        }
    }
    return FALSE;
}

/* Calculate coordinate ranges and ensure points are more than epsrel*cellside
 * appart where cellside is the side of equivalent-area square for one point. */
static void
analyse_points(XYZRasData *rdata,
               double epsrel)
{
    GwySurface *surface = rdata->surface;
    WorkQueue cellqueue, pointqueue;
    const GwyXYZ *points, *pt;
    GwyXYZ *newpoints;
    gdouble xreal, yreal, eps, eps2, xr, yr, step;
    guint npoints, i, ii, j, ig, xres, yres, ncells, oldpos;
    gdouble xmin, xmax, ymin, ymax;
    guint *cell_index;

    /* Calculate data ranges */
    npoints = rdata->norigpoints = surface->n;
    points = surface->data;
    gwy_surface_get_xrange(surface, &xmin, &xmax);
    gwy_surface_get_yrange(surface, &ymin, &ymax);

    xreal = xmax - xmin;
    yreal = ymax - ymin;

    if (xreal == 0.0 || yreal == 0.0) {
        g_warning("All points lie on a line, we are going to crash.");
    }

    /* Make a virtual grid */
    xr = xreal/sqrt(npoints)*CELL_SIDE;
    yr = yreal/sqrt(npoints)*CELL_SIDE;

    if (xr <= yr) {
        xres = (guint)ceil(xreal/xr);
        step = xreal/xres;
        yres = (guint)ceil(yreal/step);
    }
    else {
        yres = (guint)ceil(yreal/yr);
        step = yreal/yres;
        xres = (guint)ceil(xreal/step);
    }
    rdata->step = step;
    eps = epsrel*step;
    eps2 = eps*eps;

    ncells = xres*yres;
    cell_index = g_new0(guint, ncells + 1);

    for (i = 0; i < npoints; i++) {
        pt = points + i;
        ig = coords_to_grid_index(xres, yres, step, pt->x - xmin, pt->y - ymin);
        cell_index[ig]++;
    }

    index_accumulate(cell_index, xres*yres);
    g_assert(cell_index[xres*yres] == npoints);
    index_rewind(cell_index, xres*yres);
    newpoints = g_new(GwyXYZ, npoints);

    /* Sort points by cell */
    for (i = 0; i < npoints; i++) {
        pt = points + i;
        ig = coords_to_grid_index(xres, yres, step, pt->x - xmin, pt->y - ymin);
        newpoints[cell_index[ig]] = *pt;
        cell_index[ig]++;
    }
    g_assert(cell_index[xres*yres] == npoints);
    index_rewind(cell_index, xres*yres);

    /* Find groups of identical (i.e. closer than epsrel) points we need to
     * merge.  We collapse all merged points to that with the lowest id.
     * Closeness must be transitive so the group must be gathered iteratively
     * until it no longer grows. */
    work_queue_init(&pointqueue);
    work_queue_init(&cellqueue);
    g_array_set_size(rdata->points, 0);
    for (i = 0; i < npoints; i++) {
        /* Ignore merged points */
        if (newpoints[i].z == G_MAXDOUBLE)
            continue;

        pointqueue.len = 0;
        cellqueue.len = 0;
        cellqueue.pos = 0;
        work_queue_add(&pointqueue, i);
        pointqueue.pos = 1;
        oldpos = 0;

        do {
            /* Update the list of cells to process.  Most of the time this is
             * no-op. */
            while (oldpos < pointqueue.pos) {
                gdouble x, y;
                gint ix, iy;

                pt = newpoints + pointqueue.id[oldpos];
                x = (pt->x - xmin)/step;
                ix = (gint)floor(x);
                x -= ix;
                y = (pt->y - ymin)/step;
                iy = (gint)floor(y);
                y -= iy;

                if (ix < xres && iy < yres)
                    work_queue_ensure(&cellqueue, iy*xres + ix);
                if (ix > 0 && iy < yres && x <= eps)
                    work_queue_ensure(&cellqueue, iy*xres + ix-1);
                if (ix < xres && iy > 0 && y <= eps)
                    work_queue_ensure(&cellqueue, (iy - 1)*xres + ix);
                if (ix > 0 && iy > 0 && x < eps && y <= eps)
                    work_queue_ensure(&cellqueue, (iy - 1)*xres + ix-1);
                if (ix+1 < xres && iy < yres && 1-x <= eps)
                    work_queue_ensure(&cellqueue, iy*xres + ix+1);
                if (ix < xres && iy+1 < yres && 1-y <= eps)
                    work_queue_ensure(&cellqueue, (iy + 1)*xres + ix);
                if (ix+1 < xres && iy+1 < yres && 1-x <= eps && 1-y <= eps)
                    work_queue_ensure(&cellqueue, (iy + 1)*xres + ix+1);

                oldpos++;
            }

            /* Process all points from the cells and check if they belong to
             * the currently merged group. */
            while (cellqueue.pos < cellqueue.len) {
                j = cellqueue.id[cellqueue.pos];
                for (ii = cell_index[j]; ii < cell_index[j+1]; ii++) {
                    if (ii != i && newpoints[ii].z != G_MAXDOUBLE)
                        work_queue_add(&pointqueue, ii);
                }
                cellqueue.pos++;
            }

            /* Compare all not-in-group points with all group points, adding
             * them to the group on success. */
            for (ii = pointqueue.pos; ii < pointqueue.len; ii++)
                maybe_add_point(&pointqueue, newpoints, ii, eps2);
        } while (oldpos != pointqueue.pos);

        /* Calculate the representant of all contributing points. */
        {
            GwyXYZ avg = { 0.0, 0.0, 0.0 };

            for (ii = 0; ii < pointqueue.pos; ii++) {
                GwyXYZ *ptii = newpoints + pointqueue.id[ii];
                avg.x += ptii->x;
                avg.y += ptii->y;
                avg.z += ptii->z;
                ptii->z = G_MAXDOUBLE;
            }

            avg.x /= pointqueue.pos;
            avg.y /= pointqueue.pos;
            avg.z /= pointqueue.pos;
            g_array_append_val(rdata->points, avg);
        }
    }

    work_queue_destroy(&cellqueue);
    work_queue_destroy(&pointqueue);
    g_free(cell_index);
    g_free(newpoints);

    rdata->nbasepoints = rdata->points->len;
}

/* Create a data field directly if the XY positions form a complete regular
 * grid.  */
static GwyDataField*
check_regular_grid(GwySurface *surface)
{
    GwyXY xymin, dxy;
    guint n, xres, yres, k;
    GwyDataField *dfield;
    gdouble *data;
    guint *map;

    n = surface->n;
    if (!(map = gwy_check_regular_2d_grid((const gdouble*)surface->data, 3, n,
                                          -1.0, &xres, &yres, &xymin, &dxy)))
        return NULL;

    dfield = gwy_data_field_new(xres, yres, xres*dxy.x, yres*dxy.y, FALSE);
    data = gwy_data_field_get_data(dfield);
    for (k = 0; k < n; k++)
        data[k] = surface->data[map[k]].z;
    g_free(map);

    gwy_data_field_set_xoffset(dfield, xymin.x);
    gwy_data_field_set_yoffset(dfield, xymin.y);
    gwy_surface_copy_units_to_data_field(surface, dfield);
    return dfield;
}

static const gchar exterior_key[]      = "/module/xyz_raster/exterior";
static const gchar interpolation_key[] = "/module/xyz_raster/interpolation";
static const gchar mask_empty_key[]    = "/module/xyz_raster/mask_empty";
static const gchar xres_key[]          = "/module/xyz_raster/xres";
static const gchar yres_key[]          = "/module/xyz_raster/yres";

static void
xyzras_sanitize_args(XYZRasArgs *args)
{
    if (args->interpolation != GWY_INTERPOLATION_ROUND
        && args->interpolation != GWY_INTERPOLATION_NNA
        && (gint)args->interpolation != GWY_INTERPOLATION_FIELD
        && (gint)args->interpolation != GWY_INTERPOLATION_AVERAGE)
        args->interpolation = GWY_INTERPOLATION_LINEAR;
    if (args->exterior != GWY_EXTERIOR_MIRROR_EXTEND
        && args->exterior != GWY_EXTERIOR_PERIODIC)
        args->exterior = GWY_EXTERIOR_BORDER_EXTEND;
    args->mask_empty = !!args->mask_empty;
    args->xres = CLAMP(args->xres, 2, 16384);
    args->yres = CLAMP(args->yres, 2, 16384);
}

static void
xyzras_load_args(GwyContainer *container,
                 XYZRasArgs *args)
{
    *args = xyzras_defaults;

    gwy_container_gis_enum_by_name(container, interpolation_key,
                                   &args->interpolation);
    gwy_container_gis_enum_by_name(container, exterior_key, &args->exterior);
    gwy_container_gis_boolean_by_name(container, mask_empty_key,
                                      &args->mask_empty);
    gwy_container_gis_int32_by_name(container, xres_key, &args->xres);
    gwy_container_gis_int32_by_name(container, yres_key, &args->yres);

    xyzras_sanitize_args(args);
}

static void
xyzras_save_args(GwyContainer *container,
                 XYZRasArgs *args)
{
    gwy_container_set_enum_by_name(container, interpolation_key,
                                   args->interpolation);
    gwy_container_set_enum_by_name(container, exterior_key, args->exterior);
    gwy_container_set_boolean_by_name(container, mask_empty_key,
                                      args->mask_empty);
    gwy_container_set_int32_by_name(container, xres_key, args->xres);
    gwy_container_set_int32_by_name(container, yres_key, args->yres);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
