/*
 *  $Id: sfunctions.c 23305 2021-03-18 14:40:27Z yeti-dn $
 *  Copyright (C) 2003-2020 David Necas (Yeti), Petr Klapetek.
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
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/grains.h>
#include <libprocess/stats.h>
#include <libprocess/stats_uncertainty.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define GWY_TYPE_TOOL_SFUNCTIONS            (gwy_tool_sfunctions_get_type())
#define GWY_TOOL_SFUNCTIONS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_SFUNCTIONS, GwyToolSFunctions))
#define GWY_IS_TOOL_SFUNCTIONS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_SFUNCTIONS))
#define GWY_TOOL_SFUNCTIONS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_SFUNCTIONS, GwyToolSFunctionsClass))

typedef enum {
    GWY_SF_DH                     = 0,
    GWY_SF_CDH                    = 1,
    GWY_SF_DA                     = 2,
    GWY_SF_CDA                    = 3,
    GWY_SF_ACF                    = 4,
    GWY_SF_HHCF                   = 5,
    GWY_SF_PSDF                   = 6,
    GWY_SF_MINKOWSKI_VOLUME       = 7,
    GWY_SF_MINKOWSKI_BOUNDARY     = 8,
    GWY_SF_MINKOWSKI_CONNECTIVITY = 9,
    GWY_SF_RPSDF                  = 10,
    GWY_SF_RACF                   = 11,
    GWY_SF_RANGE                  = 12,
    GWY_SF_ASG                    = 13,
    GWY_SF_ANGSPEC                = 14,
    GWY_SF_NFUNCTIONS
} GwySFOutputType;

enum {
    MIN_RESOLUTION = 4,
    MAX_RESOLUTION = 16384
};

typedef struct _GwyToolSFunctions      GwyToolSFunctions;
typedef struct _GwyToolSFunctionsClass GwyToolSFunctionsClass;

typedef struct {
    GwyMaskingType masking;
    GwySFOutputType output_type;
    gboolean options_visible;
    gboolean instant_update;
    gint resolution;
    gboolean fixres;
    GwyOrientation direction;
    GwyInterpolationType interpolation;
    gboolean separate;
    GwyAppDataId target;
} ToolArgs;

struct _GwyToolSFunctions {
    GwyPlainTool parent_instance;

    ToolArgs args;

    GwyRectSelectionLabels *rlabels;

    GwyDataLine *line;
    gint isel[4];
    gint isel_prev[4];

    GwyDataField *cached_flipped_field;
    GwyDataField *cached_fp_mask; /* Always INCLUDE, possibly flipped. */

    GtkWidget *graph;
    GwyGraphModel *gmodel;

    GtkWidget *options;
    GtkWidget *output_type;
    GtkWidget *instant_update;
    GSList *direction;
    GtkObject *resolution;
    GtkWidget *fixres;
    GtkWidget *interpolation;
    GtkWidget *update;
    GtkWidget *apply;
    GtkWidget *separate;
    GtkWidget *masking;
    GtkWidget *target_graph;

    gboolean has_calibration;
    gboolean has_uline;
    GwyDataLine *uline;
    GwyDataField *xunc;
    GwyDataField *yunc;
    GwyDataField *zunc;

    /* potential class data */
    GType layer_type_rect;
};

struct _GwyToolSFunctionsClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType    gwy_tool_sfunctions_get_type         (void)                        G_GNUC_CONST;
static void     gwy_tool_sfunctions_finalize         (GObject *object);
static void     gwy_tool_sfunctions_init_dialog      (GwyToolSFunctions *tool);
static void     gwy_tool_sfunctions_data_switched    (GwyTool *gwytool,
                                                      GwyDataView *data_view);
static void     gwy_tool_sfunctions_response         (GwyTool *tool,
                                                      gint response_id);
static void     gwy_tool_sfunctions_data_changed     (GwyPlainTool *plain_tool);
static void     gwy_tool_sfunctions_mask_changed     (GwyPlainTool *plain_tool);
static void     gwy_tool_sfunctions_selection_changed(GwyPlainTool *plain_tool,
                                                      gint hint);
static void     update_selected_rectangle            (GwyToolSFunctions *tool);
static void     update_sensitivity                   (GwyToolSFunctions *tool);
static void     update_curve                         (GwyToolSFunctions *tool);
static void     instant_update_changed               (GtkToggleButton *check,
                                                      GwyToolSFunctions *tool);
static void     resolution_changed                   (GwyToolSFunctions *tool,
                                                      GtkAdjustment *adj);
static void     fixres_changed                       (GtkToggleButton *check,
                                                      GwyToolSFunctions *tool);
static void     output_type_changed                  (GtkComboBox *combo,
                                                      GwyToolSFunctions *tool);
static void     direction_changed                    (GObject *button,
                                                      GwyToolSFunctions *tool);
static void     interpolation_changed                (GtkComboBox *combo,
                                                      GwyToolSFunctions *tool);
static void     gwy_tool_sfunctions_options_expanded (GtkExpander *expander,
                                                      GParamSpec *pspec,
                                                      GwyToolSFunctions *tool);
static void     separate_changed                     (GtkToggleButton *check,
                                                      GwyToolSFunctions *tool);
static void     masking_changed                      (GtkComboBox *combo,
                                                      GwyToolSFunctions *tool);
static void     update_target_graphs                 (GwyToolSFunctions *tool);
static gboolean filter_target_graphs                 (GwyContainer *data,
                                                      gint id,
                                                      gpointer user_data);
static void     target_changed                       (GwyToolSFunctions *tool);
static void     gwy_tool_sfunctions_apply            (GwyToolSFunctions *tool);
static void     make_angular_spectrum                (GwyDataField *field,
                                                      GwyDataField *mask,
                                                      GwyMaskingType masking,
                                                      gint col,
                                                      gint row,
                                                      gint w,
                                                      gint h,
                                                      gint lineres,
                                                      GwyWindowingType windowing,
                                                      gint level,
                                                      GwyDataLine *target);
static void     gwy_data_field_area_range            (GwyDataField *dfield,
                                                      GwyDataLine *dline,
                                                      gint col,
                                                      gint row,
                                                      gint width,
                                                      gint height,
                                                      GwyOrientation direction,
                                                      GwyInterpolationType interp,
                                                      gint lineres);
static void     update_unc_fields                    (GwyPlainTool *plain_tool);
static gboolean sfunction_supports_masking           (GwySFOutputType type);
static gboolean sfunction_has_native_sampling        (GwySFOutputType type);
static gboolean sfunction_has_interpolation          (GwySFOutputType type);
static gboolean sfunction_has_direction              (GwySFOutputType type);
static gboolean sfunction_is_only_row_wise           (GwySFOutputType type);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Statistical function tool, calculates one-dimensional statistical "
       "functions (height distribution, correlations, PSDF, Minkowski "
       "functionals) of selected part of data."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.24",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

static const gchar masking_key[]         = "/module/sfunctions/masking";
static const gchar direction_key[]       = "/module/sfunctions/direction";
static const gchar fixres_key[]          = "/module/sfunctions/fixres";
static const gchar instant_update_key[]  = "/module/sfunctions/instant_update";
static const gchar interpolation_key[]   = "/module/sfunctions/interpolation";
static const gchar options_visible_key[] = "/module/sfunctions/options_visible";
static const gchar output_type_key[]     = "/module/sfunctions/output_type";
static const gchar resolution_key[]      = "/module/sfunctions/resolution";
static const gchar separate_key[]        = "/module/sfunctions/separate";

static const ToolArgs default_args = {
    GWY_MASK_IGNORE,
    GWY_SF_DH,
    FALSE,
    TRUE,
    120,
    FALSE,
    GWY_ORIENTATION_HORIZONTAL,
    GWY_INTERPOLATION_LINEAR,
    FALSE,
    GWY_APP_DATA_ID_NONE,
};

static const GwyEnum sf_types[] =  {
    { N_("Height distribution"),         GWY_SF_DH,                     },
    { N_("Cum. height distribution"),    GWY_SF_CDH,                    },
    { N_("Distribution of angles"),      GWY_SF_DA,                     },
    { N_("Cum. distribution of angles"), GWY_SF_CDA,                    },
    { N_("ACF"),                         GWY_SF_ACF,                    },
    { N_("HHCF"),                        GWY_SF_HHCF,                   },
    { N_("PSDF"),                        GWY_SF_PSDF,                   },
    { N_("Radial PSDF"),                 GWY_SF_RPSDF,                  },
    { N_("Angular spectrum"),            GWY_SF_ANGSPEC,                },
    { N_("Radial ACF"),                  GWY_SF_RACF,                   },
    { N_("Minkowski volume"),            GWY_SF_MINKOWSKI_VOLUME,       },
    { N_("Minkowski boundary"),          GWY_SF_MINKOWSKI_BOUNDARY,     },
    { N_("Minkowski connectivity"),      GWY_SF_MINKOWSKI_CONNECTIVITY, },
    { N_("Range"),                       GWY_SF_RANGE,                  },
    { N_("Area scale graph"),            GWY_SF_ASG,                    },
};

GWY_MODULE_QUERY2(module_info, sfunctions)

G_DEFINE_TYPE(GwyToolSFunctions, gwy_tool_sfunctions, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_SFUNCTIONS);

    return TRUE;
}

static void
gwy_tool_sfunctions_class_init(GwyToolSFunctionsClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_sfunctions_finalize;

    tool_class->stock_id = GWY_STOCK_GRAPH_HALFGAUSS;
    tool_class->title = _("Statistical Functions");
    tool_class->tooltip = _("Calculate 1D statistical functions");
    tool_class->prefix = "/module/sfunctions";
    tool_class->default_width = 640;
    tool_class->default_height = 400;
    tool_class->data_switched = gwy_tool_sfunctions_data_switched;
    tool_class->response = gwy_tool_sfunctions_response;

    ptool_class->data_changed = gwy_tool_sfunctions_data_changed;
    ptool_class->mask_changed = gwy_tool_sfunctions_mask_changed;
    ptool_class->selection_changed = gwy_tool_sfunctions_selection_changed;
}

static void
gwy_tool_sfunctions_finalize(GObject *object)
{
    GwyToolSFunctions *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_SFUNCTIONS(object);

    settings = gwy_app_settings_get();
    gwy_container_set_enum_by_name(settings, masking_key,
                                   tool->args.masking);
    gwy_container_set_enum_by_name(settings, output_type_key,
                                   tool->args.output_type);
    gwy_container_set_boolean_by_name(settings, options_visible_key,
                                      tool->args.options_visible);
    gwy_container_set_boolean_by_name(settings, instant_update_key,
                                      tool->args.instant_update);
    gwy_container_set_int32_by_name(settings, resolution_key,
                                    tool->args.resolution);
    gwy_container_set_boolean_by_name(settings, fixres_key,
                                      tool->args.fixres);
    gwy_container_set_boolean_by_name(settings, separate_key,
                                      tool->args.separate);
    gwy_container_set_enum_by_name(settings, interpolation_key,
                                   tool->args.interpolation);
    gwy_container_set_enum_by_name(settings, direction_key,
                                   tool->args.direction);

    GWY_OBJECT_UNREF(tool->line);
    GWY_OBJECT_UNREF(tool->gmodel);
    GWY_OBJECT_UNREF(tool->xunc);
    GWY_OBJECT_UNREF(tool->yunc);
    GWY_OBJECT_UNREF(tool->zunc);
    GWY_OBJECT_UNREF(tool->cached_flipped_field);
    GWY_OBJECT_UNREF(tool->cached_fp_mask);

    G_OBJECT_CLASS(gwy_tool_sfunctions_parent_class)->finalize(object);
}

static void
gwy_tool_sfunctions_init(GwyToolSFunctions *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_rect = gwy_plain_tool_check_layer_type(plain_tool,
                                                           "GwyLayerRectangle");
    if (!tool->layer_type_rect)
        return;

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_MARKUP;
    plain_tool->lazy_updates = TRUE;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_enum_by_name(settings, masking_key,
                                   &tool->args.masking);
    gwy_container_gis_enum_by_name(settings, output_type_key,
                                   &tool->args.output_type);
    tool->args.output_type = CLAMP(tool->args.output_type,
                                   0, GWY_SF_NFUNCTIONS);
    gwy_container_gis_boolean_by_name(settings, options_visible_key,
                                      &tool->args.options_visible);
    gwy_container_gis_boolean_by_name(settings, instant_update_key,
                                      &tool->args.instant_update);
    gwy_container_gis_int32_by_name(settings, resolution_key,
                                    &tool->args.resolution);
    gwy_container_gis_boolean_by_name(settings, fixres_key,
                                      &tool->args.fixres);
    gwy_container_gis_boolean_by_name(settings, separate_key,
                                      &tool->args.separate);
    gwy_container_gis_enum_by_name(settings, interpolation_key,
                                   &tool->args.interpolation);
    tool->args.interpolation
        = gwy_enum_sanitize_value(tool->args.interpolation,
                                  GWY_TYPE_INTERPOLATION_TYPE);
    gwy_container_gis_enum_by_name(settings, direction_key,
                                   &tool->args.direction);
    tool->args.direction
        = gwy_enum_sanitize_value(tool->args.direction, GWY_TYPE_ORIENTATION);
    tool->args.masking
        = gwy_enum_sanitize_value(tool->args.masking, GWY_TYPE_MASKING_TYPE);

    tool->line = gwy_data_line_new(4, 1.0, FALSE);
    tool->uline = gwy_data_line_new(4, 1.0, FALSE);
    tool->xunc = NULL;
    tool->yunc = NULL;
    tool->zunc = NULL;

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_rect,
                                     "rectangle");
    memset(tool->isel_prev, 0xff, 4*sizeof(gint));

    gwy_tool_sfunctions_init_dialog(tool);
}

static void
gwy_tool_sfunctions_rect_updated(GwyToolSFunctions *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_rect_selection_labels_select(tool->rlabels,
                                     plain_tool->selection,
                                     plain_tool->data_field);
}

static void
gwy_tool_sfunctions_init_dialog(GwyToolSFunctions *tool)
{
    static const GwyEnum directions[] = {
        { N_("_Horizontal direction"), GWY_ORIENTATION_HORIZONTAL, },
        { N_("_Vertical direction"),   GWY_ORIENTATION_VERTICAL,   },
    };
    GtkDialog *dialog;
    GtkWidget *label, *hbox, *vbox, *hbox2, *image;
    GtkTable *table;
    guint row;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, TRUE, TRUE, 0);

    /* Left pane */
    vbox = gtk_vbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    /* Selection info */
    tool->rlabels = gwy_rect_selection_labels_new
                         (TRUE, G_CALLBACK(gwy_tool_sfunctions_rect_updated),
                          tool);
    gtk_box_pack_start(GTK_BOX(vbox),
                       gwy_rect_selection_labels_get_table(tool->rlabels),
                       FALSE, FALSE, 0);

    /* Output type */
    hbox2 = gtk_hbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(hbox2), 4);
    gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic(_("_Quantity:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, FALSE, 0);

    tool->output_type = gwy_enum_combo_box_new
                           (sf_types, G_N_ELEMENTS(sf_types),
                            G_CALLBACK(output_type_changed), tool,
                            tool->args.output_type, TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), tool->output_type);
    gtk_box_pack_start(GTK_BOX(hbox2), tool->output_type, FALSE, FALSE, 0);

    /* Options */
    tool->options = gtk_expander_new(_("<b>Options</b>"));
    gtk_expander_set_use_markup(GTK_EXPANDER(tool->options), TRUE);
    gtk_expander_set_expanded(GTK_EXPANDER(tool->options),
                              tool->args.options_visible);
    g_signal_connect(tool->options, "notify::expanded",
                     G_CALLBACK(gwy_tool_sfunctions_options_expanded), tool);
    gtk_box_pack_start(GTK_BOX(vbox), tool->options, FALSE, FALSE, 0);

    table = GTK_TABLE(gtk_table_new(7, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_container_add(GTK_CONTAINER(tool->options), GTK_WIDGET(table));
    row = 0;

    tool->instant_update
        = gtk_check_button_new_with_mnemonic(_("_Instant updates"));
    gtk_table_attach(table, tool->instant_update,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->instant_update),
                                 tool->args.instant_update);
    g_signal_connect(tool->instant_update, "toggled",
                     G_CALLBACK(instant_update_changed), tool);
    row++;

    tool->resolution = gtk_adjustment_new(tool->args.resolution,
                                          MIN_RESOLUTION, MAX_RESOLUTION,
                                          1, 10, 0);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row,
                            _("_Fixed resolution:"), NULL,
                            tool->resolution,
                            GWY_HSCALE_CHECK | GWY_HSCALE_SQRT);
    g_signal_connect_swapped(tool->resolution, "value-changed",
                             G_CALLBACK(resolution_changed), tool);
    tool->fixres = gwy_table_hscale_get_check(tool->resolution);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->fixres),
                                 tool->args.fixres);
    g_signal_connect(tool->fixres, "toggled",
                     G_CALLBACK(fixres_changed), tool);
    gtk_table_set_row_spacing(table, row, 8);
    row++;

    tool->direction = gwy_radio_buttons_create
                        (directions, G_N_ELEMENTS(directions),
                        G_CALLBACK(direction_changed), tool,
                        tool->args.direction);
    row = gwy_radio_buttons_attach_to_table(tool->direction, table, 2, row);
    gtk_table_set_row_spacing(table, row-1, 8);

    tool->interpolation = gwy_enum_combo_box_new
                         (gwy_interpolation_type_get_enum(), -1,
                          G_CALLBACK(interpolation_changed), tool,
                          tool->args.interpolation, TRUE);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row,
                            _("_Interpolation type:"), NULL,
                            GTK_OBJECT(tool->interpolation),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    tool->masking = gwy_enum_combo_box_new
                            (gwy_masking_type_get_enum(), -1,
                             G_CALLBACK(masking_changed), tool,
                             tool->args.masking, TRUE);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("_Masking:"), NULL,
                            GTK_OBJECT(tool->masking),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    row++;

    tool->target_graph = gwy_data_chooser_new_graphs();
    gwy_data_chooser_set_none(GWY_DATA_CHOOSER(tool->target_graph),
                              _("New graph"));
    gwy_data_chooser_set_active(GWY_DATA_CHOOSER(tool->target_graph), NULL, -1);
    gwy_data_chooser_set_filter(GWY_DATA_CHOOSER(tool->target_graph),
                                filter_target_graphs, tool, NULL);
    gwy_table_attach_adjbar(GTK_WIDGET(table), row, _("Target _graph:"), NULL,
                            GTK_OBJECT(tool->target_graph),
                            GWY_HSCALE_WIDGET_NO_EXPAND);
    g_signal_connect_swapped(tool->target_graph, "changed",
                             G_CALLBACK(target_changed), tool);
    row++;

    tool->separate
        = gtk_check_button_new_with_mnemonic(_("_Separate uncertainty"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->separate),
                                 tool->args.separate);
    g_signal_connect(tool->separate, "toggled",
                     G_CALLBACK(separate_changed), tool);
    gtk_table_attach(table, tool->separate,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);

    row++;

    tool->gmodel = gwy_graph_model_new();

    tool->graph = gwy_graph_new(tool->gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(tool->graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), tool->graph, TRUE, TRUE, 2);

    tool->update = gtk_dialog_add_button(dialog, _("_Update"),
                                         GWY_TOOL_RESPONSE_UPDATE);
    image = gtk_image_new_from_stock(GTK_STOCK_EXECUTE, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(tool->update), image);
    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), FALSE);
    tool->apply = gtk_dialog_add_button(dialog, GTK_STOCK_APPLY,
                                        GTK_RESPONSE_APPLY);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    update_sensitivity(tool);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_sfunctions_data_switched(GwyTool *gwytool,
                                  GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolSFunctions *tool;
    gboolean ignore;

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);
    GWY_TOOL_CLASS(gwy_tool_sfunctions_parent_class)->data_switched(gwytool,
                                                                    data_view);

    if (ignore || plain_tool->init_failed)
        return;

    tool = GWY_TOOL_SFUNCTIONS(gwytool);
    GWY_OBJECT_UNREF(tool->cached_flipped_field);
    GWY_OBJECT_UNREF(tool->cached_fp_mask);

    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_rect,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);
        update_unc_fields(plain_tool);
    }

    update_curve(tool);
    update_target_graphs(tool);
}

static void
gwy_tool_sfunctions_response(GwyTool *tool,
                             gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_sfunctions_parent_class)->response(tool,
                                                               response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_sfunctions_apply(GWY_TOOL_SFUNCTIONS(tool));
    else if (response_id == GWY_TOOL_RESPONSE_UPDATE)
        update_curve(GWY_TOOL_SFUNCTIONS(tool));
}

static void
update_selected_rectangle(GwyToolSFunctions *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwySelection *selection = plain_tool->selection;
    GwyDataField *field = plain_tool->data_field;
    gint n;

    n = selection ? gwy_selection_get_data(selection, NULL) : 0;
    gwy_rect_selection_labels_fill(tool->rlabels, n == 1 ? selection : NULL,
                                   field, NULL, tool->isel);
}

static void
gwy_tool_sfunctions_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolSFunctions *tool = GWY_TOOL_SFUNCTIONS(plain_tool);

    GWY_OBJECT_UNREF(tool->cached_flipped_field);
    update_unc_fields(plain_tool);
    update_selected_rectangle(tool);
    update_curve(tool);
    update_target_graphs(tool);
}

static void
gwy_tool_sfunctions_mask_changed(GwyPlainTool *plain_tool)
{
    GwyToolSFunctions *tool = GWY_TOOL_SFUNCTIONS(plain_tool);

    GWY_OBJECT_UNREF(tool->cached_fp_mask);
    if (sfunction_supports_masking(tool->args.output_type))
        update_curve(tool);
}

static void
gwy_tool_sfunctions_selection_changed(GwyPlainTool *plain_tool,
                                      gint hint)
{
    GwyToolSFunctions *tool = GWY_TOOL_SFUNCTIONS(plain_tool);

    g_return_if_fail(hint <= 0);
    update_selected_rectangle(tool);
    if (tool->args.instant_update) {
        if (memcmp(tool->isel, tool->isel_prev, 4*sizeof(gint)) != 0)
            update_curve(tool);
    }
}

static void
update_sensitivity(GwyToolSFunctions *tool)
{
    ToolArgs *args = &tool->args;
    gboolean sensitive;
    GSList *l;

    gtk_widget_set_sensitive(tool->update, !args->instant_update);
    sensitive = !sfunction_has_native_sampling(args->output_type);
    gwy_table_hscale_set_sensitive(tool->resolution, sensitive);

    sensitive = (sfunction_has_interpolation(args->output_type)
                 && args->fixres);
    gwy_table_hscale_set_sensitive(GTK_OBJECT(tool->interpolation), sensitive);

    sensitive = sfunction_has_direction(args->output_type);
    for (l = tool->direction; l; l = g_slist_next(l))
        gtk_widget_set_sensitive(GTK_WIDGET(l->data), sensitive);

    sensitive = sfunction_supports_masking(args->output_type);
    gwy_table_hscale_set_sensitive(GTK_OBJECT(tool->masking), sensitive);
}

static void
update_curve(GwyToolSFunctions *tool)
{
    GwyPlainTool *plain_tool;
    GwyDataField *dfield, *mask, *mask_to_use = NULL, *field_to_use;
    GwyOrientation dir = tool->args.direction;
    GwyInterpolationType interp = tool->args.interpolation;
    GwyGraphModel *gmodel = tool->gmodel;
    GwyGraphCurveModel *gcmodel, *ugcmodel = NULL;
    gint n, nsel, lineres, col, row, w, h;
    const gchar *title, *xlabel, *ylabel;
    gboolean xy_is_flipped;

    plain_tool = GWY_PLAIN_TOOL(tool);
    dfield = plain_tool->data_field;
    mask = plain_tool->mask_field;

    if (!dfield) {
        gwy_graph_model_remove_all_curves(gmodel);
        gtk_widget_set_sensitive(tool->apply, FALSE);
        return;
    }

    if (plain_tool->pending_updates & GWY_PLAIN_TOOL_CHANGED_SELECTION)
        update_selected_rectangle(tool);
    plain_tool->pending_updates = 0;

    gwy_assign(tool->isel_prev, tool->isel, 4);
    n = gwy_graph_model_get_n_curves(gmodel);
    col = tool->isel[0];
    row = tool->isel[1];
    w = tool->isel[2]+1 - tool->isel[0];
    h = tool->isel[3]+1 - tool->isel[1];
    nsel = (dfield && w >= 4 && h >= 4) ? 1 : 0;
    gwy_debug("%d x %d at (%d, %d)", w, h, col, row);

    gtk_widget_set_sensitive(tool->apply, nsel > 0);

    if (nsel == 0 && n == 0)
        return;

    if (nsel == 0 && n > 0) {
        gwy_graph_model_remove_all_curves(gmodel);
        return;
    }

    tool->has_uline = FALSE;
    lineres = tool->args.fixres ? tool->args.resolution : -1;

    /* Create transformed/inverted mask as necesary and remember it.  Keep
     * mask_to_use as NULL if we do not want masking.  */
    if (sfunction_supports_masking(tool->args.output_type)
        && tool->args.masking != GWY_MASK_IGNORE
        && mask) {
        if (!tool->cached_fp_mask) {
            if (sfunction_is_only_row_wise(tool->args.output_type)
                && tool->args.direction == GWY_ORIENTATION_VERTICAL) {
                tool->cached_fp_mask = gwy_data_field_new_alike(mask, FALSE);
                gwy_data_field_flip_xy(mask, tool->cached_fp_mask, FALSE);
            }
            else
                tool->cached_fp_mask = gwy_data_field_duplicate(mask);

            if (tool->args.masking == GWY_MASK_EXCLUDE)
                gwy_data_field_grains_invert(tool->cached_fp_mask);
        }
        mask_to_use = tool->cached_fp_mask;
    }

    field_to_use = dfield;
    xy_is_flipped = FALSE;
    if (sfunction_is_only_row_wise(tool->args.output_type)
        && tool->args.direction == GWY_ORIENTATION_VERTICAL) {
        if (!tool->cached_flipped_field) {
            tool->cached_flipped_field = gwy_data_field_new_alike(dfield,
                                                                  FALSE);
            gwy_data_field_flip_xy(dfield, tool->cached_flipped_field, FALSE);
        }
        field_to_use = tool->cached_flipped_field;
        xy_is_flipped = TRUE;
        GWY_SWAP(gint, col, row);
        GWY_SWAP(gint, w, h);
    }

    switch (tool->args.output_type) {
        case GWY_SF_DH:
        gwy_data_field_area_dh(field_to_use, mask_to_use, tool->line,
                               col, row, w, h, lineres);
        xlabel = "z";
        ylabel = "ρ";
        if (tool->has_calibration) {
            gwy_data_field_area_dh_uncertainty(field_to_use, tool->zunc,
                                               mask_to_use,
                                               tool->uline,
                                               col, row, w, h,
                                               lineres);
            tool->has_uline = TRUE;
        }
        break;

        case GWY_SF_CDH:
        gwy_data_field_area_cdh(field_to_use, mask_to_use, tool->line,
                                col, row, w, h, lineres);
        xlabel = "z";
        ylabel = "D";
        if (tool->has_calibration) {
            gwy_data_field_area_cdh_uncertainty(field_to_use,
                                                tool->zunc, mask_to_use,
                                                tool->uline,
                                                col, row, w, h,
                                                lineres);
            tool->has_uline = TRUE;
        }
        break;

        case GWY_SF_DA:
        gwy_data_field_area_da_mask(field_to_use, mask_to_use, tool->line,
                                    col, row, w, h, dir, lineres);
        xlabel = "tan β";
        ylabel = "ρ";
        break;

        case GWY_SF_CDA:
        gwy_data_field_area_cda_mask(field_to_use, mask_to_use, tool->line,
                                     col, row, w, h, dir, lineres);
        xlabel = "tan β";
        ylabel = "D";
        break;

        case GWY_SF_ACF:
        g_object_unref(tool->line);
        tool->line = gwy_data_field_area_row_acf(field_to_use,
                                                 mask_to_use, GWY_MASK_INCLUDE,
                                                 col, row, w, h, 1,
                                                 NULL);
        xlabel = "τ";
        ylabel = "G";
        if (tool->has_calibration && !xy_is_flipped) {
            gwy_data_field_area_acf_uncertainty(field_to_use,
                                                tool->zunc, tool->uline,
                                                col, row, w, h,
                                                dir, interp, lineres);
            tool->has_uline = TRUE;
        }
        break;

        case GWY_SF_HHCF:
        g_object_unref(tool->line);
        tool->line = gwy_data_field_area_row_hhcf(field_to_use,
                                                  mask_to_use, GWY_MASK_INCLUDE,
                                                  col, row, w, h, 1,
                                                  NULL);
        xlabel = "τ";
        ylabel = "H";
        if (tool->has_calibration && !xy_is_flipped) {
            gwy_data_field_area_hhcf_uncertainty(field_to_use,
                                                 tool->zunc, tool->uline,
                                                 col, row, w, h,
                                                 dir, interp, lineres);
            tool->has_uline = TRUE;
        }
        break;

        case GWY_SF_PSDF:
        g_object_unref(tool->line);
        tool->line = gwy_data_field_area_row_psdf(field_to_use,
                                                  mask_to_use, GWY_MASK_INCLUDE,
                                                  col, row, w, h,
                                                  GWY_WINDOWING_HANN, 1);
        xlabel = "k";
        ylabel = "W<sub>1</sub>";
        break;

        case GWY_SF_MINKOWSKI_VOLUME:
        gwy_data_field_area_minkowski_volume(field_to_use, tool->line,
                                             col, row, w, h, lineres);
        xlabel = "z";
        ylabel = "V";
        break;

        case GWY_SF_MINKOWSKI_BOUNDARY:
        gwy_data_field_area_minkowski_boundary(field_to_use, tool->line,
                                               col, row, w, h, lineres);
        xlabel = "z";
        ylabel = "S";
        break;

        case GWY_SF_MINKOWSKI_CONNECTIVITY:
        gwy_data_field_area_minkowski_euler(field_to_use, tool->line,
                                            col, row, w, h, lineres);
        xlabel = "z";
        ylabel = "χ";
        break;

        case GWY_SF_RPSDF:
        gwy_data_field_area_rpsdf(field_to_use, tool->line, col, row, w, h,
                                  interp, GWY_WINDOWING_HANN, lineres);
        xlabel = "k";
        ylabel = "W<sub>r</sub>";
        break;

        case GWY_SF_ANGSPEC:
        make_angular_spectrum(field_to_use, mask_to_use, GWY_MASK_INCLUDE,
                              col, row, w, h, lineres, GWY_WINDOWING_HANN, 1,
                              tool->line);
        xlabel = "α";
        ylabel = "W<sub>a</sub>";
        break;

        case GWY_SF_RACF:
        gwy_data_field_area_racf(field_to_use, tool->line, col, row, w, h,
                                 lineres);
        xlabel = "τ";
        ylabel = "G<sub>r</sub>";
        break;

        case GWY_SF_RANGE:
        gwy_data_field_area_range(field_to_use, tool->line, col, row, w, h,
                                  dir, interp, lineres);
        xlabel = "τ";
        ylabel = "R";
        break;

        case GWY_SF_ASG:
        g_object_unref(tool->line);
        tool->line = gwy_data_field_area_row_asg(field_to_use,
                                                 mask_to_use, GWY_MASK_INCLUDE,
                                                 col, row, w, h, 1);
        xlabel = "τ";
        ylabel = "A<sub>excess</sub>";
        break;

        default:
        g_return_if_reached();
        break;
    }

    if (nsel > 0 && n == 0) {
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
        g_object_unref(gcmodel);

        if (tool->has_calibration && tool->has_uline) {
           ugcmodel = gwy_graph_curve_model_new();
           gwy_graph_model_add_curve(gmodel, ugcmodel);
           g_object_set(ugcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
           g_object_unref(ugcmodel);
        }
    }
    else {
        gcmodel = gwy_graph_model_get_curve(gmodel, 0);
        if (tool->has_calibration && tool->has_uline) {
           if (gwy_graph_model_get_n_curves(gmodel) < 2) {
               ugcmodel = gwy_graph_curve_model_new();
               gwy_graph_model_add_curve(gmodel, ugcmodel);
               g_object_set(ugcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
               g_object_unref(ugcmodel);
           }
           else {
               ugcmodel = gwy_graph_model_get_curve(gmodel, 1);
           }
        }
        else if (gwy_graph_model_get_n_curves(gmodel) > 1)
           gwy_graph_model_remove_curve(gmodel, 1);
    }

    gwy_graph_curve_model_set_data_from_dataline(gcmodel, tool->line, 0, 0);
    title = gettext(gwy_enum_to_string(tool->args.output_type,
                                       sf_types, G_N_ELEMENTS(sf_types)));

    g_object_set(gcmodel, "description", title, NULL);

    if (tool->has_calibration && tool->has_uline) {
        g_assert(ugcmodel);
        gwy_graph_curve_model_set_data_from_dataline(ugcmodel, tool->uline,
                                                     0, 0);
        g_object_set(ugcmodel, "description", "uncertainty", NULL);
    }

    g_object_set(gmodel,
                 "title", title,
                 "axis-label-bottom", xlabel,
                 "axis-label-left", ylabel,
                 NULL);

    gwy_graph_model_set_units_from_data_line(gmodel, tool->line);
    update_target_graphs(tool);
}

static void
instant_update_changed(GtkToggleButton *check, GwyToolSFunctions *tool)
{
    tool->args.instant_update = gtk_toggle_button_get_active(check);
    update_sensitivity(tool);
    if (tool->args.instant_update)
        update_curve(tool);
}

static void
resolution_changed(GwyToolSFunctions *tool, GtkAdjustment *adj)
{
    tool->args.resolution = gwy_adjustment_get_int(adj);
    /* Resolution can be changed only when fixres == TRUE */
    update_curve(tool);
}

static void
fixres_changed(GtkToggleButton *check, GwyToolSFunctions *tool)
{
    tool->args.fixres = gtk_toggle_button_get_active(check);
    update_sensitivity(tool);
    update_curve(tool);
}

static void
output_type_changed(GtkComboBox *combo, GwyToolSFunctions *tool)
{
    tool->args.output_type = gwy_enum_combo_box_get_active(combo);
    update_sensitivity(tool);
    update_curve(tool);
    update_target_graphs(tool);
}

static void
direction_changed(G_GNUC_UNUSED GObject *button, GwyToolSFunctions *tool)
{
    tool->args.direction = gwy_radio_buttons_get_current(tool->direction);
    GWY_OBJECT_UNREF(tool->cached_fp_mask);
    update_curve(tool);
}

static void
interpolation_changed(GtkComboBox *combo, GwyToolSFunctions *tool)
{
    tool->args.interpolation = gwy_enum_combo_box_get_active(combo);
    update_curve(tool);
}

static void
gwy_tool_sfunctions_options_expanded(GtkExpander *expander,
                                     G_GNUC_UNUSED GParamSpec *pspec,
                                     GwyToolSFunctions *tool)
{
    tool->args.options_visible = gtk_expander_get_expanded(expander);
}

static void
separate_changed(GtkToggleButton *check, GwyToolSFunctions *tool)
{
    tool->args.separate = gtk_toggle_button_get_active(check);
}

static void
masking_changed(GtkComboBox *combo, GwyToolSFunctions *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->args.masking = gwy_enum_combo_box_get_active(combo);
    GWY_OBJECT_UNREF(tool->cached_fp_mask);
    if (plain_tool->data_field && plain_tool->mask_field)
        update_curve(tool);
}

static void
update_target_graphs(GwyToolSFunctions *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_refilter(chooser);
}

static gboolean
filter_target_graphs(GwyContainer *data, gint id, gpointer user_data)
{
    GwyToolSFunctions *tool = (GwyToolSFunctions*)user_data;
    GwyGraphModel *gmodel, *targetgmodel;
    GQuark quark = gwy_app_get_graph_key_for_id(id);

    return ((gmodel = tool->gmodel)
            && gwy_container_gis_object(data, quark, (GObject**)&targetgmodel)
            && gwy_graph_model_units_are_compatible(gmodel, targetgmodel));
}

static void
target_changed(GwyToolSFunctions *tool)
{
    GwyDataChooser *chooser = GWY_DATA_CHOOSER(tool->target_graph);
    gwy_data_chooser_get_active_id(chooser, &tool->args.target);
}

static void
gwy_tool_sfunctions_apply(GwyToolSFunctions *tool)
{
    GwyPlainTool *plain_tool;
    GwyGraphModel *gmodel, *ugmodel;
    gchar *str, title[50];

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);

    if (tool->args.target.datano) {
        GwyContainer *data = gwy_app_data_browser_get(tool->args.target.datano);
        GQuark quark = gwy_app_get_graph_key_for_id(tool->args.target.id);
        gmodel = gwy_container_get_object(data, quark);
        g_return_if_fail(gmodel);
        gwy_graph_model_append_curves(gmodel, tool->gmodel, 1);
        return;
    }

    gmodel = gwy_graph_model_duplicate(tool->gmodel);
    if (tool->has_calibration && tool->has_uline && tool->args.separate
        && gwy_graph_model_get_n_curves(gmodel) == 2) {
        ugmodel = gwy_graph_model_duplicate(tool->gmodel);
        g_object_get(ugmodel,"title", &str, NULL);
        g_snprintf(title, sizeof(title), "%s uncertainty", str);
        g_object_set(ugmodel, "title", title, NULL);
        g_free(str);

        gwy_graph_model_remove_curve(ugmodel, 0);
        gwy_graph_model_remove_curve(gmodel, 1);

        gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container,
                                             TRUE);
        gwy_app_data_browser_add_graph_model(ugmodel, plain_tool->container,
                                             TRUE);

    }
    else
        gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container,
                                             TRUE);

    g_object_unref(gmodel);
}

static void
make_angular_spectrum(GwyDataField *field,
                      GwyDataField *mask, GwyMaskingType masking,
                      gint col, gint row, gint w, gint h,
                      gint lineres,
                      GwyWindowingType windowing, gint level,
                      GwyDataLine *target)
{
    GwyDataField *psdf = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    GwyDataLine *tmpline;

    gwy_data_field_area_2dpsdf_mask(field, psdf, mask, masking,
                                    col, row, w, h, windowing, level);
    tmpline = gwy_data_field_psdf_to_angular_spectrum(psdf, lineres);
    g_object_unref(psdf);
    gwy_data_line_assign(target, tmpline);
    g_object_unref(tmpline);

    /* Transform to degrees. */
    gwy_data_line_multiply(target, G_PI/180.0);
    gwy_data_line_set_real(target, 360.0);
    gwy_data_line_set_offset(target, -180.0/gwy_data_line_get_res(target));
    gwy_si_unit_set_from_string(gwy_data_line_get_si_unit_x(target), "deg");
}

static void
gwy_data_line_range_transform(GwyDataLine *dline, GwyDataLine *target,
                              gdouble *mindata, gdouble *maxdata)
{
    gint res = dline->res, tres = target->res;
    gint i, j;

    g_return_if_fail(tres < res);
    gwy_assign(mindata, dline->data, res);
    gwy_assign(maxdata, dline->data, res);

    for (i = 1; i < tres; i++) {
        gdouble r = 0.0;
        for (j = 0; j < res-i; j++) {
            if (mindata[j+1] < mindata[j])
                mindata[j] = mindata[j+1];
            if (maxdata[j+1] > maxdata[j])
                maxdata[j] = maxdata[j+1];
            r += maxdata[j] - mindata[j];
        }
        target->data[i] += r/(res - i);
    }
}

static void
gwy_data_field_area_range(GwyDataField *dfield,
                          GwyDataLine *dline,
                          gint col, gint row, gint width, gint height,
                          GwyOrientation direction,
                          G_GNUC_UNUSED GwyInterpolationType interp,
                          gint lineres)
{
    GwyDataLine *buf = gwy_data_line_new(1, 1.0, FALSE);
    gint res, thickness, i;
    gdouble *mindata, *maxdata;
    gdouble h;

    gwy_data_field_copy_units_to_data_line(dfield, dline);
    if (direction == GWY_ORIENTATION_HORIZONTAL) {
        res = width-1;
        thickness = height;
        h = gwy_data_field_get_dx(dfield);
    }
    else if (direction == GWY_ORIENTATION_VERTICAL) {
        res = height-1;
        thickness = width;
        h = gwy_data_field_get_dy(dfield);
    }
    else {
        g_return_if_reached();
    }

    mindata = g_new(gdouble, res+1);
    maxdata = g_new(gdouble, res+1);
    if (lineres > 0)
        res = MIN(lineres, res);

    gwy_data_line_resample(dline, res, GWY_INTERPOLATION_NONE);
    gwy_data_line_clear(dline);
    gwy_data_line_set_offset(dline, 0.0);
    gwy_data_line_set_real(dline, res*h);
    for (i = 0; i < thickness; i++) {
        if (direction == GWY_ORIENTATION_HORIZONTAL)
            gwy_data_field_get_row_part(dfield, buf, row+i, col, col+width);
        else
            gwy_data_field_get_column_part(dfield, buf, col+i, row, row+height);

        gwy_data_line_range_transform(buf, dline, mindata, maxdata);
    }
    gwy_data_line_multiply(dline, 1.0/thickness);

    g_free(maxdata);
    g_free(mindata);
    g_object_unref(buf);
}

static void
update_unc_fields(GwyPlainTool *plain_tool)
{
    GwyToolSFunctions *tool = GWY_TOOL_SFUNCTIONS(plain_tool);
    gchar xukey[24], yukey[24], zukey[24];

    g_snprintf(xukey, sizeof(xukey), "/%d/data/cal_xunc", plain_tool->id);
    g_snprintf(yukey, sizeof(yukey), "/%d/data/cal_yunc", plain_tool->id);
    g_snprintf(zukey, sizeof(zukey), "/%d/data/cal_zunc", plain_tool->id);

    GWY_OBJECT_UNREF(tool->xunc);
    GWY_OBJECT_UNREF(tool->yunc);
    GWY_OBJECT_UNREF(tool->zunc);

    if (gwy_container_gis_object_by_name(plain_tool->container,
                                         xukey, &tool->xunc)
        && gwy_container_gis_object_by_name(plain_tool->container,
                                            yukey, &tool->yunc)
        && gwy_container_gis_object_by_name(plain_tool->container,
                                            zukey, &tool->zunc)) {
        gint xres = gwy_data_field_get_xres(plain_tool->data_field);
        gint yres = gwy_data_field_get_yres(plain_tool->data_field);

        /* We need to resample uncertainties. */
        tool->xunc = gwy_data_field_new_resampled(tool->xunc, xres, yres,
                                                  GWY_INTERPOLATION_BILINEAR);
        tool->yunc = gwy_data_field_new_resampled(tool->yunc, xres, yres,
                                                  GWY_INTERPOLATION_BILINEAR);
        tool->zunc = gwy_data_field_new_resampled(tool->zunc, xres, yres,
                                                  GWY_INTERPOLATION_BILINEAR);

        tool->has_calibration = TRUE;
        gtk_widget_show(tool->separate);
    }
    else {
        tool->has_calibration = FALSE;
        gtk_widget_hide(tool->separate);
    }
}

static gboolean
sfunction_supports_masking(GwySFOutputType type)
{
    return (type == GWY_SF_DH || type == GWY_SF_CDH
            || type == GWY_SF_DA || type == GWY_SF_CDA
            || type == GWY_SF_ACF || type == GWY_SF_HHCF || type == GWY_SF_ASG
            || type == GWY_SF_PSDF || type == GWY_SF_ANGSPEC);
}

static gboolean
sfunction_has_native_sampling(GwySFOutputType type)
{
    return (type == GWY_SF_ACF || type == GWY_SF_HHCF || type == GWY_SF_ASG
            || type == GWY_SF_PSDF
            || type == GWY_SF_RANGE);
}

static gboolean
sfunction_has_interpolation(GwySFOutputType type)
{
    /* RACF has no interpolation argument and all the other related functions
     * have native sampling so we do not want interpolation. */
    return (type == GWY_SF_RPSDF);
}

static gboolean
sfunction_has_direction(GwySFOutputType type)
{
    return (type == GWY_SF_DA || type == GWY_SF_CDA
            || type == GWY_SF_ACF || type == GWY_SF_HHCF || type == GWY_SF_ASG
            || type == GWY_SF_PSDF);
}

static gboolean
sfunction_is_only_row_wise(GwySFOutputType type)
{
    return (type == GWY_SF_ACF || type == GWY_SF_HHCF || type == GWY_SF_ASG
            || type == GWY_SF_PSDF);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
