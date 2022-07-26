/*
 *  $Id: crop.c 21816 2019-01-08 17:09:03Z yeti-dn $
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/datafield.h>
#include <libgwydgets/gwystock.h>
#include <app/gwyapp.h>

#define GWY_TYPE_TOOL_CROP            (gwy_tool_crop_get_type())
#define GWY_TOOL_CROP(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_CROP, GwyToolCrop))
#define GWY_IS_TOOL_CROP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_CROP))
#define GWY_TOOL_CROP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_CROP, GwyToolCropClass))

typedef struct _GwyToolCrop      GwyToolCrop;
typedef struct _GwyToolCropClass GwyToolCropClass;

typedef struct {
    gboolean keep_offsets;
    gboolean new_channel;
} ToolArgs;

struct _GwyToolCrop {
    GwyPlainTool parent_instance;

    ToolArgs args;

    GwyRectSelectionLabels *rlabels;
    GtkWidget *keep_offsets;
    GtkWidget *new_channel;
    GtkWidget *apply;
    gdouble rsel[4];
    gint isel[4];

    /* potential class data */
    GType layer_type_rect;
};

struct _GwyToolCropClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType  gwy_tool_crop_get_type            (void) G_GNUC_CONST;
static void   gwy_tool_crop_finalize            (GObject *object);
static void   gwy_tool_crop_init_dialog         (GwyToolCrop *tool);
static void   gwy_tool_crop_data_switched       (GwyTool *gwytool,
                                                 GwyDataView *data_view);
static void   gwy_tool_crop_data_changed        (GwyPlainTool *plain_tool);
static void   gwy_tool_crop_response            (GwyTool *tool,
                                                 gint response_id);
static void   gwy_tool_crop_selection_changed   (GwyPlainTool *plain_tool,
                                                 gint hint);
static void   gwy_tool_crop_keep_offsets_toggled(GwyToolCrop *tool,
                                                 GtkToggleButton *toggle);
static void   gwy_tool_crop_new_data_toggled    (GwyToolCrop *tool,
                                                 GtkToggleButton *toggle);
static void   gwy_tool_crop_apply               (GwyToolCrop *tool);
static void   gwy_tool_crop_save_args           (GwyToolCrop *tool);
static void   update_selected_rectangle         (GwyToolCrop *tool);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Crop tool, crops data to smaller size."),
    "Yeti <yeti@gwyddion.net>",
    "2.13",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

static const gchar keep_offsets_key[] = "/module/crop/keep_offsets";
static const gchar new_channel_key[]  = "/module/crop/new_channel";

static const ToolArgs default_args = {
    FALSE,
    TRUE,
};

GWY_MODULE_QUERY2(module_info, crop)

G_DEFINE_TYPE(GwyToolCrop, gwy_tool_crop, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_CROP);

    return TRUE;
}

static void
gwy_tool_crop_class_init(GwyToolCropClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_crop_finalize;

    tool_class->stock_id = GWY_STOCK_CROP;
    tool_class->title = _("Crop");
    tool_class->tooltip = _("Crop data");
    tool_class->prefix = "/module/crop";
    tool_class->data_switched = gwy_tool_crop_data_switched;
    tool_class->response = gwy_tool_crop_response;

    ptool_class->data_changed = gwy_tool_crop_data_changed;
    ptool_class->selection_changed = gwy_tool_crop_selection_changed;
}

static void
gwy_tool_crop_finalize(GObject *object)
{
    gwy_tool_crop_save_args(GWY_TOOL_CROP(object));
    G_OBJECT_CLASS(gwy_tool_crop_parent_class)->finalize(object);
}

static void
gwy_tool_crop_init(GwyToolCrop *tool)
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
    gwy_container_gis_boolean_by_name(settings, keep_offsets_key,
                                      &tool->args.keep_offsets);
    gwy_container_gis_boolean_by_name(settings, new_channel_key,
                                      &tool->args.new_channel);

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_rect,
                                     "rectangle");

    gwy_tool_crop_init_dialog(tool);
}

static void
gwy_tool_crop_rect_updated(GwyToolCrop *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_rect_selection_labels_select(tool->rlabels,
                                     plain_tool->selection,
                                     plain_tool->data_field);
}

static void
gwy_tool_crop_init_dialog(GwyToolCrop *tool)
{
    GtkDialog *dialog;
    GtkTable *table;
    gint row;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    /* Selection info */
    tool->rlabels = gwy_rect_selection_labels_new
                         (TRUE, G_CALLBACK(gwy_tool_crop_rect_updated), tool);
    gtk_box_pack_start(GTK_BOX(dialog->vbox),
                       gwy_rect_selection_labels_get_table(tool->rlabels),
                       FALSE, FALSE, 0);

    /* Options */
    table = GTK_TABLE(gtk_table_new(2, 1, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), GTK_WIDGET(table),
                       FALSE, FALSE, 0);
    row = 0;

    tool->keep_offsets
        = gtk_check_button_new_with_mnemonic(_("Keep lateral offsets"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->keep_offsets),
                                 tool->args.keep_offsets);
    gtk_table_attach(table, tool->keep_offsets,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(tool->keep_offsets, "toggled",
                             G_CALLBACK(gwy_tool_crop_keep_offsets_toggled),
                             tool);
    row++;

    tool->new_channel
        = gtk_check_button_new_with_mnemonic(_("Create new image"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->new_channel),
                                 tool->args.new_channel);
    gtk_table_attach(table, tool->new_channel,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(tool->new_channel, "toggled",
                             G_CALLBACK(gwy_tool_crop_new_data_toggled),
                             tool);
    row++;

    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), FALSE);
    tool->apply = gtk_dialog_add_button(dialog, GTK_STOCK_APPLY,
                                        GTK_RESPONSE_APPLY);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_NO_BUTTON);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_crop_data_switched(GwyTool *gwytool,
                            GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyToolCrop *tool;
    gboolean ignore;

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_crop_parent_class)->data_switched(gwytool,
                                                              data_view);
    if (ignore || plain_tool->init_failed)
        return;

    tool = GWY_TOOL_CROP(gwytool);
    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_rect,
                                "is-crop", TRUE,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);
    }
}

static void
gwy_tool_crop_data_changed(GwyPlainTool *plain_tool)
{
    update_selected_rectangle(GWY_TOOL_CROP(plain_tool));
}

static void
gwy_tool_crop_response(GwyTool *tool,
                       gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_crop_parent_class)->response(tool, response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_crop_apply(GWY_TOOL_CROP(tool));
}

static void
gwy_tool_crop_selection_changed(GwyPlainTool *plain_tool,
                                gint hint)
{
    g_return_if_fail(hint <= 0);
    update_selected_rectangle(GWY_TOOL_CROP(plain_tool));
}

static void
gwy_tool_crop_keep_offsets_toggled(GwyToolCrop *tool,
                                   GtkToggleButton *toggle)
{
    tool->args.keep_offsets = gtk_toggle_button_get_active(toggle);
}

static void
gwy_tool_crop_new_data_toggled(GwyToolCrop *tool,
                               GtkToggleButton *toggle)
{
    tool->args.new_channel = gtk_toggle_button_get_active(toggle);
}

static void
gwy_tool_crop_one_field(GwyDataField *dfield,
                        const gint *isel,
                        const gdouble *sel,
                        gboolean keep_offsets)
{
    gwy_data_field_resize(dfield, isel[0], isel[1], isel[2]+1, isel[3]+1);

    if (keep_offsets) {
        gdouble xoff, yoff;

        xoff = gwy_data_field_get_xoffset(dfield);
        yoff = gwy_data_field_get_yoffset(dfield);
        gwy_data_field_set_xoffset(dfield, sel[0] + xoff);
        gwy_data_field_set_yoffset(dfield, sel[1] + yoff);
    }
    else {
        gwy_data_field_set_xoffset(dfield, 0.0);
        gwy_data_field_set_yoffset(dfield, 0.0);
    }
}

static void
gwy_tool_crop_apply(GwyToolCrop *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *container;
    GwyDataField *dfield, *mfield, *sfield;
    GQuark quarks[3];
    gint oldid, id;
    gboolean keep_off = tool->args.keep_offsets;
    gint isel[4];
    gdouble rsel[4];

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->id >= 0 && plain_tool->data_field != NULL);

    if (!gwy_selection_get_data(plain_tool->selection, NULL)) {
        g_warning("Apply invoked when no selection is present");
        return;
    }

    gwy_tool_crop_save_args(tool);

    container = plain_tool->container;
    oldid = plain_tool->id;
    mfield = plain_tool->mask_field;
    sfield = plain_tool->show_field;
    gwy_assign(isel, tool->isel, 4);
    gwy_assign(rsel, tool->rsel, 4);

    if (tool->args.new_channel) {
        dfield = gwy_data_field_duplicate(plain_tool->data_field);
        gwy_tool_crop_one_field(dfield, isel, rsel, keep_off);
        id = gwy_app_data_browser_add_data_field(dfield, container, TRUE);
        g_object_unref(dfield);
        gwy_app_sync_data_items(container, container, oldid, id, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_RANGE_TYPE,
                                GWY_DATA_ITEM_MASK_COLOR,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
        gwy_app_set_data_field_title(container, id, _("Detail"));
        gwy_app_channel_log_add(container, oldid, id,
                                "tool::GwyToolCrop", NULL);

        if (mfield) {
            dfield = gwy_data_field_duplicate(mfield);
            gwy_tool_crop_one_field(dfield, isel, rsel, keep_off);
            quarks[1] = gwy_app_get_mask_key_for_id(id);
            gwy_container_set_object(container, quarks[1], dfield);
            g_object_unref(dfield);
        }

        if (sfield) {
            dfield = gwy_data_field_duplicate(sfield);
            gwy_tool_crop_one_field(dfield, isel, rsel, keep_off);
            quarks[2] = gwy_app_get_show_key_for_id(id);
            gwy_container_set_object(container, quarks[2], dfield);
            g_object_unref(dfield);
        }
    }
    else {
        dfield = plain_tool->data_field;
        quarks[0] = gwy_app_get_data_key_for_id(oldid);
        quarks[1] = quarks[2] = 0;
        if (plain_tool->mask_field)
            quarks[1] = gwy_app_get_mask_key_for_id(oldid);
        if (plain_tool->show_field)
            quarks[2] = gwy_app_get_show_key_for_id(oldid);
        gwy_app_undo_qcheckpointv(container, G_N_ELEMENTS(quarks), quarks);

        gwy_tool_crop_one_field(dfield, isel, rsel, keep_off);
        gwy_data_field_data_changed(dfield);
        if (mfield) {
            gwy_tool_crop_one_field(mfield, isel, rsel, keep_off);
            gwy_data_field_data_changed(mfield);
        }
        if (sfield) {
            gwy_tool_crop_one_field(sfield, isel, rsel, keep_off);
            gwy_data_field_data_changed(sfield);
        }

        /* XXX: This is not undoable */
        gwy_app_data_clear_selections(container, oldid);
        gwy_plain_tool_log_add(plain_tool);
    }
}

static void
update_selected_rectangle(GwyToolCrop *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwySelection *selection = plain_tool->selection;
    GwyDataField *field = plain_tool->data_field;
    gint xres, yres, n;

    n = selection ? gwy_selection_get_data(selection, NULL) : 0;
    if (n != 1 || !field) {
        gwy_rect_selection_labels_fill(tool->rlabels, NULL, NULL,
                                       tool->rsel, tool->isel);
        gtk_widget_set_sensitive(tool->apply, FALSE);
        return;
    }

    gwy_rect_selection_labels_fill(tool->rlabels, selection, field,
                                   tool->rsel, tool->isel);

    /* Make Apply insensitive when the full image is selected, for whatever
     * reason.  There is nothting to crop then. */
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    if (tool->isel[2] - tool->isel[0] == xres-1
        && tool->isel[3] - tool->isel[1] == yres-1) {
        gtk_widget_set_sensitive(tool->apply, FALSE);
        return;
    }

    gtk_widget_set_sensitive(tool->apply, TRUE);
}

static void
gwy_tool_crop_save_args(GwyToolCrop *tool)
{
    GwyContainer *settings;

    settings = gwy_app_settings_get();
    gwy_container_set_boolean_by_name(settings, keep_offsets_key,
                                      tool->args.keep_offsets);
    gwy_container_set_boolean_by_name(settings, new_channel_key,
                                      tool->args.new_channel);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
