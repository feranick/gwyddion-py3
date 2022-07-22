/*
 *  $Id: mark_with.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2009-2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define MARK_RUN_MODES GWY_RUN_INTERACTIVE

typedef enum {
    MARK_WITH_MASK = 0,
    MARK_WITH_IMAGE = 1,
    MARK_WITH_SHOW = 2,
} MarkWithWhat;

/* Copied from the mask editor tool, might make sense to use the same enum. */
typedef enum {
    MASK_EDIT_SET       = 0,
    MASK_EDIT_ADD       = 1,
    MASK_EDIT_REMOVE    = 2,
    MASK_EDIT_INTERSECT = 3
} MaskEditMode;

enum {
    PARAM_MARK_WITH,
    PARAM_OPERATION,
    PARAM_MIN,
    PARAM_MAX,
    PARAM_OPERAND_IMAGE,
    PARAM_OPERAND_MASK,
    PARAM_OPERAND_SHOW,
    PARAM_UPDATE,
    PARAM_MASK_COLOR,
    LABEL_MARK_WITH,
    LABEL_RANGE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *source;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GSList *withwhat;
    GtkWidget *operand[3];
    gboolean has_any[3];
    GtkSizeGroup *sizegroup;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register                (void);
static GwyParamDef*     define_module_params           (void);
static void             mark_with                      (GwyContainer *data,
                                                        GwyRunType runtype);
static GwyDialogOutcome run_gui                        (ModuleArgs *args,
                                                        GwyContainer *data,
                                                        gint id);
static void             execute                        (ModuleArgs *args);
static void             param_changed                  (ModuleGUI *gui,
                                                        gint id);
static void             preview                        (gpointer user_data);
static GwyDataField*    get_other_field                (GwyParams *params,
                                                        GwyContainer **pdata,
                                                        gint *id,
                                                        gboolean base_field);
static void             dialog_response                (ModuleGUI *gui,
                                                        gint response);
static void             ensure_something_is_selected   (ModuleGUI *gui);
static void             gwy_data_field_threshold_to    (GwyDataField *source,
                                                        GwyDataField *dest,
                                                        gdouble min,
                                                        gdouble max);
static GtkWidget*       create_operand_row_mask        (gpointer user_data);
static GtkWidget*       create_operand_row_image       (gpointer user_data);
static GtkWidget*       create_operand_row_presentation(gpointer user_data);
static void             operand_changed                (GwyDataChooser *chooser,
                                                        ModuleGUI *gui);
static gboolean         operand_filter_mask            (GwyContainer *source,
                                                        gint id,
                                                        gpointer user_data);
static gboolean         operand_filter_image           (GwyContainer *source,
                                                        gint id,
                                                        gpointer user_data);
static gboolean         operand_filter_presentation    (GwyContainer *source,
                                                        gint id,
                                                        gpointer user_data);
static void             with_what_selected             (GtkRadioButton *button,
                                                        ModuleGUI *gui);

static const GwyEnum withwhats[] = {
    { "with|_Mask:",         MARK_WITH_MASK,  },
    { "with|_Image:",        MARK_WITH_IMAGE, },
    { "with|_Presentation:", MARK_WITH_SHOW,  },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Creates or modifies a mask using other channels."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2009",
};

GWY_MODULE_QUERY2(module_info, mark_with)

static gboolean
module_register(void)
{
    gwy_process_func_register("mark_with",
                              (GwyProcessFunc)&mark_with,
                              N_("/_Mask/Mark _With..."),
                              GWY_STOCK_MARK_WITH,
                              MARK_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Mask combining and modification"));
    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum operations[] = {
        { N_("Se_t mask"),        MASK_EDIT_SET,       },
        { N_("_Add mask"),        MASK_EDIT_ADD,       },
        { N_("_Subtract mask"),   MASK_EDIT_REMOVE,    },
        { N_("_Intersect masks"), MASK_EDIT_INTERSECT, },
    };

    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_MARK_WITH, "mark_with", _("Mark with"),
                              withwhats, G_N_ELEMENTS(withwhats), MARK_WITH_MASK);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OPERATION, "operation", _("Operation"),
                              operations, G_N_ELEMENTS(operations), MASK_EDIT_SET);
    gwy_param_def_add_percentage(paramdef, PARAM_MIN, "min", _("_Minimum"), 0.0);
    gwy_param_def_add_percentage(paramdef, PARAM_MAX, "max", _("M_aximum"), 1.0);
    gwy_param_def_add_image_id(paramdef, PARAM_OPERAND_MASK, "operand_mask", NULL);
    gwy_param_def_add_image_id(paramdef, PARAM_OPERAND_IMAGE, "operand_image", NULL);
    gwy_param_def_add_image_id(paramdef, PARAM_OPERAND_SHOW, "operand_presentation", NULL);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
mark_with(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GQuark mquark;
    gint id;

    g_return_if_fail(runtype & MARK_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     0);
    g_return_if_fail(args.field && mquark);

    args.source = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.source), NULL);
    args.result = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());

    outcome = run_gui(&args, data, id);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome == GWY_DIALOG_PROCEED)
        execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    gwy_container_set_object(data, mquark, args.result);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    gwy_params_save_to_settings(args.params);
    g_object_unref(args.result);
    g_object_unref(args.source);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *vbox, *label, *vbox2, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    gchar *s;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();

    /* Store just something with correct dimensions there.  We need to update it according to what is selected. */
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/0/mask", args->source);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gwy_container_set_object_by_name(gui.data, "/1/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/1/mask", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 1, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Mark With"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    vbox = gwy_vbox_new(8);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    /* Source mask preview. */
    vbox2 = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), vbox2, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_HALF_SIZE, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox2), dataview, FALSE, FALSE, 0);

    label = gtk_label_new(_("Operand"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox2), label, FALSE, FALSE, 0);

    /* Result preview */
    vbox2 = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), vbox2, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 1, PREVIEW_HALF_SIZE, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox2), dataview, FALSE, FALSE, 0);

    label = gtk_label_new(_("Result"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox2), label, FALSE, FALSE, 0);

    vbox = gwy_vbox_new(8);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_OPERATION);

    gwy_param_table_append_separator(table);
    s = g_strconcat(_("Mark with"), ":", NULL);
    gwy_param_table_append_message(table, LABEL_MARK_WITH, s);
    g_free(s);
    gui.withwhat = gwy_radio_buttons_create(withwhats, G_N_ELEMENTS(withwhats), G_CALLBACK(with_what_selected), &gui,
                                            gwy_params_get_enum(args->params, PARAM_MARK_WITH));
    gui.sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    gwy_param_table_append_foreign(table, PARAM_OPERAND_MASK, create_operand_row_mask, &gui, NULL);
    gwy_param_table_append_foreign(table, PARAM_OPERAND_IMAGE, create_operand_row_image, &gui, NULL);
    gwy_param_table_append_foreign(table, PARAM_OPERAND_SHOW, create_operand_row_presentation, &gui, NULL);

    s = g_strconcat(_("Marked data range"), ":", NULL);
    gwy_param_table_append_message(table, LABEL_RANGE, s);
    g_free(s);
    gwy_param_table_append_slider(table, PARAM_MIN);
    gwy_param_table_slider_set_mapping(table, PARAM_MIN, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_MAX);
    gwy_param_table_slider_set_mapping(table, PARAM_MAX, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    /* The selectable mask colour is for the result.  Source is displayed as-is. */
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 1, data, id);

    gtk_box_pack_start(GTK_BOX(vbox), gwy_param_table_widget(gui.table), FALSE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, gui.table);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    ensure_something_is_selected(&gui);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.sizegroup);

    return outcome;
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    MaskEditMode operation = gwy_params_get_enum(params, PARAM_OPERATION);
    MarkWithWhat withwhat = gwy_params_get_enum(params, PARAM_MARK_WITH);
    gdouble min = gwy_params_get_double(params, PARAM_MIN);
    gdouble max = gwy_params_get_double(params, PARAM_MAX);
    gdouble data_min, data_max, d;
    GwyDataField *otherfield;

    /* XXX: This does not have to be done when just the operation changes.  But well… */
    otherfield = get_other_field(params, NULL, NULL, FALSE);
    if (withwhat == MARK_WITH_MASK)
        gwy_data_field_copy(otherfield, args->source, FALSE);
    else {
        gwy_data_field_get_min_max(otherfield, &data_min, &data_max);
        d = data_max - data_min;
        gwy_data_field_threshold_to(otherfield, args->source, data_min + d*min, data_min + d*max);
    }

    /* Simple cases. */
    if (!args->mask || operation == MASK_EDIT_SET) {
        if (operation == MASK_EDIT_SET || operation == MASK_EDIT_ADD)
            gwy_data_field_copy(args->source, args->result, FALSE);
        else
            gwy_data_field_clear(args->result);
        return;
    }

    /* Not so simple cases. */
    if (operation == MASK_EDIT_ADD)
        gwy_data_field_max_of_fields(args->result, args->mask, args->source);
    else if (operation == MASK_EDIT_INTERSECT)
        gwy_data_field_min_of_fields(args->result, args->mask, args->source);
    else if (operation == MASK_EDIT_REMOVE) {
        const gdouble *sdata, *odata;
        gdouble *mdata;
        gint i, n;

        n = gwy_data_field_get_xres(args->source) * gwy_data_field_get_yres(args->source);
        mdata = gwy_data_field_get_data(args->result);
        odata = gwy_data_field_get_data_const(args->mask);
        sdata = gwy_data_field_get_data_const(args->source);
        for (i = 0; i < n; i++)
            mdata[i] = MIN(odata[i], 1.0 - sdata[i]);
    }
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;
    MarkWithWhat withwhat = gwy_params_get_enum(params, PARAM_MARK_WITH);

    if (id < 0 || id == PARAM_MARK_WITH) {
        gwy_param_table_set_sensitive(table, LABEL_RANGE, withwhat != MARK_WITH_MASK);
        gwy_param_table_set_sensitive(table, PARAM_MIN, withwhat != MARK_WITH_MASK);
        gwy_param_table_set_sensitive(table, PARAM_MAX, withwhat != MARK_WITH_MASK);
    }
    if (id < 0) {
        gwy_param_table_set_sensitive(table, PARAM_OPERAND_MASK, gui->has_any[0]);
        gwy_param_table_set_sensitive(table, PARAM_OPERAND_IMAGE, gui->has_any[1]);
        gwy_param_table_set_sensitive(table, PARAM_OPERAND_SHOW, gui->has_any[2]);
    }

    if (id != PARAM_MASK_COLOR && id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyDataField *otherfield;
    GwyContainer *data;
    gint id;

    if (!(otherfield = get_other_field(params, &data, &id, TRUE)))
        return;
    execute(args);
    gwy_data_field_data_changed(args->source);
    gwy_data_field_data_changed(args->result);
    gwy_container_set_object_by_name(gui->data, "/0/data", otherfield);
    gwy_app_sync_data_items(data, gui->data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE,
                            0);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static GwyDataField*
get_other_field(GwyParams *params, GwyContainer **pdata, gint *id,
                gboolean base_field)
{
    MarkWithWhat withwhat = gwy_params_get_enum(params, PARAM_MARK_WITH);
    GwyContainer *data;
    GwyAppDataId dataid;
    GQuark quark;

    if (withwhat == MARK_WITH_MASK) {
        if (gwy_params_data_id_is_none(params, PARAM_OPERAND_MASK))
            return NULL;
        dataid = gwy_params_get_data_id(params, PARAM_OPERAND_MASK);
        quark = base_field ? gwy_app_get_data_key_for_id(dataid.id) : gwy_app_get_mask_key_for_id(dataid.id);
    }
    else if (withwhat == MARK_WITH_SHOW) {
        if (gwy_params_data_id_is_none(params, PARAM_OPERAND_SHOW))
            return NULL;
        dataid = gwy_params_get_data_id(params, PARAM_OPERAND_SHOW);
        quark = gwy_app_get_show_key_for_id(dataid.id);
    }
    else {
        if (gwy_params_data_id_is_none(params, PARAM_OPERAND_IMAGE))
            return NULL;
        dataid = gwy_params_get_data_id(params, PARAM_OPERAND_IMAGE);
        quark = gwy_app_get_data_key_for_id(dataid.id);
    }
    data = gwy_app_data_browser_get(dataid.datano);
    if (pdata)
        *pdata = data;
    if (id)
        *id = dataid.id;

    return gwy_container_get_object(data, quark);
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == GWY_RESPONSE_RESET)
        ensure_something_is_selected(gui);
}

static void
ensure_something_is_selected(ModuleGUI *gui)
{
    if (!get_other_field(gui->args->params, NULL, NULL, FALSE))
        gwy_radio_buttons_set_current(gui->withwhat, MARK_WITH_IMAGE);
}

static void
gwy_data_field_threshold_to(GwyDataField *source, GwyDataField *dest,
                            gdouble min, gdouble max)
{
    const gdouble *dsrc;
    gdouble *ddst;
    gint i, n;

    n = gwy_data_field_get_xres(source) * gwy_data_field_get_yres(source);
    dsrc = gwy_data_field_get_data_const(source);
    ddst = gwy_data_field_get_data(dest);

    if (min <= max) {
        for (i = 0; i < n; i++)
            ddst[i] = (dsrc[i] >= min && dsrc[i] <= max);
    }
    else {
        for (i = 0; i < n; i++)
            ddst[i] = (dsrc[i] >= min || dsrc[i] <= max);
    }
}

static GtkWidget*
create_operand_row(ModuleGUI *gui, gint i, gint param_id)
{
    static const GwyDataChooserFilterFunc filters[] = {
        operand_filter_mask, operand_filter_image, operand_filter_presentation,
    };
    GwyDataChooser *chooser;
    GwyAppDataId dataid;
    GtkWidget *hbox;
    GSList *l;

    l = g_slist_nth(gui->withwhat, i);
    hbox = gwy_hbox_new(6);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(l->data), FALSE, FALSE, 0);

    gui->operand[i] = gwy_data_chooser_new_channels();
    g_object_set_data(G_OBJECT(gui->operand[i]), "param-id", GINT_TO_POINTER(param_id));
    chooser = GWY_DATA_CHOOSER(gui->operand[i]);
    gtk_size_group_add_widget(gui->sizegroup, gui->operand[i]);
    gwy_data_chooser_set_filter(chooser, filters[i], gui, NULL);
    gui->has_any[i] = !!gwy_data_chooser_get_active_id(chooser, &dataid);
    if (!gwy_params_data_id_is_none(gui->args->params, param_id)) {
        dataid = gwy_params_get_data_id(gui->args->params, param_id);
        gwy_data_chooser_set_active_id(chooser, &dataid);
        gwy_data_chooser_get_active_id(chooser, &dataid);
    }
    gwy_params_set_image_id(gui->args->params, param_id, dataid);
    gtk_box_pack_end(GTK_BOX(hbox), gui->operand[i], FALSE, FALSE, 0);
    g_signal_connect(chooser, "changed", G_CALLBACK(operand_changed), gui);

    return hbox;
}

static void
operand_changed(GwyDataChooser *chooser, ModuleGUI *gui)
{
    gint param_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(chooser), "param-id"));
    GwyAppDataId dataid;
    gwy_data_chooser_get_active_id(chooser, &dataid);
    if (gwy_params_set_image_id(gui->args->params, param_id, dataid))
        gwy_param_table_param_changed(gui->table, param_id);
}

static GtkWidget*
create_operand_row_mask(gpointer user_data)
{
    return create_operand_row((ModuleGUI*)user_data, 0, PARAM_OPERAND_MASK);
}

static GtkWidget*
create_operand_row_image(gpointer user_data)
{
    return create_operand_row((ModuleGUI*)user_data, 1, PARAM_OPERAND_IMAGE);
}

static GtkWidget*
create_operand_row_presentation(gpointer user_data)
{
    return create_operand_row((ModuleGUI*)user_data, 2, PARAM_OPERAND_SHOW);
}

static gboolean
operand_filter(GwyContainer *source, GQuark quark, ModuleGUI *gui)
{
    GwyDataField *target_field = gui->args->field;
    GwyDataField *source_field;

    if (!gwy_container_gis_object(source, quark, &source_field))
        return FALSE;

    return !gwy_data_field_check_compatibility(source_field, target_field,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL);
}

static gboolean
operand_filter_mask(GwyContainer *source, gint id, gpointer user_data)
{
    return operand_filter(source, gwy_app_get_mask_key_for_id(id), (ModuleGUI*)user_data);
}

static gboolean
operand_filter_image(GwyContainer *source, gint id, gpointer user_data)
{
    return operand_filter(source, gwy_app_get_data_key_for_id(id), (ModuleGUI*)user_data);
}

static gboolean
operand_filter_presentation(GwyContainer *source, gint id, gpointer user_data)
{
    return operand_filter(source, gwy_app_get_show_key_for_id(id), (ModuleGUI*)user_data);
}

static void
with_what_selected(G_GNUC_UNUSED GtkRadioButton *button, ModuleGUI *gui)
{
    if (gwy_params_set_enum(gui->args->params, PARAM_MARK_WITH, gwy_radio_buttons_get_current(gui->withwhat)))
        gwy_param_table_param_changed(gui->table, PARAM_MARK_WITH);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
