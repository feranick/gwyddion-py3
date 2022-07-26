/*
 *  $Id: gwydgetutils.c 23215 2021-03-11 16:30:24Z yeti-dn $
 *  Copyright (C) 2003-2017 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwydebugobjects.h>
#include <libdraw/gwydraw.h>
#ifndef GDK_WINDOWING_QUARTZ
/* Produces a warning with -Wundef in Fedora.  Reported as Bug 1414319. */
#include <pango/pangoft2.h>
#endif
#include <libgwydgets/gwyadjustbar.h>
#include <libgwydgets/gwydgetutils.h>

enum {
    GWY_HSCALE_WIDTH = 96
};

static void gwy_hscale_update_log (GtkAdjustment *adj,
                                   GtkAdjustment *slave);
static void gwy_hscale_update_exp (GtkAdjustment *adj,
                                   GtkAdjustment *slave);
static void gwy_hscale_update_sqrt(GtkAdjustment *adj,
                                   GtkAdjustment *slave);
static void gwy_hscale_update_sq  (GtkAdjustment *adj,
                                   GtkAdjustment *slave);
static void hscale_set_sensitive  (GtkObject *pivot,
                                   gboolean sensitive);
static void disconnect_slave      (GtkWidget *slave,
                                   GtkWidget *master);
static void disconnect_master     (GtkWidget *master,
                                   GtkWidget *slave);

/************************** Table attaching ****************************/

/**
 * gwy_table_attach_spinbutton:
 * @table: A #GtkTable.
 * @row: Table row to attach to.
 * @name: The label before @adj.
 * @units: The label after @adj.
 * @adj: An adjustment to create spinbutton from.
 *
 * Attaches a spinbutton with two labels to a table.
 *
 * Returns: The spinbutton as a #GtkWidget.
 **/
GtkWidget*
gwy_table_attach_spinbutton(GtkWidget *table,
                            gint row,
                            const gchar *name,
                            const gchar *units,
                            GtkObject *adj)
{
    GtkWidget *spin;

    g_return_val_if_fail(GTK_IS_TABLE(table), NULL);
    if (adj)
        g_return_val_if_fail(GTK_IS_ADJUSTMENT(adj), NULL);
    else
        adj = gtk_adjustment_new(0, 0, 0, 0, 0, 0);

    spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0.1, 0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gwy_table_attach_row(table, row, name, units, spin);

    return spin;
}

/**
 * gwy_table_attach_row:
 * @table: A #GtkTable.
 * @row: Table row to attach to.
 * @name: The label before @middle_widget.
 * @units: The label after @adj.
 * @middle_widget: A widget.
 *
 * Attaches a widget with two labels to a table.
 **/
void
gwy_table_attach_row(GtkWidget *table,
                     gint row,
                     const gchar *name,
                     const gchar *units,
                     GtkWidget *middle_widget)
{
    GtkWidget *label;

    g_return_if_fail(GTK_IS_TABLE(table));
    g_return_if_fail(GTK_IS_WIDGET(middle_widget));

    label = gtk_label_new_with_mnemonic(name);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

    gtk_table_attach(GTK_TABLE(table), middle_widget,
                     1, 2, row, row+1, GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), middle_widget);
    g_object_set_data(G_OBJECT(middle_widget), "label", label);
    g_object_set_data(G_OBJECT(middle_widget), "middle_widget", middle_widget);

    if (units) {
        label = gtk_label_new(units);
        gtk_table_attach(GTK_TABLE(table), label,
                         2, 3, row, row+1, GTK_FILL, 0, 0, 0);
        gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
        g_object_set_data(G_OBJECT(middle_widget), "units", label);
    }
}

/**
 * gwy_table_get_child_widget:
 * @table: A #GtkTable.
 * @row: Row in @table.
 * @col: Column in @table.
 *
 * Finds a widget in #GtkTable by its coordinates.
 *
 * Coordinates (@col, @row) are taken as coordinates of widget top left corner.
 * More precisely, the returned widget either contains the specified grid
 * point or it is attached by its left side, top side, or top left corner to
 * this point.
 *
 * If there are multiple matches due to overlapping widgets, a random
 * match is returned.
 *
 * Returns: The widget at (@col, @row) or %NULL if there is no such widget.
 **/
GtkWidget*
gwy_table_get_child_widget(GtkWidget *table,
                           gint row,
                           gint col)
{
    GList *l;

    g_return_val_if_fail(GTK_IS_TABLE(table), NULL);
    for (l = GTK_TABLE(table)->children; l; l = g_list_next(l)) {
        GtkTableChild *child = (GtkTableChild*)l->data;

        if (child->left_attach <= col && child->right_attach > col
            && child->top_attach <= row && child->bottom_attach > row)
            return child->widget;
    }
    return NULL;
}

/************************** Scale attaching ****************************/

static gdouble
ssqrt(gdouble x)
{
    return (x < 0.0) ? -sqrt(fabs(x)) : sqrt(x);
}

static gdouble
ssqr(gdouble x)
{
    return x*fabs(x);
}

static void
gwy_hscale_update_log(GtkAdjustment *adj, GtkAdjustment *slave)
{
    gulong id;

    id = g_signal_handler_find(slave,
                               G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                               0, 0, 0, gwy_hscale_update_exp, adj);
    g_signal_handler_block(slave, id);
    gtk_adjustment_set_value(slave, log(adj->value));
    g_signal_handler_unblock(slave, id);
}

static void
gwy_hscale_update_exp(GtkAdjustment *adj, GtkAdjustment *slave)
{
    gulong id;

    id = g_signal_handler_find(slave,
                               G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                               0, 0, 0, gwy_hscale_update_log, adj);
    g_signal_handler_block(slave, id);
    gtk_adjustment_set_value(slave, exp(adj->value));
    g_signal_handler_unblock(slave, id);
}

static void
gwy_hscale_update_sqrt(GtkAdjustment *adj, GtkAdjustment *slave)
{
    gulong id;

    id = g_signal_handler_find(slave,
                               G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                               0, 0, 0, gwy_hscale_update_sq, adj);
    g_signal_handler_block(slave, id);
    gtk_adjustment_set_value(slave, ssqrt(adj->value));
    g_signal_handler_unblock(slave, id);
}

static void
gwy_hscale_update_sq(GtkAdjustment *adj, GtkAdjustment *slave)
{
    gulong id;

    id = g_signal_handler_find(slave,
                               G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                               0, 0, 0, gwy_hscale_update_sqrt, adj);
    g_signal_handler_block(slave, id);
    gtk_adjustment_set_value(slave, ssqr(adj->value));
    g_signal_handler_unblock(slave, id);
}

/**
 * gwy_table_hscale_set_sensitive:
 * @pivot: The same object that was passed to gwy_table_attach_hscale() as
 *         @pivot.
 * @sensitive: %TRUE to make the row sensitive, %FALSE to insensitive.
 *
 * Sets sensitivity of a group of controls create by gwy_table_attach_hscale()
 * or gwy_table_attach_adjbar().
 *
 * For controls without an enable/disable check button controls the sensitivity
 * as expected.  If the hscale was created with %GWY_HSCALE_CHECK you usually
 * manage its sensitivity by setting state of the check button instead.
 * Only use this function when you want to enable/disable the entire group
 * of controls, including the check button.
 *
 * This function can also be used with rows created by
 * gwy_table_attach_spinbutton() too if the spinbutton is passed as @pivot.
 **/
void
gwy_table_hscale_set_sensitive(GtkObject *pivot, gboolean sensitive)
{
    GtkWidget *scale, *check;
    gboolean sens = sensitive;

    g_object_set_data(G_OBJECT(pivot), "gwy-explicit-disable",
                      GINT_TO_POINTER(!sensitive));

    if ((check = gwy_table_hscale_get_check(pivot))
        && GTK_IS_CHECK_BUTTON(check)) {
        sens = (sensitive
                && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)));
    }
    else
        check = NULL;

    hscale_set_sensitive(pivot, sens);

    if (check) {
        scale = gwy_table_hscale_get_scale(pivot);
        if (scale && GWY_IS_ADJUST_BAR(scale))
            gtk_widget_set_sensitive(scale, sensitive);
        else
            gtk_widget_set_sensitive(check, sensitive);
    }
}

static void
hscale_checkbutton_toggled(GtkToggleButton *check, GtkObject *pivot)
{
    gboolean sens, active;

    active = gtk_toggle_button_get_active(check);
    sens = !GPOINTER_TO_INT(g_object_get_data(G_OBJECT(pivot),
                                              "gwy-explicit-disable"));
    hscale_set_sensitive(pivot, active && sens);
}

static void
hscale_set_sensitive(GtkObject *pivot, gboolean sensitive)
{
    GtkWidget *widget;
    gboolean is_adjbar = FALSE;

    widget = gwy_table_hscale_get_scale(pivot);
    if (widget && GWY_IS_ADJUST_BAR(widget))
        is_adjbar = TRUE;

    if (is_adjbar)
        gwy_adjust_bar_set_bar_sensitive(GWY_ADJUST_BAR(widget), sensitive);
    else {
        if (widget)
            gtk_widget_set_sensitive(widget, sensitive);
        if ((widget = gwy_table_hscale_get_label(pivot)))
            gtk_widget_set_sensitive(widget, sensitive);
    }

    if ((widget = gwy_table_hscale_get_middle_widget(pivot)))
        gtk_widget_set_sensitive(widget, sensitive);
    if ((widget = gwy_table_hscale_get_units(pivot)))
        gtk_widget_set_sensitive(widget, sensitive);
}

/**
 * gwy_table_attach_hscale:
 * @table: A #GtkTable.
 * @row: Row in @table to attach stuff to.
 * @name: The label before @pivot widget.
 * @units: The label after @pivot widget.
 * @pivot: Either a #GtkAdjustment, or a widget to use instead of the spin
 *         button and scale widgets (if @style is %GWY_HSCALE_WIDGET).
 * @style: A mix of options an flags determining what and how will be attached
 *         to the table.
 *
 * Attaches a spinbutton with a scale and labels, or something else, to a table
 * row.
 *
 * The group of controls takes four table columns: label, scale, spinbutton
 * and units.
 *
 * You can use functions gwy_table_hscale_get_scale(),
 * gwy_table_hscale_get_check(), etc. to get the various widgets from pivot
 * later.
 *
 * The function usually does the right thing but what exactly happens with
 * various @style values is a bit convoluted.
 *
 * Returns: The middle widget.  If a spinbutton is attached, then this
 *          spinbutton is returned.  Otherwise (in %GWY_HSCALE_WIDGET case)
 *          @pivot itself.
 **/
GtkWidget*
gwy_table_attach_hscale(GtkWidget *table, gint row,
                        const gchar *name, const gchar *units,
                        GtkObject *pivot,
                        GwyHScaleStyle style)
{
    GtkWidget *spin, *scale, *label, *check = NULL, *middle_widget, *align;
    GtkAdjustment *scale_adj = NULL, *adj = NULL;
    GwyHScaleStyle base_style;
    GtkTable *tab;
    gdouble u, l;
    gint digits;

    g_return_val_if_fail(GTK_IS_TABLE(table), NULL);
    tab = GTK_TABLE(table);

    base_style = style & ~(GWY_HSCALE_CHECK | GWY_HSCALE_SNAP);
    switch (base_style) {
        case GWY_HSCALE_DEFAULT:
        case GWY_HSCALE_NO_SCALE:
        case GWY_HSCALE_LOG:
        case GWY_HSCALE_SQRT:
        case GWY_HSCALE_LINEAR:
        if (pivot) {
            g_return_val_if_fail(GTK_IS_ADJUSTMENT(pivot), NULL);
            adj = GTK_ADJUSTMENT(pivot);
        }
        else {
            if (base_style == GWY_HSCALE_LOG || base_style == GWY_HSCALE_SQRT)
                g_warning("Nonlinear scale doesn't work with implicit adj.");
            adj = GTK_ADJUSTMENT(gtk_adjustment_new(0.01, 0.01, 1.00, 0.01, 0.1,
                                                    0));
        }
        break;

        case GWY_HSCALE_WIDGET:
        case GWY_HSCALE_WIDGET_NO_EXPAND:
        g_return_val_if_fail(GTK_IS_WIDGET(pivot), NULL);
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }

    if (style & GWY_HSCALE_SNAP) {
        g_warning("gwy_table_attach_hscale() does not implement "
                  "snapping to ticks.");
    }

    if (base_style != GWY_HSCALE_WIDGET
        && base_style != GWY_HSCALE_WIDGET_NO_EXPAND) {
        u = adj->step_increment;
        digits = (u > 0.0) ? (gint)floor(-log10(u)) : 0;
        spin = gtk_spin_button_new(adj, 0.1, MAX(digits, 0));
        u = adj->value;
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
        gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(spin),
                                          style & GWY_HSCALE_SNAP);
        gtk_table_attach(tab, spin, 2, 3, row, row+1, GTK_FILL, 0, 0, 0);
        gtk_adjustment_set_value(adj, u);
        middle_widget = spin;

        if (base_style == GWY_HSCALE_LOG) {
            u = log(adj->upper);
            l = log(adj->lower);
            scale_adj
                = GTK_ADJUSTMENT(gtk_adjustment_new(log(adj->value),
                                                    l, u,
                                                    (u - l)/GWY_HSCALE_WIDTH,
                                                    10*(u - l)/GWY_HSCALE_WIDTH,
                                                    0));
            g_signal_connect(adj, "value-changed",
                             G_CALLBACK(gwy_hscale_update_log), scale_adj);
            g_signal_connect(scale_adj, "value-changed",
                             G_CALLBACK(gwy_hscale_update_exp), adj);
        }
        else if (base_style == GWY_HSCALE_SQRT) {
            u = sqrt(adj->upper);
            l = sqrt(adj->lower);
            scale_adj
                = GTK_ADJUSTMENT(gtk_adjustment_new(ssqrt(adj->value),
                                                    l, u,
                                                    (u - l)/GWY_HSCALE_WIDTH,
                                                    10*(u - l)/GWY_HSCALE_WIDTH,
                                                    0));
            g_signal_connect(adj, "value-changed",
                             G_CALLBACK(gwy_hscale_update_sqrt), scale_adj);
            g_signal_connect(scale_adj, "value-changed",
                             G_CALLBACK(gwy_hscale_update_sq), adj);
        }
        else
            scale_adj = adj;
    }
    else {
        align = middle_widget = GTK_WIDGET(pivot);
        if (base_style == GWY_HSCALE_WIDGET_NO_EXPAND) {
            if (GTK_IS_MISC(middle_widget))
                gtk_misc_set_alignment(GTK_MISC(middle_widget), 0.0, 0.5);
            else {
                align = gtk_alignment_new(0.0, 0.5, 0.0, 0.0);
                gtk_container_add(GTK_CONTAINER(align), middle_widget);
            }
        }
        gtk_table_attach(GTK_TABLE(table), align, 1, 3, row, row+1,
                         GTK_EXPAND | GTK_FILL, 0, 0, 0);
    }
    g_object_set_data(G_OBJECT(pivot), "middle_widget", middle_widget);

    if (base_style == GWY_HSCALE_DEFAULT
        || base_style == GWY_HSCALE_LOG
        || base_style == GWY_HSCALE_SQRT
        || base_style == GWY_HSCALE_LINEAR) {
        scale = gtk_hscale_new(scale_adj);
        gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
        gtk_widget_set_size_request(scale, GWY_HSCALE_WIDTH, -1);
        gtk_table_attach(tab, scale, 1, 2, row, row+1,
                         GTK_EXPAND | GTK_FILL, 0, 0, 0);
        g_object_set_data(G_OBJECT(pivot), "scale", scale);
    }

    if (style & GWY_HSCALE_CHECK) {
        check = gtk_check_button_new_with_mnemonic(name);
        gtk_table_attach(tab, check, 0, 1, row, row+1, GTK_FILL, 0, 0, 0);
        g_signal_connect(check, "toggled",
                         G_CALLBACK(hscale_checkbutton_toggled), pivot);
        g_object_set_data(G_OBJECT(pivot), "check", check);
    }
    else {
        label = gtk_label_new_with_mnemonic(name);
        gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
        gtk_table_attach(tab, label, 0, 1, row, row+1, GTK_FILL, 0, 0, 0);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), middle_widget);
        g_object_set_data(G_OBJECT(pivot), "label", label);
    }

    if (units) {
        label = gtk_label_new(units);
        gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
        gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
        gtk_table_attach(tab, label, 3, 4, row, row+1, GTK_FILL, 0, 0, 0);
        g_object_set_data(G_OBJECT(pivot), "units", label);
    }

    if (check)
        hscale_checkbutton_toggled(GTK_TOGGLE_BUTTON(check), pivot);

    return middle_widget;
}

/**
 * gwy_table_attach_adjbar:
 * @table: A #GtkTable.
 * @row: Row in @table to attach stuff to.
 * @name: The label before @pivot widget.
 * @units: The label after @pivot widget.
 * @pivot: Either a #GtkAdjustment, or a widget to use instead of the spin
 *         button and scale widgets (if @style is %GWY_HSCALE_WIDGET).
 * @style: A mix of options an flags determining what and how will be attached
 *         to the table.
 *
 * Attaches an adjustment bar with spinbutton and labels, or something else, to
 * a table row.
 *
 * The group of controls takes three table columns: adjustment bar, spinbutton
 * and units.
 *
 * You can use functions gwy_table_hscale_get_scale(),
 * gwy_table_hscale_get_check(), etc. to get the various widgets from pivot
 * later.
 *
 * The function usually does the right thing but what exactly happens with
 * various @style values is a bit convoluted.
 *
 * Returns: The middle widget.  If a spinbutton is attached, then this
 *          spinbutton is returned.  Otherwise (in %GWY_HSCALE_WIDGET case)
 *          @pivot itself.
 *
 * Since: 2.49
 **/
GtkWidget*
gwy_table_attach_adjbar(GtkWidget *table, gint row,
                        const gchar *name, const gchar *units,
                        GtkObject *pivot,
                        GwyHScaleStyle style)
{
    GtkWidget *spin, *label, *check = NULL, *middle_widget, *hbox;
    GtkAdjustment *adj = NULL;
    GwyHScaleStyle base_style;
    GwyAdjustBar *adjbar;
    GtkTable *tab;
    gdouble u;
    gint digits, spacing;

    g_return_val_if_fail(GTK_IS_TABLE(table), NULL);
    tab = GTK_TABLE(table);

    base_style = style & ~(GWY_HSCALE_CHECK | GWY_HSCALE_SNAP);
    switch (base_style) {
        case GWY_HSCALE_DEFAULT:
        case GWY_HSCALE_NO_SCALE:
        case GWY_HSCALE_LOG:
        case GWY_HSCALE_SQRT:
        case GWY_HSCALE_LINEAR:
        if (pivot) {
            g_return_val_if_fail(GTK_IS_ADJUSTMENT(pivot), NULL);
            adj = GTK_ADJUSTMENT(pivot);
        }
        else {
            if (base_style == GWY_HSCALE_LOG || base_style == GWY_HSCALE_SQRT)
                g_warning("Nonlinear scale doesn't work with implicit adj.");
            adj = GTK_ADJUSTMENT(gtk_adjustment_new(0.01, 0.01, 1.00, 0.01, 0.1,
                                                    0));
        }
        break;

        case GWY_HSCALE_WIDGET:
        case GWY_HSCALE_WIDGET_NO_EXPAND:
        g_return_val_if_fail(GTK_IS_WIDGET(pivot), NULL);
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }

    if ((style & GWY_HSCALE_SNAP)
        && (base_style == GWY_HSCALE_NO_SCALE
            || base_style == GWY_HSCALE_WIDGET
            || base_style == GWY_HSCALE_WIDGET_NO_EXPAND)) {
        g_warning("There is no adjust bar that could snap to ticks.");
    }

    if (base_style == GWY_HSCALE_WIDGET
        || base_style == GWY_HSCALE_WIDGET_NO_EXPAND) {
        middle_widget = GTK_WIDGET(pivot);

        g_object_get(table, "column-spacing", &spacing, NULL);
        hbox = gtk_hbox_new(FALSE, spacing);
        gtk_table_attach(GTK_TABLE(table), hbox, 0, 2, row, row+1,
                         GTK_FILL, 0, 0, 0);

        if (base_style == GWY_HSCALE_WIDGET_NO_EXPAND)
            gtk_box_pack_end(GTK_BOX(hbox), middle_widget, FALSE, FALSE, 0);
        else
            gtk_box_pack_end(GTK_BOX(hbox), middle_widget, TRUE, TRUE, 0);

        if (style & GWY_HSCALE_CHECK) {
            check = gtk_check_button_new_with_mnemonic(name);
            gtk_box_pack_start(GTK_BOX(hbox), check, FALSE, FALSE, 0);
            g_signal_connect(check, "toggled",
                             G_CALLBACK(hscale_checkbutton_toggled), pivot);
            g_object_set_data(G_OBJECT(pivot), "check", check);
        }
        else {
            label = gtk_label_new_with_mnemonic(name);
            gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
            gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
            gtk_label_set_mnemonic_widget(GTK_LABEL(label), middle_widget);
            g_object_set_data(G_OBJECT(pivot), "label", label);
        }
    }
    else {
        u = adj->step_increment;
        digits = (u > 0.0) ? (gint)floor(-log10(u)) : 0;
        spin = gtk_spin_button_new(adj, 0.1, MAX(digits, 0));
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
        gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(spin),
                                          style & GWY_HSCALE_SNAP);
        gtk_table_attach(tab, spin, 1, 2, row, row+1, GTK_FILL, 0, 0, 0);
        middle_widget = spin;

        if (base_style == GWY_HSCALE_NO_SCALE) {
            if (style & GWY_HSCALE_CHECK) {
                check = gtk_check_button_new_with_mnemonic(name);
                gtk_table_attach(tab, check, 0, 1, row, row+1,
                                 GTK_EXPAND | GTK_FILL, 0, 0, 0);
                g_signal_connect(check, "toggled",
                                 G_CALLBACK(hscale_checkbutton_toggled), pivot);
                g_object_set_data(G_OBJECT(pivot), "check", check);
            }
            else {
                label = gtk_label_new_with_mnemonic(name);
                gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
                gtk_table_attach(tab, label, 0, 1, row, row+1,
                                 GTK_EXPAND | GTK_FILL, 0, 0, 0);
                gtk_label_set_mnemonic_widget(GTK_LABEL(label), middle_widget);
                g_object_set_data(G_OBJECT(pivot), "label", label);
            }
        }
        else {
            adjbar = GWY_ADJUST_BAR(gwy_adjust_bar_new(adj, name));
            if (base_style == GWY_HSCALE_LINEAR)
                gwy_adjust_bar_set_mapping(adjbar, GWY_SCALE_MAPPING_LINEAR);
            else if (base_style == GWY_HSCALE_SQRT)
                gwy_adjust_bar_set_mapping(adjbar, GWY_SCALE_MAPPING_SQRT);
            else if (base_style == GWY_HSCALE_LOG)
                gwy_adjust_bar_set_mapping(adjbar, GWY_SCALE_MAPPING_LOG);

            gtk_table_attach(tab, GTK_WIDGET(adjbar), 0, 1, row, row+1,
                             GTK_EXPAND | GTK_FILL, 0, 0, 0);
            g_object_set_data(G_OBJECT(pivot), "scale", adjbar);

            label = gwy_adjust_bar_get_label(adjbar);
            g_object_set_data(G_OBJECT(pivot), "label", label);
            gtk_label_set_mnemonic_widget(GTK_LABEL(label), middle_widget);

            if (style & GWY_HSCALE_CHECK) {
                gwy_adjust_bar_set_has_check_button(adjbar, TRUE);
                check = gwy_adjust_bar_get_check_button(adjbar);
                g_signal_connect(check, "toggled",
                                 G_CALLBACK(hscale_checkbutton_toggled), pivot);
                g_object_set_data(G_OBJECT(pivot), "check", check);
            }
            if (style & GWY_HSCALE_SNAP)
                gwy_adjust_bar_set_snap_to_ticks(adjbar, TRUE);
        }
    }

    g_object_set_data(G_OBJECT(pivot), "middle_widget", middle_widget);

    if (units) {
        label = gtk_label_new(units);
        gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
        gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
        gtk_table_attach(tab, label, 2, 3, row, row+1, GTK_FILL, 0, 0, 0);
        g_object_set_data(G_OBJECT(pivot), "units", label);
    }

    if (check)
        hscale_checkbutton_toggled(GTK_TOGGLE_BUTTON(check), pivot);

    return middle_widget;
}

/**
 * gwy_table_hscale_get_scale:
 * @pivot: Pivot object passed to gwy_table_attach_hscale().
 *
 * Gets the horizontal scale associated with a pivot object.
 *
 * May return %NULL if constructed with %GWY_HSCALE_NO_SCALE,
 * %GWY_HSCALE_WIDGET, or %GWY_HSCALE_WIDGET_NO_EXPAND.
 **/
GtkWidget*
gwy_table_hscale_get_scale(GtkObject *pivot)
{
    g_return_val_if_fail(pivot, NULL);
    return (GtkWidget*)g_object_get_data(G_OBJECT(pivot), "scale");
}

/**
 * gwy_table_hscale_get_check:
 * @pivot: Pivot object passed to gwy_table_attach_hscale().
 *
 * Gets the check button associated with a pivot object.
 *
 * May return %NULL if not constructed with %GWY_HSCALE_CHECK.
 **/
GtkWidget*
gwy_table_hscale_get_check(GtkObject *pivot)
{
    g_return_val_if_fail(pivot, NULL);
    return (GtkWidget*)g_object_get_data(G_OBJECT(pivot), "check");
}

/**
 * gwy_table_hscale_get_label:
 * @pivot: Pivot object passed to gwy_table_attach_hscale().
 *
 * Gets the (left) label associated with a pivot object.
 *
 * May return %NULL if constructed with %GWY_HSCALE_CHECK.
 **/
GtkWidget*
gwy_table_hscale_get_label(GtkObject *pivot)
{
    g_return_val_if_fail(pivot, NULL);
    return (GtkWidget*)g_object_get_data(G_OBJECT(pivot), "label");
}

/**
 * gwy_table_hscale_get_units:
 * @pivot: Pivot object passed to gwy_table_attach_hscale().
 *
 * Gets the units label associated with a pivot object.
 *
 * May return %NULL if constructed without units.
 **/
GtkWidget*
gwy_table_hscale_get_units(GtkObject *pivot)
{
    g_return_val_if_fail(pivot, NULL);
    return (GtkWidget*)g_object_get_data(G_OBJECT(pivot), "units");
}

/**
 * gwy_table_hscale_get_middle_widget:
 * @pivot: Pivot object passed to gwy_table_attach_hscale().
 *
 * Gets the middle widget associated with a pivot object.
 **/
GtkWidget*
gwy_table_hscale_get_middle_widget(GtkObject *pivot)
{
    g_return_val_if_fail(pivot, NULL);
    return (GtkWidget*)g_object_get_data(G_OBJECT(pivot), "middle_widget");
}

/************************** Mask colors ****************************/

typedef struct {
    GwyColorButton *color_button;
    GwyContainer *container;
    gchar *prefix;
} MaskColorSelectorData;

static void
mask_color_updated_cb(GtkWidget *sel, MaskColorSelectorData *mcsdata)
{
    GdkColor gdkcolor;
    guint16 gdkalpha;
    GwyRGBA rgba;

    gwy_debug("mcsdata = %p", mcsdata);
    if (gtk_color_selection_is_adjusting(GTK_COLOR_SELECTION(sel)))
        return;

    gtk_color_selection_get_current_color(GTK_COLOR_SELECTION(sel), &gdkcolor);
    gdkalpha = gtk_color_selection_get_current_alpha(GTK_COLOR_SELECTION(sel));

    gwy_rgba_from_gdk_color_and_alpha(&rgba, &gdkcolor, gdkalpha);
    gwy_rgba_store_to_container(&rgba, mcsdata->container, mcsdata->prefix);

    if (mcsdata->color_button)
        gwy_color_button_set_color(mcsdata->color_button, &rgba);
}

/**
 * gwy_color_selector_for_mask:
 * @dialog_title: Title of the color selection dialog (%NULL to use default).
 * @color_button: Color button to update on color change (or %NULL).
 * @container: Container to initialize the color from and save it to.
 * @prefix: Prefix in @container (normally "/0/mask").
 *
 * Creates and runs a color selector dialog for a mask.
 *
 * See gwy_mask_color_selector_run() for details.
 **/
void
gwy_color_selector_for_mask(const gchar *dialog_title,
                            GwyColorButton *color_button,
                            GwyContainer *container,
                            const gchar *prefix)
{
    gwy_mask_color_selector_run(dialog_title, NULL, color_button, container,
                                prefix);
}

/**
 * gwy_mask_color_selector_run:
 * @dialog_title: Title of the color selection dialog (%NULL to use default).
 * @parent: Dialog parent window.  The color selector dialog will be made
 *          transient for this window.
 * @color_button: Color button to update on color change (or %NULL).
 * @container: Container to initialize the color from and save it to.
 * @prefix: Prefix in @container (normally "/0/mask").
 *
 * Creates and runs a color selector dialog for a mask.
 *
 * Note this function does not return anything, it runs the color selection
 * dialog modally and returns when it is finished.
 *
 * Since: 2.1
 **/
void
gwy_mask_color_selector_run(const gchar *dialog_title,
                            GtkWindow *parent,
                            GwyColorButton *color_button,
                            GwyContainer *container,
                            const gchar *prefix)
{
    GtkWidget *selector, *dialog;
    MaskColorSelectorData *mcsdata;
    GdkColor gdkcolor;
    guint16 gdkalpha;
    GwyRGBA rgba;
    gint response;
    gboolean parent_is_modal;

    g_return_if_fail(prefix && *prefix == '/');

    mcsdata = g_new(MaskColorSelectorData, 1);
    mcsdata->color_button = color_button;
    mcsdata->container = container;
    mcsdata->prefix = g_strdup(prefix);

    gwy_rgba_get_from_container(&rgba, container, mcsdata->prefix);
    gwy_rgba_to_gdk_color(&rgba, &gdkcolor);
    gdkalpha = gwy_rgba_to_gdk_alpha(&rgba);

    dialog = gtk_color_selection_dialog_new(dialog_title
                                            ? dialog_title
                                            : _("Change Mask Color"));
    selector = GTK_COLOR_SELECTION_DIALOG(dialog)->colorsel;
    gtk_color_selection_set_current_color(GTK_COLOR_SELECTION(selector),
                                          &gdkcolor);
    gtk_color_selection_set_current_alpha(GTK_COLOR_SELECTION(selector),
                                          gdkalpha);
    gtk_color_selection_set_has_palette(GTK_COLOR_SELECTION(selector), FALSE);
    gtk_color_selection_set_has_opacity_control(GTK_COLOR_SELECTION(selector),
                                                TRUE);
    g_signal_connect(selector, "color-changed",
                     G_CALLBACK(mask_color_updated_cb), mcsdata);

    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
        /* Steal modality from the parent window, prevents appearing under it
         * on MS Windows */
        parent_is_modal = gtk_window_get_modal(parent);
        if (parent_is_modal)
            gtk_window_set_modal(parent, FALSE);
    }
    else
        parent_is_modal = FALSE;

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (parent_is_modal)
        gtk_window_set_modal(parent, TRUE);

    if (response != GTK_RESPONSE_OK) {
        gwy_rgba_store_to_container(&rgba, container, mcsdata->prefix);
        if (mcsdata->color_button)
            gwy_color_button_set_color(mcsdata->color_button, &rgba);
    }
    g_free(mcsdata->prefix);
    g_free(mcsdata);
}

/************************** ListStore ****************************/

/**
 * gwy_list_store_row_changed:
 * @store: A list store.
 * @iter: A tree model iterator in @store, or %NULL for none.
 * @path: A tree model path in @store, or %NULL for none.
 * @row: A row number in @store, or -1 for none.
 *
 * Convenience function to emit "GtkTreeModel::row-changed" signal on a tree
 * store.
 *
 * At least one of @iter, @path, @row must be set to identify the row to emit
 * "row-changed" on, and usually exactly one should be set.  The remaining
 * information necessary to call gtk_tree_model_row_changed() is inferred
 * automatically.
 *
 * The behaviour of this function is undefined for specified, but inconsistent
 * @iter, @path, and @row.
 **/
void
gwy_list_store_row_changed(GtkListStore *store,
                           GtkTreeIter *iter,
                           GtkTreePath *path,
                           gint row)
{
    GtkTreeIter myiter;
    GtkTreeModel *model;
    gboolean iter_ok;

    g_return_if_fail(GTK_IS_LIST_STORE(store));
    g_return_if_fail(iter || path || row >= 0);

    model = GTK_TREE_MODEL(store);
    if (iter && path) {
        gtk_tree_model_row_changed(model, path, iter);
        return;
    }

    if (!iter && row >= 0) {
        iter_ok = gtk_tree_model_iter_nth_child(model, &myiter, NULL, row);
        g_return_if_fail(iter_ok);
        iter = &myiter;
    }

    if (!iter) {
        iter_ok = gtk_tree_model_get_iter(model, &myiter, path);
        g_return_if_fail(iter_ok);
        iter = &myiter;
    }

    if (path) {
        gtk_tree_model_row_changed(model, path, iter);
        return;
    }

    path = gtk_tree_model_get_path(model, iter);
    gtk_tree_model_row_changed(model, path, iter);
    gtk_tree_path_free(path);
}

/************************** Activate on Unfocus ****************************/

static gboolean
activate_on_unfocus(GtkWidget *widget)
{
    gtk_widget_activate(widget);
    return FALSE;
}

/**
 * gwy_widget_get_activate_on_unfocus:
 * @widget: A widget.
 *
 * Obtains the activate-on-unfocus state of a widget.
 *
 * Returns: %TRUE if signal "GtkWidget::activate" is emitted when focus leaves
 *          the widget.
 *
 * Since: 2.5
 **/
gboolean
gwy_widget_get_activate_on_unfocus(GtkWidget *widget)
{
    g_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);
    return !!g_signal_handler_find(widget, G_SIGNAL_MATCH_FUNC,
                                   0, 0, NULL, activate_on_unfocus, NULL);
}

/**
 * gwy_widget_set_activate_on_unfocus:
 * @widget: A widget.
 * @activate: %TRUE to enable activate-on-unfocus, %FALSE disable it.
 *
 * Sets the activate-on-unfocus state of a widget.
 *
 * When it is enabled, signal "GtkWidget::activate" is emited whenever focus
 * leaves the widget.
 *
 * Since: 2.5
 **/
void
gwy_widget_set_activate_on_unfocus(GtkWidget *widget,
                                   gboolean activate)
{
    gulong id;

    g_return_if_fail(GTK_IS_WIDGET(widget));
    g_return_if_fail(GTK_WIDGET_GET_CLASS(widget)->activate_signal);
    id = g_signal_handler_find(widget, G_SIGNAL_MATCH_FUNC,
                               0, 0, NULL, activate_on_unfocus, NULL);
    if (id && !activate)
        g_signal_handler_disconnect(widget, id);
    if (!id && activate)
        g_signal_connect(widget, "focus-out-event",
                         G_CALLBACK(activate_on_unfocus), NULL);
}

/************************** Utils ****************************/

/**
 * gwy_dialog_prevent_delete_cb:
 *
 * Returns %TRUE.
 *
 * Use gtk_true() instead.
 *
 * Returns: %TRUE.
 **/
gboolean
gwy_dialog_prevent_delete_cb(void)
{
    return TRUE;
}

/**
 * gwy_hbox_new:
 * @spacing: The number of pixels to place between children by default.
 *
 * Creates a new horizontal #GtkBox.
 *
 * This is a lowest common denominator wrapper for creating an inhomogeneous
 * horizontal box. Use gtk_box_set_homogeneous() to get a homogeneous box.
 *
 * Returns: A new horizontal #GtkBox.
 *
 * Since: 2.59
 **/
GtkWidget*
gwy_hbox_new(gint spacing)
{
    return gtk_hbox_new(FALSE, spacing);
}

/**
 * gwy_vbox_new:
 * @spacing: The number of pixels to place between children by default.
 *
 * Creates a new vertical #GtkBox.
 *
 * This is a lowest common denominator wrapper for creating an inhomogeneous
 * vertical box. Use gtk_box_set_homogeneous() to get a homogeneous box.
 *
 * Returns: A new vertical #GtkBox.
 *
 * Since: 2.59
 **/
GtkWidget*
gwy_vbox_new(gint spacing)
{
    return gtk_vbox_new(FALSE, spacing);
}

/**
 * gwy_label_new_header:
 * @text: Text to put into the label.  It must be a valid markup and it will
 *        be made bold by adding appropriate markup around it.
 *
 * Creates a bold, left aligned label.
 *
 * The purpose of this function is to avoid propagation of too much markup to
 * translations (and to reduce code clutter by avoiding dummy constructor and
 * left-aligning automatically).
 *
 * Returns: A newly created #GtkLabel.
 **/
GtkWidget*
gwy_label_new_header(const gchar *text)
{
    GtkWidget *label;
    guint len;
    gchar *s;

    label = gtk_label_new(NULL);
    len = strlen(text);
    s = g_newa(gchar, len + sizeof("<b></b>"));
    s[0] = '\0';
    g_stpcpy(g_stpcpy(g_stpcpy(s, "<b>"), text), "</b>");
    gtk_label_set_markup(GTK_LABEL(label), s);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);

    return label;
}

/**
 * gwy_stock_like_button_new:
 * @label_text: Button label text (with mnemonic).
 * @stock_id: Button icon stock id.
 *
 * Creates a button that looks like a stock button, but can have different
 * label text.
 *
 * Returns: The newly created button as #GtkWidget.
 **/
GtkWidget*
gwy_stock_like_button_new(const gchar *label_text,
                          const gchar *stock_id)
{
    GtkWidget *button, *image;

    button = gtk_button_new_with_mnemonic(label_text);
    image = gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(button), image);

    return button;
}

/**
 * gwy_tool_like_button_new:
 * @label_text: Button label text (with mnemonic).
 * @stock_id: Button icon stock id.
 *
 * Creates a button that looks like a tool button, but can have different
 * label text.
 *
 * Returns: The newly created button as #GtkWidget.
 **/
GtkWidget*
gwy_tool_like_button_new(const gchar *label_text,
                         const gchar *stock_id)
{
    GtkWidget *button, *image, *vbox, *label;
    GdkPixbuf *pixbuf;

    button = gtk_button_new();
    vbox = gtk_vbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(button), vbox);

    if (stock_id) {
        pixbuf = gtk_widget_render_icon(button, stock_id,
                                        GTK_ICON_SIZE_LARGE_TOOLBAR,
                                        "toolitem");
    }
    else {
        /* Align text when there is no image */
        pixbuf = gtk_widget_render_icon(button, GTK_STOCK_OK,
                                        GTK_ICON_SIZE_LARGE_TOOLBAR,
                                        "toolitem");
        gdk_pixbuf_fill(pixbuf, 0);
    }
    image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    gtk_box_pack_start(GTK_BOX(vbox), image, FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic(label_text);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), button);

    return button;
}

static void
sync_sensitivity(GtkWidget *master,
                 G_GNUC_UNUSED GtkStateType state,
                 GtkWidget *slave)
{
    gtk_widget_set_sensitive(slave, GTK_WIDGET_IS_SENSITIVE(master));
}

static void
disconnect_slave(GtkWidget *slave,
                 GtkWidget *master)
{
    g_signal_handlers_disconnect_by_func(master, sync_sensitivity, slave);
    g_signal_handlers_disconnect_by_func(master, disconnect_master, slave);
}

static void
disconnect_master(GtkWidget *master,
                  GtkWidget *slave)
{
    g_signal_handlers_disconnect_by_func(master, disconnect_slave, slave);
}

/**
 * gwy_widget_sync_sensitivity:
 * @master: Master widget.
 * @slave: Slave widget.
 *
 * Make widget's sensitivity follow the sensitivity of another widget.
 *
 * The sensitivity of @slave is set according to @master's effective
 * sensitivity (as returned by GTK_WIDGET_IS_SENSITIVE()), i.e. it does not
 * just synchronize GtkWidget:sensitive property.
 *
 * Since: 2.8
 **/
void
gwy_widget_sync_sensitivity(GtkWidget *master,
                            GtkWidget *slave)
{
    g_signal_connect(master, "state-changed",
                     G_CALLBACK(sync_sensitivity), slave);
    g_signal_connect(slave, "destroy",
                     G_CALLBACK(disconnect_slave), master);
    g_signal_connect(master, "destroy",
                     G_CALLBACK(disconnect_master), slave);
}

/**
 * gwy_get_pango_ft2_font_map:
 * @unref: If %TRUE, function removes the font map reference and returns %NULL.
 *
 * Returns global Pango FT2 font map, eventually creating it.
 *
 * FT2 portability to Win32 is questionable, use PangoCairo instead.
 *
 * Returns: Pango FT2 font map.  Add your own reference if you want it to
 *          never go away.
 **/
PangoFontMap*
gwy_get_pango_ft2_font_map(gboolean unref)
{
    static PangoFontMap *ft2_font_map = NULL;

    if (unref) {
        GWY_OBJECT_UNREF(ft2_font_map);
        return NULL;
    }

#ifndef GDK_WINDOWING_QUARTZ
    if (ft2_font_map)
        return ft2_font_map;

    ft2_font_map = pango_ft2_font_map_new();
    pango_ft2_font_map_set_resolution(PANGO_FT2_FONT_MAP(ft2_font_map), 72, 72);
#else
    g_critical("FT2 is not available on Quartz");
#endif
    return ft2_font_map;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwydgetutils
 * @title: gwydgetutils
 * @short_description: Miscellaneous widget utilities
 **/

/**
 * gwy_adjustment_get_int:
 * @adj: A #GtkAdjustment to get value of.
 *
 * Gets a properly rounded integer value from an adjustment, cast to #gint.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
