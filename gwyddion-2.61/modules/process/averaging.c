/*
 *  $Id: averaging.c 24626 2022-03-03 12:59:05Z yeti-dn $
 *  Copyright (C) 2011, David Necas (Yeti), Petr Klapetek, Daniil Bratashov
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, dn2010@gmail.com
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
#include <libprocess/datafield.h>
#include <libprocess/gwyprocess.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-tool.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define AVERAGING_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef struct {
    gint row;
    gint col;
    gdouble zvalue;
} GwyMaximum;

static gboolean     module_register   (void);
static void         averaging         (GwyContainer *data,
                                       GwyRunType run);
static GwyDataField *averaging_dialog (GwyContainer *data);
static GwyDataField *averaging_do     (GwyContainer *data,
                                       GwySelection *selected);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Similar structures averaging using autocorrelation"),
    "Daniil Bratashov <dn2010@gmail.com>",
    "0.2",
    "David Nečas (Yeti) & Petr Klapetek & Daniil Bratashov",
    "2011",
};

GWY_MODULE_QUERY2(module_info, averaging)

static gboolean
module_register(void)
{
    gwy_process_func_register("averaging",
                              (GwyProcessFunc)&averaging,
                              N_("/_Correct Data/_Correlation Averaging..."),
                              NULL,
                              AVERAGING_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Averaging of similar structures"));

    return TRUE;
}

static void
averaging(GwyContainer *data, GwyRunType run)
{
    GwyDataField *dfield, *score;
    GwyContainer *mydata;
    gint id, newid;

    g_return_if_fail(run & AVERAGING_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_DATA_FIELD(dfield));

    mydata = gwy_container_new();
    gwy_container_set_object_by_name(mydata, "/0/data", dfield);
    gwy_app_sync_data_items(data, mydata, id, 0, FALSE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    score = averaging_dialog(mydata);
    if (score) {
        newid = gwy_app_data_browser_add_data_field(score, data, TRUE);
        gwy_app_sync_data_items(mydata, data, 0, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
        gwy_app_set_data_field_title(data, newid, _("Averaged"));
        gwy_app_channel_log_add_proc(data, id, newid);

        g_object_unref(score);
    }
    g_object_unref(mydata);
}

static GwyDataField*
averaging_dialog(GwyContainer *data)
{
    GtkWidget *dialog;
    GtkWidget *hbox, *vbox, *label;
    GtkWidget *view;
    GwySelection *zselection;
    GwyDataField *score;
    gint response;

    dialog = gtk_dialog_new_with_buttons(_("Averaging of Similar Structures"),
                                         NULL, 0,
                                         GTK_STOCK_CANCEL,
                                         GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK,
                                         GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);

    hbox = gtk_hbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
                       hbox, TRUE, TRUE, 0);

    /* Data view */
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    label = gtk_label_new(_("Select the sample area below"));
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    view = gwy_create_preview(data, 0, PREVIEW_SIZE, FALSE);
    zselection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(view), 0, "Rectangle", 1, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), view, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            return NULL;
            break;
            case GTK_RESPONSE_OK:
            score = averaging_do(data, zselection);
            break;
            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);
    gtk_widget_destroy(dialog);

    return score;
}

static void
find_local_maxima(GwyDataField *dfield, GArray *maxima)
{
    gint xres, yres;
    const gdouble *data;
    gint row, col;
    GwyMaximum max;

    xres = dfield->xres;
    yres = dfield->yres;
    data = dfield->data;
    for (row = 1; row < yres - 1; row++)
        for (col = 1; col < xres - 1; col++)
            if ((data[row * xres + col] < data[row * xres + col - 1])
             || (data[row * xres + col] < data[row * xres + col + 1])
             || (data[row * xres + col] < data[row * (xres - 1) + col])
             || (data[row * xres + col] < data[row * (xres + 1) + col]))
                continue;
            else {
                max.col = col;
                max.row = row;
                max.zvalue = data[row * xres + col];
                if (max.zvalue > 0.75 * gwy_data_field_get_max(dfield))
                    g_array_append_val(maxima, max);
            }
}

static GwyDataField*
averaging_do(GwyContainer *data, GwySelection *selected)
{
    gdouble area[4];
    GwyDataField *dfield, *kernel, *correlation_score;
    GwyDataField *result, *res_kernel;
    gint xtop, ytop, xbottom, ybottom;
    GArray *maxima;
    GwyMaximum maximum;
    gint i;
    gdouble width, height, divider = 0;

    if (!gwy_selection_get_data(selected, area))
        /* error */
        return NULL;
    dfield = gwy_container_get_object_by_name(data, "/0/data");
    xtop = gwy_data_field_rtoj(dfield, area[0]);
    ytop = gwy_data_field_rtoi(dfield, area[1]);
    xbottom = gwy_data_field_rtoj(dfield, area[2]);
    ybottom = gwy_data_field_rtoi(dfield, area[3]);
    kernel = gwy_data_field_area_extract(dfield, xtop, ytop,
                                         xbottom - xtop,
                                         ybottom - ytop);
    correlation_score = gwy_data_field_new_alike(dfield, FALSE);
    gwy_data_field_correlate(dfield, kernel, correlation_score,
                             GWY_CORRELATION_NORMAL);
    gwy_data_field_filter_gaussian(correlation_score,
                                   2.0/(2.0*sqrt(2.0*G_LN2)));
    maxima = g_array_new(FALSE, TRUE, sizeof(GwyMaximum));
    find_local_maxima(correlation_score, maxima);
    g_object_unref(correlation_score);

    res_kernel = gwy_data_field_new_alike(kernel, TRUE);
    width = gwy_data_field_get_xres(kernel);
    height = gwy_data_field_get_yres(kernel);
    for (i = 0; i < maxima->len; i++) {
        maximum = g_array_index(maxima, GwyMaximum, i);
        xtop = maximum.col - width/2;
        ytop = maximum.row - height/2;
        kernel = gwy_data_field_area_extract(dfield, xtop, ytop,
                                             width, height);
        gwy_data_field_linear_combination(res_kernel,
                                          1.0, res_kernel,
                                          maximum.zvalue, kernel,
                                          0.0);
        g_object_unref(kernel);
        divider += maximum.zvalue;
    }
    if (divider <= 0.0)
        divider = 1.0;
    gwy_data_field_multiply(res_kernel, 1.0/divider);

    result = gwy_data_field_new_alike(dfield, FALSE);
    gwy_data_field_copy(dfield, result, TRUE);
    for (i = 0; i < maxima->len; i++) {
        maximum = g_array_index(maxima, GwyMaximum, i);
        xtop = maximum.col - width/2;
        ytop = maximum.row - height/2;
        gwy_data_field_area_copy(res_kernel, result, 0, 0,
                                 width, height, xtop, ytop);
    }

    g_array_free(maxima, TRUE);
    g_object_unref(res_kernel);

    return result;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
