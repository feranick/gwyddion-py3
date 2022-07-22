/*
 *  $Id: filter.c 23305 2021-03-18 14:40:27Z yeti-dn $
 *  Copyright (C) 2003-2018 David Necas (Yeti), Petr Klapetek.
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
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/elliptic.h>
#include <libprocess/filters.h>
#include <libprocess/linestats.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwyadjustbar.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>

#define GWY_TYPE_TOOL_FILTER            (gwy_tool_filter_get_type())
#define GWY_TOOL_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_FILTER, GwyToolFilter))
#define GWY_IS_TOOL_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_FILTER))
#define GWY_TOOL_FILTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_FILTER, GwyToolFilterClass))

#define FWHM2SIGMA (1.0/(2.0*sqrt(2*G_LN2)))

/* Keep numbering for settings */
typedef enum {
    FILTER_MEAN          = 0,
    FILTER_MEDIAN        = 1,
    FILTER_CONSERVATIVE  = 2,
    FILTER_MINIMUM       = 3,
    FILTER_MAXIMUM       = 4,
    FILTER_KUWAHARA      = 5,
    FILTER_DECHECKER     = 6,
    FILTER_GAUSSIAN      = 7,
    FILTER_SHARPEN       = 8,
    FILTER_OPENING       = 9,
    FILTER_CLOSING       = 10,
    FILTER_ASF_OPENING   = 11,
    FILTER_ASF_CLOSING   = 12,
    FILTER_NFILTERS
} FilterType;

typedef struct _GwyToolFilter      GwyToolFilter;
typedef struct _GwyToolFilterClass GwyToolFilterClass;

typedef struct {
    FilterType filter_type;
    GwyMaskingType masking;
    gint size;
    gdouble gauss_size;
} ToolArgs;

struct _GwyToolFilter {
    GwyPlainTool parent_instance;

    ToolArgs args;
    gint isel[4];

    GwyRectSelectionLabels *rlabels;
    GtkWidget *filter_type;
    GtkObject *size;
    GtkWidget *size_spin;
    GSList *masking;
    GtkWidget *apply;

    /* potential class data */
    GType layer_type_rect;
};

struct _GwyToolFilterClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register                  (void);
static GType    gwy_tool_filter_get_type         (void)                      G_GNUC_CONST;
static void     gwy_tool_filter_finalize         (GObject *object);
static void     gwy_tool_filter_init_dialog      (GwyToolFilter *tool);
static void     gwy_tool_filter_data_switched    (GwyTool *gwytool,
                                                  GwyDataView *data_view);
static void     gwy_tool_filter_data_changed     (GwyPlainTool *plain_tool);
static void     gwy_tool_filter_response         (GwyTool *tool,
                                                  gint response_id);
static void     gwy_tool_filter_selection_changed(GwyPlainTool *plain_tool,
                                                  gint hint);
static void     gwy_tool_filter_size_changed     (GwyToolFilter *tool,
                                                  GtkAdjustment *adj);
static void     gwy_tool_filter_type_changed     (GtkComboBox *combo,
                                                  GwyToolFilter *tool);
static void     gwy_tool_filter_masking_changed  (GtkWidget *button,
                                                  GwyToolFilter *tool);
static gboolean gwy_tool_filter_is_sized         (FilterType type);
static gboolean gwy_tool_filter_is_float_sized   (FilterType type);
static void     update_selected_rectangle        (GwyToolFilter *tool);
static void     setup_size_adjustment            (GwyToolFilter *tool);
static void     gwy_tool_filter_apply            (GwyToolFilter *tool);
static void     filter_area_sharpen              (GwyDataField *dfield,
                                                  gdouble sigma,
                                                  gint col,
                                                  gint row,
                                                  gint width,
                                                  gint height);
static void     apply_masking                    (GwyDataField *dfield,
                                                  GwyDataField *orig,
                                                  GwyDataField *mask,
                                                  GwyMaskingType masking);
static void     gwy_tool_filter_save_args        (GwyToolFilter *tool);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Filter tool, processes selected part of data with a filter "
       "(conservative denoise, mean, median. Kuwahara, minimum, maximum)."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "3.17",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

static const gchar filter_type_key[] = "/module/filter/filter_type";
static const gchar gauss_size_key[]  = "/module/filter/gauss_size";
static const gchar masking_key[]     = "/module/filter/masking";
static const gchar size_key[]        = "/module/filter/size";

static const ToolArgs default_args = {
    FILTER_MEAN,
    GWY_MASK_IGNORE,
    5,
    5.0,
};

GWY_MODULE_QUERY2(module_info, filter)

G_DEFINE_TYPE(GwyToolFilter, gwy_tool_filter, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_FILTER);

    return TRUE;
}

static void
gwy_tool_filter_class_init(GwyToolFilterClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_filter_finalize;

    tool_class->stock_id = GWY_STOCK_FILTER;
    tool_class->title = _("Filter");
    tool_class->tooltip = _("Basic filters: mean, median, denoise, …");
    tool_class->prefix = "/module/filter";
    tool_class->data_switched = gwy_tool_filter_data_switched;
    tool_class->response = gwy_tool_filter_response;

    ptool_class->data_changed = gwy_tool_filter_data_changed;
    ptool_class->selection_changed = gwy_tool_filter_selection_changed;
}

static void
gwy_tool_filter_finalize(GObject *object)
{
    gwy_tool_filter_save_args(GWY_TOOL_FILTER(object));
    G_OBJECT_CLASS(gwy_tool_filter_parent_class)->finalize(object);
}

static void
gwy_tool_filter_init(GwyToolFilter *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_rect = gwy_plain_tool_check_layer_type(plain_tool,
                                                           "GwyLayerRectangle");
    if (!tool->layer_type_rect)
        return;

    plain_tool->lazy_updates = TRUE;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_enum_by_name(settings, filter_type_key,
                                   &tool->args.filter_type);
    gwy_container_gis_enum_by_name(settings, masking_key,
                                   &tool->args.masking);
    gwy_container_gis_int32_by_name(settings, size_key,
                                    &tool->args.size);
    gwy_container_gis_double_by_name(settings, gauss_size_key,
                                     &tool->args.gauss_size);

    tool->args.filter_type = MIN(tool->args.filter_type, FILTER_NFILTERS-1);
    tool->args.masking = gwy_enum_sanitize_value(tool->args.masking,
                                                 GWY_TYPE_MASKING_TYPE);

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_rect,
                                     "rectangle");

    gwy_tool_filter_init_dialog(tool);
}

static void
gwy_tool_filter_rect_updated(GwyToolFilter *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_rect_selection_labels_select(tool->rlabels,
                                     plain_tool->selection,
                                     plain_tool->data_field);
}

static void
gwy_tool_filter_init_dialog(GwyToolFilter *tool)
{
    static const GwyEnum filters[] = {
        { N_("Mean value"),           FILTER_MEAN,         },
        { N_("Median value"),         FILTER_MEDIAN,       },
        { N_("Conservative denoise"), FILTER_CONSERVATIVE, },
        { N_("Minimum"),              FILTER_MINIMUM,      },
        { N_("Maximum"),              FILTER_MAXIMUM,      },
        { N_("filter|Opening"),       FILTER_OPENING,      },
        { N_("filter|Closing"),       FILTER_CLOSING,      },
        { N_("ASF Opening"),          FILTER_ASF_OPENING,  },
        { N_("ASF Closing"),          FILTER_ASF_CLOSING,  },
        { N_("Kuwahara"),             FILTER_KUWAHARA,     },
        { N_("Dechecker"),            FILTER_DECHECKER,    },
        { N_("filter|Gaussian"),      FILTER_GAUSSIAN,     },
        { N_("Sharpen"),              FILTER_SHARPEN,      },
    };
    GtkDialog *dialog;
    GtkTable *table;
    GtkWidget *label;
    gint row;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    /* Selection info */
    tool->rlabels = gwy_rect_selection_labels_new
                         (TRUE, G_CALLBACK(gwy_tool_filter_rect_updated), tool);
    gtk_box_pack_start(GTK_BOX(dialog->vbox),
                       gwy_rect_selection_labels_get_table(tool->rlabels),
                       FALSE, FALSE, 0);

    /* Options */
    table = GTK_TABLE(gtk_table_new(4, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), GTK_WIDGET(table),
                       FALSE, FALSE, 0);
    row = 0;

    label = gwy_label_new_header(_("Filter"));
    gtk_table_attach(table, label, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    tool->filter_type = gwy_enum_combo_box_new
                                     (filters, G_N_ELEMENTS(filters),
                                      G_CALLBACK(gwy_tool_filter_type_changed),
                                      tool,
                                      tool->args.filter_type, TRUE);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Type:"), NULL,
                            GTK_OBJECT(tool->filter_type),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    tool->size = gtk_adjustment_new(0.0, 0.0, 1.0, 0.1, 1.0, 0);
    tool->size_spin
        = gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("Si_ze:"), _("px"),
                                  tool->size, GWY_HSCALE_SQRT);
    gwy_table_hscale_set_sensitive
                 (tool->size, gwy_tool_filter_is_sized(tool->args.filter_type));
    setup_size_adjustment(tool);
    g_signal_connect_swapped(tool->size, "value-changed",
                             G_CALLBACK(gwy_tool_filter_size_changed), tool);
    row++;

    gtk_table_set_row_spacing(table, row-1, 8);
    label = gwy_label_new_header(_("Masking Mode"));
    gtk_table_attach(table, label, 0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    tool->masking
        = gwy_radio_buttons_create(gwy_masking_type_get_enum(), -1,
                                   G_CALLBACK(gwy_tool_filter_masking_changed),
                                   tool,
                                   tool->args.masking);
    row = gwy_radio_buttons_attach_to_table(tool->masking, table, 2, row);

    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), FALSE);
    tool->apply = gtk_dialog_add_button(dialog, GTK_STOCK_APPLY,
                                        GTK_RESPONSE_APPLY);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_filter_data_switched(GwyTool *gwytool,
                              GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolFilter *tool;
    gboolean ignore;

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_filter_parent_class)->data_switched(gwytool,
                                                                data_view);

    if (ignore || plain_tool->init_failed)
        return;

    tool = GWY_TOOL_FILTER(gwytool);
    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_rect,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);
    }

    gtk_widget_set_sensitive(tool->apply, data_view != NULL);
}

static void
gwy_tool_filter_data_changed(GwyPlainTool *plain_tool)
{
    update_selected_rectangle(GWY_TOOL_FILTER(plain_tool));
}

static void
gwy_tool_filter_response(GwyTool *tool,
                         gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_filter_parent_class)->response(tool, response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_filter_apply(GWY_TOOL_FILTER(tool));
}

static void
gwy_tool_filter_selection_changed(GwyPlainTool *plain_tool,
                                  gint hint)
{
    g_return_if_fail(hint <= 0);
    update_selected_rectangle(GWY_TOOL_FILTER(plain_tool));
}

static void
gwy_tool_filter_size_changed(GwyToolFilter *tool,
                             GtkAdjustment *adj)
{
    if (gwy_tool_filter_is_float_sized(tool->args.filter_type))
        tool->args.gauss_size = gtk_adjustment_get_value(adj);
    else
        tool->args.size = gwy_adjustment_get_int(adj);
}

static void
gwy_tool_filter_type_changed(GtkComboBox *combo,
                             GwyToolFilter *tool)
{
    FilterType prevtype, newtype;
    gboolean sensitive, prevfloat, newfloat;

    prevtype = tool->args.filter_type;
    tool->args.filter_type = newtype = gwy_enum_combo_box_get_active(combo);
    sensitive = gwy_tool_filter_is_sized(tool->args.filter_type);
    gwy_table_hscale_set_sensitive(tool->size, sensitive);

    prevfloat = gwy_tool_filter_is_float_sized(prevtype);
    newfloat = gwy_tool_filter_is_float_sized(newtype);
    if (prevfloat ^ newfloat)
        setup_size_adjustment(tool);
}

static void
gwy_tool_filter_masking_changed(GtkWidget *button,
                                GwyToolFilter *tool)
{
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
        return;

    tool->args.masking = gwy_radio_button_get_value(button);
}

static void
update_selected_rectangle(GwyToolFilter *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwySelection *selection = plain_tool->selection;
    GwyDataField *field = plain_tool->data_field;
    gint n;

    n = selection ? gwy_selection_get_data(selection, NULL) : 0;
    gwy_rect_selection_labels_fill(tool->rlabels, n == 1 ? selection : NULL,
                                   field, NULL, tool->isel);
}

static gboolean
gwy_tool_filter_is_float_sized(FilterType type)
{
    return (type == FILTER_GAUSSIAN
            || type == FILTER_SHARPEN);
}

static gboolean
gwy_tool_filter_is_sized(FilterType type)
{
    return (type != FILTER_KUWAHARA
            && type != FILTER_DECHECKER);
}

static gboolean
gwy_tool_filter_needs_kernel(FilterType type)
{
    return (type == FILTER_MINIMUM
            || type == FILTER_MAXIMUM
            || type == FILTER_OPENING
            || type == FILTER_CLOSING
            || type == FILTER_MEAN
            || type == FILTER_MEDIAN);
}

static void
setup_size_adjustment(GwyToolFilter *tool)
{
    GtkAdjustment *adj = GTK_ADJUSTMENT(tool->size);
    GtkWidget *adjbar = gwy_table_hscale_get_scale(tool->size);

    if (gwy_tool_filter_is_float_sized(tool->args.filter_type)) {
        g_object_set(adj,
                     "lower", 0.01,
                     "upper", 40.0,
                     "step-increment", 0.01,
                     "page-increment", 1.0,
                     "value", tool->args.gauss_size,
                     NULL);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(tool->size_spin), 2);
        gwy_adjust_bar_set_snap_to_ticks(GWY_ADJUST_BAR(adjbar), FALSE);
    }
    else {
        g_object_set(adj,
                     "lower", 2.0,
                     "upper", 31.0,
                     "step-increment", 1.0,
                     "page-increment", 5.0,
                     "value", (gdouble)tool->args.size,
                     NULL);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(tool->size_spin), 0);
        gwy_adjust_bar_set_snap_to_ticks(GWY_ADJUST_BAR(adjbar), TRUE);
    }
}

static void
gwy_tool_filter_apply(GwyToolFilter *tool)
{
    GwyPlainTool *plain_tool;
    GwyDataField *dfield, *origfield = NULL, *kernel = NULL;
    gdouble sigma;
    gint size, col, row, w, h, n = 0;

    plain_tool = GWY_PLAIN_TOOL(tool);
    size = tool->args.size;
    sigma = tool->args.gauss_size*FWHM2SIGMA;
    dfield = plain_tool->data_field;
    g_return_if_fail(plain_tool->id >= 0 && dfield != NULL);
    gwy_tool_filter_save_args(tool);

    col = tool->isel[0];
    row = tool->isel[1];
    w = tool->isel[2]+1 - tool->isel[0];
    h = tool->isel[3]+1 - tool->isel[1];

    gwy_app_undo_qcheckpoint(plain_tool->container,
                             gwy_app_get_data_key_for_id(plain_tool->id), 0);

    if (gwy_tool_filter_needs_kernel(tool->args.filter_type)) {
        kernel = gwy_data_field_new(size, size, size, size, TRUE);
        n = gwy_data_field_elliptic_area_fill(kernel, 0, 0, size, size, 1.0);
        if (tool->args.filter_type == FILTER_MEAN)
            gwy_data_field_multiply(kernel, 1.0/n);
    }

    /*
     * Remember the original for merging when masking is used.
     * XXX: This is inefficient when the area to actually modify is small.
     * However, linear operations are implemented using FFT and most
     * morphological operations are implemented using some moving window
     * algorithms.  For these we would have to switch to an idependent
     * pixel-by-pixel evaluation method (based on some ad-hoc threshold).
     * Kuwahara, dechecker, conservative denoising and (currently) median
     * are evaluated pixel-by-pixel anyway so native masking would not be
     * difficult, but apart from median probably also not worth it.
     */
    if (tool->args.masking != GWY_MASK_IGNORE && plain_tool->mask_field)
        origfield = gwy_data_field_duplicate(dfield);

    if (tool->args.filter_type == FILTER_MEAN) {
        gwy_data_field_area_ext_convolve(dfield, col, row, w, h, dfield, kernel,
                                         GWY_EXTERIOR_BORDER_EXTEND, 0.0,
                                         FALSE);
    }
    else if (tool->args.filter_type == FILTER_MEDIAN) {
        gwy_data_field_area_filter_kth_rank(dfield, kernel, col, row, w, h,
                                            n/2, NULL);
    }
    else if (tool->args.filter_type == FILTER_MINIMUM) {
        gwy_data_field_area_filter_min_max(dfield, kernel,
                                           GWY_MIN_MAX_FILTER_MINIMUM,
                                           col, row, w, h);
    }
    else if (tool->args.filter_type == FILTER_MAXIMUM) {
        gwy_data_field_area_filter_min_max(dfield, kernel,
                                           GWY_MIN_MAX_FILTER_MAXIMUM,
                                           col, row, w, h);
    }
    else if (tool->args.filter_type == FILTER_CONSERVATIVE)
        gwy_data_field_area_filter_conservative(dfield, size, col, row, w, h);
    else if (tool->args.filter_type == FILTER_KUWAHARA)
        gwy_data_field_area_filter_kuwahara(dfield, col, row, w, h);
    else if (tool->args.filter_type == FILTER_DECHECKER)
        gwy_data_field_area_filter_dechecker(dfield, col, row, w, h);
    else if (tool->args.filter_type == FILTER_GAUSSIAN)
        gwy_data_field_area_filter_gaussian(dfield, sigma, col, row, w, h);
    else if (tool->args.filter_type == FILTER_SHARPEN)
        filter_area_sharpen(dfield, sigma, col, row, w, h);
    else if (tool->args.filter_type == FILTER_OPENING) {
        gwy_data_field_area_filter_min_max(dfield, kernel,
                                           GWY_MIN_MAX_FILTER_OPENING,
                                           col, row, w, h);
    }
    else if (tool->args.filter_type == FILTER_CLOSING) {
        gwy_data_field_area_filter_min_max(dfield, kernel,
                                           GWY_MIN_MAX_FILTER_CLOSING,
                                           col, row, w, h);
    }
    else if (tool->args.filter_type == FILTER_ASF_OPENING) {
        gwy_data_field_area_filter_disc_asf(dfield, size/2, FALSE,
                                            col, row, w, h);
    }
    else if (tool->args.filter_type == FILTER_ASF_CLOSING) {
        gwy_data_field_area_filter_disc_asf(dfield, size/2, TRUE,
                                            col, row, w, h);
    }
    else {
        g_assert_not_reached();
    }

    if (origfield) {
        apply_masking(dfield, origfield,
                      plain_tool->mask_field, tool->args.masking);
        GWY_OBJECT_UNREF(origfield);
    }

    GWY_OBJECT_UNREF(kernel);
    gwy_data_field_data_changed(dfield);
    gwy_plain_tool_log_add(plain_tool);
}

static void
filter_area_sharpen(GwyDataField *dfield, gdouble sigma,
                    gint col, gint row, gint width, gint height)
{
    GwyDataField *origpart;
    gint xres, i, j;
    gdouble *d, *p;

    origpart = gwy_data_field_area_extract(dfield, col, row, width, height);
    gwy_data_field_area_filter_gaussian(dfield, sigma, col, row, width, height);

    xres = dfield->xres;

    for (i = 0; i < height; i++) {
        d = dfield->data + (i + row)*xres + col;
        p = origpart->data + i*width;
        for (j = 0; j < width; j++)
            d[j] = 2*p[j] - d[j];
    }

    g_object_unref(origpart);
}

static void
apply_masking(GwyDataField *dfield, GwyDataField *orig, GwyDataField *mask,
              GwyMaskingType masking)
{
    const gdouble *r = gwy_data_field_get_data_const(orig);
    const gdouble *m = gwy_data_field_get_data_const(mask);
    gdouble *d = gwy_data_field_get_data(dfield);
    gint xres = gwy_data_field_get_xres(dfield);
    gint yres = gwy_data_field_get_yres(dfield);
    gint k;

    if (masking == GWY_MASK_INCLUDE) {
        for (k = 0; k < xres*yres; k++) {
            if (m[k] <= 0.0)
                d[k] = r[k];
        }
    }
    else {
        for (k = 0; k < xres*yres; k++) {
            if (m[k] > 0.0)
                d[k] = r[k];
        }
    }

    gwy_data_field_invalidate(dfield);
}

static void
gwy_tool_filter_save_args(GwyToolFilter *tool)
{
    GwyContainer *settings;

    settings = gwy_app_settings_get();
    gwy_container_set_enum_by_name(settings, filter_type_key,
                                   tool->args.filter_type);
    gwy_container_set_enum_by_name(settings, masking_key, tool->args.masking);
    gwy_container_set_int32_by_name(settings, size_key,
                                    tool->args.size);
    gwy_container_set_double_by_name(settings, gauss_size_key,
                                     tool->args.gauss_size);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
