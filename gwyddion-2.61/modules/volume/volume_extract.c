/*
 *  $Id: volume_extract.c 21135 2018-06-12 13:58:33Z yeti-dn $
 *  Copyright (C) 2013 David Necas (Yeti), Petr Klapetek.
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
#include <cairo.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/brick.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwylayer-mask.h>
#include <libgwydgets/gwyoptionmenus.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>

#define EXTRACT_RUN_MODES (GWY_RUN_INTERACTIVE)
#define MAXPIX 600
#define MAXSIMPLIFY 5

enum {
    PREVIEW_SIZE = 400,
    MAX_LENGTH = 1024
};

enum {
    RESPONSE_RESET   = 1,
    RESPONSE_PREVIEW = 2
};

typedef struct {
    const gchar *gradient;
    gboolean perspective;
    gboolean update;
    gdouble size;
    gdouble zscale;
    gdouble opacity;
    gdouble threshold;
} ExtractArgs;

typedef struct {
    ExtractArgs *args;
    GtkWidget *dialog;
    GtkObject *size;
    GtkObject *zscale;
    GtkObject *opacity;
    GtkWidget *drawarea;
    GtkWidget *perspective;
    GtkWidget *update;
    GtkObject *threshold;
    GtkWidget *gradient;
    GwyContainer *mydata;
    GwyContainer *data;
    GwyBrick *brick;
    gboolean in_init;
    gdouble rpx;
    gdouble rpy;
    gdouble rm[3][3];
    gdouble *px;
    gdouble *py;
    gdouble *pz;
    gdouble *ps;
    gdouble *wpx;
    gdouble *wpy;
    gdouble *wpz;
    gdouble xangle;
    gdouble yangle;
    gdouble zangle;
    gdouble bwidth;
    gdouble bheight;
    gdouble bdepth;
    gdouble brick_min;
    gdouble brick_max;
    gint nps;
    gboolean in_move;
    gboolean render_now;
    gboolean opdata_valid;
    gboolean image_valid;
    gdouble *opdata;
    cairo_surface_t *image;
    guint timeout;
} ExtractControls;

static gboolean module_register               (void);
static void     extract                       (GwyContainer *data,
                                               GwyRunType run);
static void     extract_dialog                (ExtractArgs *args,
                                               GwyContainer *data,
                                               GwyBrick *brick,
                                               gint id);
static void     extract_dialog_update_controls(ExtractControls *controls,
                                               ExtractArgs *args);
static void     extract_dialog_update_values  (ExtractControls *controls,
                                               ExtractArgs *args);
static void     extract_invalidate            (ExtractControls *controls);
static void     preview                       (ExtractControls *controls,
                                               ExtractArgs *args);
static void     extract_load_args             (GwyContainer *container,
                                               ExtractArgs *args);
static void     extract_save_args             (GwyContainer *container,
                                               ExtractArgs *args);
static void     p3d_build                     (ExtractControls *controls,
                                               ExtractArgs *args);
static void     p3d_prepare_wdata             (ExtractControls *controls,
                                               ExtractArgs *args);
static void     update_changed                (ExtractControls *controls);
static void     extract_zscale                (ExtractControls *controls,
                                               GtkAdjustment *adj);
static void     extract_opacity               (ExtractControls *controls,
                                               GtkAdjustment *adj);
static void     extract_threshold             (ExtractControls *controls,
                                               GtkAdjustment *adj);
static gboolean p3d_expose                    (GtkWidget *widget,
                                               GdkEventExpose *event,
                                               ExtractControls *controls);
static gboolean p3d_clicked                   (GtkWidget *widget,
                                               GdkEventButton *event,
                                               ExtractControls *controls);
static gboolean p3d_released                  (GtkWidget *widget,
                                               GdkEventButton *event,
                                               ExtractControls *controls);
static gboolean p3d_moved                     (GtkWidget *widget,
                                               GdkEventMotion *event,
                                               ExtractControls *controls);
static void     p3d_xview                     (ExtractControls *controls);
static void     p3d_yview                     (ExtractControls *controls);
static void     p3d_zview                     (ExtractControls *controls);
static void     p3d_up                        (ExtractControls *controls);
static void     p3d_down                      (ExtractControls *controls);
static void     p3d_left                      (ExtractControls *controls);
static void     p3d_right                     (ExtractControls *controls);
static void     p3d_stop                      (ExtractControls *controls);
static void     perspective_changed           (ExtractControls *controls,
                                               GtkToggleButton *toggle);
static void     gradient_changed              (GtkTreeSelection *selection,
                                               ExtractControls *controls);
static void     p3d_set_axes                  (ExtractControls *controls);
static void     p3d_add_wireframe             (ExtractControls *controls);
static void     rotate                        (ExtractControls *controls,
                                               gdouble x,
                                               gdouble y,
                                               gdouble z);
static void     save_image                    (ExtractControls *controls,
                                               ExtractArgs *args);
static void     rotatem                       (ExtractControls *controls);


static const ExtractArgs extract_defaults = {
    GWY_GRADIENT_DEFAULT,
    TRUE,
    FALSE,
    50,
    100,
    50,
    0.5,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Shows 3D representations of volume data"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2013",
};

GWY_MODULE_QUERY2(module_info, volume_extract)

static gboolean
module_register(void)
{
    gwy_volume_func_register("extract",
                             (GwyVolumeFunc)&extract,
                             N_("/3D View..."),
                             NULL,
                             EXTRACT_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Show a 3D view for the volume data"));

    return TRUE;
}

static void
extract(GwyContainer *data, GwyRunType run)
{
    ExtractArgs args;
    GwyBrick *brick = NULL;
    gint id;

    g_return_if_fail(run & EXTRACT_RUN_MODES);

    extract_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    extract_dialog(&args, data, brick, id);
}

static void
extract_dialog(ExtractArgs *args,
               GwyContainer *data,
               GwyBrick *brick,
               gint id)
{
    GtkWidget *dialog, *table, *hbox, *button, *hbox2, *table2, *scwin;
    GtkTreeSelection *selection;
    GtkTreePath *path;
    GtkTreeIter iter;
    GtkTreeModel *model;
    GwyDataField *dfield;
    ExtractControls controls;
    gint response;
    gint row;
    gboolean temp;

    g_return_if_fail(GWY_IS_BRICK(brick));

    gwy_clear(&controls, 1);
    controls.in_init = TRUE;
    controls.args = args;
    controls.data = data;
    controls.brick = brick;
    controls.rm[0][0] = controls.rm[1][1] = controls.rm[2][2] = 1;
    controls.rm[1][0] = controls.rm[2][0] = controls.rm[0][1] = controls.rm[0][2] = 0;
    controls.rm[1][2] = controls.rm[2][1] = 0;
    controls.px = NULL;
    controls.py = NULL;
    controls.pz = NULL;
    controls.ps = NULL;
    controls.wpx = NULL;
    controls.wpy = NULL;
    controls.wpz = NULL;
    controls.xangle = controls.yangle = controls.zangle = 0;
    controls.nps = 0;
    controls.in_move = FALSE;
    controls.image = NULL;
    controls.render_now = FALSE;
    controls.opdata = g_new(gdouble, PREVIEW_SIZE*PREVIEW_SIZE);
    controls.image = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                PREVIEW_SIZE, PREVIEW_SIZE);

    controls.brick_min = gwy_brick_get_min(controls.brick);
    controls.brick_max = gwy_brick_get_max(controls.brick);

    /*dialogue controls*/

    dialog = gtk_dialog_new_with_buttons(_("Volume data"), NULL, 0, NULL);

    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                                 gwy_stock_like_button_new(_("_Render"),
                                 GTK_STOCK_EXECUTE),
                                 RESPONSE_PREVIEW);


    gtk_dialog_add_button(GTK_DIALOG(dialog),_("_Reset"), RESPONSE_RESET);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                          gwy_stock_like_button_new(_("_Save image"),
                                                    GTK_STOCK_SAVE),
                          GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    gwy_help_add_to_volume_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    hbox = gtk_hbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       TRUE, TRUE, 4);

    controls.mydata = gwy_container_new();
    dfield = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE,
                                PREVIEW_SIZE, PREVIEW_SIZE,
                                TRUE);
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);

    if (data) {
        gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                                GWY_DATA_ITEM_PALETTE,
                                0);
    }

    controls.drawarea = gtk_drawing_area_new();
    gtk_widget_add_events(controls.drawarea,
                          GDK_BUTTON_PRESS_MASK
                          | GDK_BUTTON_RELEASE_MASK
                          | GDK_POINTER_MOTION_MASK);

    g_signal_connect(GTK_DRAWING_AREA(controls.drawarea), "expose-event",
                     G_CALLBACK(p3d_expose), &controls);
    g_signal_connect(controls.drawarea, "button-press-event",
                     G_CALLBACK(p3d_clicked), &controls);
    g_signal_connect(controls.drawarea, "button-release-event",
                     G_CALLBACK(p3d_released), &controls);
    g_signal_connect(controls.drawarea, "motion-notify-event",
                     G_CALLBACK(p3d_moved), &controls);

    gtk_box_pack_start(GTK_BOX(hbox), controls.drawarea, FALSE, FALSE, 4);
    gtk_widget_set_size_request(controls.drawarea, PREVIEW_SIZE, PREVIEW_SIZE);

    table = gtk_table_new(9, 4, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 4);
    row = 0;
    controls.size = gtk_adjustment_new(args->size, 1, 100, 1, 10, 0);
    gwy_table_attach_adjbar(table, row++, _("Zoom"), "%",
                            controls.size, GWY_HSCALE_SQRT);
    g_signal_connect_swapped(controls.size, "value-changed",
                             G_CALLBACK(extract_invalidate), &controls);
    row++;

    controls.threshold = gtk_adjustment_new(args->threshold, 1, 100, 1, 10, 0);
    gwy_table_attach_adjbar(table, row++, _("Wireframe threshold"), "%",
                            controls.threshold, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls.threshold, "value-changed",
                             G_CALLBACK(extract_threshold), &controls);
    row++;

    controls.zscale = gtk_adjustment_new(args->zscale, 1, 100, 1, 10, 0);
    gwy_table_attach_adjbar(table, row++, _("Z scale"), "%",
                            controls.zscale, GWY_HSCALE_SQRT);
    g_signal_connect_swapped(controls.zscale, "value-changed",
                             G_CALLBACK(extract_zscale), &controls);
    row++;

    controls.opacity = gtk_adjustment_new(args->opacity, 1, 100, 1, 10, 0);
    gwy_table_attach_adjbar(table, row++, _("Opacity scale"), "%",
                            controls.opacity, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls.opacity, "value-changed",
                             G_CALLBACK(extract_opacity), &controls);
    row++;

    controls.perspective = gtk_check_button_new_with_label(_("Apply perspective"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.perspective),
                                 args->perspective);
    gtk_table_attach(GTK_TABLE(table), controls.perspective,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.perspective, "toggled",
                             G_CALLBACK(perspective_changed), &controls);
    row++;

    controls.update = gtk_check_button_new_with_label(_("Instant 3D render"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.update),
                                 args->update);
    gtk_table_attach(GTK_TABLE(table), controls.update,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.update, "toggled",
                             G_CALLBACK(update_changed), &controls);
    row++;

    table2 = gtk_table_new(3, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table2), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table2), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table2), 4);

    gtk_table_attach(GTK_TABLE(table), table2,
                     0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    button = gtk_button_new_with_label("←");
    gtk_table_attach(GTK_TABLE(table2), button,
                     0, 1, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(button, "pressed",
                             G_CALLBACK(p3d_left), &controls);
    g_signal_connect_swapped(button, "released",
                             G_CALLBACK(p3d_stop), &controls);

    button = gtk_button_new_with_label("→");
    gtk_table_attach(GTK_TABLE(table2), button,
                     2, 3, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(button, "pressed",
                             G_CALLBACK(p3d_right), &controls);
    g_signal_connect_swapped(button, "released",
                             G_CALLBACK(p3d_stop), &controls);

    button = gtk_button_new_with_label("↑");
    gtk_table_attach(GTK_TABLE(table2), button,
                     1, 2, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(button, "pressed",
                             G_CALLBACK(p3d_up), &controls);
    g_signal_connect_swapped(button, "released",
                             G_CALLBACK(p3d_stop), &controls);

    button = gtk_button_new_with_label("↓");
    gtk_table_attach(GTK_TABLE(table2), button,
                     1, 2, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(button, "pressed",
                             G_CALLBACK(p3d_down), &controls);
    g_signal_connect_swapped(button, "released",
                             G_CALLBACK(p3d_stop), &controls);
    row++;

    hbox2 = gtk_hbox_new(TRUE, 2);
    gtk_table_attach(GTK_TABLE(table), hbox2,
                     0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    button = gtk_button_new_with_mnemonic(_("X view"));
    gtk_box_pack_start(GTK_BOX(hbox2), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(p3d_xview), &controls);

    button = gtk_button_new_with_mnemonic(_("Y view"));
    gtk_box_pack_start(GTK_BOX(hbox2), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(p3d_yview), &controls);

    button = gtk_button_new_with_mnemonic(_("Z view"));
    gtk_box_pack_start(GTK_BOX(hbox2), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(p3d_zview), &controls);

    row++;

    gtk_table_set_row_spacing(GTK_TABLE(table), row-1, 8);
    controls.gradient
        = gwy_gradient_tree_view_new(G_CALLBACK(gradient_changed), &controls,
                                     args->gradient);
    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin),
                                   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(scwin), controls.gradient);
    gtk_table_attach(GTK_TABLE(table), scwin, 0, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(controls.gradient));
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        path = gtk_tree_model_get_path(model, &iter);
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(controls.gradient), path,
                                     NULL, FALSE, 0.0, 0.0);
        gtk_tree_path_free(path);
    }

    p3d_build(&controls, args);
    p3d_prepare_wdata(&controls, args);
    rotate(&controls, 0, 0, 0);

    extract_invalidate(&controls);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls.dialog),
                                          RESPONSE_PREVIEW, !args->update);

    controls.in_init = FALSE;
    preview(&controls, args);

    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            extract_dialog_update_values(&controls, args);
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            goto finalize;
            break;

            case GTK_RESPONSE_OK:
            save_image(&controls, args);
            break;

            case RESPONSE_RESET:
            temp = args->update;
            *args = extract_defaults;
            args->update = temp;
            *args = extract_defaults;
            controls.in_init = TRUE;
            extract_dialog_update_controls(&controls, args);
            controls.in_init = FALSE;
            preview(&controls, args);
            break;

            case RESPONSE_PREVIEW:
            extract_dialog_update_values(&controls, args);
            controls.render_now = TRUE;
            preview(&controls, args);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (TRUE);

finalize:
    extract_save_args(gwy_app_settings_get(), args);
    g_free(controls.opdata);
    cairo_surface_destroy(controls.image);
    g_object_unref(controls.mydata);
}

static void
extract_dialog_update_controls(G_GNUC_UNUSED ExtractControls *controls,
                               G_GNUC_UNUSED ExtractArgs *args)
{
}

static void
extract_dialog_update_values(ExtractControls *controls,
                                ExtractArgs *args)
{
    args->size = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->size));
}

static void
invalidate_image(ExtractControls *controls)
{
    controls->image_valid = FALSE;
}

static void
invalidate_opdata(ExtractControls *controls)
{
    controls->opdata_valid = FALSE;
    /* Image cannot be valid when opdata is not. */
    invalidate_image(controls);
}

static void
extract_invalidate(ExtractControls *controls)
{
    /* create preview if instant updates are on for the rest, 3d is instantly
     * updated always*/
    if (!controls->in_init) {
        extract_dialog_update_values(controls, controls->args);
        invalidate_opdata(controls);
        preview(controls, controls->args);
    }
}

static void
preview(ExtractControls *controls,
        G_GNUC_UNUSED ExtractArgs *args)
{
    if (!controls->brick)
        return;
    gtk_widget_queue_draw(controls->drawarea);
}

static void
perspective_changed(ExtractControls *controls, GtkToggleButton *toggle)
{
    controls->args->perspective = gtk_toggle_button_get_active(toggle);
    //??? gtk_toggle_button_set_active(toggle, controls->args->perspective);
    invalidate_opdata(controls);
    gtk_widget_queue_draw(controls->drawarea);
}

static void
gradient_changed(GtkTreeSelection *selection,
                 ExtractControls *controls)
{
    GwyResource *resource;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 0, &resource, -1);
    controls->args->gradient = gwy_resource_get_name(resource);
    invalidate_image(controls);
    preview(controls, controls->args);
}

static void
update_changed(ExtractControls *controls)
{
    controls->args->update
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->update));

    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      RESPONSE_PREVIEW,
                                      !controls->args->update);

    gtk_widget_queue_draw(controls->drawarea);
}

static void
save_image(ExtractControls *controls, G_GNUC_UNUSED ExtractArgs *args)
{
    GtkWidget *dialog, *mdialog;
    gchar *filename;
    cairo_status_t ret;

    if (!controls->image) {
        mdialog = gtk_message_dialog_new(GTK_WINDOW(controls->dialog),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_OK,
                                         _("No image was rendered so far"));
        gtk_dialog_run(GTK_DIALOG(mdialog));
        gtk_widget_destroy(mdialog);
    }

    dialog = gtk_file_chooser_dialog_new (_("Export 3D view"),
                                          GTK_WINDOW(controls->dialog),
                                          GTK_FILE_CHOOSER_ACTION_SAVE,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                          NULL);

    if (gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        ret = cairo_surface_write_to_png(controls->image, filename);

        if (ret != CAIRO_STATUS_SUCCESS) {
            mdialog = gtk_message_dialog_new(GTK_WINDOW(controls->dialog),
                                             GTK_DIALOG_DESTROY_WITH_PARENT,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_OK,
                                             _("Cairo error while saving image"));
            gtk_dialog_run(GTK_DIALOG(mdialog));
            gtk_widget_destroy(mdialog);
        }
    }
    gtk_widget_destroy(dialog);
}

#define CX 200
#define CY 200

static void
convert_3d2d(gdouble x, gdouble y, gdouble z, gdouble *px, gdouble *py, gboolean perspective, gdouble size)
{
    if (perspective) {
        *px = 9*size*(x/(z+4)) + CX;
        *py = 9*size*(y/(z+4)) + CY;
    } else {
        *px = 3*size*x + CX;
        *py = 3*size*y + CY;
    }
}

/*
static void
printm(gdouble m[3][3])
{
    printf("%g %g %g\n", m[0][0], m[0][1], m[0][2]);
    printf("%g %g %g\n", m[1][0], m[1][1], m[1][2]);
    printf("%g %g %g\n", m[2][0], m[2][1], m[2][2]);
}
*/

static void
xrotmatrix(gdouble m[3][3], gdouble theta)
{
    m[0][0] = 1;
    m[1][0] = 0;
    m[2][0] = 0;

    m[0][1] = 0;
    m[1][1] = cos(theta);
    m[2][1] = -sin(theta);

    m[0][2] = 0;
    m[1][2] = sin(theta);
    m[2][2] = cos(theta);
}

static void
yrotmatrix(gdouble m[3][3], gdouble theta)
{
    m[0][0] = cos(theta);
    m[1][0] = 0;
    m[2][0] = sin(theta);

    m[0][1] = 0;
    m[1][1] = 1;
    m[2][1] = 0;

    m[0][2] = -sin(theta);
    m[1][2] = 0;
    m[2][2] = cos(theta);
}

static void
zrotmatrix(gdouble m[3][3], gdouble theta)
{
    m[0][0] = cos(theta);
    m[1][0] = sin(theta);
    m[2][0] = 0;

    m[0][1] = -sin(theta);
    m[1][1] = cos(theta);
    m[2][1] = 0;

    m[0][2] = 0;
    m[1][2] = 0;
    m[2][2] = 1;
}

static void
mmultm(gdouble a[3][3], gdouble b[3][3], gdouble result[3][3])
{
    gint i, j, k;

    for(i = 0; i < 3; i++)
    {
        for(j = 0; j < 3; j++)
        {
            result[i][j] = 0;
        }
    }

    for(i = 0; i < 3; i++)
    {
        for(j = 0; j < 3; j++)
        {
            for(k = 0; k < 3; k++)
            {
                result[i][j] += a[i][k]*b[k][j];
            }
        }
    }
}

static void
mmultv(gdouble m[3][3], gdouble x, gdouble y, gdouble z,
       gdouble *px, gdouble *py, gdouble *pz)
{
    *px = m[0][0]*x + m[0][1]*y + m[0][2]*z;
    *py = m[1][0]*x + m[1][1]*y + m[1][2]*z;
    *pz = m[2][0]*x + m[2][1]*y + m[2][2]*z;
}

/*
static gdouble
mdet(gdouble m[3][3])
{
    return (m[0][0] * m[1][1] * m[2][2]) + (m[1][0] * m[2][1] * m[3][2]) + (m[2][0] * m[3][1] * m[4][2])
           - (m[0][2] * m[1][1] * m[2][0]) - (m[1][2] * m[2][1] * m[3][0]) - (m[2][2] * m[3][1] * m[4][0]);
}
*/

static gboolean
minv(gdouble m[3][3], gdouble ret[3][3])
{
    ret[0][0] = m[0][0];
    ret[1][1] = m[1][1];
    ret[2][2] = m[2][2];

    ret[0][1] = m[1][0];
    ret[0][2] = m[2][0];
    ret[1][2] = m[2][1];

    ret[1][0] = m[0][1];
    ret[2][0] = m[0][2];
    ret[2][1] = m[1][2];

    //gdouble ddet = 1.0/mdet(m);
    /*
    if (ddet == 0) return FALSE;

    ret[0][0] =  ((m[1][1]*m[2][2])-(m[1][2]*m[2][1]))*ddet;
    ret[0][1] = -((m[1][0]*m[2][2])-(m[1][2]*m[2][0]))*ddet;
    ret[0][2] =  ((m[1][0]*m[2][1])-(m[1][1]*m[2][0]))*ddet;

    ret[1][0] = -((m[0][1]*m[2][2])-(m[0][2]*m[2][1]))*ddet;
    ret[1][1] =  ((m[0][0]*m[2][2])-(m[0][2]*m[2][0]))*ddet;
    ret[1][2] = -((m[0][0]*m[2][1])-(m[0][1]*m[2][0]))*ddet;

    ret[2][0] =  ((m[0][1]*m[1][2])-(m[0][2]*m[1][1]))*ddet;
    ret[2][1] = -((m[0][0]*m[1][2])-(m[0][2]*m[1][0]))*ddet;
    ret[2][2] =  ((m[0][0]*m[1][1])-(m[0][1]*m[1][0]))*ddet;
    */

    return TRUE;
}

static gdouble
raysum(ExtractControls *controls, gdouble pos[3], gdouble dir[3],
       gdouble min, gdouble max, gdouble zscale)
{
    gint xres, yres, zres;
    gdouble mult;
    gdouble sum = 0;
    gdouble posd, posx, posy, posz, *data;
    gdouble normzscale = zscale/100;
    GwyBrick *brick = controls->brick;
    gint brick_xoffset, brick_yoffset, brick_zoffset;

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);

    brick_xoffset = xres/2;
    brick_yoffset = yres/2;
    brick_zoffset = zres/2;

    data = gwy_brick_get_data(brick);

    mult = 0.6/(max - min)/(xres + yres + zres);
    //printf("%g %g %g\n", mult, min, max);
    for (posd = -3*zres; posd < 3*zres; posd++) {
        //all in pixel coordinates
        posx = pos[0] + dir[0]*posd + brick_xoffset;
        posy = pos[1] + dir[1]*posd + brick_yoffset;
        posz = pos[2]/normzscale + dir[2]*posd/normzscale + brick_zoffset;

        if (sum >= 1)
            continue;
        if (posx >= 0 && posy >= 0 && posz >= 0
            && posx < xres && posy < yres && posz < zres) {
              sum += (data[(gint)posx
                           + xres*((gint)posy)
                           + xres*yres*((gint)posz)] - min)*mult;
        }
    }
    return sum;
}

static gboolean
p3d_expose(GtkWidget *widget,
           G_GNUC_UNUSED GdkEventExpose *event,
           ExtractControls *controls)
{
    ExtractArgs *args = controls->args;
    cairo_t *cr;
    GwyGradient *gradient;
    gdouble sx, sy;
    gint i, j, rowstride;
    gdouble pos[3], dir[3], min, max, val, xoff, yoff, size, zscale;
    gdouble cdir[3];
    gdouble px, py, pz, dx, dy, dz;
    gdouble inv[3][3];
    gboolean perspective;
    GwyRGBA rgba;
    guint32 r, g, b;
    guint32 *imgdata;

    cr = gdk_cairo_create(GDK_WINDOW(widget->window));
    if (controls->image_valid) {
        cairo_set_source_surface(cr, controls->image, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        return FALSE;
    }

    size = args->size;
    perspective = args->perspective;
    if (!controls->opdata_valid
        && !controls->render_now
        && (!args->update || controls->in_move)) {
        cairo_rectangle(cr, 0.0, 0.0, PREVIEW_SIZE, PREVIEW_SIZE);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 0.5);

        convert_3d2d(controls->wpx[3], controls->wpy[3], controls->wpz[3],
                     &sx, &sy, perspective, size);
        cairo_move_to(cr, sx, sy);

        for (i = 4; i < controls->nps; i++) {
            convert_3d2d(controls->wpx[i], controls->wpy[i], controls->wpz[i],
                         &sx, &sy, perspective, size);
            if (controls->ps[i])
                cairo_line_to(cr, sx, sy);
            else
                cairo_move_to(cr, sx, sy);
        }

        /*axes description*/
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12.0);

        convert_3d2d(controls->wpx[3], controls->wpy[3], controls->wpz[3],
                     &sx, &sy, perspective, size);
        if (sx <= 200)
            sx -= 12;
        else
            sx += 12;
        cairo_move_to(cr, sx, sy);
        cairo_show_text(cr, "0");

        convert_3d2d(controls->wpx[4], controls->wpy[4], controls->wpz[4],
                     &sx, &sy, perspective, size);
        if (sx <= 200)
            sx -= 12;
        else
            sx += 12;
        cairo_move_to(cr, sx, sy);
        cairo_show_text(cr, "x");

        convert_3d2d(controls->wpx[14], controls->wpy[14], controls->wpz[14],
                     &sx, &sy, perspective, size);
        if (sx <= 200)
            sx -= 12;
        else
            sx += 12;
        cairo_move_to(cr, sx, sy);
        cairo_show_text(cr, "y");

        convert_3d2d(controls->wpx[8], controls->wpy[8], controls->wpz[8],
                     &sx, &sy, perspective, size);
        if (sx <= 200)
            sx -= 12;
        else
            sx += 12;
        cairo_move_to(cr, sx, sy);
        cairo_show_text(cr, "z");


        cairo_stroke(cr);
    }
    else {
        if (controls->render_now)
            controls->render_now = FALSE; //do it only once

        gradient = gwy_gradients_get_gradient(args->gradient);
        min = controls->brick_min;
        max = controls->brick_max;
        xoff = PREVIEW_SIZE/2;
        yoff = PREVIEW_SIZE/2;

//        printf("rotation matrix: --------------\n");
//        printm(controls->rm);
//        printf("-------------------------------\n");

        minv(controls->rm, inv);  //inverse of the rotation matrix
        cdir[0] = 0;
        cdir[1] = 0;
        cdir[2] = 1;
        mmultv(controls->rm, cdir[0], cdir[1], cdir[2], &dx, &dy, &dz); //look direction

        //printf("dir: %g %g %g\n", dx, dy, dz);
        cairo_surface_flush(controls->image);
        imgdata = (guint32*)cairo_image_surface_get_data(controls->image);
        rowstride = cairo_image_surface_get_stride(controls->image);
        g_assert(rowstride % sizeof(guint32) == 0);
        rowstride /= sizeof(guint32);

        if (!controls->opdata_valid) {
            zscale = args->zscale;
            for (j = 0; j < PREVIEW_SIZE; j++) {
                for (i = 0; i < PREVIEW_SIZE; i++) {
                    pos[0] = (60.0/size)*(i - xoff);          //camera location
                    pos[1] = (60.0/size)*(j - yoff);
                    pos[2] = -100;
                    mmultv(controls->rm, pos[0], pos[1], pos[2], &px, &py, &pz);
                    pos[0] = px;
                    pos[1] = py;
                    pos[2] = pz;
                    dir[0] = dx;
                    dir[1] = dy;
                    dir[2] = dz;
                    val = raysum(controls, pos, dir, min, max, zscale);
                    controls->opdata[j*PREVIEW_SIZE + i] = val;
                }
            }
            controls->opdata_valid = TRUE;
        }

        for (j = 0; j < PREVIEW_SIZE; j++) {
            for (i = 0; i < PREVIEW_SIZE; i++) {
                val = controls->opdata[j*PREVIEW_SIZE + i] * args->opacity;
                gwy_gradient_get_color(gradient, CLAMP(val, 0.0, 1.0), &rgba);
                r = (guint)floor(rgba.r*255.999999);
                g = (guint)floor(rgba.g*255.999999);
                b = (guint)floor(rgba.b*255.999999);
                imgdata[j*rowstride + i] = (r << 16) | (g << 8) | b;
            }
        }
        cairo_surface_mark_dirty(controls->image);
        cairo_set_source_surface(cr, controls->image, 0, 0);
        cairo_paint(cr);
    }

    cairo_destroy(cr);

    return FALSE;
}

static gboolean
p3d_clicked(G_GNUC_UNUSED GtkWidget *widget,
            GdkEventButton *event,
            ExtractControls *controls)
{
    controls->rpx = event->x;
    controls->rpy = event->y;
    controls->in_move = TRUE;
    invalidate_opdata(controls);
    gtk_widget_queue_draw(controls->drawarea);

    return TRUE;
}

static gboolean
p3d_released(G_GNUC_UNUSED GtkWidget *widget,
             G_GNUC_UNUSED GdkEventButton *event,
             ExtractControls *controls)
{
    controls->in_move = FALSE;
    invalidate_opdata(controls);
    gtk_widget_queue_draw(controls->drawarea);

    return TRUE;
}

static void rotatem(ExtractControls *controls)
{
    gdouble px, py, pz, im[3][3];
    gint i;

    im[0][0] = controls->rm[0][0]; im[0][1] = controls->rm[1][0]; im[0][2] = controls->rm[2][0];
    im[1][0] = controls->rm[0][1]; im[1][1] = controls->rm[1][1]; im[1][2] = controls->rm[2][1];
    im[2][0] = controls->rm[0][2]; im[2][1] = controls->rm[1][2]; im[2][2] = controls->rm[2][2];

    /*printf("rotation matrix:\n%g %g %g\n%g %g %g\n%g %g %g\n--------------\n",
           controls->rm[0][0], controls->rm[0][1], controls->rm[0][2],
           controls->rm[1][0], controls->rm[1][1], controls->rm[1][2],
           controls->rm[2][0], controls->rm[2][1], controls->rm[2][2]);
*/
    for (i=0; i<controls->nps; i++)
    {
        mmultv(im, controls->wpx[i], controls->wpy[i], controls->wpz[i], &px, &py, &pz);
        controls->wpx[i] = px;
        controls->wpy[i] = py;
        controls->wpz[i] = pz;
    }
}

static void rotate(ExtractControls *controls, gdouble x, gdouble y, gdouble z)
{
    gdouble rotx[3][3], roty[3][3], rotz[3][3], rotbuf[3][3];
    gdouble px, py, pz;
    gint i;

    xrotmatrix(rotx, x);
    yrotmatrix(roty, y);
    zrotmatrix(rotz, z);

    mmultm(rotx, roty, rotbuf);
    mmultm(rotbuf, rotz, controls->rm);

    for (i=0; i<controls->nps; i++)
    {
        mmultv(controls->rm, controls->wpx[i], controls->wpy[i], controls->wpz[i], &px, &py, &pz);
        controls->wpx[i] = px;
        controls->wpy[i] = py;
        controls->wpz[i] = pz;
    }



    controls->rm[0][0] = controls->wpx[0];
    controls->rm[0][1] = controls->wpy[0];
    controls->rm[0][2] = controls->wpz[0];

    controls->rm[1][0] = controls->wpx[1];
    controls->rm[1][1] = controls->wpy[1];
    controls->rm[1][2] = controls->wpz[1];

    controls->rm[2][0] = controls->wpx[2];
    controls->rm[2][1] = controls->wpy[2];
    controls->rm[2][2] = controls->wpz[2];


//    printf("total rotation by angle %g %g %g (%g %g %g), rotating by %g %g %g\n", controls->xangle,
//           controls->yangle,
//           controls->zangle,
//           controls->wpx[0], controls->wpy[1], controls->wpz[2], x, y, z);

}

static void
extract_zscale(ExtractControls *controls, GtkAdjustment *adj)
{
    ExtractArgs *args = controls->args;

    args->zscale = gtk_adjustment_get_value(adj);
    p3d_prepare_wdata(controls, args);
    rotatem(controls);
    invalidate_opdata(controls);
    preview(controls, args);
}

static void
extract_opacity(ExtractControls *controls, GtkAdjustment *adj)
{
    ExtractArgs *args = controls->args;

    args->opacity = gtk_adjustment_get_value(adj);
    p3d_prepare_wdata(controls, args);
    rotatem(controls);
    invalidate_image(controls);
    preview(controls, args);
}

static void
extract_threshold(ExtractControls *controls, GtkAdjustment *adj)
{
    ExtractArgs *args = controls->args;

    args->threshold = gtk_adjustment_get_value(adj);
    p3d_build(controls, args);
    p3d_prepare_wdata(controls, args);
    rotatem(controls);
    invalidate_opdata(controls);
    preview(controls, args);
}

static gboolean
p3d_moved(GtkWidget *widget, GdkEventMotion *event,
                 ExtractControls *controls)
{
    gdouble diffx, diffy;

    if (((event->state & GDK_BUTTON1_MASK) == GDK_BUTTON1_MASK)) {
        controls->rm[0][0] = controls->rm[1][1] = controls->rm[2][2] = 1;
        controls->rm[1][0] = controls->rm[2][0] = controls->rm[0][1] = 0;
        controls->rm[0][2] = controls->rm[1][2] = controls->rm[2][1] = 0;

        diffx = event->x - controls->rpx;
        diffy = event->y - controls->rpy;
        controls->rpx = event->x;
        controls->rpy = event->y;

        rotate(controls, -0.02*diffy, 0.02*diffx, 0);

//        printf("on move\n");

        invalidate_opdata(controls);
        gtk_widget_queue_draw(widget);
    }

    return TRUE;
}

static gboolean
move_left(gpointer data)
{
    ExtractControls *controls = (ExtractControls *)data;

    rotate(controls, 0, -0.05*G_PI, 0);
    invalidate_opdata(controls);
    gtk_widget_queue_draw(controls->drawarea);

    return TRUE;
}

static gboolean
move_right(gpointer data)
{
    ExtractControls *controls = (ExtractControls *)data;

    rotate(controls, 0, 0.05*G_PI, 0);
    invalidate_opdata(controls);
    gtk_widget_queue_draw(controls->drawarea);

    return TRUE;
}

static gboolean
move_up(gpointer data)
{
    ExtractControls *controls = (ExtractControls *)data;

    rotate(controls, 0.05*G_PI, 0, 0);
    invalidate_opdata(controls);
    gtk_widget_queue_draw(controls->drawarea);

    return TRUE;
}

static gboolean
move_down(gpointer data)
{
    ExtractControls *controls = (ExtractControls *)data;

    rotate(controls, -0.05*G_PI, 0, 0);
    invalidate_opdata(controls);
    gtk_widget_queue_draw(controls->drawarea);

    return TRUE;
}

static void
p3d_left(ExtractControls *controls)
{
    move_left(controls);
    controls->timeout = g_timeout_add(200, move_left, controls);
}

static void
p3d_right(ExtractControls *controls)
{
    move_right(controls);
    controls->timeout = g_timeout_add(200, move_right, controls);
}

static void
p3d_up(ExtractControls *controls)
{
    move_up(controls);
    controls->timeout = g_timeout_add(200, move_up, controls);
}

static void
p3d_down(ExtractControls *controls)
{
    move_down(controls);
    controls->timeout = g_timeout_add(200, move_down, controls);
}

static void
p3d_stop(ExtractControls *controls)
{
    g_source_remove(controls->timeout);
}

static void
p3d_xview(ExtractControls *controls)
{
    p3d_prepare_wdata(controls, controls->args);
    rotate(controls, 0, G_PI/2, 0);
    invalidate_opdata(controls);
    gtk_widget_queue_draw(controls->drawarea);
}

static void
p3d_yview(ExtractControls *controls)
{
    p3d_prepare_wdata(controls, controls->args);
    rotate(controls, G_PI/2, 0, 0);
    invalidate_opdata(controls);
    gtk_widget_queue_draw(controls->drawarea);
}

static void
p3d_zview(ExtractControls *controls)
{
    p3d_prepare_wdata(controls, controls->args);
    rotate(controls, 0, 0, 0);
    invalidate_opdata(controls);
    gtk_widget_queue_draw(controls->drawarea);
}

static void
p3d_set_axes(ExtractControls *controls)
{
    gint i = 0;
    gint max;

    if (!controls->px || controls->nps < 21)
        controls->px = g_new(gdouble, 21);
    if (!controls->py || controls->nps < 21)
        controls->py = g_new(gdouble, 21);
    if (!controls->pz || controls->nps < 21)
        controls->pz = g_new(gdouble, 21);
    if (!controls->ps || controls->nps < 21)
        controls->ps = g_new(gdouble, 21);

    controls->bwidth = controls->bheight = controls->bdepth = 1;

    if (controls->brick) {
       max = MAX(MAX(gwy_brick_get_xres(controls->brick),
                     gwy_brick_get_yres(controls->brick)),
                 gwy_brick_get_zres(controls->brick));

       controls->bwidth = gwy_brick_get_xres(controls->brick)/max;
       controls->bheight = gwy_brick_get_yres(controls->brick)/max;
       controls->bdepth = gwy_brick_get_zres(controls->brick)/max;
    }

    controls->px[i] = 1;
    controls->py[i] = 0;
    controls->pz[i] = 0;
    controls->ps[i] = 0;
    i++;
    controls->px[i] = 0;
    controls->py[i] = 1;
    controls->pz[i] = 0;
    controls->ps[i] = 0;
    i++;
    controls->px[i] = 0;
    controls->py[i] = 0;
    controls->pz[i] = 1;
    controls->ps[i] = 0;
    i++;

    controls->px[i] = -1;
    controls->py[i] = -1;
    controls->pz[i] = -1;
    controls->ps[i] = 0;
    i++;
    controls->px[i] = 1;
    controls->py[i] = -1;
    controls->pz[i] = -1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = 1;
    controls->py[i] = 1;
    controls->pz[i] = -1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = 1;
    controls->py[i] = 1;
    controls->pz[i] = 1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = -1;
    controls->py[i] = 1;
    controls->pz[i] = 1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = -1;
    controls->py[i] = -1;
    controls->pz[i] = 1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = 1;
    controls->py[i] = -1;
    controls->pz[i] = 1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = 1;
    controls->py[i] = -1;
    controls->pz[i] = -1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = -1;
    controls->py[i] = -1;
    controls->pz[i] = -1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = -1;
    controls->py[i] = -1;
    controls->pz[i] = 1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = -1;
    controls->py[i] = -1;
    controls->pz[i] = -1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = -1;
    controls->py[i] = 1;
    controls->pz[i] = -1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = -1;
    controls->py[i] = 1;
    controls->pz[i] = 1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = -1;
    controls->py[i] = 1;
    controls->pz[i] = -1;
    controls->ps[i] = 0;
    i++;
    controls->px[i] = 1;
    controls->py[i] = 1;
    controls->pz[i] = -1;
    controls->ps[i] = 1;
    i++;
    controls->px[i] = 1;
    controls->py[i] = 1;
    controls->pz[i] = 1;
    controls->ps[i] = 0;
    i++;
    controls->px[i] = 1;
    controls->py[i] = -1;
    controls->pz[i] = 1;
    controls->ps[i] = 1;

    for (i = 3; i < 20; i++) {
        controls->px[i] *= controls->bwidth;
        controls->py[i] *= controls->bheight;
        controls->pz[i] *= controls->bdepth;
    }

    controls->nps = 20;

}

static gint
simplify(gdouble *px, gdouble *py, gdouble *pz, gdouble *ps, gint nps)
{
    gint i;
    gdouble *nx, *ny, *nz, *ns;
    gint newn = 6;

    nx = g_new(gdouble, 4*nps);
    ny = nx + nps;
    nz = ny + nps;
    ns = nz + nps;

    gwy_assign(nx, px, newn);
    gwy_assign(ny, py, newn);
    gwy_assign(nz, pz, newn);
    gwy_assign(ns, ps, newn);

    for (i = 6; i < nps; i++) {
        if (ps[i] == 0 || !((px[i]-px[i-1]) == (px[i-1]-px[i-2])
                             && (py[i]-py[i-1]) == (py[i-1]-py[i-2])
                             && (pz[i]-pz[i-1]) == (pz[i-1]-pz[i-2]))) {
            nx[newn] = px[i];
            ny[newn] = py[i];
            nz[newn] = pz[i];
            ns[newn] = ps[i];
            newn++;
        }
    }

    gwy_assign(px, nx, newn);
    gwy_assign(py, ny, newn);
    gwy_assign(pz, nz, newn);
    gwy_assign(ps, ns, newn);

    g_free(nx);

    return newn;
}

static void
p3d_build(ExtractControls *controls, G_GNUC_UNUSED ExtractArgs *args)
{
    if (controls->brick == NULL) {
        //printf("No brick\n");
        return;
    }

    gwy_app_wait_start(GTK_WINDOW(controls->dialog),
                       _("Building wireframe model..."));
    p3d_set_axes(controls);
    p3d_add_wireframe(controls);
    gwy_app_wait_finish();
}


static void
p3d_prepare_wdata(ExtractControls *controls, ExtractArgs *args)
{
    gint i;

    if (controls->brick == NULL) {
        //printf("No brick\n");
        return;
    }

    for (i = 0; i < 3; i++) {
        controls->wpx[i] = controls->px[i];
        controls->wpy[i] = controls->py[i];
        controls->wpz[i] = controls->pz[i];
    }

    for (i = 3; i < controls->nps; i++) {
        controls->wpx[i] = controls->px[i];
        controls->wpy[i] = controls->py[i];
        controls->wpz[i] = controls->pz[i]*args->zscale/100.0;
    }
}

static gboolean
gothere(gdouble *data, gdouble *vdata, gint xres, gint yres, gint col, gint row, gdouble threshold)
{
    guint k = col + xres*row;

    if (vdata[k] == 1)
        return FALSE;
    if (row < 1 || row >= yres-1)
        return FALSE;
    if (col < 1 || col >= xres-1)
        return FALSE;

    if ((data[k] > threshold) &&
        ((data[k-1] < threshold) || (data[k-xres] < threshold)
         || (data[k+1] < threshold) || (data[k+xres] < threshold)
         || (data[k+xres+1] < threshold) || (data[k-xres-1] < threshold)
         || (data[k-xres+1] < threshold) || (data[k+xres-1] < threshold)))
        return TRUE;

    vdata[k] = 1;
    return FALSE;
}

static void
visitme(ExtractControls *controls, gint *actual_nps, gdouble *data, gdouble *vdata, gint xres, gint yres, gint zres, gint col, gint row, gint dir, gint tval, gboolean *move, gdouble threshold)
{
    gint first_res, second_res;
    /*detect ad add a segment of necessary*/
    //printf("pos %d %d ", col, row);

    /*increase allocation if necessary*/
    if (((*actual_nps)-controls->nps)<1000) {
        *actual_nps += 1000;
        controls->px = g_renew(gdouble, controls->px, *actual_nps);
        controls->py = g_renew(gdouble, controls->py, *actual_nps);
        controls->pz = g_renew(gdouble, controls->pz, *actual_nps);
        controls->ps = g_renew(gdouble, controls->ps, *actual_nps);
    }

    if (dir == 0) /*y const*/ {
        controls->px[controls->nps] = 2*controls->bwidth*tval/xres - controls->bwidth;
        controls->py[controls->nps] = 2*controls->bheight*col/yres - controls->bheight;
        controls->pz[controls->nps] = 2*controls->bdepth*row/zres - controls->bdepth;
    }
    else if (dir == 1) /*y const*/ {
        controls->px[controls->nps] = 2*controls->bwidth*col/xres - controls->bwidth;
        controls->py[controls->nps] = 2*controls->bheight*tval/yres - controls->bheight;
        controls->pz[controls->nps] = 2*controls->bdepth*row/zres - controls->bdepth;
    }
    else {
        controls->px[controls->nps] = 2*controls->bwidth*col/xres - controls->bwidth;
        controls->py[controls->nps] = 2*controls->bheight*row/yres - controls->bheight;
        controls->pz[controls->nps] = 2*controls->bdepth*tval/zres - controls->bdepth;
    }
    if (*move) {
        controls->ps[controls->nps] = 0;
        *move = 0;
    }
    else controls->ps[controls->nps] = 1;

    controls->nps += 1;
    vdata[col+xres*row] = 1;

    if (dir == 0) {
        first_res = yres;
        second_res = zres;
    }
    else if (dir == 1) {
        first_res = xres;
        second_res = zres;
    }
    else {
        first_res = xres;
        second_res = yres;
    }

    /*go to neighbor positions*/
    if (gothere(data, vdata, first_res, second_res, col+1, row, threshold))
        visitme(controls, actual_nps,data, vdata, xres, yres, zres, col+1, row, dir, tval, move, threshold);
    else if (gothere(data, vdata, first_res, second_res, col-1, row, threshold))
        visitme(controls, actual_nps, data, vdata, xres, yres, zres, col-1, row, dir, tval, move, threshold);
    else if (gothere(data, vdata, first_res, second_res, col, row+1, threshold))
        visitme(controls, actual_nps, data, vdata, xres, yres, zres, col, row+1, dir, tval, move, threshold);
    else if (gothere(data, vdata, first_res, second_res, col, row-1, threshold))
        visitme(controls, actual_nps, data, vdata, xres, yres, zres, col, row-1, dir, tval, move, threshold);
    else if (gothere(data, vdata, first_res, second_res, col+1, row+1, threshold))
        visitme(controls, actual_nps, data, vdata, xres, yres, zres, col+1, row+1, dir, tval, move, threshold);
    else if (gothere(data, vdata, first_res, second_res, col-1, row-1, threshold))
        visitme(controls, actual_nps, data, vdata, xres, yres, zres, col-1, row-1, dir, tval, move, threshold);
    else if (gothere(data, vdata, first_res, second_res, col+1, row-1, threshold))
        visitme(controls, actual_nps, data, vdata, xres, yres, zres, col+1, row-1, dir, tval, move, threshold);
    else if (gothere(data, vdata, first_res, second_res, col-1, row+1, threshold))
        visitme(controls, actual_nps, data, vdata, xres, yres, zres, col-1, row+1, dir, tval, move, threshold);
}

static void
p3d_add_wireframe(ExtractControls *controls)
{
    gint actual_nps = controls->nps;
    GwyDataField *cut, *visited;
    gdouble threshold, *data, *vdata;
    gint col, row, i, spacing = 40;
    gint xres, yres, zres;
    gboolean move;

    if (controls->brick == NULL) {
        //printf("No brick\n");
        return;
    }

    xres = gwy_brick_get_xres(controls->brick);
    yres = gwy_brick_get_yres(controls->brick);
    zres = gwy_brick_get_zres(controls->brick);
    cut = gwy_data_field_new(1, 1, 1, 1, FALSE);
    visited = gwy_data_field_new(yres, zres, gwy_brick_get_yreal(controls->brick), gwy_brick_get_zreal(controls-> brick), FALSE);
    //printf("brick min %g, max %g\n", gwy_brick_get_min(controls->brick), gwy_brick_get_max(controls->brick));
    threshold = gwy_brick_get_min(controls->brick)
        + (gwy_brick_get_max(controls->brick) - gwy_brick_get_min(controls->brick))/100.0*controls->args->threshold;

    for (i = 0; i < xres; i += spacing) { // why use 40 as a magic number - choose xres / 15 or something like that?
        /* Extract yz plane. */
        gwy_brick_extract_plane(controls->brick, cut, i, 0, 0, -1, yres, zres, FALSE);
        data = gwy_data_field_get_data(cut);
        gwy_data_field_clear(visited);
        vdata = gwy_data_field_get_data(visited);
        gwy_data_field_threshold(cut, threshold, 0, 1);
        move = 1;
        /*here comes the algorithm*/
        for (col = 1; col < zres-1; col++) {
            for (row = 1; row < yres-1; row++) {
                move = 1;
                if (gothere(data, vdata, yres, zres, col, row, threshold)) {
                    visitme(controls, &actual_nps, data, vdata,
                            xres, yres, zres, col, row, 0, i, &move, threshold);
                }
            }
        }
    }

    gwy_data_field_resample(visited, xres, zres, GWY_INTERPOLATION_NONE);

    for (i = 0; i < yres; i += spacing) {
        /* Extract xz plane. */
        gwy_brick_extract_plane(controls->brick, cut, 0, i, 0, xres, -1, zres, FALSE);
        data = gwy_data_field_get_data(cut);

        gwy_data_field_clear(visited);
        vdata = gwy_data_field_get_data(visited);

        gwy_data_field_threshold(cut, threshold, 0, 1);

        move = 1;

        /*here comes the algorithm*/
        for (col = 1; col < xres; col++) {
            for (row = 1; row < zres; row++) {
                move = 1;
                if (gothere(data, vdata, xres, zres, col, row, threshold))
                    visitme(controls, &actual_nps, data, vdata, xres, yres, zres, col, row, 1, i, &move, threshold);
            }
        }
    }

    gwy_data_field_resample(visited, xres, yres, GWY_INTERPOLATION_NONE);

    for (i = 0; i < zres; i += spacing) {
        /* Extract xy plane. */
        gwy_brick_extract_plane(controls->brick, cut, 0, 0, i, xres, yres, -1, FALSE);
        data = gwy_data_field_get_data(cut);

        gwy_data_field_clear(visited);
        vdata = gwy_data_field_get_data(visited);

        gwy_data_field_threshold(cut, threshold, 0, 1);

        move = 1;

        /*here comes the algorithm*/
        for (col = 1; col < xres; col++) {
            for (row = 1; row < yres; row++) {
                move = 1;
                if (gothere(data, vdata, xres, yres, col, row, threshold))
                    visitme(controls, &actual_nps, data, vdata, xres, yres, zres, col, row, 2, i, &move, threshold);
            }
        }
    }
//    printf("we have %d segments at the end\nRunning simplification:\n", controls->nps);
    controls->nps = simplify(controls->px, controls->py, controls->pz,
                             controls->ps, controls->nps);
    controls->wpx = g_renew(gdouble, controls->wpx, controls->nps);
    controls->wpy = g_renew(gdouble, controls->wpy, controls->nps);
    controls->wpz = g_renew(gdouble, controls->wpz, controls->nps);

//    printf("we have %d segments after simplification\n", controls->nps);
}


static const gchar gradient_key[]    = "/module/volume_extract/gradient";
static const gchar opacity_key[]     = "/module/volume_extract/opacity";
static const gchar perspective_key[] = "/module/volume_extract/perspective";
static const gchar size_key[]        = "/module/volume_extract/size";
static const gchar update_key[]      = "/module/volume_extract/update";
static const gchar zscale_key[]      = "/module/volume_extract/zscale";

static void
extract_sanitize_args(ExtractArgs *args)
{
    args->size = CLAMP(args->size, 1, 100);
    args->zscale = CLAMP(args->zscale, 1, 100);
    args->opacity = CLAMP(args->opacity, 1, 100);
    args->perspective = !!args->perspective;
    args->update = !!args->update;
    if (!gwy_inventory_get_item(gwy_gradients(), args->gradient))
        args->gradient = GWY_GRADIENT_DEFAULT;
}

static void
extract_load_args(GwyContainer *container, ExtractArgs *args)
{
    *args = extract_defaults;

    gwy_container_gis_double_by_name(container, size_key, &args->size);
    gwy_container_gis_double_by_name(container, zscale_key, &args->zscale);
    gwy_container_gis_double_by_name(container, opacity_key, &args->opacity);
    gwy_container_gis_boolean_by_name(container, perspective_key,
                                      &args->perspective);
    gwy_container_gis_boolean_by_name(container, update_key, &args->update);
    gwy_container_gis_string_by_name(container, gradient_key,
                                     (const guchar**)&args->gradient);

    extract_sanitize_args(args);
}

static void
extract_save_args(GwyContainer *container, ExtractArgs *args)
{
    gwy_container_set_double_by_name(container, size_key, args->size);
    gwy_container_set_double_by_name(container, zscale_key, args->zscale);
    gwy_container_set_double_by_name(container, opacity_key, args->opacity);
    gwy_container_set_boolean_by_name(container, perspective_key,
                                      args->perspective);
    gwy_container_set_boolean_by_name(container, update_key, args->update);
    gwy_container_set_string_by_name(container, gradient_key,
                                     g_strdup(args->gradient));
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
