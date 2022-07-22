/*
 *  $Id: gwyresultsexport.c 24709 2022-03-21 17:31:45Z yeti-dn $
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

#include "config.h"
#include <string.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include "app.h"
#include "menu.h"
#include "data-browser.h"
#include "gwymoduleutils.h"
#include "gwyresultsexport.h"

enum {
    COPY,
    SAVE,
    FORMAT_CHANGED,
    LAST_SIGNAL
};

struct _GwyResultsExportPrivate {
    GwyResults *results;
    GtkWidget *format;
    GtkWidget *machine;
    gchar *title;
    gboolean updating;
};

typedef struct _GwyResultsExportPrivate ResultsExportPriv;

static void gwy_results_export_dispose  (GObject *object);
static void gwy_results_export_finalize (GObject *object);
static void gwy_results_export_save_impl(GwyResultsExport *rexport);
static void gwy_results_export_copy_impl(GwyResultsExport *rexport);
static void gwy_results_export_save     (GwyResultsExport *rexport);
static void gwy_results_export_copy     (GwyResultsExport *rexport);
static void update_format_controls      (GwyResultsExport *rexport);
static void machine_changed             (GwyResultsExport *rexport,
                                         GtkToggleButton *toggle);
static void format_changed              (GtkComboBox *combo,
                                         GwyResultsExport *rexport);

static guint results_export_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(GwyResultsExport, gwy_results_export, GTK_TYPE_HBOX)

static void
gwy_results_export_class_init(GwyResultsExportClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(ResultsExportPriv));

    gobject_class->dispose = gwy_results_export_dispose;
    gobject_class->finalize = gwy_results_export_finalize;
    klass->save = gwy_results_export_save_impl;
    klass->copy = gwy_results_export_copy_impl;

    /* NB: "copy" and "save" signals are created with LAST flag so that user's handlers are run before save_impl() and
     * copy_impl() and have a chance to update the results. */

    /**
     * GwyResultsExport::copy:
     * @rexport: The #GwyResultsExport which received the signal.
     *
     * The ::copy signal is emitted when user presses the Copy button.
     *
     * Since: 2.50
     **/
    results_export_signals[COPY]
        = g_signal_new("copy",
                       G_OBJECT_CLASS_TYPE(gobject_class),
                       G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                       G_STRUCT_OFFSET(GwyResultsExportClass, copy),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__VOID,
                       G_TYPE_NONE, 0);

    /**
     * GwyResultsExport::save:
     * @rexport: The #GwyResultsExport which received the signal.
     *
     * The ::save signal is emitted when user presses the Save button.
     *
     * Since: 2.50
     **/
    results_export_signals[SAVE]
        = g_signal_new("save",
                       G_OBJECT_CLASS_TYPE(gobject_class),
                       G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                       G_STRUCT_OFFSET(GwyResultsExportClass, save),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__VOID,
                       G_TYPE_NONE, 0);

    /**
     * GwyResultsExport::format-changed:
     * @rexport: The #GwyResultsExport which received the signal.
     *
     * The ::format-changed signal is emitted when the selected format changes.
     *
     * Since: 2.50
     **/
    results_export_signals[FORMAT_CHANGED]
        = g_signal_new("format-changed",
                       G_OBJECT_CLASS_TYPE(gobject_class),
                       G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                       G_STRUCT_OFFSET(GwyResultsExportClass, format_changed),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__VOID,
                       G_TYPE_NONE, 0);
}

static GtkWidget*
create_image_button(GwyResultsExport *rexport,
                    const gchar *stock_id, const gchar *tooltip)
{
    GtkWidget *button;

    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_box_pack_end(GTK_BOX(rexport), button, FALSE, FALSE, 0);

    return button;
}

static void
gwy_results_export_dispose(GObject *object)
{
    ResultsExportPriv *rexport = ((GwyResultsExport*)object)->priv;
    GWY_OBJECT_UNREF(rexport->results);
    /* FIXME: Can we have the file save dialogue open when we get here? */
}

static void
gwy_results_export_finalize(GObject *object)
{
    ResultsExportPriv *rexport = ((GwyResultsExport*)object)->priv;
    g_free(rexport->title);
}

static GtkWidget*
create_image_toggle(GwyResultsExport *rexport,
                    const gchar *stock_id, const gchar *tooltip,
                    gboolean active)
{
    GtkWidget *button;

    button = gtk_toggle_button_new();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), active);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_box_pack_end(GTK_BOX(rexport), button, FALSE, FALSE, 0);

    return button;
}

static void
gwy_results_export_init(GwyResultsExport *rexport)
{
    ResultsExportPriv *priv;

    priv = G_TYPE_INSTANCE_GET_PRIVATE(rexport, GWY_TYPE_RESULTS_EXPORT, ResultsExportPriv);
    rexport->priv = priv;
    rexport->format = GWY_RESULTS_REPORT_COLON;
    rexport->style = GWY_RESULTS_EXPORT_PARAMETERS;
}

static void
gwy_results_export_save(GwyResultsExport *rexport)
{
    g_signal_emit(rexport, results_export_signals[SAVE], 0);
}

static void
gwy_results_export_copy(GwyResultsExport *rexport)
{
    g_signal_emit(rexport, results_export_signals[COPY], 0);
}

static void
machine_changed(GwyResultsExport *rexport, GtkToggleButton *toggle)
{
    if (rexport->priv->updating)
        return;

    if (gtk_toggle_button_get_active(toggle))
        rexport->format |= GWY_RESULTS_REPORT_MACHINE;
    else
        rexport->format &= ~GWY_RESULTS_REPORT_MACHINE;

    g_signal_emit(rexport, results_export_signals[FORMAT_CHANGED], 0);
}

static void
format_changed(GtkComboBox *combo, GwyResultsExport *rexport)
{
    guint flags;

    if (rexport->priv->updating)
        return;

    flags = (rexport->format & GWY_RESULTS_REPORT_MACHINE);
    rexport->format = gwy_enum_combo_box_get_active(combo);
    rexport->format |= flags;

    g_signal_emit(rexport, results_export_signals[FORMAT_CHANGED], 0);
}

static void
gwy_results_export_save_impl(GwyResultsExport *rexport)
{
    ResultsExportPriv *priv = rexport->priv;
    GtkWindow *window;
    const gchar *title;
    gchar *text;

    if (!priv->results)
        return;

    if (rexport->priv->title)
        title = rexport->priv->title;
    else
        title = _("Save Results to File");

    text = gwy_results_create_report(rexport->priv->results, rexport->format);
    if ((window = (GtkWindow*)gtk_widget_get_toplevel(GTK_WIDGET(rexport)))) {
        if (!GTK_IS_WINDOW(window))
            window = NULL;
    }
    gwy_save_auxiliary_data(title, window, -1, text);
    g_free(text);
}

static void
gwy_results_export_copy_impl(GwyResultsExport *rexport)
{
    ResultsExportPriv *priv = rexport->priv;
    GtkClipboard *clipboard;
    GdkDisplay *display;
    gchar *text;

    if (!priv->results)
        return;

    display = gtk_widget_get_display(GTK_WIDGET(rexport));
    text = gwy_results_create_report(priv->results, rexport->format);
    clipboard = gtk_clipboard_get_for_display(display, GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    g_free(text);
}

/**
 * gwy_results_export_new:
 * @format: Format to select.
 *
 * Creates new controls for result set export.
 *
 * Returns: A newly created result set export widget.
 *
 * Since: 2.50
 **/
GtkWidget*
gwy_results_export_new(GwyResultsReportType format)
{
    GwyResultsExport *rexport;
    ResultsExportPriv *priv;

    rexport = g_object_new(GWY_TYPE_RESULTS_EXPORT, NULL);
    priv = rexport->priv;

    rexport->save = create_image_button(rexport, GTK_STOCK_SAVE, _("Save results to a file"));
    rexport->copy = create_image_button(rexport, GTK_STOCK_COPY, _("Copy results to clipboard"));
    g_signal_connect_swapped(rexport->save, "clicked", G_CALLBACK(gwy_results_export_save), rexport);
    g_signal_connect_swapped(rexport->copy, "clicked", G_CALLBACK(gwy_results_export_copy), rexport);

    rexport->format = format;
    priv->updating = TRUE;
    update_format_controls(rexport);
    priv->updating = FALSE;

    return (GtkWidget*)rexport;
}

static void
update_format_controls(GwyResultsExport *rexport)
{
    static const GwyEnum formats[] = {
        { N_("Colon:"), GWY_RESULTS_REPORT_COLON,  },
        { N_("TAB"),    GWY_RESULTS_REPORT_TABSEP, },
        { N_("CSV"),    GWY_RESULTS_REPORT_CSV,    },
    };

    ResultsExportPriv *priv;
    GtkWidget *combo;
    GtkTreeModel *model;
    gint off, copypos, wantnformats, nformats = 0;
    guint base_format;
    gboolean is_machine;

    priv = rexport->priv;

    if (rexport->style == GWY_RESULTS_EXPORT_FIXED_FORMAT) {
        if (priv->format) {
            gtk_widget_destroy(priv->format);
            priv->format = NULL;
        }
        if (priv->machine) {
            gtk_widget_destroy(priv->machine);
            priv->machine = NULL;
        }
        return;
    }

    gtk_container_child_get(GTK_CONTAINER(rexport), rexport->copy, "position", &copypos, NULL);
    if (priv->format) {
        model = gtk_combo_box_get_model(GTK_COMBO_BOX(priv->format));
        nformats = gtk_tree_model_iter_n_children(model, NULL);
    }

    base_format = (rexport->format & ~GWY_RESULTS_REPORT_MACHINE);
    if (rexport->style == GWY_RESULTS_EXPORT_TABULAR_DATA) {
        wantnformats = 2;
        if (base_format == GWY_RESULTS_REPORT_COLON)
            base_format = GWY_RESULTS_REPORT_TABSEP;
    }
    else
        wantnformats = 3;

    if (wantnformats != nformats) {
        off = (wantnformats == 2) ? 1 : 0;
        if (priv->format)
            gtk_widget_destroy(priv->format);
        combo = gwy_enum_combo_box_new(formats + off, wantnformats, G_CALLBACK(format_changed), rexport,
                                       base_format, TRUE);
        priv->format = combo;
        gtk_widget_set_tooltip_text(combo, _("Result formatting"));
        gtk_box_pack_end(GTK_BOX(rexport), combo, FALSE, FALSE, 0);
        /* A bit confusing when packing from end, but +1 seems to work. */
        gtk_box_reorder_child(GTK_BOX(rexport), combo, copypos+1);
    }

    if (!priv->machine) {
        is_machine = rexport->format & GWY_RESULTS_REPORT_MACHINE;
        priv->machine = create_image_toggle(rexport, GWY_STOCK_SCIENTIFIC_NUMBER_FORMAT,
                                            _("Machine-readable format"), is_machine);
        g_signal_connect_swapped(priv->machine, "toggled", G_CALLBACK(machine_changed), rexport);
    }

    /* The format might not changed but was kind of undefined.  Emit a signal
     * to make sure listeneres set the format now. */
    if (!priv->updating)
        g_signal_emit(rexport, results_export_signals[FORMAT_CHANGED], 0);
}

/**
 * gwy_results_export_set_format:
 * @rexport: Controls for result set export.
 * @format: Format to select.
 *
 * Sets the selected format in result set export controls.
 *
 * Since: 2.50
 **/
void
gwy_results_export_set_format(GwyResultsExport *rexport,
                              GwyResultsReportType format)
{
    gboolean for_machine;
    guint base_format;

    g_return_if_fail(GWY_IS_RESULTS_EXPORT(rexport));

    if (format == rexport->format)
        return;

    for_machine = !!(format & GWY_RESULTS_REPORT_MACHINE);
    base_format = (format & ~GWY_RESULTS_REPORT_MACHINE);
    g_return_if_fail(base_format == GWY_RESULTS_REPORT_COLON
                     || base_format == GWY_RESULTS_REPORT_TABSEP
                     || base_format == GWY_RESULTS_REPORT_CSV);

    g_return_if_fail(!rexport->priv->updating);
    rexport->format = format;
    rexport->priv->updating = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rexport->priv->machine), for_machine);
    gwy_enum_combo_box_set_active(GTK_COMBO_BOX(rexport->priv->format), base_format);
    rexport->priv->updating = FALSE;
}

/**
 * gwy_results_export_get_format:
 * @rexport: Controls for result set export.
 *
 * Gets the selected format in result set export controls.
 *
 * Returns: Currently selected format.
 *
 * Since: 2.50
 **/
GwyResultsReportType
gwy_results_export_get_format(GwyResultsExport *rexport)
{
    g_return_val_if_fail(GWY_IS_RESULTS_EXPORT(rexport), 0);
    return rexport->format;
}

/**
 * gwy_results_export_set_results:
 * @rexport: Controls for result set export.
 * @results: Set of reported scalar values.  May be %NULL to unset.
 *
 * Sets the set of scalar values to save or copy by result set export controls.
 *
 * If you supply a result set to save or copy @rexport handles the Save and Copy buttons itself.  So you do not need
 * to connect to the "copy" and "save" signals, except if you need to ensure @results are updated.
 *
 * Since: 2.50
 **/
void
gwy_results_export_set_results(GwyResultsExport *rexport,
                               GwyResults *results)
{
    GwyResults *old;

    g_return_if_fail(GWY_IS_RESULTS_EXPORT(rexport));
    g_return_if_fail(!results || GWY_IS_RESULTS(results));

    if (rexport->priv->results == results)
        return;

    if (results)
        g_object_ref(results);
    old = rexport->priv->results;
    rexport->priv->results = results;
    GWY_OBJECT_UNREF(old);
}

/**
 * gwy_results_export_get_results:
 * @rexport: Controls for result set export.
 *
 * Gets the set of scalar values to save or copy by result set export controls.
 *
 * Returns: The #GwyResults object, or %NULL.
 *
 * Since: 2.50
 **/
GwyResults*
gwy_results_export_get_results(GwyResultsExport *rexport)
{
    g_return_val_if_fail(GWY_IS_RESULTS_EXPORT(rexport), NULL);
    return rexport->priv->results;
}

/**
 * gwy_results_export_set_title:
 * @rexport: Controls for result set export.
 * @title: File save dialogue title (or %NULL to use the default).
 *
 * Sets the title of file save dialogue for result set export controls.
 *
 * Since: 2.50
 **/
void
gwy_results_export_set_title(GwyResultsExport *rexport,
                             const gchar *title)
{
    g_return_if_fail(GWY_IS_RESULTS_EXPORT(rexport));
    gwy_assign_string(&rexport->priv->title, title);
}

/**
 * gwy_results_export_set_style:
 * @rexport: Controls for result set export.
 * @style: Report style.
 *
 * Sets the report style for result set export controls.
 *
 * The report style determines which controls will be shown.  For %GWY_RESULTS_EXPORT_FIXED_FORMAT only the action
 * buttons are shown. Tabular data (%GWY_RESULTS_EXPORT_TABULAR_DATA) do not have the %GWY_RESULTS_REPORT_COLON
 * option.  The default style %GWY_RESULTS_EXPORT_PARAMETERS have the full controls.
 *
 * Since: 2.50
 **/
void
gwy_results_export_set_style(GwyResultsExport *rexport,
                             GwyResultsExportStyle style)
{
    g_return_if_fail(GWY_IS_RESULTS_EXPORT(rexport));
    rexport->style = style;
    update_format_controls(rexport);
}

/**
 * gwy_results_export_get_style:
 * @rexport: Controls for result set export.
 *
 * Gets the report style for result set export controls.
 *
 * See gwy_results_export_set_style() for discussion.
 *
 * Returns: The style of the results export controls.
 *
 * Since: 2.50
 **/
GwyResultsExportStyle
gwy_results_export_get_style(GwyResultsExport *rexport)
{
    g_return_val_if_fail(GWY_IS_RESULTS_EXPORT(rexport), GWY_RESULTS_EXPORT_PARAMETERS);
    return rexport->style;
}

/**
 * gwy_results_export_set_actions_sensitive:
 * @rexport: Controls for result set export.
 * @sensitive: %TRUE to make the action buttons sensitive, %FALSE to make them insensitive.
 *
 * Makes the action buttons (copy and save) sensitive or insensitive.
 *
 * This is generally preferred to making the entire widget insensitive as the format can be selected independently on
 * whether there is already anything to export.
 *
 * Since: 2.50
 **/
void
gwy_results_export_set_actions_sensitive(GwyResultsExport *rexport,
                                         gboolean sensitive)
{
    g_return_if_fail(GWY_IS_RESULTS_EXPORT(rexport));
    gtk_widget_set_sensitive(rexport->copy, sensitive);
    gtk_widget_set_sensitive(rexport->save, sensitive);
}

static void
fill_result_string_from_container(GwyResults *results, const gchar *id,
                                  GwyContainer *container, GQuark quark)
{
    const guchar *name;

    if (gwy_container_gis_string(container, quark, &name))
        gwy_results_fill_values(results, id, name, NULL);
    else
        gwy_results_set_na(results, id, NULL);
}

/**
 * gwy_results_fill_filename:
 * @results: Set of reported scalar values.
 * @id: Value identifier.
 * @container: Data container corresponding to a file.
 *
 * Fills data file name in a set of reported scalar values.
 *
 * This is a helper function for #GwyResults.  If @container has no file name
 * associated the value @id is set to N.A.
 *
 * Since: 2.50
 **/
void
gwy_results_fill_filename(GwyResults *results,
                          const gchar *id,
                          GwyContainer *container)
{
    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);
    g_return_if_fail(GWY_IS_CONTAINER(container));
    fill_result_string_from_container(results, id, container, g_quark_from_static_string("/filename"));
}

/**
 * gwy_results_fill_channel:
 * @results: Set of reported scalar values.
 * @id: Value identifier.
 * @container: Data container corresponding to a file.
 * @i: Channel number.
 *
 * Fills image channel title in a set of reported scalar values.
 *
 * This is a helper function for #GwyResults.  If the image has no title the associated the value @id is set to N.A.
 *
 * Since: 2.50
 **/
void
gwy_results_fill_channel(GwyResults *results,
                         const gchar *id,
                         GwyContainer *container,
                         gint i)
{
    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);
    g_return_if_fail(GWY_IS_CONTAINER(container));
    fill_result_string_from_container(results, id, container, gwy_app_get_data_title_key_for_id(i));
}

/**
 * gwy_results_fill_volume:
 * @results: Set of reported scalar values.
 * @id: Value identifier.
 * @container: Data container corresponding to a file.
 * @i: Volume data number.
 *
 * Fills volume data title in a set of reported scalar values.
 *
 * This is a helper function for #GwyResults.  If the volume data have no title the associated the value @id is set to
 * N.A.
 *
 * Since: 2.50
 **/
void
gwy_results_fill_volume(GwyResults *results,
                        const gchar *id,
                        GwyContainer *container,
                        gint i)
{
    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);
    g_return_if_fail(GWY_IS_CONTAINER(container));
    fill_result_string_from_container(results, id, container, gwy_app_get_brick_title_key_for_id(i));
}

/**
 * gwy_results_fill_xyz:
 * @results: Set of reported scalar values.
 * @id: Value identifier.
 * @container: Data container corresponding to a file.
 * @i: XYZ data number.
 *
 * Fills XYZ data title in a set of reported scalar values.
 *
 * This is a helper function for #GwyResults.  If the XYZ data have no title the associated the value @id is set to
 * N.A.
 *
 * Since: 2.50
 **/
void
gwy_results_fill_xyz(GwyResults *results,
                     const gchar *id,
                     GwyContainer *container,
                     gint i)
{
    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);
    g_return_if_fail(GWY_IS_CONTAINER(container));
    fill_result_string_from_container(results, id, container, gwy_app_get_surface_title_key_for_id(i));
}

/**
 * gwy_results_fill_curve_map:
 * @results: Set of reported scalar values.
 * @id: Value identifier.
 * @container: Data container corresponding to a file.
 * @i: Curve map data number.
 *
 * Fills curve map data title in a set of reported scalar values.
 *
 * This is a helper function for #GwyResults.  If the curve map data have no title the associated the value @id is set
 * to N.A.
 *
 * Since: 2.60
 **/
void
gwy_results_fill_curve_map(GwyResults *results,
                           const gchar *id,
                           GwyContainer *container,
                           gint i)
{
    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);
    g_return_if_fail(GWY_IS_CONTAINER(container));
    fill_result_string_from_container(results, id, container, gwy_app_get_lawn_title_key_for_id(i));
}

/**
 * gwy_results_fill_graph:
 * @results: Set of reported scalar values.
 * @id: Value identifier.
 * @graphmodel: A graph model.
 *
 * Fills graph title in a set of reported scalar values.
 *
 * This is a helper function for #GwyResults.
 *
 * Since: 2.53
 **/
void
gwy_results_fill_graph(GwyResults *results,
                       const gchar *id,
                       GwyGraphModel *graphmodel)
{
    gchar *title = NULL;

    g_return_if_fail(GWY_IS_GRAPH_MODEL(graphmodel));
    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);

    g_object_get(graphmodel, "title", &title, NULL);
    gwy_results_fill_values(results, id, title, NULL);
    g_free(title);
}

/**
 * gwy_results_fill_graph_curve:
 * @results: Set of reported scalar values.
 * @id: Value identifier.
 * @container: Data container corresponding to a file.
 * @curvemodel: A graph curve model.
 *
 * Fills graph curve description in a set of reported scalar values.
 *
 * This is a helper function for #GwyResults.
 *
 * Since: 2.53
 **/
void
gwy_results_fill_graph_curve(GwyResults *results,
                             const gchar *id,
                             GwyGraphCurveModel *curvemodel)
{
    gchar *title = NULL;

    g_return_if_fail(GWY_IS_GRAPH_CURVE_MODEL(curvemodel));
    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);

    g_object_get(curvemodel, "description", &title, NULL);
    gwy_results_fill_values(results, id, title, NULL);
    g_free(title);
}

/**
 * gwy_results_fill_lawn_curve:
 * @results: Set of reported scalar values.
 * @id: Value identifier.
 * @lawn: Curve map data object.
 * @i: Curve number in @lawn.
 *
 * Fills lawn curve description in a set of reported scalar values.
 *
 * This is a helper function for #GwyResults.
 *
 * Since: 2.60
 **/
void
gwy_results_fill_lawn_curve(GwyResults *results,
                            const gchar *id,
                            GwyLawn *lawn,
                            gint i)
{
    const gchar *title;
    gchar *s;

    g_return_if_fail(GWY_IS_LAWN(lawn));
    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);
    g_return_if_fail(i >= 0 && i < gwy_lawn_get_n_curves(lawn));

    if ((title = gwy_lawn_get_curve_label(lawn, i)))
        gwy_results_fill_values(results, id, title, NULL);
    else {
        s = g_strdup_printf("%s %d", _("Untitled"), i);
        gwy_results_fill_values(results, id, s, NULL);
        g_free(s);
    }
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyresultsexport
 * @title: GwyResultsExport
 * @short_description: Controls for value set export
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
