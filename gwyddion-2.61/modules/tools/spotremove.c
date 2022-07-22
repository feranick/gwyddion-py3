/*
 *  $Id: spotremove.c 24706 2022-03-21 17:00:25Z yeti-dn $
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/stats.h>
#include <libprocess/fractals.h>
#include <libprocess/grains.h>
#include <libprocess/elliptic.h>
#include <libprocess/correct.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwymodule/gwymodule-tool.h>
#include <app/gwyapp.h>

#define GWY_TYPE_TOOL_SPOT_REMOVER            (gwy_tool_spot_remover_get_type())
#define GWY_TOOL_SPOT_REMOVER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_SPOT_REMOVER, GwyToolSpotRemover))
#define GWY_IS_TOOL_SPOT_REMOVER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_SPOT_REMOVER))
#define GWY_TOOL_SPOT_REMOVER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_SPOT_REMOVER, GwyToolSpotRemoverClass))

enum {
    MAX_SIZE = 82,
    SCALE = 5,
    NCOORDS = 4,
};

typedef enum {
    GWY_SPOT_REMOVE_HYPER_FLATTEN   = 0,
    GWY_SPOT_REMOVE_PSEUDO_LAPLACE  = 1,
    GWY_SPOT_REMOVE_LAPLACE         = 2,
    GWY_SPOT_REMOVE_FRACTAL         = 3,
    GWY_SPOT_REMOVE_FRACTAL_LAPLACE = 4,
    GWY_SPOT_REMOVE_ZERO            = 5,
    GWY_SPOT_REMOVE_NMETHODS
} SpotRemoveMethod;

typedef enum {
    GWY_SPOT_REMOVE_RECTANGLE = 0,
    GWY_SPOT_REMOVE_ELLIPSE   = 1,
} SpotRemoveShape;

typedef void (*AreaFillFunc)(GwyDataField *dfield,
                             gint col, gint row, gint width, gint height,
                             gdouble value);

typedef struct _GwyToolSpotRemover      GwyToolSpotRemover;
typedef struct _GwyToolSpotRemoverClass GwyToolSpotRemoverClass;

typedef struct {
    gint from;
    gint to;
    gint dest;
} Range;

typedef struct {
    gdouble z;
    gint i;
    gint j;
} PixelValue;

typedef struct {
    SpotRemoveMethod method;
    SpotRemoveShape shape;
} ToolArgs;

struct _GwyToolSpotRemover {
    GwyPlainTool parent_instance;

    ToolArgs args;

    GwyContainer *data;
    GwyDataField *detail;

    GtkWidget *zoomview;
    GtkWidget *method;
    GSList *shape;
    GtkWidget *message_label;
    GtkWidget *apply;
    GtkWidget *clear;
    GwySelection *zselection;
    gulong zsel_id;

    gulong palette_id;
    gboolean complete;
    Range xr;
    Range yr;
    gint zisel[4];

    GwySIValueFormat *pixel_format;
    GtkWidget *label_real[NCOORDS];
    GtkWidget *label_pix[NCOORDS];

    /* to prevent double-update on data_changed -- badly designed code? */
    gboolean drawn;

    gboolean has_selection;
    gboolean has_zselection;

    /* potential class data */
    GType layer_type_point;
    GType layer_type_rect;
    GType layer_type_ell;
};

struct _GwyToolSpotRemoverClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType      gwy_tool_spot_remover_get_type         (void)                      G_GNUC_CONST;
static void       gwy_tool_spot_remover_finalize         (GObject *object);
static void       gwy_tool_spot_remover_init_dialog      (GwyToolSpotRemover *tool);
static void       gwy_tool_spot_remover_data_switched    (GwyTool *gwytool,
                                                          GwyDataView *data_view);
static void       gwy_tool_spot_remover_data_changed     (GwyPlainTool *plain_tool);
static void       gwy_tool_spot_remover_palette_changed  (GwyToolSpotRemover *tool);
static void       gwy_tool_spot_remover_response         (GwyTool *gwytool,
                                                          gint response_id);
static void       gwy_tool_spot_remover_resize_detail    (GwyToolSpotRemover *tool);
static void       gwy_tool_spot_remover_selection_changed(GwyPlainTool *plain_tool,
                                                          gint hint);
static GtkWidget* create_selection_info_table            (GwyToolSpotRemover *tool);
static void       setup_zoom_vector_layer                (GwyToolSpotRemover *tool);
static void       zselection_changed                     (GwySelection *selection,
                                                          gint hint,
                                                          GwyToolSpotRemover *tool);
static void       update_selection_info_table            (GwyToolSpotRemover *tool);
static void       gwy_tool_spot_remover_draw_zoom        (GwyToolSpotRemover *tool);
static void       gwy_tool_spot_remover_update_message   (GwyToolSpotRemover *tool);
static void       method_changed                         (GtkComboBox *combo,
                                                          GwyToolSpotRemover *tool);
static void       shape_changed                          (GtkToggleButton *toggle,
                                                          GwyToolSpotRemover *tool);
static void       gwy_tool_spot_remover_apply            (GwyToolSpotRemover *tool);
static gboolean   find_subrange                          (gint center,
                                                          gint res,
                                                          gint size,
                                                          Range *r);
static void       blend_fractal_and_laplace              (GwyDataField *dfield,
                                                          GwyDataField *area,
                                                          GwyDataField *distances,
                                                          gint col,
                                                          gint row);
static void       pseudo_laplace_average                 (GwyDataField *dfield,
                                                          GwyDataField *mask);
static void       hyperbolic_average                     (GwyDataField *dfield,
                                                          GwyDataField *mask);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Spot removal tool, interpolates small parts of data (displayed on "
       "a zoomed view) using selected algorithm."),
    "Yeti <yeti@gwyddion.net>",
    "3.4",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

static const gchar method_key[] = "/module/spotremover/method";
static const gchar shape_key[]  = "/module/spotremover/shape";

static const ToolArgs default_args = {
    GWY_SPOT_REMOVE_PSEUDO_LAPLACE,
    GWY_SPOT_REMOVE_RECTANGLE,
};

GWY_MODULE_QUERY2(module_info, spotremove)

G_DEFINE_TYPE(GwyToolSpotRemover, gwy_tool_spot_remover, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_SPOT_REMOVER);

    return TRUE;
}

static void
gwy_tool_spot_remover_class_init(GwyToolSpotRemoverClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_spot_remover_finalize;

    tool_class->stock_id = GWY_STOCK_SPOT_REMOVE;
    tool_class->title = _("Remove Spots");
    tool_class->tooltip = _("Interpolate small defects, manually selected");
    tool_class->prefix = "/module/spotremover";
    tool_class->data_switched = gwy_tool_spot_remover_data_switched;
    tool_class->response = gwy_tool_spot_remover_response;

    ptool_class->data_changed = gwy_tool_spot_remover_data_changed;
    ptool_class->selection_changed = gwy_tool_spot_remover_selection_changed;
}

static void
gwy_tool_spot_remover_finalize(GObject *object)
{
    GwyToolSpotRemover *tool = GWY_TOOL_SPOT_REMOVER(object);
    GwyContainer *settings;

    settings = gwy_app_settings_get();
    gwy_container_set_enum_by_name(settings, method_key, tool->args.method);
    gwy_container_set_enum_by_name(settings, shape_key, tool->args.shape);

    GWY_SIGNAL_HANDLER_DISCONNECT(GWY_PLAIN_TOOL(object)->container,
                                  tool->palette_id);
    GWY_SI_VALUE_FORMAT_FREE(tool->pixel_format);
    GWY_OBJECT_UNREF(tool->data);
    GWY_OBJECT_UNREF(tool->detail);

    G_OBJECT_CLASS(gwy_tool_spot_remover_parent_class)->finalize(object);
}

static void
gwy_tool_spot_remover_init(GwyToolSpotRemover *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_point = gwy_plain_tool_check_layer_type(plain_tool,
                                                             "GwyLayerPoint");
    tool->layer_type_rect = gwy_plain_tool_check_layer_type(plain_tool,
                                                            "GwyLayerRectangle");
    tool->layer_type_ell = gwy_plain_tool_check_layer_type(plain_tool,
                                                           "GwyLayerEllipse");
    if (!tool->layer_type_point
        || !tool->layer_type_rect
        || !tool->layer_type_ell)
        return;

    plain_tool->lazy_updates = TRUE;
    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_VFMARKUP;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_enum_by_name(settings, method_key, &tool->args.method);
    gwy_container_gis_enum_by_name(settings, shape_key, &tool->args.shape);

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_point,
                                     "pointer");

    tool->data = gwy_container_new();
    tool->detail = gwy_data_field_new(MAX_SIZE, MAX_SIZE, MAX_SIZE, MAX_SIZE,
                                      TRUE);
    gwy_container_set_object_by_name(tool->data, "/0/data", tool->detail);
    gwy_container_set_double_by_name(tool->data, "/0/base/min", 0.0);
    gwy_container_set_double_by_name(tool->data, "/0/base/max", 0.0);
    gwy_container_set_enum_by_name(tool->data, "/0/base/range-type",
                                   GWY_LAYER_BASIC_RANGE_FULL);

    tool->pixel_format = gwy_si_unit_value_format_new(1.0, 0, _("px"));

    gwy_tool_spot_remover_init_dialog(tool);
}

static void
gwy_tool_spot_remover_init_dialog(GwyToolSpotRemover *tool)
{
    static const GwyEnum methods[] = {
        { N_("Hyperbolic flatten"),    GWY_SPOT_REMOVE_HYPER_FLATTEN,   },
        { N_("Pseudo-Laplace"),        GWY_SPOT_REMOVE_PSEUDO_LAPLACE,  },
        { N_("Laplace solver"),        GWY_SPOT_REMOVE_LAPLACE,         },
        { N_("Fractal interpolation"), GWY_SPOT_REMOVE_FRACTAL,         },
        { N_("Fractal-Laplace blend"), GWY_SPOT_REMOVE_FRACTAL_LAPLACE, },
        { N_("Zero"),                  GWY_SPOT_REMOVE_ZERO,            },
    };
    static struct {
        guint type;
        const gchar *stock_id;
        const gchar *text;
    }
    shapes[] = {
        {
            GWY_SPOT_REMOVE_RECTANGLE,
            GWY_STOCK_MASK,
            N_("Rectangle"),
        },
        {
            GWY_SPOT_REMOVE_ELLIPSE,
            GWY_STOCK_MASK_CIRCLE,
            N_("Ellipse"),
        },
    };

    GtkDialog *dialog;
    GtkWidget *hbox, *vbox, *label, *hbox2, *image, *button, *info;
    GtkTable *table;
    GtkRadioButton *group;
    GwyPixmapLayer *layer;
    gint row;
    guint i, ir;

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

    setup_zoom_vector_layer(tool);

    /* Right pane */
    vbox = gtk_vbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    /* Options */
    table = GTK_TABLE(gtk_table_new(5, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(table), FALSE, FALSE, 0);
    row = 0;

    info = create_selection_info_table(tool);
    gtk_table_attach(table, info, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    gtk_table_set_row_spacing(table, row-1, 8);
    label = gwy_label_new_header(_("Options"));
    gtk_table_attach(table, label, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    tool->method = gwy_enum_combo_box_new(methods, G_N_ELEMENTS(methods),
                                          G_CALLBACK(method_changed), tool,
                                          tool->args.method, TRUE);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row++,
                            _("_Interpolation method:"), NULL,
                            GTK_OBJECT(tool->method),
                            GWY_HSCALE_WIDGET_NO_EXPAND);

    hbox2 = gtk_hbox_new(FALSE, 0);
    gtk_table_attach(table, hbox2, 1, 2, row, row+1, GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Shape:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1, GTK_FILL, 0, 0, 0);

    group = NULL;
    for (i = 0; i < G_N_ELEMENTS(shapes); i++) {
        ir = G_N_ELEMENTS(shapes)-1 - i;
        button = gtk_radio_button_new_from_widget(group);
        g_object_set(button, "draw-indicator", FALSE, NULL);
        image = gtk_image_new_from_stock(shapes[ir].stock_id,
                                         GTK_ICON_SIZE_LARGE_TOOLBAR);
        gtk_container_add(GTK_CONTAINER(button), image);
        gwy_radio_button_set_value(button, shapes[ir].type);
        gtk_box_pack_end(GTK_BOX(hbox2), button, FALSE, FALSE, 0);
        gtk_widget_set_tooltip_text(button, gettext(shapes[ir].text));
        g_signal_connect(button, "clicked", G_CALLBACK(shape_changed), tool);
        if (!group)
            group = GTK_RADIO_BUTTON(button);
    }
    tool->shape = gtk_radio_button_get_group(group);
    gwy_radio_buttons_set_current(tool->shape, tool->args.shape);
    row++;

    gtk_table_set_row_spacing(table, row-1, 8);
    tool->message_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(tool->message_label), 0.0, 0.5);
    gtk_table_attach(table, tool->message_label,
                     0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    tool->clear = gtk_dialog_add_button(dialog, GTK_STOCK_CLEAR,
                                        GWY_TOOL_RESPONSE_CLEAR);
    gwy_tool_add_hide_button(GWY_TOOL(tool), FALSE);
    tool->apply = gtk_dialog_add_button(dialog, GTK_STOCK_APPLY,
                                        GTK_RESPONSE_APPLY);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    gtk_widget_set_sensitive(tool->clear, FALSE);
    gwy_tool_spot_remover_resize_detail(tool);

    gtk_widget_show_all(dialog->vbox);
}

static GtkWidget*
create_selection_info_table(GwyToolSpotRemover *tool)
{
    GtkTable *table;
    GtkWidget *label;
    gint i, row = 0;

    table = GTK_TABLE(gtk_table_new(6, 3, FALSE));
    gtk_table_set_col_spacings(table, 8);
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_row_spacing(table, 3, 8);

    label = gwy_label_new_header(_("Origin"));
    gtk_table_attach(table, label, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new("X");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new("Y");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gwy_label_new_header(_("Size"));
    gtk_table_attach(table, label, 0, 1, 3, 4, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Width"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 4, 5, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Height"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 5, 6, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    for (i = 0; i < NCOORDS; i++) {
        row = 1 + i + i/2;

        tool->label_real[i] = label = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_table_attach(table, label, 1, 2, row, row+1,
                         GTK_EXPAND | GTK_FILL, 0, 0, 0);

        tool->label_pix[i] = label = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_table_attach(table, label, 2, 3, row, row+1,
                         GTK_EXPAND | GTK_FILL, 0, 0, 0);
    }

    return GTK_WIDGET(table);
}

static void
gwy_tool_spot_remover_data_switched(GwyTool *gwytool,
                                    GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolSpotRemover *tool;
    GwyPixmapLayer *layer;
    const gchar *key;
    gchar *sigdetail;
    gboolean ignore;

    tool = GWY_TOOL_SPOT_REMOVER(gwytool);
    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);

    if (!ignore)
        GWY_SIGNAL_HANDLER_DISCONNECT(plain_tool->container, tool->palette_id);

    GWY_TOOL_CLASS(gwy_tool_spot_remover_parent_class)->data_switched(gwytool,
                                                                      data_view);

    if (ignore || plain_tool->init_failed)
        return;

    tool->xr.from = tool->yr.from = tool->xr.to = tool->yr.to = -1;
    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_point,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);
        gwy_tool_spot_remover_resize_detail(tool);

        layer = gwy_data_view_get_base_layer(data_view);
        g_return_if_fail(GWY_IS_LAYER_BASIC(layer));
        key = gwy_layer_basic_get_gradient_key(GWY_LAYER_BASIC(layer));
        if (key) {
            sigdetail = g_strconcat("item-changed::", key, NULL);
            tool->palette_id = g_signal_connect_swapped
                             (plain_tool->container, sigdetail,
                              G_CALLBACK(gwy_tool_spot_remover_palette_changed),
                              tool);
            g_free(sigdetail);
        }
        gwy_tool_spot_remover_palette_changed(tool);
        gwy_tool_spot_remover_selection_changed(plain_tool, -1);
    }
    else {
        tool->has_selection = FALSE;
        tool->has_zselection = FALSE;
        update_selection_info_table(tool);
    }
}

static void
gwy_tool_spot_remover_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolSpotRemover *tool;

    tool = GWY_TOOL_SPOT_REMOVER(plain_tool);
    tool->drawn = FALSE;
    gwy_tool_spot_remover_resize_detail(tool);
    gwy_tool_spot_remover_selection_changed(plain_tool, -1);
    if (!tool->drawn)
        gwy_tool_spot_remover_draw_zoom(tool);
}

static void
gwy_tool_spot_remover_palette_changed(GwyToolSpotRemover *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_app_sync_data_items(plain_tool->container, tool->data,
                            plain_tool->id, 0,
                            TRUE,
                            GWY_DATA_ITEM_GRADIENT, 0);
}

static void
gwy_tool_spot_remover_response(GwyTool *gwytool,
                               gint response_id)
{
    GwyToolSpotRemover *tool;

    GWY_TOOL_CLASS(gwy_tool_spot_remover_parent_class)->response(gwytool,
                                                                 response_id);

    tool = GWY_TOOL_SPOT_REMOVER(gwytool);
    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_spot_remover_apply(tool);
    else if (response_id == GWY_TOOL_RESPONSE_CLEAR)
        gwy_selection_clear(tool->zselection);
}

static void
gwy_tool_spot_remover_resize_detail(GwyToolSpotRemover *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    gint xres, yres, dxres, dyres, minres, maxres, newdxres, newdyres;
    gdouble newzoom;

    if (!plain_tool->data_field)
        return;

    xres = gwy_data_field_get_xres(plain_tool->data_field);
    yres = gwy_data_field_get_yres(plain_tool->data_field);
    dxres = gwy_data_field_get_xres(tool->detail);
    dyres = gwy_data_field_get_yres(tool->detail);
    gwy_debug("image %dx%d, detail %dx%d", xres, yres, dxres, dyres);

    /* Max determines the displayed region. */
    maxres = MIN(MAX(xres, yres), MAX_SIZE);
    /* Min determines posible cut in orthogonal direction. */
    minres = MIN(MIN(xres, yres), maxres);
    gwy_debug("minres %d, maxres %d", minres, maxres);

    newdxres = (xres == minres) ? minres : maxres;
    newdyres = (yres == minres) ? minres : maxres;
    gwy_debug("detail should be %dx%d", newdxres, newdyres);

    if (newdxres == dxres && newdyres == dyres)
        return;

    gwy_data_field_resample(tool->detail, newdxres, newdyres,
                            GWY_INTERPOLATION_NONE);
    gwy_data_field_clear(tool->detail);

    newzoom = (gdouble)SCALE/MAX(newdxres, newdyres)*MAX_SIZE;
    gwy_debug("updating zoom to %g", newzoom);
    gwy_data_view_set_zoom(GWY_DATA_VIEW(tool->zoomview), newzoom);
    gwy_data_field_data_changed(tool->detail);
    gwy_selection_clear(tool->zselection);
}

static void
gwy_tool_spot_remover_selection_changed(GwyPlainTool *plain_tool,
                                        gint hint)
{
    GwyToolSpotRemover *tool;
    Range xr, yr;
    gboolean has_selection, complete;
    gint xres, yres, dxres, dyres;
    gdouble sel[2];
    gint isel[2];

    tool = GWY_TOOL_SPOT_REMOVER(plain_tool);
    g_return_if_fail(hint <= 0);

    has_selection = FALSE;
    if (plain_tool->selection)
        has_selection = gwy_selection_get_object(plain_tool->selection, 0, sel);

    complete = TRUE;
    if (has_selection) {
        dxres = gwy_data_field_get_xres(tool->detail);
        dyres = gwy_data_field_get_yres(tool->detail);
        isel[0] = floor(gwy_data_field_rtoj(plain_tool->data_field, sel[0]));
        isel[1] = floor(gwy_data_field_rtoi(plain_tool->data_field, sel[1]));
        xres = gwy_data_field_get_xres(plain_tool->data_field);
        yres = gwy_data_field_get_yres(plain_tool->data_field);
        complete &= find_subrange(isel[0], xres, dxres, &xr);
        complete &= find_subrange(isel[1], yres, dyres, &yr);
    }
    else
        xr.from = yr.from = xr.to = yr.to = -1;

    tool->has_selection = has_selection;
    if (tool->xr.from == xr.from && tool->yr.from == yr.from
        && tool->xr.to == xr.to && tool->yr.to == yr.to) {
        gwy_tool_spot_remover_update_message(tool);
        return;
    }

    tool->xr = xr;
    tool->yr = yr;
    tool->complete = complete;
    zselection_changed(tool->zselection, -1, tool);
    gwy_tool_spot_remover_draw_zoom(tool);
    tool->drawn = TRUE;
}

static void
setup_zoom_vector_layer(GwyToolSpotRemover *tool)
{
    SpotRemoveShape shape = tool->args.shape;
    GwyVectorLayer *vlayer;

    if (tool->zsel_id) {
        g_signal_handler_disconnect(tool->zselection, tool->zsel_id);
        tool->zsel_id = 0;
    }

    if (shape == GWY_SPOT_REMOVE_RECTANGLE) {
        vlayer = GWY_VECTOR_LAYER(g_object_new(tool->layer_type_rect, NULL));
        gwy_vector_layer_set_selection_key(vlayer, "/0/select/rect");
    }
    else if (shape == GWY_SPOT_REMOVE_ELLIPSE) {
        vlayer = GWY_VECTOR_LAYER(g_object_new(tool->layer_type_ell, NULL));
        gwy_vector_layer_set_selection_key(vlayer, "/0/select/ell");
    }
    else {
        g_return_if_reached();
    }

    gwy_data_view_set_top_layer(GWY_DATA_VIEW(tool->zoomview), vlayer);
    tool->zselection = gwy_vector_layer_ensure_selection(vlayer);
    gwy_selection_set_max_objects(tool->zselection, 1);
    tool->zsel_id = g_signal_connect(tool->zselection, "changed",
                                     G_CALLBACK(zselection_changed), tool);
}

static void
zselection_changed(GwySelection *selection,
                   gint hint,
                   GwyToolSpotRemover *tool)
{
    GwyDataField *data_field;
    gdouble sel[4];
    gboolean is_ok = FALSE;

    g_return_if_fail(hint <= 0);

    data_field = GWY_PLAIN_TOOL(tool)->data_field;
    if (!data_field) {
        gtk_widget_set_sensitive(tool->apply, FALSE);
        return;
    }

    if (tool->xr.from >= 0 && tool->yr.from >= 0
        && gwy_selection_get_object(selection, 0, sel)) {
        if (sel[0] > sel[2])
            GWY_SWAP(gdouble, sel[0], sel[2]);
        if (sel[1] > sel[3])
            GWY_SWAP(gdouble, sel[1], sel[3]);
        /* `real' dimensions on the zoom are actually pixel dimensions on the
         * data field */
        tool->zisel[0] = (gint)floor(sel[0]) + tool->xr.from - tool->xr.dest;
        tool->zisel[1] = (gint)floor(sel[1]) + tool->yr.from - tool->yr.dest;
        tool->zisel[2] = (gint)ceil(sel[2]) + tool->xr.from - tool->xr.dest;
        tool->zisel[3] = (gint)ceil(sel[3]) + tool->yr.from - tool->yr.dest;
        is_ok = (tool->zisel[0] > 0
                 && tool->zisel[1] > 0
                 && tool->zisel[2] < gwy_data_field_get_xres(data_field)
                 && tool->zisel[3] < gwy_data_field_get_yres(data_field));
        gtk_widget_set_sensitive(tool->clear, TRUE);
    }
    else
        gtk_widget_set_sensitive(tool->clear, FALSE);

    gtk_widget_set_sensitive(tool->apply, is_ok);

    tool->has_zselection = gwy_selection_get_data(selection, NULL);
    gwy_tool_spot_remover_update_message(tool);
    update_selection_info_table(tool);
}

static void
update_selection_info_table(GwyToolSpotRemover *tool)
{
    GwySIValueFormat *vf;
    GwyDataField *dfield;
    gdouble dx, dy, v;
    gint icoord[4];
    gchar buf[48];
    gint i;

    vf = tool->pixel_format;
    if (!tool->has_zselection) {
        for (i = 0; i < NCOORDS; i++) {
            gtk_label_set_text(GTK_LABEL(tool->label_real[i]), "");
            gtk_label_set_text(GTK_LABEL(tool->label_pix[i]), vf->units);
        }
        return;
    }

    gwy_assign(icoord, tool->zisel, NCOORDS);
    icoord[2] -= icoord[0];
    icoord[3] -= icoord[1];

    for (i = 0; i < NCOORDS; i++) {
        g_snprintf(buf, sizeof(buf), "%.*f %s",
                   vf->precision, icoord[i]/vf->magnitude, vf->units);
        gtk_label_set_markup(GTK_LABEL(tool->label_pix[i]), buf);
    }

    vf = GWY_PLAIN_TOOL(tool)->coord_format;
    dfield = GWY_PLAIN_TOOL(tool)->data_field;
    g_return_if_fail(dfield);

    dx = gwy_data_field_get_dx(dfield);
    dy = gwy_data_field_get_dx(dfield);
    for (i = 0; i < NCOORDS; i++) {
        v = icoord[i]*(i % 2 ? dy : dx);
        g_snprintf(buf, sizeof(buf), "%.*f%s%s",
                   vf->precision, v/vf->magnitude,
                   vf->units && *vf->units ? " " : "", vf->units);
        gtk_label_set_markup(GTK_LABEL(tool->label_real[i]), buf);
    }
}

static void
gwy_tool_spot_remover_draw_zoom(GwyToolSpotRemover *tool)
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
gwy_tool_spot_remover_update_message(GwyToolSpotRemover *tool)
{
    static const gchar *message_data = NULL;
    static const gchar *message_zoom = NULL;

    if (!message_data)
        message_data = _("No point in the image selected.");
    if (!message_zoom)
        message_zoom = _("No area in the zoom selected.");

    if (tool->has_selection) {
        if (tool->has_zselection)
            gtk_label_set_text(GTK_LABEL(tool->message_label), NULL);
        else
            gtk_label_set_text(GTK_LABEL(tool->message_label), message_zoom);
    }
    else {
        if (tool->has_zselection)
            gtk_label_set_text(GTK_LABEL(tool->message_label), message_data);
        else {
            gchar *s = g_strconcat(message_data, "\n", message_zoom, NULL);
            gtk_label_set_text(GTK_LABEL(tool->message_label), s);
            g_free(s);
        }
    }
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
method_changed(GtkComboBox *combo, GwyToolSpotRemover *tool)
{
    tool->args.method = gwy_enum_combo_box_get_active(combo);
}

static void
shape_changed(GtkToggleButton *toggle, GwyToolSpotRemover *tool)
{
    gdouble sel[4];
    gboolean restore_sel = FALSE;

    if (!gtk_toggle_button_get_active(toggle))
        return;

    tool->args.shape = gwy_radio_buttons_get_current(tool->shape);
    restore_sel = (tool->zselection
                   && gwy_selection_get_object(tool->zselection, 0, sel));
    setup_zoom_vector_layer(tool);
    if (restore_sel)
        gwy_selection_set_data(tool->zselection, 1, sel);
}

static void
fill_elliptic_area(GwyDataField *dfield,
                   gint col, gint row, gint width, gint height,
                   gdouble value)
{
    /* Ignore the return value to match prototype. */
    gwy_data_field_elliptic_area_fill(dfield, col, row, width, height, value);
}

static void
gwy_tool_spot_remover_apply(GwyToolSpotRemover *tool)
{
    GwyPlainTool *plain_tool;
    gint xmin, xmax, ymin, ymax, w, h;
    GwyDataField *dfield, *area, *mask = NULL;
    SpotRemoveMethod method = tool->args.method;
    AreaFillFunc fill_area;

    plain_tool = GWY_PLAIN_TOOL(tool);
    dfield = plain_tool->data_field;
    g_return_if_fail(plain_tool->id >= 0 && dfield);
    g_return_if_fail(method < GWY_SPOT_REMOVE_NMETHODS);
    if (tool->args.shape == GWY_SPOT_REMOVE_ELLIPSE)
        fill_area = fill_elliptic_area;
    else
        fill_area = gwy_data_field_area_fill;

    gwy_app_undo_qcheckpoint(plain_tool->container,
                             gwy_app_get_data_key_for_id(plain_tool->id), 0);

    xmin = tool->zisel[0];
    ymin = tool->zisel[1];
    xmax = tool->zisel[2];
    ymax = tool->zisel[3];
    w = xmax - xmin;
    h = ymax - ymin;
    if (method == GWY_SPOT_REMOVE_FRACTAL_LAPLACE) {
        /* Fractal interpolation is full-size because it analyses the entire
         * data field. */
        mask = gwy_data_field_new_alike(dfield, TRUE);
        fill_area(mask, xmin, ymin, w, h, 1.0);
        gwy_data_field_fractal_correction(dfield, mask,
                                          GWY_INTERPOLATION_LINEAR);
        g_object_unref(mask);

        area = gwy_data_field_area_extract(dfield, xmin-1, ymin-1, w+2, h+2);
        mask = gwy_data_field_new_alike(area, TRUE);
        fill_area(mask, 1, 1, w, h, 1.0);
        gwy_data_field_laplace_solve(area, mask, 1, 1.0);

        gwy_data_field_grain_distance_transform(mask);
        blend_fractal_and_laplace(dfield, area, mask, xmin-1, ymin-1);
        g_object_unref(area);
    }
    else if (method == GWY_SPOT_REMOVE_FRACTAL) {
        /* Fractal interpolation is full-size because it analyses the entire
         * data field. */
        mask = gwy_data_field_new_alike(dfield, TRUE);
        fill_area(mask, xmin, ymin, w, h, 1.0);
        gwy_data_field_fractal_correction(dfield, mask,
                                          GWY_INTERPOLATION_LINEAR);
    }
    else if (method == GWY_SPOT_REMOVE_ZERO) {
        fill_area(dfield, xmin, ymin, w, h, 0.0);
    }
    else {
        area = gwy_data_field_area_extract(dfield, xmin-1, ymin-1, w+2, h+2);
        mask = gwy_data_field_new_alike(area, TRUE);
        fill_area(mask, 1, 1, w, h, 1.0);

        if (method == GWY_SPOT_REMOVE_LAPLACE)
            gwy_data_field_laplace_solve(area, mask, 1, 2.0);
        else if (method == GWY_SPOT_REMOVE_PSEUDO_LAPLACE)
            pseudo_laplace_average(area, mask);
        else if (method == GWY_SPOT_REMOVE_HYPER_FLATTEN)
            hyperbolic_average(area, mask);
        else {
            g_assert_not_reached();
        }

        gwy_data_field_area_copy(area, dfield, 1, 1, w, h, xmin, ymin);
        g_object_unref(area);
    }

    GWY_OBJECT_UNREF(mask);
    gwy_data_field_data_changed(dfield);
    gwy_plain_tool_log_add(plain_tool);
}

static void
blend_fractal_and_laplace(GwyDataField *dfield,
                          GwyDataField *area,
                          GwyDataField *distances,
                          gint col, gint row)
{
    gint xres, w, h, i, j, k, kk;
    const gdouble *a, *e;
    gdouble *d;
    gdouble t;

    xres = gwy_data_field_get_xres(dfield);
    w = gwy_data_field_get_xres(area);
    h = gwy_data_field_get_yres(area);
    a = gwy_data_field_get_data_const(area);
    e = gwy_data_field_get_data_const(distances);
    d = gwy_data_field_get_data(dfield) + row*xres + col;

    for (i = k = kk = 0; i < h; i++) {
        for (j = 0; j < w; j++, k++, kk++) {
            if (e[k] <= 0.0)
                continue;

            t = exp(0.167*(1.0 - e[k]));
            d[kk] *= (1.0 - t);
            d[kk] += t*a[k];
        }
        kk += xres - w;
    }
}

static void
find_hyperbolic_lines(GwyDataField *dfield,
                      GwyDataField *mask,
                      gint *itop, gdouble *ztop,
                      gint *jleft, gdouble *zleft,
                      gint *jright, gdouble *zright,
                      gint *ibot, gdouble *zbot)
{
    gint xres, yres, i, j;
    const gdouble *d, *m;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    d = gwy_data_field_get_data_const(dfield);
    m = gwy_data_field_get_data_const(mask);

    for (j = 0; j < xres; j++) {
        itop[j] = G_MAXINT;
        ibot[j] = -1;
    }
    for (i = 0; i < yres; i++) {
        jleft[i] = G_MAXINT;
        jright[i] = -1;
    }

    for (i = 1; i < yres-1; i++) {
        for (j = 1; j < xres-1; j++) {
            if (m[i*xres + j] <= 0.0)
                continue;

            if (i < itop[j])
                itop[j] = i;
            if (i > ibot[j])
                ibot[j] = i;
            if (j < jleft[i])
                jleft[i] = j;
            if (j > jright[i])
                jright[i] = j;
        }
    }

    for (j = 1; j < xres-1; j++) {
        g_assert(itop[j] < yres);
        itop[j]--;
        ztop[j] = d[itop[j]*xres + j];

        g_assert(ibot[j] > 0);
        ibot[j]++;
        zbot[j] = d[ibot[j]*xres + j];
    }
    for (i = 1; i < yres-1; i++) {
        g_assert(jleft[i] < xres);
        jleft[i]--;
        zleft[i] = d[i*xres + jleft[i]];

        g_assert(jright[i] > 0);
        jright[i]++;
        zright[i] = d[i*xres + jright[i]];
    }
}

static void
hyperbolic_average(GwyDataField *dfield, GwyDataField *mask)
{
    const gdouble *m;
    gdouble *d, *ztop, *zbot, *zleft, *zright;
    gint *itop, *ibot, *jleft, *jright;
    gint i, j, xres, yres;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);

    ztop = g_new(gdouble, 2*(xres + yres));
    zleft = ztop + xres;
    zright = zleft + yres;
    zbot = zright + yres;

    itop = g_new(gint, 2*(xres + yres));
    jleft = itop + xres;
    jright = jleft + yres;
    ibot = jright + yres;

    find_hyperbolic_lines(dfield, mask,
                          itop, ztop, jleft, zleft, jright, zright, ibot, zbot);
    d = gwy_data_field_get_data(dfield);
    m = gwy_data_field_get_data_const(mask);

    for (i = 1; i < yres-1; i++) {
        for (j = 1; j < xres-1; j++) {
            gint pos = i*xres + j;

            if (m[pos] > 0.0) {
                gdouble px = zleft[i], qx = zright[i];
                gdouble y = (gdouble)(i - itop[j])/(ibot[j] - itop[j]);
                gdouble wx = 1.0/y + 1.0/(1.0 - y);

                gdouble py = ztop[j], qy = zbot[j];
                gdouble x = (gdouble)(j - jleft[i])/(jright[i] - jleft[i]);
                gdouble wy = 1.0/x + 1.0/(1.0 - x);

                gdouble vy = px/x + qx/(1.0 - x);
                gdouble vx = py/y + qy/(1.0 - y);

                d[pos] = (vx + vy)/(wx + wy);
            }
        }
    }

    g_free(ztop);
    g_free(itop);
}

static void
find_bounary_pixel_values(GwyDataField *dfield,
                          GwyDataField *mask,
                          GArray *pvals)
{
    gint xres, yres, i, j, k;
    const gdouble *d, *m;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    d = gwy_data_field_get_data_const(dfield);
    m = gwy_data_field_get_data_const(mask);
    g_array_set_size(pvals, 0);

    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            k = i*xres + j;
            if (m[k] > 0.0)
                continue;

            if ((i && m[k-xres] > 0.0)
                || (j && m[k-1] > 0.0)
                || (j < xres-1 && m[k+1] > 0.0)
                || (i < yres-1 && m[k+xres] > 0.0)) {
                PixelValue pv = { d[k], i, j };
                g_array_append_val(pvals, pv);
            }
        }
    }
}

static void
pseudo_laplace_average(GwyDataField *dfield, GwyDataField *mask)
{
    gint i, j, n, xres, yres;
    GArray *boundary;
    PixelValue *pvals;
    const gdouble *m;
    gdouble *d;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);

    boundary = g_array_new(FALSE, FALSE, sizeof(PixelValue));
    find_bounary_pixel_values(dfield, mask, boundary);
    n = boundary->len;
    pvals = (PixelValue*)g_array_free(boundary, FALSE);

    d = gwy_data_field_get_data(dfield);
    m = gwy_data_field_get_data_const(mask);

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(d,m,pvals,n,xres,yres)
#endif
    for (i = 1; i < yres-1; i++) {
        for (j = 1; j < xres-1; j++) {
            gint k, pos = i*xres + j;
            gdouble s = 0.0, sz = 0.0;

            if (m[pos] <= 0.0)
                continue;

            for (k = 0; k < n; k++) {
                gint dx = pvals[k].j - j, dy = pvals[k].i - i;
                gdouble ss = 1.0/(dx*dx + dy*dy);

                s += ss;
                sz += ss*pvals[k].z;
            }
            d[pos] = sz/s;
        }
    }

    g_free(pvals);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
