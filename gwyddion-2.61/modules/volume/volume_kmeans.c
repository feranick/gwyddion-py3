/*
 *  $Id: volume_kmeans.c 20677 2017-12-18 18:22:52Z yeti-dn $
 *  Copyright (C) 2003 David Necas (Yeti), Petr Klapetek,
 *  Daniil Bratashov, Evgeniy Ryabov.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, dn2010@gmail.com.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/brick.h>
#include <libprocess/datafield.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwydgets.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>

#define KMEANS_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    RESPONSE_RESET   = 1,
};

typedef struct {
    gint k;              /* number of clusters */
    gdouble epsilon;     /* convergence precision */
    gint max_iterations; /* maximum number of main cycle iterations */
    gboolean normalize;  /* normalize brick before K-means run */
    gboolean remove_outliers;
    gdouble outliers_threshold;
} KMeansArgs;

typedef struct {
    KMeansArgs *args;
    GtkObject *k;
    GtkObject *epsilon;
    GtkObject *max_iterations;
    GtkWidget *normalize;
    GtkWidget *remove_outliers;
    GtkObject *outliers_threshold;
} KMeansControls;

static gboolean  module_register        (void);
static void      volume_kmeans          (GwyContainer *data,
                                         GwyRunType run);
static void      kmeans_dialog          (GwyContainer *data,
                                         KMeansArgs *args);
static void      remove_outliers_toggled(KMeansControls *controls,
                                         GtkToggleButton *toggle);
static void      kmeans_dialog_update   (KMeansControls *controls,
                                         KMeansArgs *args);
static void      kmeans_values_update   (KMeansControls *controls,
                                         KMeansArgs *args);
static GwyBrick* normalize_brick        (GwyBrick *brick,
                                         GwyDataField *intfield);
static void      volume_kmeans_do       (GwyContainer *data,
                                         KMeansArgs *args);
static void      kmeans_load_args       (GwyContainer *container,
                                         KMeansArgs *args);
static void      kmeans_save_args       (GwyContainer *container,
                                         KMeansArgs *args);

static const KMeansArgs kmeans_defaults = {
    10,
    1.0e-12,
    100,
    FALSE,
    FALSE,
    3.0
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates K-means clustering on volume data."),
    "Daniil Bratashov <dn2010@gmail.com> & Evgeniy Ryabov <k1u2r3ka@mail.ru>",
    "1.5",
    "David Nečas (Yeti) & Petr Klapetek & Daniil Bratashov & Evgeniy Ryabov",
    "2014",
};

GWY_MODULE_QUERY2(module_info, volume_kmeans)

static gboolean
module_register(void)
{
    gwy_volume_func_register("kmeans",
                              (GwyVolumeFunc)&volume_kmeans,
                              N_("/_K-Means Clustering..."),
                              GWY_STOCK_VOLUME_KMEANS,
                              KMEANS_RUN_MODES,
                              GWY_MENU_FLAG_VOLUME,
                              N_("Calculate K-means clustering on volume data"));

    return TRUE;
}

static void
volume_kmeans(GwyContainer *data, GwyRunType run)
{
    KMeansArgs args;
    GwyBrick *brick = NULL;
    gint id;

    g_return_if_fail(run & KMEANS_RUN_MODES);
    g_return_if_fail(data);

    kmeans_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    if (run == GWY_RUN_INTERACTIVE) {
        kmeans_dialog(data, &args);
        kmeans_save_args(gwy_app_settings_get(), &args);
    }
    else if (run == GWY_RUN_IMMEDIATE) {
        volume_kmeans_do(data, &args);
    }
}

static void
kmeans_dialog(GwyContainer *data, KMeansArgs *args)
{
    GtkWidget *dialog, *table;
    gint response;
    KMeansControls controls;
    gint row = 0;

    controls.args = args;

    dialog = gtk_dialog_new_with_buttons(_("K-means"), NULL, 0, NULL);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          _("_Reset"), RESPONSE_RESET);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_volume_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);

    table = gtk_table_new(6, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), table,
                       TRUE, TRUE, 4);

    controls.k = gtk_adjustment_new(args->k, 2, 100, 1, 10, 0);
    gwy_table_attach_adjbar(table, row, _("_Number of clusters:"), NULL,
                            controls.k, GWY_HSCALE_SQRT | GWY_HSCALE_SNAP);
    row++;

    controls.epsilon = gtk_adjustment_new(-log10(args->epsilon),
                                          1.0, 20.0, 0.01, 1.0, 0);
    gwy_table_attach_adjbar(table, row,
                            _("Convergence _precision digits:"), NULL,
                            controls.epsilon, GWY_HSCALE_LINEAR);
    row++;

    controls.max_iterations = gtk_adjustment_new(args->max_iterations,
                                                 1, 10000, 1, 1, 0);
    gwy_table_attach_adjbar(table, row, _("_Max. iterations:"), NULL,
                            controls.max_iterations, GWY_HSCALE_LOG);
    row++;

    controls.normalize = gtk_check_button_new_with_mnemonic(_("_Normalize"));
    gtk_table_attach(GTK_TABLE(table), controls.normalize,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    controls.remove_outliers
        = gtk_check_button_new_with_mnemonic(_("_Remove outliers"));
    gtk_table_attach(GTK_TABLE(table), controls.remove_outliers,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.remove_outliers, "toggled",
                             G_CALLBACK(remove_outliers_toggled), &controls);
    row++;

    controls.outliers_threshold = gtk_adjustment_new(args->outliers_threshold,
                                                     1.0, 10.0, 0.1, 1, 0);
    gwy_table_attach_adjbar(table, row, _("Outliers _threshold:"), NULL,
                            controls.outliers_threshold, GWY_HSCALE_LINEAR);
    gwy_table_hscale_set_sensitive(controls.outliers_threshold,
                                   args->remove_outliers);
    row++;

    kmeans_dialog_update(&controls, args);
    gtk_widget_show_all(dialog);

    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            kmeans_values_update(&controls, args);
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            return;
            break;

            case GTK_RESPONSE_OK:
            break;

            case RESPONSE_RESET:
            *args = kmeans_defaults;
            kmeans_dialog_update(&controls, args);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    kmeans_values_update(&controls, args);
    gtk_widget_destroy(dialog);
    volume_kmeans_do(data, args);
}

static void
remove_outliers_toggled(KMeansControls *controls, GtkToggleButton *toggle)
{
    gwy_table_hscale_set_sensitive(controls->outliers_threshold,
                                   gtk_toggle_button_get_active(toggle));
}

/* XXX: Duplicate with volume_kmedians.c */
static GwyBrick*
normalize_brick(GwyBrick *brick, GwyDataField *intfield)
{
    GwyBrick *result;
    gdouble wmin, dataval, dataval2, integral;
    gint i, j, l, k, xres, yres, zres;
    gint len = 25;
    const gdouble *olddata;
    gdouble *newdata, *intdata;

    result = gwy_brick_new_alike(brick, TRUE);
    wmin = gwy_brick_get_min(brick);
    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);
    olddata = gwy_brick_get_data_const(brick);
    newdata = gwy_brick_get_data(result);
    intdata = gwy_data_field_get_data(intfield);

    for (i = 0; i < xres; i++) {
        for (j = 0; j < yres; j++) {
            integral = 0;
            for (l = 0; l < zres; l++) {
                dataval = *(olddata + l * xres * yres + j * xres + i);
                wmin = dataval;
                for (k = -len; k < len; k++) {
                    if (l + k < 0) {
                        k = -l;
                        continue;
                    }
                    if (l + k >= zres)
                        break;
                    dataval2 = *(olddata + (l + k) * xres * yres
                                                        + j * xres + i);
                    if (dataval2 < wmin)
                        wmin = dataval2;
                }
                integral += (dataval - wmin);
            }
            for (l = 0; l < zres; l++) {
                dataval = *(olddata + l * xres * yres + j * xres + i);
                wmin = dataval;
                for (k = -len; k < len; k++) {
                    if (l + k < 0) {
                        k = -l;
                        continue;
                    }
                    if (l + k >= zres)
                        break;
                    dataval2 = *(olddata + (l + k)* xres * yres + j * xres + i);
                    if (dataval2 < wmin)
                        wmin = dataval2;
                }
                if (integral != 0.0) {
                    *(newdata + l * xres * yres + j * xres + i)
                                   = (dataval - wmin) * zres / integral;
                }
            }
            *(intdata + j * xres + i) = integral / zres;
        }
    }

    return result;
}

static void
kmeans_values_update(KMeansControls *controls,
                     KMeansArgs *args)
{
    args->k = gwy_adjustment_get_int(GTK_ADJUSTMENT(controls->k));
    args->epsilon = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->epsilon));
    args->epsilon = pow(0.1, args->epsilon);
    args->max_iterations = gwy_adjustment_get_int(GTK_ADJUSTMENT(controls->max_iterations));
    args->normalize = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->normalize));
    args->remove_outliers = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->remove_outliers));
    args->outliers_threshold = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->outliers_threshold));
}

static void
kmeans_dialog_update(KMeansControls *controls,
                     KMeansArgs *args)
{
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->k), args->k);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->epsilon),
                             -log10(args->epsilon));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->max_iterations),
                             args->max_iterations);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->normalize),
                                 args->normalize);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->remove_outliers),
                                 args->remove_outliers);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->outliers_threshold),
                             args->outliers_threshold);
}

static void
volume_kmeans_do(GwyContainer *container, KMeansArgs *args)
{
    GwyBrick *brick = NULL, *normalized = NULL;
    GwyDataField *dfield = NULL, *errormap = NULL, *intmap = NULL;
    GwyGraphCurveModel *gcmodel;
    GwyGraphModel *gmodel;
    GwyDataLine *calibration = NULL;
    GwySIUnit *siunit;
    const GwyRGBA *rgba;
    gint id;
    gchar *description;
    GRand *rand;
    const gdouble *data;
    gdouble *centers, *oldcenters, *sum, *data1, *xdata, *ydata;
    gdouble *variance, *errordata;
    gdouble min, dist, xreal, yreal, zreal, xoffset, yoffset, zoffset;
    gdouble epsilon = args->epsilon;
    gint xres, yres, zres, i, j, l, c, newid;
    gint *npix;
    gint k = args->k;
    gint iterations = 0;
    gint max_iterations = args->max_iterations;
    gboolean converged = FALSE, cancelled = FALSE;
    gboolean normalize = args->normalize;

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);
    xreal = gwy_brick_get_xreal(brick);
    yreal = gwy_brick_get_yreal(brick);
    zreal = gwy_brick_get_zreal(brick);
    xoffset = gwy_brick_get_xoffset(brick);
    yoffset = gwy_brick_get_yoffset(brick);
    zoffset = gwy_brick_get_zoffset(brick);

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, TRUE);
    gwy_data_field_set_xoffset(dfield, xoffset);
    gwy_data_field_set_yoffset(dfield, yoffset);

    siunit = gwy_brick_get_si_unit_x(brick);
    gwy_data_field_set_si_unit_xy(dfield, siunit);

    intmap = gwy_data_field_new_alike(dfield, TRUE);
    siunit = gwy_brick_get_si_unit_w(brick);
    gwy_data_field_set_si_unit_z(intmap, siunit);

    gwy_app_wait_start(gwy_app_find_window_for_volume(container, id),
                       _("Initializing..."));

    if (normalize) {
        normalized = normalize_brick(brick, intmap);
        data = gwy_brick_get_data_const(normalized);
    }
    else {
        data = gwy_brick_get_data_const(brick);
    }

    centers = g_new(gdouble, zres*k);
    oldcenters = g_new(gdouble, zres*k);
    sum = g_new(gdouble, zres*k);
    npix = g_new(gint, k);
    variance = g_new(gdouble, k);
    data1 = gwy_data_field_get_data(dfield);

    rand = g_rand_new();
    for (c = 0; c < k; c++) {
        i = g_rand_int_range(rand, 0, xres);
        j = g_rand_int_range(rand, 0, yres);
        for (l = 0; l < zres; l++) {
            *(centers + c * zres + l)
                             = *(data + l * xres * yres + j * xres + i);
        };
    };
    g_rand_free(rand);

    if (!gwy_app_wait_set_message(_("K-means iteration...")))
        cancelled = TRUE;

    while (!converged && !cancelled) {
        if (!gwy_app_wait_set_fraction((gdouble)iterations/max_iterations)) {
            cancelled = TRUE;
            break;
        }

        /* pixels belong to cluster with min distance */
        for (j = 0; j < yres; j++)
            for (i = 0; i < xres; i++) {
                *(data1 + j * xres + i) = 0;
                min = G_MAXDOUBLE;
                for (c = 0; c < k; c++) {
                    dist = 0;
                    for (l = 0; l < zres; l++) {
                        *(oldcenters + c * zres + l)
                                            = *(centers + c * zres + l);
                        dist
                            += (*(data + l * xres * yres + j * xres + i)
                              - *(centers + c * zres + l))
                             * (*(data + l * xres * yres + j * xres + i)
                              - *(centers + c * zres + l));
                    }
                    if (dist < min) {
                        min = dist;
                        *(data1 + j * xres + i) = c;
                    }
                }
            }

        /* new center coordinates as average of pixels */
        for (c = 0; c < k; c++) {
            *(npix + c) = 0;
            for (l = 0; l < zres; l++) {
                *(sum + c * zres + l) = 0;
            }
        }
        for (i = 0; i < xres; i++) {
            for (j = 0; j < yres; j++) {
                c = (gint)(*(data1 + j * xres + i));
                *(npix + c) += 1;
                for (l = 0; l < zres; l++) {
                    *(sum + c * zres + l)
                            += *(data + l * xres * yres + j * xres + i);
                }
            }
        }
        for (c = 0; c < k; c++)
            for (l = 0; l < zres; l++) {
                *(centers + c * zres + l) = (*(npix + c) > 0) ?
                     *(sum + c * zres + l)/(gdouble)(*(npix + c)) : 0.0;
        }

        converged = TRUE;
        for (c = 0; c < k; c++) {
            for (l = 0; l < zres; l++)
                if (fabs(*(oldcenters + c * zres + l)
                               - *(centers + c * zres + l)) > epsilon) {
                    converged = FALSE;
                    break;
                }
        }
        if (iterations == max_iterations) {
            converged = TRUE;
            break;
        }
        iterations++;
    }

    if (cancelled) {
        gwy_app_wait_finish();
        goto fail;
    }

    /* second try, outliers are not counted now */
    if (args->remove_outliers) {
        converged = FALSE;
        while (!converged && !cancelled) {
            if (!gwy_app_wait_set_fraction((gdouble)iterations/max_iterations)) {
                cancelled = TRUE;
                break;
            }

            /* pixels belong to cluster with min distance */
            for (j = 0; j < yres; j++)
                for (i = 0; i < xres; i++) {
                    *(data1 + j * xres + i) = 0;
                    min = G_MAXDOUBLE;
                    for (c = 0; c < k; c++) {
                        dist = 0;
                        for (l = 0; l < zres; l++) {
                            *(oldcenters + c * zres + l)
                                            = *(centers + c * zres + l);
                            dist
                            += (*(data + l * xres * yres + j * xres + i)
                                            - *(centers + c * zres + l))
                             * (*(data + l * xres * yres + j * xres + i)
                                           - *(centers + c * zres + l));
                        }
                        if (dist < min) {
                            min = dist;
                            *(data1 + j * xres + i) = c;
                        }
                    }
                }

            /* variance calculation */
            for (c = 0; c < k; c++) {
                *(npix + c) = 0;
                *(variance + c) = 0.0;
            }

            for (i = 0; i < xres; i++) {
                for (j = 0; j < yres; j++) {
                    c = (gint)(*(data1 + j * xres + i));
                    *(npix + c) += 1;
                    dist = 0;
                    for (l = 0; l < zres; l++) {
                        dist
                            += (*(data + l * xres * yres + j * xres + i)
                                            - *(centers + c * zres + l))
                             * (*(data + l * xres * yres + j * xres + i)
                                           - *(centers + c * zres + l));
                    }
                    *(variance + c) += dist;
                }
            }

            for (c = 0; c < k; c++) {
                if (*(npix + c) > 0) {
                    *(variance + c) /= *(npix+c);
                    *(variance + c) = sqrt(*(variance + c));
                }
            }

            /* new center coordinates as average of pixels */
            for (c = 0; c < k; c++) {
                *(npix + c) = 0;
                for (l = 0; l < zres; l++) {
                    *(sum + c * zres + l) = 0;
                }
            }
            for (i = 0; i < xres; i++) {
                for (j = 0; j < yres; j++) {
                    c = (gint)(*(data1 + j * xres + i));
                    dist = 0;
                    for (l = 0; l < zres; l++) {
                        dist
                            += (*(data + l * xres * yres + j * xres + i)
                                            - *(centers + c * zres + l))
                             * (*(data + l * xres * yres + j * xres + i)
                                           - *(centers + c * zres + l));
                    }
                    if (sqrt(dist) < args->outliers_threshold * (*(variance + c))) {
                        *(npix + c) += 1;
                        for (l = 0; l < zres; l++) {
                            *(sum + c * zres + l)
                            += *(data + l * xres * yres + j * xres + i);
                        }
                    }
                }
            }
            for (c = 0; c < k; c++)
                for (l = 0; l < zres; l++) {
                    *(centers + c * zres + l) = (*(npix + c) > 0) ?
                         *(sum + c * zres + l)/(gdouble)(*(npix + c)) : 0.0;
            }

            converged = TRUE;
            for (c = 0; c < k; c++) {
                for (l = 0; l < zres; l++)
                    if (fabs(*(oldcenters + c * zres + l)
                                   - *(centers + c * zres + l)) > epsilon) {
                        converged = FALSE;
                        break;
                    }
            }
            if (iterations == max_iterations) {
                converged = TRUE;
                break;
            }
            iterations++;
        }
    }

    gwy_app_wait_finish();
    if (cancelled)
        goto fail;

    errormap = gwy_data_field_new_alike(dfield, TRUE);
    if (!normalize) {
        siunit = gwy_brick_get_si_unit_w(brick);
        siunit = gwy_si_unit_duplicate(siunit);
        gwy_data_field_set_si_unit_z(errormap, siunit);
        g_object_unref(siunit);
    }
    errordata = gwy_data_field_get_data(errormap);

    for (i = 0; i < xres; i++) {
        for (j = 0; j < yres; j++) {
            dist = 0.0;
            c = (gint)(*(data1 + j * xres + i));
            for (l = 0; l < zres; l++) {
                dist += (*(data + l * xres * yres + j * xres + i)
                         - *(centers + c * zres + l))
                    * (*(data + l * xres * yres + j * xres + i)
                       - *(centers + c * zres + l));
            }
            *(errordata + j * xres + i) = sqrt(dist);
        }
    }

    gwy_data_field_add(dfield, 1.0);
    newid = gwy_app_data_browser_add_data_field(dfield,
                                                container, TRUE);
    gwy_object_unref(dfield);
    description = gwy_app_get_brick_title(container, id);
    gwy_app_set_data_field_title(container, newid,
                                 g_strdup_printf(_("K-means cluster of %s"),
                                                 description));
    gwy_app_channel_log_add(container, -1, newid, "volume::kmeans",
                            NULL);

    newid = gwy_app_data_browser_add_data_field(errormap,
                                                container, TRUE);
    gwy_object_unref(errormap);
    gwy_app_set_data_field_title(container, newid,
                                 g_strdup_printf(_("K-means error of %s"),
                                                 description));
    gwy_app_channel_log_add(container, -1, newid, "volume::kmeans",
                            NULL);

    if (normalize) {
        newid = gwy_app_data_browser_add_data_field(intmap,
                                                    container, TRUE);
        gwy_object_unref(intmap);
        gwy_app_set_data_field_title(container, newid,
                                     g_strdup_printf(_("Pre-normalized "
                                                       "intensity of %s"),
                                                     description));

        gwy_app_channel_log_add(container, -1, newid,
                                "volume::kmeans", NULL);
    }

    g_free(description);

    gmodel = gwy_graph_model_new();
    calibration = gwy_brick_get_zcalibration(brick);
    xdata = g_new(gdouble, zres);
    ydata = g_new(gdouble, zres);
    if (calibration) {
        memcpy(xdata, gwy_data_line_get_data(calibration),
               zres*sizeof(gdouble));
        siunit = gwy_data_line_get_si_unit_y(calibration);
    }
    else {
        for (i = 0; i < zres; i++)
            *(xdata + i) = zreal * i / zres + zoffset;
        siunit = gwy_brick_get_si_unit_z(brick);
    }
    for (c = 0; c < k; c++) {
        memcpy(ydata, centers + c * zres, zres * sizeof(gdouble));
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, zres);
        rgba = gwy_graph_get_preset_color(c);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description",
                     g_strdup_printf(_("K-means center %d"), c + 1),
                     "color", rgba,
                     NULL);
        if (!gwy_graph_curve_model_is_ordered(gcmodel)) {
            gwy_graph_curve_model_enforce_order(gcmodel);
        }
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }
    g_free(xdata);
    g_free(ydata);
    g_object_set(gmodel,
                 "si-unit-x", siunit,
                 "si-unit-y", gwy_brick_get_si_unit_w(brick),
                 "axis-label-bottom", "x",
                 "axis-label-left", "y",
                 NULL);
    gwy_app_data_browser_add_graph_model(gmodel, container, TRUE);
    g_object_unref(gmodel);

    gwy_app_volume_log_add_volume(container, id, id);

fail:
    gwy_object_unref(errormap);
    gwy_object_unref(intmap);
    gwy_object_unref(dfield);
    gwy_object_unref(normalized);
    g_free(variance);
    g_free(npix);
    g_free(sum);
    g_free(oldcenters);
    g_free(centers);
}

static const gchar epsilon_key[]         = "/module/kmeans/epsilon";
static const gchar kmeans_k_key[]        = "/module/kmeans/k";
static const gchar max_iterations_key[]  = "/module/kmeans/max_iterations";
static const gchar normalize_key[]       = "/module/kmeans/normalize";
static const gchar remove_outliers_key[] = "/module/kmeans/remove_outliers";
static const gchar outliers_threshold_key[] = "/module/kmeans/outliers_threshold";

static void
kmeans_sanitize_args(KMeansArgs *args)
{
    args->k = CLAMP(args->k, 2, 100);
    args->epsilon = CLAMP(args->epsilon, 1e-20, 0.1);
    args->max_iterations = CLAMP(args->max_iterations, 0, 10000);
    args->normalize = !!args->normalize;
    args->remove_outliers = !!args->remove_outliers;
    args->outliers_threshold = CLAMP(args->outliers_threshold, 1.0, 10.0);
}

static void
kmeans_load_args(GwyContainer *container,
                 KMeansArgs *args)
{
    *args = kmeans_defaults;

    gwy_container_gis_int32_by_name(container, kmeans_k_key, &args->k);
    gwy_container_gis_double_by_name(container, epsilon_key,
                                     &args->epsilon);
    gwy_container_gis_int32_by_name(container, max_iterations_key,
                                    &args->max_iterations);
    gwy_container_gis_boolean_by_name(container, normalize_key,
                                      &args->normalize);
    gwy_container_gis_boolean_by_name(container,
                                      remove_outliers_key,
                                      &args->remove_outliers);
    gwy_container_gis_double_by_name(container, outliers_threshold_key,
                                     &args->outliers_threshold);

    kmeans_sanitize_args(args);
}

static void
kmeans_save_args(GwyContainer *container,
                 KMeansArgs *args)
{
    gwy_container_set_int32_by_name(container, kmeans_k_key, args->k);
    gwy_container_set_double_by_name(container, epsilon_key,
                                     args->epsilon);
    gwy_container_set_int32_by_name(container, max_iterations_key,
                                    args->max_iterations);
    gwy_container_set_boolean_by_name(container, normalize_key,
                                      args->normalize);
    gwy_container_set_boolean_by_name(container, remove_outliers_key,
                                      args->remove_outliers);
    gwy_container_set_double_by_name(container, outliers_threshold_key,
                                     args->outliers_threshold);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
