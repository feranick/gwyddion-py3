/*
 *  $Id: volume_arithmetic.c 24708 2022-03-21 17:14:46Z yeti-dn $
 *  Copyright (C) 2018-2019 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyexpr.h>
#include <libprocess/brick.h>
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "../process/preview.h"

#define ARITH_RUN_MODES GWY_RUN_INTERACTIVE

enum {
    NARGS = 8,
    HISTSIZE = 96,
    USER_UNITS_ID = G_MAXINT,
};

enum {
    SENS_EXPR_OK = 1 << 0,
    SENS_USERUINTS = 1 << 1
};

enum {
    COMMON_COORD_X = 0,
    COMMON_COORD_Y = 1,
    COMMON_COORD_Z = 2,
    COMMON_COORD_ZCAL = 3,
    COMMON_COORD_NCOORDS
};

enum {
    ARITHMETIC_NARGS = NARGS + COMMON_COORD_NCOORDS
};

enum {
    ARITHMETIC_OK      = 0,
    ARITHMETIC_DATA    = 1,
    ARITHMETIC_EXPR    = 2,
    ARITHMETIC_NUMERIC = 4
};

typedef struct {
    GwyExpr *expr;
    gchar *expression;
    gint dataunits;
    gchar *userunits;
    GtkTreeModel *history;
    guint err;
    GwyAppDataId objects[NARGS];
    gchar *name[ARITHMETIC_NARGS];
    guint pos[ARITHMETIC_NARGS];
    GPtrArray *ok_masks;
} ArithmeticArgs;

typedef struct {
    ArithmeticArgs *args;
    GtkWidget *dialog;
    GtkWidget *view;
    GtkWidget *expression;
    GtkWidget *userunits;
    GtkWidget *userunits_label;
    GtkWidget *result;
    GtkWidget *data[NARGS];
    GSList *dataunits;
    GwyContainer *mydata;
} ArithmeticControls;

static gboolean     module_register              (void);
static void         arithmetic                   (GwyContainer *data,
                                                  GwyRunType run);
static void         arithmetic_load_args         (GwyContainer *settings,
                                                  ArithmeticArgs *args);
static void         arithmetic_save_args         (GwyContainer *settings,
                                                  ArithmeticArgs *args);
static gboolean     arithmetic_dialog            (GwyContainer *data,
                                                  gint id,
                                                  ArithmeticArgs *args);
static void         arithmetic_data_chosen       (GwyDataChooser *chooser,
                                                  ArithmeticControls *controls);
static void         arithmetic_expr_changed      (GtkWidget *entry,
                                                  ArithmeticControls *controls);
static void         arithmetic_userunits_changed (GtkEntry *entry,
                                                  ArithmeticControls *controls);
static void         arithmetic_dataunits_selected(ArithmeticControls *controls);
static void         arithmetic_show_state        (ArithmeticControls *controls,
                                                  const gchar *message);
static const gchar* arithmetic_check_bricks      (ArithmeticArgs *args);
static void         arithmetic_preview           (ArithmeticControls *controls);
static GwyBrick*    arithmetic_do                (ArithmeticArgs *args,
                                                  gint *id);
static void         arithmetic_need_data         (const ArithmeticArgs *args,
                                                  gboolean *need_data);
static void         arithmetic_update_history    (ArithmeticArgs *args);
static GwyBrick*    make_x                       (GwyBrick *brick);
static GwyBrick*    make_y                       (GwyBrick *brick);
static GwyBrick*    make_z                       (GwyBrick *brick);
static GwyBrick*    make_zcal                    (GwyBrick *brick,
                                                  GwyDataLine *zcal);

static const gchar default_units[] = "";
static const gchar default_expression[] = "d1 - d2";

static GwyAppDataId object_ids[NARGS];

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Simple arithmetic operations with volume data."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, volume_arithmetic)

static gboolean
module_register(void)
{
    guint i;

    for (i = 0; i < NARGS; i++) {
        object_ids[i].datano = 0;
        object_ids[i].id = -1;
    }
    gwy_volume_func_register("volume_arithmetic",
                             (GwyVolumeFunc)&arithmetic,
                             N_("/_Arithmetic..."),
                             GWY_STOCK_VOLUME_ARITHMETIC,
                             ARITH_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Arithmetic operations on volume data"));

    return TRUE;
}

void
arithmetic(GwyContainer *data, GwyRunType run)
{
    ArithmeticArgs args;
    guint i;
    GwyContainer *settings;
    gboolean dorun;
    gint datano, id, newid;

    g_return_if_fail(run & ARITH_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_BRICK_ID, &id,
                                     GWY_APP_CONTAINER_ID, &datano,
                                     0);

    args.objects[0].datano = datano;
    args.objects[0].id = id;

    settings = gwy_app_settings_get();
    arithmetic_load_args(settings, &args);
    args.ok_masks = g_ptr_array_new();
    args.expr = gwy_expr_new();

    gwy_expr_define_constant(args.expr, "pi", G_PI, NULL);
    gwy_expr_define_constant(args.expr, "π", G_PI, NULL);

    dorun = arithmetic_dialog(data, id, &args);
    arithmetic_save_args(settings, &args);
    if (dorun) {
        GwyBrick *result = arithmetic_do(&args, &id);

        newid = gwy_app_data_browser_add_brick(result, NULL, data, TRUE);
        g_object_unref(result);
        gwy_app_set_brick_title(data, newid, _("Calculated"));
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                0);
        gwy_app_volume_log_add_volume(data, -1, newid);
    }
    g_ptr_array_free(args.ok_masks, TRUE);
    gwy_expr_free(args.expr);
    for (i = 0; i < ARITHMETIC_NARGS; i++)
        g_free(args.name[i]);
}

static gboolean
arithmetic_dialog(GwyContainer *data,
                  gint id,
                  ArithmeticArgs *args)
{
    GtkWidget *dialog, *hbox, *hbox2, *vbox, *table, *chooser, *entry,
              *label, *button;
    ArithmeticControls controls;
    GwyDataField *dfield;
    GQuark quark;
    guint i, row, response;
    const guchar *gradient;
    gchar *s;

    controls.args = args;

    dialog = gtk_dialog_new_with_buttons(_("Arithmetic"), NULL, 0, NULL);
    controls.dialog = dialog;
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                                 gwy_stock_like_button_new(_("_Update"),
                                                           GTK_STOCK_EXECUTE),
                                 RESPONSE_PREVIEW);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_volume_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);

    hbox = gtk_hbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    vbox = gtk_vbox_new(FALSE, 4);
    /* Ensure no wild changes of the dialog size due to non-square data. */
    gtk_widget_set_size_request(vbox, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 4);

    quark = gwy_app_get_brick_preview_key_for_id(id);
    dfield = gwy_container_get_object(data, quark);
    dfield = gwy_data_field_new_alike(dfield, TRUE);
    controls.mydata = gwy_container_new();
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    g_object_unref(dfield);

    quark = gwy_app_get_brick_palette_key_for_id(id);
    if (gwy_container_gis_string(data, quark, &gradient)) {
        gwy_container_set_const_string_by_name(controls.mydata,
                                               "/0/base/palette", gradient);
    }

    controls.view = gwy_create_preview(controls.mydata, 0, PREVIEW_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), controls.view, FALSE, FALSE, 0);

    table = gtk_table_new(5 + NARGS, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 4);
    row = 0;

    label = gtk_label_new_with_mnemonic(_("_Expression:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);
    row++;

    entry = gtk_combo_box_entry_new_with_model(args->history, 0);
    controls.expression = entry;
    gtk_combo_box_set_active(GTK_COMBO_BOX(controls.expression), 0);
    gtk_table_attach(GTK_TABLE(table), entry, 0, 3, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect(entry, "changed",
                     G_CALLBACK(arithmetic_expr_changed), &controls);
    g_signal_connect_swapped(gtk_bin_get_child(GTK_BIN(entry)), "activate",
                             G_CALLBACK(arithmetic_preview), &controls);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    row++;

    controls.result = label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);
    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    label = gtk_label_new(_("Operands"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Units"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, row, row+1,
                     GTK_FILL, 0, 0, 0);
    row++;

    controls.dataunits = NULL;
    for (i = 0; i < NARGS; i++) {
        /* VALUE is 0 */
        args->name[i] = g_strdup_printf("d_%d", i+1);
        label = gtk_label_new_with_mnemonic(args->name[i]);
        gwy_strkill(args->name[i], "_");
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
        gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row+1,
                         GTK_FILL, 0, 0, 0);

        chooser = gwy_data_chooser_new_volumes();
        gwy_data_chooser_set_active_id(GWY_DATA_CHOOSER(chooser),
                                       args->objects + i);
        g_signal_connect(chooser, "changed",
                         G_CALLBACK(arithmetic_data_chosen), &controls);
        g_object_set_data(G_OBJECT(chooser), "index", GUINT_TO_POINTER(i));
        gtk_table_attach(GTK_TABLE(table), chooser, 1, 2, row, row+1,
                         GTK_EXPAND | GTK_FILL, 0, 0, 0);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), chooser);
        controls.data[i] = chooser;

        button = gtk_radio_button_new(controls.dataunits);
        controls.dataunits
            = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
        gwy_radio_button_set_value(button, i);
        s = g_strdup_printf(_("Take result units from data d%d"), i+1);
        gtk_widget_set_tooltip_text(button, s);
        g_free(s);
        gtk_table_attach(GTK_TABLE(table), button, 2, 3, row, row+1,
                         0, 0, 0, 0);
        g_signal_connect_swapped(button, "clicked",
                                 G_CALLBACK(arithmetic_dataunits_selected),
                                 &controls);

        row++;
    }
    args->name[NARGS + 0] = g_strdup("x");
    args->name[NARGS + 1] = g_strdup("y");
    args->name[NARGS + 2] = g_strdup("z");
    args->name[NARGS + 3] = g_strdup("zcal");

    hbox2 = gtk_hbox_new(FALSE, 6);
    gtk_table_attach(GTK_TABLE(table), hbox2, 0, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);

    label = gtk_label_new_with_mnemonic(_("Specify un_its:"));
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, FALSE, 0);
    controls.userunits_label = label;
    gtk_widget_set_sensitive(controls.userunits_label,
                             args->dataunits == USER_UNITS_ID);

    controls.userunits = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(controls.userunits), args->userunits);
    gtk_box_pack_start(GTK_BOX(hbox2), controls.userunits, TRUE, TRUE, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.userunits);
    gtk_widget_set_sensitive(controls.userunits,
                             args->dataunits == USER_UNITS_ID);
    g_signal_connect(controls.userunits, "changed",
                     G_CALLBACK(arithmetic_userunits_changed), &controls);

    button = gtk_radio_button_new(controls.dataunits);
    controls.dataunits = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    gwy_radio_button_set_value(button, USER_UNITS_ID);
    gtk_widget_set_tooltip_text(button, _("Specify result units explicitly"));
    gtk_table_attach(GTK_TABLE(table), button, 2, 3, row, row+1,
                     0, 0, 0, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(arithmetic_dataunits_selected),
                             &controls);
    row++;

    gtk_widget_grab_focus(controls.expression);
    gtk_widget_show_all(dialog);
    gwy_radio_buttons_set_current(controls.dataunits, args->dataunits);
    arithmetic_expr_changed(entry, &controls);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            return FALSE;
            break;

            case RESPONSE_PREVIEW:
            arithmetic_preview(&controls);
            break;

            case GTK_RESPONSE_OK:
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
arithmetic_data_chosen(GwyDataChooser *chooser,
                       ArithmeticControls *controls)
{
    ArithmeticArgs *args;
    guint i;

    args = controls->args;
    i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(chooser), "index"));
    gwy_data_chooser_get_active_id(chooser, args->objects + i);
    if (!(args->err & ARITHMETIC_EXPR))
        arithmetic_show_state(controls, NULL);
}

static void
arithmetic_expr_changed(GtkWidget *entry,
                        ArithmeticControls *controls)
{
    ArithmeticArgs *args;
    GtkComboBox *combo;
    GError *error = NULL;
    const gchar *message = NULL;
    gchar *s = NULL;

    combo = GTK_COMBO_BOX(entry);
    args = controls->args;
    g_free(args->expression);
    args->expression = g_strdup(gtk_combo_box_get_active_text(combo));
    args->err = ARITHMETIC_OK;

    if (gwy_expr_compile(args->expr, args->expression, &error)) {
        guint nvars = gwy_expr_get_variables(args->expr, NULL);
        g_return_if_fail(nvars);
        if (nvars == 1) {
            gdouble v = gwy_expr_execute(args->expr, NULL);
            message = s = g_strdup_printf("%g", v);
            args->err = ARITHMETIC_NUMERIC;
        }
        else {
            if (gwy_expr_resolve_variables(args->expr, ARITHMETIC_NARGS,
                                           (const gchar*const*)args->name,
                                           args->pos)) {
                args->err = ARITHMETIC_EXPR;
                message = _("Expression contains unknown identifiers");
            }
        }
    }
    else {
        args->err = ARITHMETIC_EXPR;
        message = error->message;
    }

    arithmetic_show_state(controls, message);
    g_clear_error(&error);
    g_free(s);
}

static void
arithmetic_userunits_changed(GtkEntry *entry,
                             ArithmeticControls *controls)
{
    g_free(controls->args->userunits);
    controls->args->userunits = g_strdup(gtk_entry_get_text(entry));
}

static void
arithmetic_dataunits_selected(ArithmeticControls *controls)
{
    ArithmeticArgs *args = controls->args;
    args->dataunits = gwy_radio_buttons_get_current(controls->dataunits);
    gtk_widget_set_sensitive(controls->userunits,
                             args->dataunits == USER_UNITS_ID);
    gtk_widget_set_sensitive(controls->userunits_label,
                             args->dataunits == USER_UNITS_ID);
}

static void
arithmetic_show_state(ArithmeticControls *controls,
                      const gchar *message)
{
    ArithmeticArgs *args = controls->args;
    GtkDialog *dialog = GTK_DIALOG(controls->dialog);
    gboolean ok;

    if (!message && args->err != ARITHMETIC_NUMERIC) {
        message = arithmetic_check_bricks(args);
        if (args->err)
            gtk_label_set_text(GTK_LABEL(controls->result), message);
        else
            gtk_label_set_text(GTK_LABEL(controls->result), "");
    }
    else {
        if (message)
            gtk_label_set_text(GTK_LABEL(controls->result), message);
    }

    ok = (args->err == ARITHMETIC_OK);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_OK, ok);
    gtk_dialog_set_response_sensitive(dialog, RESPONSE_PREVIEW, ok);

    if (ok)
        set_widget_as_ok_message(controls->result);
    else
        set_widget_as_error_message(controls->result);
}

static const gchar*
arithmetic_check_bricks(ArithmeticArgs *args)
{
    guint first = 0, i;
    GwyContainer *data;
    GQuark quark;
    GwyBrick *bfirst, *brick;
    GwyDataCompatibilityFlags diff;
    gboolean need_data[NARGS];

    if (args->err & (ARITHMETIC_EXPR | ARITHMETIC_NUMERIC))
        return NULL;

    arithmetic_need_data(args, need_data);

    for (i = 0; i < NARGS; i++) {
        if (need_data[i]) {
            first = i;
            break;
        }
    }
    if (i == NARGS) {
        /* no variables */
        args->err &= ~ARITHMETIC_DATA;
        return NULL;
    }

    /* each window must match with first, this is transitive */
    data = gwy_app_data_browser_get(args->objects[first].datano);
    g_return_val_if_fail(data, NULL);
    quark = gwy_app_get_brick_key_for_id(args->objects[first].id);
    bfirst = GWY_BRICK(gwy_container_get_object(data, quark));
    for (i = first+1; i < NARGS; i++) {
        if (!need_data[i])
            continue;

        data = gwy_app_data_browser_get(args->objects[i].datano);
        g_return_val_if_fail(data, NULL);
        quark = gwy_app_get_brick_key_for_id(args->objects[i].id);
        brick = GWY_BRICK(gwy_container_get_object(data, quark));

        diff = gwy_brick_check_compatibility(bfirst, brick,
                                             GWY_DATA_COMPATIBILITY_RES
                                             | GWY_DATA_COMPATIBILITY_REAL
                                             | GWY_DATA_COMPATIBILITY_LATERAL
                                             | GWY_DATA_COMPATIBILITY_AXISCAL);
        if (diff) {
            args->err |= ARITHMETIC_DATA;
            if (diff & GWY_DATA_COMPATIBILITY_RES)
                return _("Pixel dimensions differ");
            if (diff & GWY_DATA_COMPATIBILITY_LATERAL)
                return _("Lateral dimensions are different physical "
                         "quantities");
            if (diff & GWY_DATA_COMPATIBILITY_REAL)
                return _("Physical dimensions differ");
            if (diff & GWY_DATA_COMPATIBILITY_AXISCAL)
                return _("Z-axis calibrations differ");
        }
    }

    args->err &= ~ARITHMETIC_DATA;
    return NULL;
}

static void
arithmetic_preview(ArithmeticControls *controls)
{
    GwyBrick *result;
    GwyDataField *dfield;
    gint id = -1;

    /* We can also get here by activation of the entry so check again. */
    if (controls->args->err != ARITHMETIC_OK)
        return;

    result = arithmetic_do(controls->args, &id);
    g_return_if_fail(result);

    dfield = gwy_container_get_object_by_name(controls->mydata, "/0/data");
    gwy_brick_mean_xy_plane(result, dfield);
    /* FIXME: This is a bit wasteful.  We should keep the result and use it
     * if the user does not change the expression. */
    g_object_unref(result);
    gwy_data_field_data_changed(dfield);
    gwy_set_data_preview_size(GWY_DATA_VIEW(controls->view), PREVIEW_SIZE);
}

static GwyBrick*
arithmetic_do(ArithmeticArgs *args, gint *id)
{
    GwyContainer *data;
    GwySIUnit *unit, *unit2;
    GQuark quark;
    GwyBrick **data_bricks, *brick, *result = NULL;
    GwyDataLine *zcal = NULL;
    const gdouble **d;
    gboolean need_data[NARGS];
    gdouble *r = NULL;
    gboolean first = TRUE;
    guint n = 0, i;

    g_return_val_if_fail(args->err == ARITHMETIC_OK, NULL);

    arithmetic_need_data(args, need_data);
    /* We know the expression can't contain more variables */
    data_bricks = g_new0(GwyBrick*, ARITHMETIC_NARGS);
    d = g_new0(const gdouble*, ARITHMETIC_NARGS + 1);
    d[0] = NULL;

    /* First get all the data bricks we directly have */
    for (i = 0; i < NARGS; i++) {
        gwy_debug("brick[%u]: %s", i, need_data[i] ? "NEEDED" : "not needed");
        if (!need_data[i])
            continue;

        data = gwy_app_data_browser_get(args->objects[i].datano);
        g_return_val_if_fail(data, NULL);
        quark = gwy_app_get_brick_key_for_id(args->objects[i].id);
        brick = GWY_BRICK(gwy_container_get_object(data, quark));
        if (!i)
            zcal = gwy_brick_get_zcalibration(brick);

        data_bricks[i] = brick;
        d[args->pos[i]] = gwy_brick_get_data_const(brick);
        gwy_debug("d[%u] set to PRIMARY %u", args->pos[i], i);
        if (first) {
            first = FALSE;
            n = (gwy_brick_get_xres(brick)
                 * gwy_brick_get_yres(brick)
                 * gwy_brick_get_zres(brick));
            result = gwy_brick_new_alike(brick, FALSE);
            r = gwy_brick_get_data(result);
            *id = args->objects[i].id;
        }
    }

    /* Then create the common coordinate bricks if we have to. */
    i = NARGS + COMMON_COORD_X;
    if (args->pos[i]) {
        brick = make_x(data_bricks[0]);
        data_bricks[i] = brick;
        d[args->pos[i]] = gwy_brick_get_data_const(brick);
    }

    i = NARGS + COMMON_COORD_Y;
    if (args->pos[i]) {
        brick = make_y(data_bricks[0]);
        data_bricks[i] = brick;
        d[args->pos[i]] = gwy_brick_get_data_const(brick);
    }

    i = NARGS + COMMON_COORD_Z;
    if (args->pos[i]) {
        brick = make_z(data_bricks[0]);
        data_bricks[i] = brick;
        d[args->pos[i]] = gwy_brick_get_data_const(brick);
    }

    i = NARGS + COMMON_COORD_ZCAL;
    if (args->pos[i]) {
        if (zcal)
            brick = make_zcal(data_bricks[0], zcal);
        else
            brick = make_z(data_bricks[0]);

        data_bricks[i] = brick;
        d[args->pos[i]] = gwy_brick_get_data_const(brick);
    }

    /* Execute */
    gwy_expr_vector_execute(args->expr, n, d, r);

    /* Set units. */
    unit = gwy_brick_get_si_unit_w(result);
    if (args->dataunits == USER_UNITS_ID)
        gwy_si_unit_set_from_string(unit, args->userunits);
    else {
        i = args->dataunits;
        if (!(brick = data_bricks[i])) {
            data = gwy_app_data_browser_get(args->objects[i].datano);
            g_return_val_if_fail(data, NULL);
            quark = gwy_app_get_brick_key_for_id(args->objects[i].id);
            brick = GWY_BRICK(gwy_container_get_object(data, quark));
        }
        unit2 = gwy_brick_get_si_unit_w(brick);
        gwy_si_unit_assign(unit, unit2);
    }

    /* Free stuff */
    for (i = NARGS; i < ARITHMETIC_NARGS; i++) {
        if (data_bricks[i])
            g_object_unref(data_bricks[i]);
    }
    g_free(data_bricks);
    g_free(d);

    return result;
}

/* Find which data we need. */
static void
arithmetic_need_data(const ArithmeticArgs *args,
                     gboolean *need_data)
{
    guint i;

    gwy_clear(need_data, NARGS);
    for (i = 0; i < NARGS; i++) {
        if (args->pos[i])
            need_data[i] = TRUE;
    }

    // When x and y are needed, always take them from field 1.  This also
    // ensures the expression is considered to be a field expression.
    for (i = 0; i < COMMON_COORD_NCOORDS; i++) {
        if (args->pos[NARGS + i]) {
            need_data[0] = TRUE;
            break;
        }
    }
}

static GwyBrick*
make_x(GwyBrick *brick)
{
    GwyBrick *result;
    gdouble dx, xoff;
    guint xres, yres, zres, i, j;
    gdouble *data;

    result = gwy_brick_new_alike(brick, FALSE);
    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);
    dx = gwy_brick_get_xreal(brick)/xres;
    xoff = gwy_brick_get_xoffset(brick);
    data = gwy_brick_get_data(result);

    for (j = 0; j < xres; j++)
        data[j] = (j + 0.5)*dx + xoff;

    for (i = 1; i < yres*zres; i++)
        gwy_assign(data + i*xres, data, xres);

    return result;
}

static GwyBrick*
make_y(GwyBrick *brick)
{
    GwyBrick *result;
    gdouble dy, yoff;
    guint xres, yres, zres, i, j, k;
    gdouble *data;

    result = gwy_brick_new_alike(brick, FALSE);
    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);
    dy = gwy_brick_get_yreal(brick)/yres;
    yoff = gwy_brick_get_yoffset(brick);
    data = gwy_brick_get_data(result);

    for (k = 0; k < zres; k++) {
        for (i = 0; i < yres; i++) {
            gdouble y = (i + 0.5)*dy + yoff;
            gdouble *rrow = data + (k*yres + i)*xres;
            for (j = 0; j < xres; j++, rrow++)
                *rrow = y;
        }
    }

    return result;
}

static GwyBrick*
make_z(GwyBrick *brick)
{
    GwyBrick *result;
    gdouble dz, zoff;
    guint xres, yres, zres, i, j;
    gdouble *data;

    result = gwy_brick_new_alike(brick, FALSE);
    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);
    dz = gwy_brick_get_zreal(brick)/zres;
    zoff = gwy_brick_get_yoffset(brick);
    data = gwy_brick_get_data(result);

    for (i = 0; i < zres; i++) {
        gdouble *rrow = data + i*xres*yres;
        gdouble z = (i + 0.5)*dz + zoff;
        for (j = 0; j < xres*yres; j++)
            *rrow = z;
    }

    return result;
}

static GwyBrick*
make_zcal(GwyBrick *brick, GwyDataLine *zcal)
{
    GwyBrick *result;
    guint xres, yres, zres, i, j;
    gdouble *data;
    const gdouble *zd;

    result = gwy_brick_new_alike(brick, FALSE);
    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);
    data = gwy_brick_get_data(result);

    g_return_val_if_fail(GWY_IS_DATA_LINE(zcal), brick);
    g_return_val_if_fail(gwy_data_line_get_res(zcal) == xres, brick);
    zd = gwy_data_line_get_data_const(zcal);

    for (i = 0; i < zres; i++) {
        gdouble *rrow = data + i*xres*yres;
        gdouble z = zd[i];
        for (j = 0; j < xres*yres; j++)
            *rrow = z;
    }

    return result;
}

static void
arithmetic_update_history(ArithmeticArgs *args)
{
    GtkListStore *store;
    GtkTreeIter iter;
    gchar *s;

    if (!*args->expression)
        return;

    store = GTK_LIST_STORE(args->history);
    gtk_list_store_prepend(store, &iter);
    gtk_list_store_set(store, &iter, 0, args->expression, -1);

    while (gtk_tree_model_iter_next(args->history, &iter)) {
        gtk_tree_model_get(args->history, &iter, 0, &s, -1);
        if (gwy_strequal(s, args->expression)) {
            gtk_list_store_remove(store, &iter);
            break;
        }
    }
}

static const gchar expression_key[] = "/module/volume_arithmetic/expression";
static const gchar dataunits_key[]  = "/module/volume_arithmetic/dataunits";
static const gchar userunits_key[]  = "/module/volume_arithmetic/userunits";

static void
arithmetic_load_args(GwyContainer *settings,
                     ArithmeticArgs *args)
{
    GtkListStore *store;
    const guchar *str;
    gchar *buffer, *line, *p;
    gsize size;
    guint i;

    str = default_expression;
    gwy_container_gis_string_by_name(settings, expression_key, &str);
    args->expression = g_strdup(str);

    str = default_units;
    gwy_container_gis_string_by_name(settings, userunits_key, &str);
    args->userunits = g_strdup(str);

    args->dataunits = 0;
    gwy_container_gis_int32_by_name(settings, dataunits_key, &args->dataunits);

    store = gtk_list_store_new(1, G_TYPE_STRING);
    args->history = GTK_TREE_MODEL(store);

    if (gwy_module_data_load("volume_arithmetic", "history", &buffer, &size,
                             NULL)) {
        p = buffer;
        for (line = gwy_str_next_line(&p); line; line = gwy_str_next_line(&p)) {
            GtkTreeIter iter;

            g_strstrip(line);
            if (*line) {
                gtk_list_store_append(store, &iter);
                gtk_list_store_set(store, &iter, 0, line, -1);
            }
        }
        g_free(buffer);
    }

    for (i = 1; i < NARGS; i++) {
        args->objects[i] = object_ids[i];
        /* Init to d1 instead of ‘none’ when we lose the bricks. */
        if (!gwy_app_data_id_verify_channel(args->objects + i))
            args->objects[i] = args->objects[0];
    }

    /* Ensures args->expression comes first */
    arithmetic_update_history(args);
}

static void
arithmetic_save_args(GwyContainer *settings,
                     ArithmeticArgs *args)
{
        GtkTreeIter iter;
        guint i = 0;
        gchar *s;
    FILE *fh;

    gwy_assign(object_ids, args->objects, NARGS);

    gwy_container_set_string_by_name(settings, expression_key,
                                     args->expression);
    gwy_container_set_string_by_name(settings, userunits_key, args->userunits);
    gwy_container_set_int32_by_name(settings,  dataunits_key, args->dataunits);

    if (!(fh = gwy_module_data_fopen("volume_arithmetic", "history", "w",
                                     NULL)))
        return;

    if (gtk_tree_model_get_iter_first(args->history, &iter)) {
        do {
            gtk_tree_model_get(args->history, &iter, 0, &s, -1);
            fputs(s, fh);
            fputc('\n', fh);
            g_free(s);
        } while (++i < HISTSIZE
                 && gtk_tree_model_iter_next(args->history, &iter));
    }
    fclose(fh);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
