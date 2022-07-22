/*
 *  $Id: gwyresultsexport.h 24262 2021-10-06 15:38:12Z yeti-dn $
 *  Copyright (C) 2017-2021 David Necas (Yeti).
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

#ifndef __GWY_RESULTS_EXPORT_H__
#define __GWY_RESULTS_EXPORT_H__

#include <gtk/gtk.h>
#include <libgwyddion/gwyresults.h>
#include <libgwyddion/gwycontainer.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libprocess/lawn.h>

G_BEGIN_DECLS

typedef enum {
    GWY_RESULTS_EXPORT_PARAMETERS   = 0,
    GWY_RESULTS_EXPORT_TABULAR_DATA = 1,
    GWY_RESULTS_EXPORT_FIXED_FORMAT = 2,
} GwyResultsExportStyle;

#define GWY_TYPE_RESULTS_EXPORT            (gwy_results_export_get_type())
#define GWY_RESULTS_EXPORT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_RESULTS_EXPORT, GwyResultsExport))
#define GWY_RESULTS_EXPORT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_RESULTS_EXPORT, GwyResultsExportClass))
#define GWY_IS_RESULTS_EXPORT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_RESULTS_EXPORT))
#define GWY_IS_RESULTS_EXPORT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_RESULTS_EXPORT))
#define GWY_RESULTS_EXPORT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_RESULTS_EXPORT, GwyResultsExportClass))

typedef struct _GwyResultsExport      GwyResultsExport;
typedef struct _GwyResultsExportClass GwyResultsExportClass;

struct _GwyResultsExport {
    GtkHBox hbox;
    struct _GwyResultsExportPrivate *priv;

    GwyResultsReportType format;
    GwyResultsExportStyle style;
    guint int1;
    gboolean boolean1;

    GtkWidget *copy;
    GtkWidget *save;

    gpointer reserved1;
    gpointer reserved2;
    gpointer reserved3;
};

struct _GwyResultsExportClass {
    GtkHBoxClass parent_class;

    /* Signals */
    void (*copy)(GwyResultsExport *results_export);
    void (*save)(GwyResultsExport *results_export);
    void (*format_changed)(GwyResultsExport *results_export);

    void (*reserved1)(void);
    void (*reserved2)(void);
};


GType                 gwy_results_export_get_type             (void)                            G_GNUC_CONST;
GtkWidget*            gwy_results_export_new                  (GwyResultsReportType format);
void                  gwy_results_export_set_format           (GwyResultsExport *rexport,
                                                               GwyResultsReportType format);
GwyResultsReportType  gwy_results_export_get_format           (GwyResultsExport *rexport);
void                  gwy_results_export_set_results          (GwyResultsExport *rexport,
                                                               GwyResults *results);
GwyResults*           gwy_results_export_get_results          (GwyResultsExport *rexport);
void                  gwy_results_export_set_title            (GwyResultsExport *rexport,
                                                               const gchar *title);
void                  gwy_results_export_set_style            (GwyResultsExport *rexport,
                                                               GwyResultsExportStyle style);
GwyResultsExportStyle gwy_results_export_get_style            (GwyResultsExport *rexport);
void                  gwy_results_export_set_actions_sensitive(GwyResultsExport *rexport,
                                                               gboolean sensitive);
void                  gwy_results_fill_filename               (GwyResults *results,
                                                               const gchar *id,
                                                               GwyContainer *container);
void                  gwy_results_fill_channel                (GwyResults *results,
                                                               const gchar *id,
                                                               GwyContainer *container,
                                                               gint i);
void                  gwy_results_fill_volume                 (GwyResults *results,
                                                               const gchar *id,
                                                               GwyContainer *container,
                                                               gint i);
void                  gwy_results_fill_xyz                    (GwyResults *results,
                                                               const gchar *id,
                                                               GwyContainer *container,
                                                               gint i);
void                  gwy_results_fill_curve_map              (GwyResults *results,
                                                               const gchar *id,
                                                               GwyContainer *container,
                                                               gint i);
void                  gwy_results_fill_graph                  (GwyResults *results,
                                                               const gchar *id,
                                                               GwyGraphModel *graphmodel);
void                  gwy_results_fill_graph_curve            (GwyResults *results,
                                                               const gchar *id,
                                                               GwyGraphCurveModel *curvemodel);
void                  gwy_results_fill_lawn_curve             (GwyResults *results,
                                                               const gchar *id,
                                                               GwyLawn *lawn,
                                                               gint i);

G_END_DECLS

#endif /* __GWY_RESULTS_EXPORT_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
