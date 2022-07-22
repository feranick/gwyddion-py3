/*
 *  $Id: preview.h 24708 2022-03-21 17:14:46Z yeti-dn $
 *  Copyright (C) 2015-2017 David Necas (Yeti).
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

#ifndef __GWY_PROCESS_PREVIEW_H__
#define __GWY_PROCESS_PREVIEW_H__

#include <string.h>
#include <libgwydgets/gwycolorbutton.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwylayer-mask.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/menu.h>
#include <app/gwymoduleutils.h>
#include <app/data-browser.h>

enum {
    /* Standard preview size. */
    PREVIEW_SIZE = 480,
    /* For slow synth modules or if there are lots of other things to fit. */
    PREVIEW_SMALL_SIZE = 360,
    /* When we need to fit two preview-sized areas. */
    PREVIEW_HALF_SIZE = 240,
};

enum {
    RESPONSE_RESET     = 101,
    RESPONSE_PREVIEW   = 102,
    RESPONSE_CLEAR     = 103,
    RESPONSE_INIT      = 104,
    RESPONSE_ESTIMATE  = 105,
    RESPONSE_REFINE    = 106,
    RESPONSE_CALCULATE = 107,
    RESPONSE_LOAD      = 108,
    RESPONSE_SAVE      = 109,
    RESPONSE_COPY      = 110,
};

typedef struct {
    GwyGraphModel *gmodel;
    GwyDataChooserFilterFunc filter;
    gpointer filter_data;
} TargetGraphFilterData;

G_GNUC_UNUSED
static void
ensure_mask_color(GwyContainer *data, gint id)
{
    const gchar *key = g_quark_to_string(gwy_app_get_mask_key_for_id(id));
    GwyRGBA rgba;

    if (!gwy_rgba_get_from_container(&rgba, data, key)) {
        gwy_rgba_get_from_container(&rgba, gwy_app_settings_get(), "/mask");
        gwy_rgba_store_to_container(&rgba, data, key);
    }
}

G_GNUC_UNUSED
static void
load_mask_color_to_button(GtkWidget *color_button,
                          GwyContainer *data,
                          gint id)
{
    const gchar *key = g_quark_to_string(gwy_app_get_mask_key_for_id(id));
    GwyRGBA rgba;

    ensure_mask_color(data, id);
    gwy_rgba_get_from_container(&rgba, data, key);
    gwy_color_button_set_color(GWY_COLOR_BUTTON(color_button), &rgba);
}

G_GNUC_UNUSED
static void
mask_color_changed(GtkWidget *color_button)
{
    GObject *object = G_OBJECT(color_button);
    GtkWindow *dialog;
    GwyContainer *data;
    GQuark quark;
    gint id;

    data = GWY_CONTAINER(g_object_get_data(object, "data"));
    dialog = GTK_WINDOW(g_object_get_data(object, "dialog"));
    id = GPOINTER_TO_INT(g_object_get_data(object, "id"));
    quark = gwy_app_get_mask_key_for_id(id);
    gwy_mask_color_selector_run(NULL, dialog,
                                GWY_COLOR_BUTTON(color_button), data,
                                g_quark_to_string(quark));
    load_mask_color_to_button(color_button, data, id);
}

G_GNUC_UNUSED
static GtkWidget*
create_mask_color_button(GwyContainer *data, GtkWidget *dialog, gint id)
{
    GtkWidget *color_button;
    GObject *object;

    color_button = gwy_color_button_new();
    object = G_OBJECT(color_button);
    g_object_set_data(object, "data", data);
    g_object_set_data(object, "dialog", dialog);
    g_object_set_data(object, "id", GINT_TO_POINTER(id));

    gwy_color_button_set_use_alpha(GWY_COLOR_BUTTON(color_button), TRUE);
    load_mask_color_to_button(color_button, data, id);
    g_signal_connect(color_button, "clicked",
                     G_CALLBACK(mask_color_changed), NULL);

    return color_button;
}

static void
mask_merge_enabled_changed(GtkToggleButton *toggle, GtkWidget *widget)
{
    gtk_widget_set_sensitive(widget, gtk_toggle_button_get_active(toggle));
}

/* Pass NULL @pcheck if you do not want a checkbox.
 * XXX: Cannot work with both hscale and adjbar.  Must use with adjbar. */
G_GNUC_UNUSED
static void
create_mask_merge_buttons(GtkWidget *table, gint row, const gchar *name,
                          gboolean enabled, GCallback enabled_callback,
                          GwyMergeType merge, GCallback merge_type_callback,
                          gpointer user_data,
                          GtkWidget **pcheck, GSList **ptype)
{
    GSList *group, *l;
    GtkWidget *hbox, *button, *label;
    GQuark quark = g_quark_from_string("gwy-radiobuttons-key");
    gint value;

    if (!name)
        name = _("Combine with existing mask:");

    button = gtk_radio_button_new(NULL);
    g_object_set_qdata(G_OBJECT(button), quark,
                       GINT_TO_POINTER(GWY_MERGE_INTERSECTION));
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_stock(GWY_STOCK_MASK_INTERSECT,
                                               GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(button, _("Intersection"));

    button = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(button));
    g_object_set_qdata(G_OBJECT(button), quark,
                       GINT_TO_POINTER(GWY_MERGE_UNION));
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_stock(GWY_STOCK_MASK_ADD,
                                               GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(button, _("Union"));

    group = *ptype = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    hbox = gtk_hbox_new(FALSE, 0);
    gtk_table_attach(GTK_TABLE(table), hbox,
                     1, 2, row, row+1, GTK_FILL, 0, 0, 0);

    for (l = group; l; l = g_slist_next(l)) {
        button = (GtkWidget*)l->data;
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button), FALSE);
        gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);

        value = GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(button), quark));
        if (value == merge)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    }

    if (merge_type_callback) {
        for (l = group; l; l = g_slist_next(l)) {
            g_signal_connect_swapped(l->data, "clicked",
                                     merge_type_callback, user_data);
        }
    }

    if (pcheck) {
        label = *pcheck = gtk_check_button_new_with_mnemonic(name);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(label), enabled);
        gtk_widget_set_sensitive(hbox, enabled);
        g_signal_connect(label, "toggled",
                         G_CALLBACK(mask_merge_enabled_changed), hbox);
        if (enabled_callback) {
            g_signal_connect_swapped(*pcheck, "toggled",
                                     enabled_callback, user_data);
        }
    }
    else {
        label = gtk_label_new(name);
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    }
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, row, row+1, GTK_FILL, 0, 0, 0);
}

/* NB: The channel needs full-range linear mapping for this to work! */
G_GNUC_UNUSED
static void
set_gradient_for_residuum(GwyGradient *gradient,
                          gdouble fullmin, gdouble fullmax,
                          gdouble *dispmin, gdouble *dispmax)
{
    static const GwyRGBA rgba_negative = { 0.0, 0.0, 1.0, 1.0 };
    static const GwyRGBA rgba_positive = { 1.0, 0.0, 0.0, 1.0 };
    static const GwyRGBA rgba_neutral = { 1.0, 1.0, 1.0, 1.0 };

    gdouble zero, range;
    GwyGradientPoint zero_pt = { 0.5, rgba_neutral };
    GwyRGBA rgba;
    gint pos;

    gwy_gradient_reset(gradient);
    fullmin = fmin(fullmin, *dispmin);
    fullmax = fmax(fullmax, *dispmax);

    /* Stretch the scale to the range when all the data are too high or too
     * low. */
    if (*dispmin >= 0.0) {
        gwy_gradient_set_point_color(gradient, 0, &rgba_neutral);
        gwy_gradient_set_point_color(gradient, 1, &rgba_positive);
        return;
    }
    if (*dispmax <= 0.0) {
        gwy_gradient_set_point_color(gradient, 0, &rgba_negative);
        gwy_gradient_set_point_color(gradient, 1, &rgba_neutral);
        return;
    }
    if (!(*dispmax > *dispmin)) {
        gwy_gradient_set_point_color(gradient, 0, &rgba_negative);
        gwy_gradient_set_point_color(gradient, 1, &rgba_positive);
        pos = gwy_gradient_insert_point_sorted(gradient, &zero_pt);
        g_assert(pos == 1);
        return;
    }

    /* Otherwise make zero neutral and map the two colours to both side,
     * with the same scale. */
    range = fmax(-*dispmin, *dispmax);
    *dispmin = -range;
    *dispmax = range;

    if (-fullmin >= range && fullmax >= range) {
        /* Symmetrically extended display range lies within the full data
         * range.  Use this as the display range with fully extended colour
         * gradients. */
        gwy_gradient_set_point_color(gradient, 0, &rgba_negative);
        gwy_gradient_set_point_color(gradient, 1, &rgba_positive);
    }
    else {
        if (fullmax < range) {
            /* Map [-range, fullmax] to colours [negative, cut_positive]. */
            zero = range/(fullmax + range);
            *dispmax = fullmax;
            gwy_gradient_set_point_color(gradient, 0, &rgba_negative);
            gwy_rgba_interpolate(&rgba_neutral, &rgba_positive,
                                 (1.0 - zero)/zero, &rgba);
            gwy_gradient_set_point_color(gradient, 1, &rgba);
        }
        else {
            /* Map [fullmin, range] to colours [cut_negative, positive]. */
            zero = -fullmin/(range - fullmin);
            *dispmin = fullmin;
            gwy_rgba_interpolate(&rgba_neutral, &rgba_negative,
                                 zero/(1.0 - zero), &rgba);
            gwy_gradient_set_point_color(gradient, 0, &rgba);
            gwy_gradient_set_point_color(gradient, 1, &rgba_positive);
        }
        zero_pt.x = zero;
    }

    pos = gwy_gradient_insert_point_sorted(gradient, &zero_pt);
    g_assert(pos == 1);
}

G_GNUC_UNUSED
static void
set_widget_as_error_message(GtkWidget *widget)
{
    GdkColor gdkcolor_bad = { 0, 51118, 0, 0 };

    gtk_widget_modify_fg(widget, GTK_STATE_NORMAL, &gdkcolor_bad);
}

G_GNUC_UNUSED
static void
set_widget_as_warning_message(GtkWidget *widget)
{
    GdkColor gdkcolor_warning = { 0, 45056, 20480, 0 };

    gtk_widget_modify_fg(widget, GTK_STATE_NORMAL, &gdkcolor_warning);
}

G_GNUC_UNUSED
static void
set_widget_as_ok_message(GtkWidget *widget)
{
    gtk_widget_modify_fg(widget, GTK_STATE_NORMAL, NULL);
}

#endif

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
