/*
 *  $Id: tips.c 21345 2018-08-27 18:48:04Z yeti-dn $
 *  Copyright (C) 2008-2016 David Necas (Yeti).
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwydgets/gwystock.h>
#include <app/app.h>
#include <app/settings.h>

#define DEFAULT_ICON GTK_STOCK_INFO

typedef struct {
    const gchar *stock_id;
    const gchar *tip;
} StartupTip;

typedef struct {
    guint *order;
    guint tipno;
    GtkWidget *dialog;
    GtkWidget *icon;
    GtkWidget *text;
    GtkWidget *show_at_startup;
} GwyTipOfTheDay;

enum {
    RESPONSE_PREV = 1,
    RESPONSE_NEXT = 2
};

static const StartupTip tips[] = {
    /* Interface */
    {
        GWY_STOCK_SELECTIONS,
        N_("Holding Shift restricts directions of selected lines "
           "to multiples of 15°."),
    },
    {
        GWY_STOCK_SELECTIONS,
        N_("Holding Shift restricts shapes of selected rectangles "
           "to perfect squares."),
    },
    {
        GWY_STOCK_SELECTIONS,
        N_("Holding Shift restricts shapes of selected ellipses "
           "to perfect circles."),
    },
    {
        GWY_STOCK_GRAPH,
        N_("If multiple regions are selected on a graph, e.g. in 1D FFT "
           "Filtering, individual regions can be deleted by clicking on them "
           "with the right mouse button."),
    },
    {
        GWY_STOCK_SHADER,
        N_("Data presentations created by functions in Data Process → "
           "Presentation do not change the underlying data.  All subsequent "
           "operations still apply to the underlying data."),
    },
/*
    {
        NULL,
        N_("Menus can be torn off by clicking the dashed line near the top "
           "for quick access to a subset of functions."),
    },
*/
    {
        GTK_STOCK_EXECUTE,
        N_("Pressing Ctrl-F runs the last used data processing function with "
           "with the same parameters on the current data."),
    },
    {
        GTK_STOCK_EXECUTE,
        N_("Pressing Ctrl-Shift-F re-shows the parameter dialog of the last "
           "used data processing function (or executes it immediately if it "
           "has no parameters)."),
    },
    /* Files */
    {
        GTK_STOCK_OPEN,
        N_("When Gwyddion is run with a directory argument it opens "
           "a file open dialog showing this directory."),
    },
    {
        GTK_STOCK_OPEN,
        N_("File → Open Recent → Document History opens a browser "
           "of recently loaded files with the possibility to search them "
           "by name."),
    },
    {
        GTK_STOCK_OPEN,
        N_("Automatic import of unrecognized files as raw data can be "
           "enabled/disabled in the Raw file import dialog."),
    },
    {
        GTK_STOCK_SAVE,
        N_("To export the image of a channel to a pixmap graphic format "
           "(PNG, TIFF, JPEG, ...) just save it as this format with "
           "File → Save As."),
    },
    {
        GTK_STOCK_OPEN,
        N_("File → Merge imports all data from selected file to the current "
           "file."),
    },
    {
        GTK_STOCK_OPEN,
        N_("Previews in the file open dialog can be shown with plane "
           "and/or line leveling applied.  Use the switches at the bottom "
           "of the preview list."),
    },
    /* Resources */
    {
        GWY_STOCK_PALETTES,
        N_("Your favorite false color gradient can be set as default in the "
           "gradient editor: Edit → Color Gradients.  The default gradient "
           "is shown in bold face."),
    },
    {
        GWY_STOCK_GL_MATERIAL,
        N_("Your favorite GL material can be set as default in the "
           "material editor: Edit → GL Materials.  The default material "
           "is shown in bold face."),
    },
    {
        GWY_STOCK_MASK,
        N_("Edit → Default Mask Color sets the default mask color.  "
           "This color is used when a mask is created on data that "
           "have not had a mask before."),
    },
    {
        GTK_STOCK_INDEX,
        N_("Each channel has its own metadata.  Display them by "
           "clicking with the right mouse button and choosing Metadata "
           "Browser."),
    },
    {
        GTK_STOCK_INDEX,
        N_("Each volume data have their own metadata.  Display them by "
           "clicking with the right mouse button and choosing Metadata "
           "Browser."),
    },
    {
        GTK_STOCK_INDEX,
        N_("A record of applied data processing operations can be browsed "
           "using View Log in the channel or volume data right-click menu."),
    },
    /* 1D Views */
    {
        GTK_STOCK_DND_MULTIPLE,
        N_("Curves can be copied to other (compatible) graphs by "
           "dragging them from Curves tab to the graph window."),
    },
    {
        GWY_STOCK_GRAPH,
        N_("Graph curve properties can be edited by clicking on the curve."),
    },
    {
        GWY_STOCK_GRAPH,
        N_("Graph axis labels can be edited after by clicking on the label."),
    },
    {
        GWY_STOCK_GRAPH,
        N_("Graph key (legend) properties can be edited by double-clicking "
           "on the legend."),
    },
    {
        GWY_STOCK_GRAPH,
        N_("Curves can be deleted from graphs by selecting the curve "
           "in Curves tab and pressing Delete."),
    },
    /* 2D Views */
    {
        GWY_STOCK_ZOOM_IN,
        N_("Key ‛+’ or ‛=’ zooms in a data window."),
    },
    {
        GWY_STOCK_ZOOM_OUT,
        N_("Key ‛-’ (minus) zooms out a data window."),
    },
    {
        GWY_STOCK_ZOOM_1_1,
        N_("Key ‛Z’ resets data window zoom to 1:1."),
    },
    {
        GWY_STOCK_DATA_MEASURE,
        N_("Data with different <i>x</i> and <i>y</i> measures can be "
           "displayed either with pixel-wise or realistic aspect ratio.  "
           "The menu in data window top left corner enables switching "
           "between these two modes."),
    },
    {
        GWY_STOCK_PALETTES,
        N_("Clicking on a false color scale with the right mouse button "
           "brings a false color gradient selector."),
    },
    {
        GWY_STOCK_MASK,
        N_("Mask color can be changed by right-clicking on a data view "
           "and selecting Mask Color from the menu."),
    },
    /* 3D Views */
    {
        GWY_STOCK_3D_BASE,
        N_("Clicking on a 3D view with the right mouse button brings "
           "a GL material or false color gradient selector."),
    },
    {
        GWY_STOCK_3D_BASE,
        N_("3D view transformation modes can be selected with keys: "
           "R (rotate), S (scale), V (value scale) and L (light source)."),
    },
    {
        GWY_STOCK_3D_BASE,
        N_("You can make a specific 3D view setup the default using the "
           "Set as Default button."),
    },
    /* Data Browser */
    {
        GTK_STOCK_COPY,
        N_("Pressing Ctrl-C copies the image of a channel, graph or 3D view "
           "to the clipboard."),
    },
    {
        GTK_STOCK_DND_MULTIPLE,
        N_("Dragging channels or graphs from Data Browser to a window "
           "copies them to the corresponding file."),
    },
    {
        GTK_STOCK_INDEX,
        N_("Info → Show Data Browser brings back a closed Data Browser."),
    },
    {
        GTK_STOCK_EDIT,
        N_("Channels and graphs can be renamed by double-clicking on their "
           "name in Data Browser."),
    },
    /* Data Processing */
    {
        GWY_STOCK_FACET_LEVEL,
        N_("Facet Level offers to use/exclude the masked area if a mask is "
           "present on the data."),
    },
    {
        GWY_STOCK_LEVEL,
        N_("Plane Level offers to use/exclude the masked area if a mask is "
           "present on the data."),
    },
    {
        GWY_STOCK_LINE_LEVEL,
        N_("Align Rows offers to use/exclude the masked area if a mask is "
           "present on the data."),
    },
    {
        GWY_STOCK_ARITHMETIC,
        N_("Data Arithmetic works as a scientific calculator: "
           "just type an arithmetic expression."),
    },
    {
        GWY_STOCK_ARITHMETIC,
        N_("Data Arithmetic expressions can include values (d), "
           "mask values (m), derivatives (bx, by) and coordinates (x, y)."),
    },
    {
        GWY_STOCK_FACET_LEVEL,
        N_("Facet Level can often level data with large features that make "
           "impossible to use standard plane leveling.  It levels the surface "
           "by making normals of flat areas point upwards."),
    },
    {
        GWY_STOCK_LEVEL,
        N_("Flatten Base performs automated leveling of base flat surface "
           "with positive features."),
    },
    {
        GWY_STOCK_GRAINS_REMOVE,
        N_("Too small grains can be filtered out with Data Process → Grains "
           "→ Filter."),
    },
    {
        GWY_STOCK_EDGE,
        N_("Data Process → Presentation → Edge Detection → Step "
           "is a fine step detector with a good dynamic range"),
    },
    {
        GWY_STOCK_DATA_MEASURE,
        N_("Data Process → Basic Operations → Dimensions and Units "
           "changes scales, offsets and even lateral and value units."),
    },
    {
        GWY_STOCK_MASK,
        N_("Grains or other areas of interest are marked with masks.  "
           "Many functions then can do something interesting with the masked "
           "areas."),
    },
    {
        GWY_STOCK_SCARS,
        N_("Remove Scars in the toolbox runs with the settings last "
           "used in Mark Scars."),
    },
    {
        GWY_STOCK_SYNTHETIC_OBJECTS,
        N_("Data synthesis modules can be also used to modify existing "
           "images."),
    },
    {
        GWY_STOCK_MARK_WITH,
        N_("Data Process → Mask → Mark With can set the image mask based on "
           "another data, mask or presentation."),
    },
    /* Graphing */
    {
        GWY_STOCK_GRAPH_MEASURE,
        N_("Graph → Critical Dimension measures steps on extracted profile "
           "graphs."),
    },
    {
        GWY_STOCK_GRAPH,
        N_("All Graph functions are available also in the graph right-click "
           "menu."),
    },
    /* Tools */
    {
        NULL,
        N_("Pressing Esc hides tool windows."),
    },
    {
        GWY_STOCK_POINTER_MEASURE,
        N_("Read Value tool displays also the local facet normal."),
    },
    {
        GWY_STOCK_POINTER_MEASURE,
        N_("Read Value tool can shift data to make <i>z</i>=0 plane pass "
           "through the selected point."),
    },
    {
        GWY_STOCK_DISTANCE,
        N_("Individual lines can be deleted in Distance tool by selecting "
           "them in the list and pressing Delete."),
    },
    {
        GWY_STOCK_PROFILE,
        N_("Individual lines can be deleted in Profiles tool by selecting "
           "them in the list and pressing Delete."),
    },
    {
        GWY_STOCK_SPECTRUM,
        N_("Spectroscopy tool displays point spectroscopy data and extracts "
           "them to standalone graphs that can be subsequently analysed "
           "for instance with Graph → Fit FD Curve."),
    },
    {
        GWY_STOCK_STAT_QUANTITIES,
        N_("Statistical Quantities tool allows limiting the area of interest "
           "by a mask, rectangular selection or the intersection of both."),
    },
    {
        GWY_STOCK_GRAPH_HALFGAUSS,
        N_("Beside height and angle distributions, Statistical Functions tool "
           "calculates also correlation functions, power spectrum density "
           "(PSDF) and some more exotic functions."),
    },
    {
        GWY_STOCK_PATH_LEVEL,
        N_("Path Level tool levels misaligned rows by lining them up along "
           "manually selected lines.  If there are no large features "
           "automatic Align Rows usually works well."),
    },
    {
        GWY_STOCK_GRAINS_MEASURE,
        N_("Grain Measure tool is great for examining individual grains.  "
           "Overall grain statistics are available in "
           "Data Processing → Grains."),
    },
    {
        GWY_STOCK_MASK_EDITOR,
        N_("Mask Editor tool can create, edit, invert, grow and shrink masks."),
    },
    {
        GWY_STOCK_COLOR_RANGE,
        N_("Color Range tool offers several false color scale mapping modes "
           "and can make any of them the default mode."),
    },
    {
        GWY_STOCK_SELECTIONS,
        N_("Dragging selections from Selections Manager tool to a data window "
           "copies the selection to the target data."),
    },
    /* General and bragging. */
    {
        GTK_STOCK_HELP,
        N_("Pressing F1 or the Help buttons in most windows shows a relevant "
           "part of the user guide in a web browser."),
    },
    {
        GTK_STOCK_HELP,
        N_("Gwyddion User Guide explains in detail "
           "many of the methods and algorithms implemented in Gwyddion."),
    },
    {
        GWY_STOCK_GWYDDION,
        N_("Gwyddion is a son of Math."),
    },
};

static void
show_tip(GwyTipOfTheDay *tod, guint tipno)
{
    const StartupTip *stip;
    const gchar *stock_id;

    tod->tipno = tipno % G_N_ELEMENTS(tips);
    stip = tips + tod->order[tod->tipno];

    stock_id = stip->stock_id ? stip->stock_id : DEFAULT_ICON;
    gtk_image_set_from_stock(GTK_IMAGE(tod->icon), stock_id,
                             GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_label_set_markup(GTK_LABEL(tod->text), _(stip->tip));
}

static void
response(GwyTipOfTheDay *tod, gint response_id)
{
    if (response_id == GTK_RESPONSE_DELETE_EVENT)
        gtk_widget_destroy(tod->dialog);
    else if (response_id == RESPONSE_NEXT)
        show_tip(tod, tod->tipno + 1);
    else if (response_id == RESPONSE_PREV)
        show_tip(tod, tod->tipno + G_N_ELEMENTS(tips) - 1);
}

static void
finalize(GwyTipOfTheDay *tod)
{
    g_free(tod->order);
    g_free(tod);
}

static void
show_at_startup_changed(GtkToggleButton *toggle, GwyContainer *settings)
{
    gboolean show_at_startup = gtk_toggle_button_get_active(toggle);

    gwy_container_set_boolean_by_name(settings, "/app/tips/show-at-startup",
                                      show_at_startup);
}

void
gwy_app_tip_of_the_day(void)
{
    GtkWidget *dialog, *image, *button, *hbox, *align;
    GwyContainer *settings;
    GwyTipOfTheDay *tod;
    guint i, n, *source;
    gboolean show_at_startup = FALSE;

    settings = gwy_app_settings_get();
    gwy_container_gis_boolean_by_name(settings, "/app/tips/show-at-startup",
                                      &show_at_startup);

    n = G_N_ELEMENTS(tips);
    tod = g_new0(GwyTipOfTheDay, 1);

    dialog = gtk_dialog_new_with_buttons(_("Gwyddion Tip of the Day"),
                                         GTK_WINDOW(gwy_app_main_window_get()),
                                         GTK_DIALOG_NO_SEPARATOR
                                         | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         NULL);
    tod->dialog = dialog;
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    gwy_app_add_main_accel_group(GTK_WINDOW(dialog));
    g_signal_connect_swapped(dialog, "destroy", G_CALLBACK(finalize), tod);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(response), tod);

    image = gtk_image_new_from_stock(GTK_STOCK_GO_BACK,
                                     GTK_ICON_SIZE_BUTTON);
    button = gtk_button_new_with_mnemonic(_("_Previous Tip"));
    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, RESPONSE_PREV);

    image = gtk_image_new_from_stock(GTK_STOCK_GO_FORWARD,
                                     GTK_ICON_SIZE_BUTTON);
    button = gtk_button_new_with_mnemonic(_("_Next Tip"));
    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, RESPONSE_NEXT);

    hbox = gtk_hbox_new(FALSE, 12);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 16);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 8);

    align = gtk_alignment_new(0.5, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, TRUE, 0);

    tod->icon = gtk_image_new();
    gtk_container_add(GTK_CONTAINER(align), tod->icon);

    align = gtk_alignment_new(0.5, 0.0, 1.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, TRUE, 0);

    tod->text = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(tod->text), 0.0, 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(tod->text), TRUE);
    gtk_label_set_selectable(GTK_LABEL(tod->text), TRUE);
    gtk_widget_set_size_request(tod->text, 320, 72);
    gtk_container_add(GTK_CONTAINER(align), tod->text);

    align = gtk_alignment_new(0.0, 0.5, 1.0, 0.0);
    gtk_alignment_set_padding(GTK_ALIGNMENT(align), 2, 2, 16, 16);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), align,
                       FALSE, FALSE, 0);

    tod->show_at_startup
        = gtk_check_button_new_with_mnemonic(_("_Show tips at startup"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tod->show_at_startup),
                                 show_at_startup);
    gtk_container_add(GTK_CONTAINER(align), tod->show_at_startup);
    g_signal_connect(tod->show_at_startup, "toggled",
                     G_CALLBACK(show_at_startup_changed), settings);

    /* Randomize tips */
    source = g_new(guint, n);
    for (i = 0; i < n; i++)
        source[i] = i;
    tod->order = g_new(guint, n);
    for (i = 0; i < n; i++) {
        guint j = g_random_int_range(0, n-i);

        tod->order[i] = source[j];
        source[j] = source[n-i-1];
    }
    g_free(source);

    show_tip(tod, 0);

    gtk_widget_show_all(dialog);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
