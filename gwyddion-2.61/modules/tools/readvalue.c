/*
 *  $Id: readvalue.c 24706 2022-03-21 17:00:25Z yeti-dn $
 *  Copyright (C) 2003-2019 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/elliptic.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwymodule/gwymodule-tool.h>
#include <app/gwyapp.h>

#define GWY_TYPE_TOOL_READ_VALUE            (gwy_tool_read_value_get_type())
#define GWY_TOOL_READ_VALUE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_READ_VALUE, GwyToolReadValue))
#define GWY_IS_TOOL_READ_VALUE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_READ_VALUE))
#define GWY_TOOL_READ_VALUE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_READ_VALUE, GwyToolReadValueClass))

enum {
    RADIUS_MAX = 40,
    PREVIEW_SIZE = 2*RADIUS_MAX + 3,
    SCALE = 5,
};

typedef struct _GwyToolReadValue      GwyToolReadValue;
typedef struct _GwyToolReadValueClass GwyToolReadValueClass;

typedef struct {
    gint from;
    gint to;
    gint dest;
} Range;

typedef struct {
    gint radius;
    gboolean show_selection;
} ToolArgs;

struct _GwyToolReadValue {
    GwyPlainTool parent_instance;

    ToolArgs args;

    GwyContainer *data;
    GwyDataField *detail;

    gdouble avg;
    gdouble bx;
    gdouble by;
    gdouble k1;
    gdouble k2;

    gdouble *values;
    gint *xpos;
    gint *ypos;

    GtkWidget *zoomview;
    GwySelection *zselection;
    Range xr;
    Range yr;
    gint zisel[4];
    gulong palette_id;

    GtkWidget *x;
    GtkWidget *xpix;
    GtkWidget *y;
    GtkWidget *ypix;
    GtkWidget *z;
    GtkWidget *theta;
    GtkWidget *phi;
    GtkWidget *curv1;
    GtkWidget *curv2;
    GtkObject *radius;
    GtkWidget *show_selection;
    GtkWidget *set_zero;

    gboolean same_units;
    gboolean complete;
    gboolean in_update;

    /* to prevent double-update on data_changed -- badly designed code? */
    gboolean drawn;

    GwyDataField *xunc;
    GwyDataField *yunc;
    GwyDataField *zunc;
    gboolean has_calibration;

    /* potential class data */
    GwySIValueFormat *angle_format;
    GType layer_type_point;
};

struct _GwyToolReadValueClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType gwy_tool_read_value_get_type              (void)                      G_GNUC_CONST;
static void  gwy_tool_read_value_finalize              (GObject *object);
static void  gwy_tool_read_value_init_dialog           (GwyToolReadValue *tool);
static void  gwy_tool_read_value_data_switched         (GwyTool *gwytool,
                                                        GwyDataView *data_view);
static void  gwy_tool_read_value_update_units          (GwyToolReadValue *tool);
static void  gwy_tool_read_value_resize_detail         (GwyToolReadValue *tool);
static void  gwy_tool_read_value_data_changed          (GwyPlainTool *plain_tool);
static void  gwy_tool_read_value_palette_changed       (GwyToolReadValue *tool);
static void  gwy_tool_read_value_selection_changed     (GwyPlainTool *plain_tool,
                                                        gint hint);
static void  gwy_tool_read_value_radius_changed        (GwyToolReadValue *tool);
static void  gwy_tool_read_value_show_selection_changed(GtkToggleButton *check,
                                                        GwyToolReadValue *tool);
static void  gwy_tool_read_value_draw_zoom             (GwyToolReadValue *tool);
static void  gwy_tool_read_value_pix_spinned           (GwyToolReadValue *tool);
static void  gwy_tool_read_value_update_values         (GwyToolReadValue *tool);
static void  gwy_tool_read_value_calculate             (GwyToolReadValue *tool,
                                                        gint col,
                                                        gint row);
static void  gwy_tool_read_value_set_zero              (GwyToolReadValue *tool);
static void  calc_curvatures                           (const gdouble *values,
                                                        const gint *xpos,
                                                        const gint *ypos,
                                                        guint npts,
                                                        gdouble dx,
                                                        gdouble dy,
                                                        gdouble *pc1,
                                                        gdouble *pc2);

static const gchar radius_key[]         = "/module/readvalue/radius";
static const gchar show_selection_key[] = "/module/readvalue/show-selection";

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Pointer tool, reads value under pointer."),
    "Yeti <yeti@gwyddion.net>",
    "3.2",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

static const ToolArgs default_args = {
    1, FALSE,
};

GWY_MODULE_QUERY2(module_info, readvalue)

G_DEFINE_TYPE(GwyToolReadValue, gwy_tool_read_value, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_READ_VALUE);

    return TRUE;
}

static void
gwy_tool_read_value_class_init(GwyToolReadValueClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_read_value_finalize;

    tool_class->stock_id = GWY_STOCK_POINTER_MEASURE;
    tool_class->title = _("Read Value");
    tool_class->tooltip = _("Read value under mouse cursor");
    tool_class->prefix = "/module/readvalue";
    tool_class->data_switched = gwy_tool_read_value_data_switched;

    ptool_class->data_changed = gwy_tool_read_value_data_changed;
    ptool_class->selection_changed = gwy_tool_read_value_selection_changed;
}

static void
gwy_tool_read_value_finalize(GObject *object)
{
    GwyToolReadValue *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_READ_VALUE(object);

    g_free(tool->values);
    g_free(tool->xpos);
    g_free(tool->ypos);

    settings = gwy_app_settings_get();
    gwy_container_set_int32_by_name(settings, radius_key, tool->args.radius);
    gwy_container_set_boolean_by_name(settings, show_selection_key,
                                      tool->args.show_selection);

    GWY_SIGNAL_HANDLER_DISCONNECT(GWY_PLAIN_TOOL(object)->container,
                                  tool->palette_id);
    GWY_SI_VALUE_FORMAT_FREE(tool->angle_format);
    GWY_OBJECT_UNREF(tool->data);
    GWY_OBJECT_UNREF(tool->detail);

    G_OBJECT_CLASS(gwy_tool_read_value_parent_class)->finalize(object);
}

static void
gwy_tool_read_value_init(GwyToolReadValue *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_point = gwy_plain_tool_check_layer_type(plain_tool,
                                                             "GwyLayerPoint");
    if (!tool->layer_type_point)
        return;

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_MARKUP;
    plain_tool->lazy_updates = TRUE;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_int32_by_name(settings, radius_key, &tool->args.radius);
    gwy_container_gis_boolean_by_name(settings, show_selection_key,
                                      &tool->args.show_selection);

    tool->angle_format = gwy_si_unit_value_format_new(1.0, 1, _("deg"));
    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_point,
                                     "pointer");

    tool->data = gwy_container_new();
    tool->detail = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE,
                                      PREVIEW_SIZE, PREVIEW_SIZE,
                                      TRUE);
    gwy_container_set_object_by_name(tool->data, "/0/data", tool->detail);
    gwy_container_set_double_by_name(tool->data, "/0/base/min", 0.0);
    gwy_container_set_double_by_name(tool->data, "/0/base/max", 0.0);
    gwy_container_set_enum_by_name(tool->data, "/0/base/range-type",
                                   GWY_LAYER_BASIC_RANGE_FULL);

    gwy_tool_read_value_init_dialog(tool);
}

static void
attach_param_label(GtkTable *table, const gchar *str, gint row)
{
    GtkWidget *label;

    label = gtk_label_new(str);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
}

static GtkWidget*
attach_param_value(GtkTable *table, gint col, gint row)
{
    GtkWidget *label;

    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(table, label, col, col+1, row, row+1, GTK_FILL, 0, 0, 0);

    return label;
}

static void
attach_coord_row(GtkTable *table, const gchar *name, gint row,
                 GtkWidget **pxspin, GtkWidget **reallabel)
{
    GtkObject *adj;
    GtkWidget *spin, *hbox, *label;

    hbox = gtk_hbox_new(FALSE, 4);
    gtk_table_attach(table, hbox,
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(name);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);

    gtk_box_pack_end(GTK_BOX(hbox), gtk_label_new(_("px")), FALSE, FALSE, 0);

    adj = gtk_adjustment_new(1.0, 1.0, 100.0, 1.0, 10.0, 0.0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0.0, 0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(spin), 4);
    gtk_entry_set_text(GTK_ENTRY(spin), "");
    gtk_box_pack_end(GTK_BOX(hbox), spin, FALSE, FALSE, 0);

    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 4);

    *pxspin = spin;
    *reallabel = label;
}

static void
gwy_tool_read_value_init_dialog(GwyToolReadValue *tool)
{
    GtkDialog *dialog;
    GtkTable *table;
    GtkWidget *align, *hbox, *vbox;
    GwyPixmapLayer *layer;
    GwyVectorLayer *vlayer;
    gint row;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    hbox = gtk_hbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, TRUE, TRUE, 0);

    /* Zoom view */
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    tool->zoomview = gwy_data_view_new(tool->data);
    gwy_data_view_set_zoom(GWY_DATA_VIEW(tool->zoomview), (gdouble)SCALE);
    gtk_box_pack_start(GTK_BOX(vbox), tool->zoomview, FALSE, FALSE, 0);

    layer = gwy_layer_basic_new();
    gwy_pixmap_layer_set_data_key(layer, "/0/data");
    gwy_layer_basic_set_gradient_key(GWY_LAYER_BASIC(layer), "/0/base/palette");
    gwy_layer_basic_set_range_type_key(GWY_LAYER_BASIC(layer),
                                       "/0/base/range-type");
    gwy_data_view_set_base_layer(GWY_DATA_VIEW(tool->zoomview), layer);


    vlayer = GWY_VECTOR_LAYER(g_object_new(tool->layer_type_point, NULL));
    gwy_vector_layer_set_selection_key(vlayer, "/0/select/pointer");
    g_object_set(vlayer,
                 "marker-radius", tool->args.radius,
                 "editable", FALSE,
                 "focus", -1,
                 NULL);
    gwy_data_view_set_top_layer(GWY_DATA_VIEW(tool->zoomview), vlayer);
    tool->zselection = gwy_vector_layer_ensure_selection(vlayer);
    gwy_selection_set_max_objects(tool->zselection, 1);

    /* Right pane */
    vbox = gtk_vbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    table = GTK_TABLE(gtk_table_new(12, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(table), FALSE, FALSE, 0);
    row = 0;

    gtk_table_attach(table, gwy_label_new_header(_("Position")),
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    attach_coord_row(table, "X", row, &tool->xpix, &tool->x);
    g_signal_connect_swapped(tool->xpix, "value-changed",
                             G_CALLBACK(gwy_tool_read_value_pix_spinned), tool);
    row++;

    attach_coord_row(table, "Y", row, &tool->ypix, &tool->y);
    g_signal_connect_swapped(tool->ypix, "value-changed",
                             G_CALLBACK(gwy_tool_read_value_pix_spinned), tool);
    row++;

    gtk_table_set_row_spacing(table, row-1, 8);
    gtk_table_attach(table, gwy_label_new_header(_("Value")),
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    attach_param_label(table, "Z", row);
    tool->z = attach_param_value(table, 2, row++);

    align = gtk_alignment_new(1.0, 0.5, 0.0, 0.0);
    gtk_table_attach(table, align, 1, 3, row, row+1, GTK_FILL, 0, 0, 0);

    tool->set_zero = gtk_button_new_with_mnemonic(_("Set _Zero"));
    gtk_container_add(GTK_CONTAINER(align), tool->set_zero);
    gtk_widget_set_tooltip_text(tool->set_zero, _("Shift plane z=0 to pass through the selected point"));
    gtk_widget_set_sensitive(tool->set_zero, FALSE);
    g_signal_connect_swapped(tool->set_zero, "clicked",
                             G_CALLBACK(gwy_tool_read_value_set_zero), tool);
    row++;

    gtk_table_set_row_spacing(table, row-1, 8);
    gtk_table_attach(table, gwy_label_new_header(_("Facet")),
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    attach_param_label(table, _("Inclination θ"), row);
    tool->theta = attach_param_value(table, 2, row++);

    attach_param_label(table, _("Inclination φ"), row);
    tool->phi = attach_param_value(table, 2, row++);

    gtk_table_set_row_spacing(table, row-1, 8);
    gtk_table_attach(table, gwy_label_new_header(_("Curvatures")),
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    attach_param_label(table, _("Curvature 1"), row);
    tool->curv1 = attach_param_value(table, 2, row++);

    attach_param_label(table, _("Curvature 2"), row);
    tool->curv2 = attach_param_value(table, 2, row++);

    table = GTK_TABLE(gtk_table_new(2, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(table), FALSE, FALSE, 0);
    row = 0;

    tool->radius = gtk_adjustment_new(tool->args.radius,
                                      1, RADIUS_MAX, 1, 5, 0);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row,
                            _("_Averaging radius:"), _("px"),
                            tool->radius, GWY_HSCALE_LINEAR | GWY_HSCALE_SNAP);
    g_signal_connect_swapped(tool->radius, "value-changed",
                             G_CALLBACK(gwy_tool_read_value_radius_changed),
                             tool);
    row++;

    tool->show_selection
        = gtk_check_button_new_with_mnemonic(_("Show _selection"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->show_selection),
                                 tool->args.show_selection);
    gtk_table_attach(GTK_TABLE(table), tool->show_selection,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect(tool->show_selection, "toggled",
                     G_CALLBACK(gwy_tool_read_value_show_selection_changed),
                             tool);
    row++;

    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), TRUE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    gwy_tool_read_value_resize_detail(tool);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_read_value_data_switched(GwyTool *gwytool,
                                  GwyDataView *data_view)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(gwytool);
    GwyContainer *container;
    GwyToolReadValue *tool;
    GwyPixmapLayer *layer;
    const gchar *key;
    gchar *sigdetail;
    gboolean ignore;
    gchar xukey[24], yukey[24], zukey[24];

    tool = GWY_TOOL_READ_VALUE(gwytool);
    ignore = (data_view == plain_tool->data_view);

    if (!ignore)
        GWY_SIGNAL_HANDLER_DISCONNECT(plain_tool->container, tool->palette_id);

    GWY_TOOL_CLASS(gwy_tool_read_value_parent_class)->data_switched(gwytool,
                                                                    data_view);

    if (ignore || plain_tool->init_failed)
        return;

    if (data_view) {
        container = plain_tool->container;
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_point,
                                "draw-marker", tool->args.show_selection,
                                "marker-radius", tool->args.radius,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);
        gwy_tool_read_value_resize_detail(tool);
        gwy_tool_read_value_update_units(tool);
        /* We need to do this after the detail is resized. */
        gwy_tool_read_value_selection_changed(plain_tool, -1);

        layer = gwy_data_view_get_base_layer(data_view);
        g_return_if_fail(GWY_IS_LAYER_BASIC(layer));
        key = gwy_layer_basic_get_gradient_key(GWY_LAYER_BASIC(layer));
        if (key) {
            sigdetail = g_strconcat("item-changed::", key, NULL);
            tool->palette_id = g_signal_connect_swapped
                             (plain_tool->container, sigdetail,
                              G_CALLBACK(gwy_tool_read_value_palette_changed),
                              tool);
            g_free(sigdetail);
        }
        gwy_tool_read_value_palette_changed(tool);

        g_snprintf(xukey, sizeof(xukey), "/%d/data/cal_xunc", plain_tool->id);
        g_snprintf(yukey, sizeof(yukey), "/%d/data/cal_yunc", plain_tool->id);
        g_snprintf(zukey, sizeof(zukey), "/%d/data/cal_zunc", plain_tool->id);

        tool->has_calibration = FALSE;
        if (gwy_container_gis_object_by_name(container, xukey, &tool->xunc)
            && gwy_container_gis_object_by_name(container, yukey, &tool->yunc)
            && gwy_container_gis_object_by_name(container, zukey, &tool->zunc))
            tool->has_calibration = TRUE;
    }
    else {
        gtk_entry_set_text(GTK_ENTRY(tool->xpix), "");
        gtk_entry_set_text(GTK_ENTRY(tool->ypix), "");
    }

    gtk_widget_set_sensitive(tool->xpix, !!data_view);
    gtk_widget_set_sensitive(tool->ypix, !!data_view);
}

static void
gwy_tool_read_value_update_units(GwyToolReadValue *tool)
{
    GwyPlainTool *plain_tool;
    GwySIUnit *siunitxy, *siunitz;
    GwyDataField *dfield;
    gint dxres, dyres;

    plain_tool = GWY_PLAIN_TOOL(tool);
    dfield = plain_tool->data_field;

    siunitxy = gwy_data_field_get_si_unit_xy(dfield);
    siunitz = gwy_data_field_get_si_unit_z(dfield);
    tool->same_units = gwy_si_unit_equal(siunitxy, siunitz);

    gwy_data_field_copy_units(dfield, tool->detail);
    dxres = gwy_data_field_get_xres(tool->detail);
    dyres = gwy_data_field_get_yres(tool->detail);
    gwy_data_field_set_xreal(tool->detail, dxres*gwy_data_field_get_dx(dfield));
    gwy_data_field_set_yreal(tool->detail, dyres*gwy_data_field_get_dy(dfield));

    gtk_spin_button_set_range(GTK_SPIN_BUTTON(tool->xpix),
                              1, gwy_data_field_get_xres(dfield));
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(tool->ypix),
                              1, gwy_data_field_get_yres(dfield));
}

static void
gwy_tool_read_value_resize_detail(GwyToolReadValue *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    gint xres, yres, dxres, dyres, minres, maxres, newdxres, newdyres, newmaxr;
    gdouble newzoom;

    if (!plain_tool->data_field)
        return;

    xres = gwy_data_field_get_xres(plain_tool->data_field);
    yres = gwy_data_field_get_yres(plain_tool->data_field);
    dxres = gwy_data_field_get_xres(tool->detail);
    dyres = gwy_data_field_get_yres(tool->detail);
    gwy_debug("image %dx%d, detail %dx%d", xres, yres, dxres, dyres);

    /* Max determines the displayed region. */
    maxres = MIN(MAX(xres, yres), PREVIEW_SIZE);
    /* Min determines posible cut in orthogonal direction. */
    minres = MIN(MIN(xres, yres), maxres);
    gwy_debug("minres %d, maxres %d", minres, maxres);

    newdxres = (xres == minres) ? minres : maxres;
    newdyres = (yres == minres) ? minres : maxres;
    gwy_debug("detail should be %dx%d", newdxres, newdyres);

    if (newdxres == dxres && newdyres == dyres)
        return;

    newmaxr = MAX((MIN(newdyres, newdyres) - 3)/2, 1);
    g_object_set(tool->radius,
                 "value", (gdouble)MIN(newmaxr, tool->args.radius),
                 "upper", (gdouble)newmaxr,
                 NULL);

    gwy_data_field_resample(tool->detail, newdxres, newdyres,
                            GWY_INTERPOLATION_NONE);
    gwy_data_field_clear(tool->detail);

    newzoom = (gdouble)SCALE/MAX(newdxres, newdyres)*PREVIEW_SIZE;
    gwy_debug("updating zoom to %g", newzoom);
    gwy_data_view_set_zoom(GWY_DATA_VIEW(tool->zoomview), newzoom);
    gwy_data_field_data_changed(tool->detail);
}

static void
gwy_tool_read_value_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolReadValue *tool = GWY_TOOL_READ_VALUE(plain_tool);
    GwyContainer *container = plain_tool->container;
    gchar xukey[24], yukey[24], zukey[24];

    tool->has_calibration = FALSE;
    g_snprintf(xukey, sizeof(xukey), "/%d/data/cal_xunc", plain_tool->id);
    g_snprintf(yukey, sizeof(yukey), "/%d/data/cal_yunc", plain_tool->id);
    g_snprintf(zukey, sizeof(zukey), "/%d/data/cal_zunc", plain_tool->id);

    if (gwy_container_gis_object_by_name(container, xukey, &tool->xunc)
        && gwy_container_gis_object_by_name(container, yukey, &tool->yunc)
        && gwy_container_gis_object_by_name(container, zukey, &tool->zunc))
        tool->has_calibration = TRUE;

    gwy_tool_read_value_resize_detail(tool);
    gwy_tool_read_value_update_units(tool);

    tool->drawn = FALSE;
    gwy_tool_read_value_selection_changed(plain_tool, -1);
    if (!tool->drawn)
        gwy_tool_read_value_draw_zoom(tool);
}

static void
gwy_tool_read_value_palette_changed(GwyToolReadValue *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_app_sync_data_items(plain_tool->container, tool->data,
                            plain_tool->id, 0,
                            TRUE,
                            GWY_DATA_ITEM_GRADIENT, 0);
}

static gboolean
find_subrange(gint center, gint res, gint size, Range *r)
{
    /* complete interval always fit in size */
    if (res <= size) {
        r->from = 0;
        r->to = res;
        r->dest = (size - res)/2;
        return FALSE;
    }

    /* try to keep center in center */
    r->dest = 0;
    r->from = center - size/2;
    r->to = center + size/2 + 1;
    /* but move it if not possible */
    if (r->from < 0) {
        r->to -= r->from;
        r->from = 0;
    }
    if (r->to > res) {
        r->from -= (r->to - res);
        r->to = res;
    }
    g_assert(r->from >= 0);
    return TRUE;
}

static void
gwy_tool_read_value_selection_changed(GwyPlainTool *plain_tool,
                                      gint hint)
{
    GwyToolReadValue *tool;
    GwyDataField *dfield;
    Range xr, yr;
    gboolean has_selection, complete;
    gint xres, yres, dxres, dyres;
    gdouble sel[2];
    gint isel[2];

    tool = GWY_TOOL_READ_VALUE(plain_tool);
    g_return_if_fail(hint <= 0);

    has_selection = FALSE;
    dfield = plain_tool->data_field;
    if (plain_tool->selection)
        has_selection = gwy_selection_get_object(plain_tool->selection, 0, sel);

    gwy_tool_read_value_update_values(tool);
    gtk_widget_set_sensitive(tool->set_zero, has_selection);

    complete = TRUE;
    if (has_selection) {
        dxres = gwy_data_field_get_xres(tool->detail);
        dyres = gwy_data_field_get_yres(tool->detail);
        isel[0] = floor(gwy_data_field_rtoj(dfield, sel[0]));
        isel[1] = floor(gwy_data_field_rtoi(dfield, sel[1]));
        xres = gwy_data_field_get_xres(dfield);
        yres = gwy_data_field_get_yres(dfield);
        complete &= find_subrange(isel[0], xres, dxres, &xr);
        complete &= find_subrange(isel[1], yres, dyres, &yr);
        gwy_debug("complete: %d", complete);
        tool->in_update = TRUE;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(tool->xpix), isel[0] + 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(tool->ypix), isel[1] + 1);
        tool->in_update = FALSE;
    }
    else {
        xr.from = yr.from = xr.to = yr.to = -1;
        gtk_entry_set_text(GTK_ENTRY(tool->xpix), "");
        gtk_entry_set_text(GTK_ENTRY(tool->ypix), "");
    }

    tool->xr = xr;
    tool->yr = yr;
    tool->complete = complete;
    gwy_tool_read_value_draw_zoom(tool);
    tool->drawn = TRUE;

    if (!has_selection) {
        gwy_selection_clear(tool->zselection);
        return;
    }

    gwy_debug("x: %d - %d => %d", isel[0], tool->xr.from, isel[0] - tool->xr.from);
    gwy_debug("x: %d - %d => %d", isel[1], tool->yr.from, isel[1] - tool->yr.from);
    sel[0] = gwy_data_field_jtor(dfield, isel[0] - tool->xr.from + 0.5);
    sel[1] = gwy_data_field_itor(dfield, isel[1] - tool->yr.from + 0.5);
    gwy_selection_set_object(tool->zselection, 0, sel);
}

static void
gwy_tool_read_value_radius_changed(GwyToolReadValue *tool)
{
    GwyPlainTool *plain_tool;
    GwyVectorLayer *vlayer;

    tool->args.radius = gwy_adjustment_get_int(tool->radius);
    plain_tool = GWY_PLAIN_TOOL(tool);
    if (plain_tool->layer) {
        g_object_set(plain_tool->layer,
                     "marker-radius", tool->args.radius,
                     NULL);
    }
    if (plain_tool->selection)
        gwy_tool_read_value_update_values(tool);

    vlayer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(tool->zoomview));
    g_object_set(vlayer, "marker-radius", tool->args.radius, NULL);
}

static void
gwy_tool_read_value_show_selection_changed(GtkToggleButton *check,
                                           GwyToolReadValue *tool)
{
    GwyPlainTool *plain_tool;

    tool->args.show_selection = gtk_toggle_button_get_active(check);
    plain_tool = GWY_PLAIN_TOOL(tool);
    if (plain_tool->layer) {
        g_object_set(plain_tool->layer,
                     "draw-marker", tool->args.show_selection,
                     NULL);
    }
}

static void
gwy_tool_read_value_draw_zoom(GwyToolReadValue *tool)
{
    GwyPlainTool *plain_tool;
    gdouble min;

    if (tool->xr.from < 0 || tool->yr.from < 0) {
        gwy_data_field_clear(tool->detail);
        gwy_container_set_double_by_name(tool->data, "/0/base/min", 0.0);
        gwy_container_set_double_by_name(tool->data, "/0/base/max", 0.0);
    }
    else {
        plain_tool = GWY_PLAIN_TOOL(tool);
        if (!tool->complete) {
            min = gwy_data_field_area_get_min(plain_tool->data_field, NULL,
                                              tool->xr.from, tool->yr.from,
                                              tool->xr.to - tool->xr.from,
                                              tool->yr.to - tool->yr.from);
            gwy_data_field_fill(tool->detail, min);
        }
        gwy_data_field_area_copy(plain_tool->data_field, tool->detail,
                                 tool->xr.from, tool->yr.from,
                                 tool->xr.to - tool->xr.from,
                                 tool->yr.to - tool->yr.from,
                                 tool->xr.dest, tool->yr.dest);
    }
    gwy_data_field_data_changed(tool->detail);
}

static void
gwy_tool_read_value_pix_spinned(GwyToolReadValue *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GtkSpinButton *spin;
    gdouble sel[2];

    if (tool->in_update)
        return;

    if (!plain_tool->selection || !plain_tool->data_field)
        return;

    if (!strlen(gtk_entry_get_text(GTK_ENTRY(tool->xpix)))
        || !strlen(gtk_entry_get_text(GTK_ENTRY(tool->ypix))))
        return;

    spin = GTK_SPIN_BUTTON(tool->xpix);
    sel[0] = gwy_data_field_jtor(plain_tool->data_field,
                                 gtk_spin_button_get_value(spin) - 0.5);
    spin = GTK_SPIN_BUTTON(tool->ypix);
    sel[1] = gwy_data_field_itor(plain_tool->data_field,
                                 gtk_spin_button_get_value(spin) - 0.5);
    gwy_selection_set_object(plain_tool->selection, 0, sel);
}

static void
update_label(GwySIValueFormat *units,
             GtkWidget *label,
             gdouble value)
{
    static gchar buffer[64];

    g_return_if_fail(units);
    g_return_if_fail(GTK_IS_LABEL(label));

    g_snprintf(buffer, sizeof(buffer), "%.*f%s%s",
               units->precision, value/units->magnitude,
               *units->units ? " " : "", units->units);
    gtk_label_set_markup(GTK_LABEL(label), buffer);
}

static void
update_curvature_label(GtkWidget *label, gdouble value,
                       GwyDataField *dfield)
{
    GwySIUnit *unit = gwy_data_field_get_si_unit_xy(dfield);
    GwySIUnit *curvunit = gwy_si_unit_power(unit, -1, NULL);
    GwySIValueFormat *vf;

    vf = gwy_si_unit_get_format_with_digits(curvunit,
                                            GWY_SI_UNIT_FORMAT_VFMARKUP,
                                            value, 3, NULL);
    update_label(vf, label, value);
    gwy_si_unit_value_format_free(vf);
    g_object_unref(curvunit);
}

static void
update_label_unc(GwySIValueFormat *units,
             GtkWidget *label,
             gdouble value,
             gdouble unc)
{
    static gchar buffer[64];

    g_return_if_fail(units);
    g_return_if_fail(GTK_IS_LABEL(label));

    g_snprintf(buffer, sizeof(buffer), "(%.*f±%.*f)%s%s",
               units->precision, value/units->magnitude, units->precision, unc/units->magnitude,
               *units->units ? " " : "", units->units);
    gtk_label_set_markup(GTK_LABEL(label), buffer);
}

static void
gwy_tool_read_value_update_values(GwyToolReadValue *tool)
{
    GwyPlainTool *plain_tool;
    gboolean is_selected = FALSE;
    gdouble point[2];
    gdouble xoff, yoff;
    gint col, row;

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (plain_tool->data_field && plain_tool->selection)
        is_selected = gwy_selection_get_object(plain_tool->selection, 0, point);

    if (!is_selected) {
        gtk_label_set_text(GTK_LABEL(tool->x), NULL);
        gtk_label_set_text(GTK_LABEL(tool->y), NULL);
        gtk_label_set_text(GTK_LABEL(tool->z), NULL);
        gtk_label_set_text(GTK_LABEL(tool->theta), NULL);
        gtk_label_set_text(GTK_LABEL(tool->phi), NULL);
        gtk_label_set_text(GTK_LABEL(tool->curv1), NULL);
        gtk_label_set_text(GTK_LABEL(tool->curv1), NULL);
        return;
    }

    xoff = gwy_data_field_get_xoffset(plain_tool->data_field);
    yoff = gwy_data_field_get_yoffset(plain_tool->data_field);

    col = floor(gwy_data_field_rtoj(plain_tool->data_field, point[0]));
    row = floor(gwy_data_field_rtoi(plain_tool->data_field, point[1]));

    update_label(plain_tool->coord_format, tool->x, point[0] + xoff);
    update_label(plain_tool->coord_format, tool->y, point[1] + yoff);
    gwy_tool_read_value_calculate(tool, col, row);

    /* FIXME, use local plane fitting and uncertainty propagation */
    if (tool->has_calibration)
          update_label_unc(plain_tool->value_format,
                           tool->z,
                           tool->avg,
                           gwy_data_field_get_dval_real(tool->zunc,
                                                        point[0],
                                                        point[1],
                                                        GWY_INTERPOLATION_BILINEAR));
    else
        update_label(plain_tool->value_format, tool->z, tool->avg);

    if (tool->same_units) {
        update_label(tool->angle_format, tool->theta,
                     180.0/G_PI*atan(hypot(tool->bx, tool->by)));
        update_label(tool->angle_format, tool->phi,
                     180.0/G_PI*atan2(tool->by, tool->bx));
        update_curvature_label(tool->curv1, tool->k1, plain_tool->data_field);
        update_curvature_label(tool->curv2, tool->k2, plain_tool->data_field);
    }
    else {
        gtk_label_set_text(GTK_LABEL(tool->theta), _("N.A."));
        gtk_label_set_text(GTK_LABEL(tool->phi), _("N.A."));
        gtk_label_set_text(GTK_LABEL(tool->curv1), _("N.A."));
        gtk_label_set_text(GTK_LABEL(tool->curv2), _("N.A."));
    }
}

static void
gwy_tool_read_value_calculate(GwyToolReadValue *tool,
                              gint col,
                              gint row)
{
    GwyPlainTool *plain_tool;
    GwyDataField *dfield;
    gint n, i;
    gdouble m[6], z[3];

    plain_tool = GWY_PLAIN_TOOL(tool);
    dfield = plain_tool->data_field;

    if (tool->args.radius == 1) {
        tool->avg = gwy_data_field_get_val(dfield, col, row);
        tool->bx = gwy_data_field_get_xder(dfield, col, row);
        tool->by = gwy_data_field_get_yder(dfield, col, row);
        tool->k1 = tool->k2 = 0.0;
        return;
    }

    /* Create the arrays the first time radius > 1 is requested */
    if (!tool->values) {
        n = gwy_data_field_get_circular_area_size(RADIUS_MAX - 0.5);
        tool->values = g_new(gdouble, n);
        tool->xpos = g_new(gint, n);
        tool->ypos = g_new(gint, n);
    }

    n = gwy_data_field_circular_area_extract_with_pos(dfield, col, row,
                                                      tool->args.radius - 0.5,
                                                      tool->values,
                                                      tool->xpos, tool->ypos);
    tool->avg = 0.0;
    if (!n) {
        tool->bx = tool->by = 0.0;
        tool->k1 = tool->k2 = 0.0;
        g_warning("Z average calculated from an empty area");
        return;
    }

    /* Fit plane through extracted data */
    memset(m, 0, 6*sizeof(gdouble));
    memset(z, 0, 3*sizeof(gdouble));
    for (i = 0; i < n; i++) {
        m[0] += 1.0;
        m[1] += tool->xpos[i];
        m[2] += tool->xpos[i] * tool->xpos[i];
        m[3] += tool->ypos[i];
        m[4] += tool->xpos[i] * tool->ypos[i];
        m[5] += tool->ypos[i] * tool->ypos[i];
        z[0] += tool->values[i];
        z[1] += tool->values[i] * tool->xpos[i];
        z[2] += tool->values[i] * tool->ypos[i];
    }
    tool->avg = z[0]/n;
    gwy_math_choleski_decompose(3, m);
    gwy_math_choleski_solve(3, m, z);
    /* The signs may seem odd.  We have to invert y due to coordinate system
     * and then invert both for downward slopes.  As a result x is inverted. */
    tool->bx = -z[1]/gwy_data_field_get_dx(dfield);
    tool->by = z[2]/gwy_data_field_get_dy(dfield);

    calc_curvatures(tool->values, tool->xpos, tool->ypos, n,
                    gwy_data_field_get_dx(dfield),
                    gwy_data_field_get_dy(dfield),
                    &tool->k1, &tool->k2);
}

static void
gwy_tool_read_value_set_zero(GwyToolReadValue *tool)
{
    GwyPlainTool *plain_tool;
    GQuark quark;

    plain_tool = GWY_PLAIN_TOOL(tool);
    if (!plain_tool->data_field
        || !gwy_selection_get_data(plain_tool->selection, NULL)
        || !tool->avg)
        return;

    quark = gwy_app_get_data_key_for_id(plain_tool->id);
    gwy_app_undo_qcheckpointv(plain_tool->container, 1, &quark);
    gwy_data_field_add(plain_tool->data_field, -tool->avg);
    gwy_data_field_data_changed(plain_tool->data_field);
}

static void
calc_curvatures(const gdouble *values,
                const gint *xpos, const gint *ypos,
                guint npts,
                gdouble dx, gdouble dy,
                gdouble *pc1,
                gdouble *pc2)
{
    gdouble sx2 = 0.0, sy2 = 0.0, sx4 = 0.0, sx2y2 = 0.0, sy4 = 0.0;
    gdouble sz = 0.0, szx = 0.0, szy = 0.0, szx2 = 0.0, szxy = 0.0, szy2 = 0.0;
    gdouble scale = sqrt(dx*dy)*4.0;
    gdouble a[21], b[6], k1, k2;
    gint i, n = 0;

    for (i = 0; i < npts; i++) {
        gdouble x = xpos[i]*dx/scale;
        gdouble y = ypos[i]*dy/scale;
        gdouble z = values[i]/scale;
        gdouble xx = x*x, yy = y*y;

        sx2 += xx;
        sx2y2 += xx*yy;
        sy2 += yy;
        sx4 += xx*xx;
        sy4 += yy*yy;

        sz += z;
        szx += x*z;
        szy += y*z;
        szx2 += xx*z;
        szxy += x*y*z;
        szy2 += yy*z;
        n++;
    }

    gwy_clear(a, 21);
    a[0] = n;
    a[2] = a[6] = sx2;
    a[5] = a[15] = sy2;
    a[18] = a[14] = sx2y2;
    a[9] = sx4;
    a[20] = sy4;
    if (gwy_math_choleski_decompose(6, a)) {
        b[0] = sz;
        b[1] = szx;
        b[2] = szy;
        b[3] = szx2;
        b[4] = szxy;
        b[5] = szy2;
        gwy_math_choleski_solve(6, a, b);
    }
    else {
        *pc1 = *pc2 = 0.0;
        return;
    }

    gwy_math_curvature_at_origin(b, &k1, &k2, NULL, NULL);
    *pc1 = k1/scale;
    *pc2 = k2/scale;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
