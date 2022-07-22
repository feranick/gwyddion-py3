/*
 *  $Id: wait.c 24545 2021-11-25 14:51:19Z yeti-dn $
 *  Copyright (C) 2004-2021 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <gtk/gtk.h>
#include "wait.h"

static void gwy_app_wait_create_dialog (GtkWindow *window,
                                        const gchar *message);
static void gwy_app_wait_cancelled      (void);

static gboolean wait_enabled = TRUE;

static GtkWidget *dialog = NULL;
static GtkWidget *progress = NULL;
static GtkWidget *label = NULL;
static GtkWidget *preview = NULL;
static gchar *message_prefix = NULL;
static gboolean cancelled = FALSE;
static GTimer *timer = NULL;
static gdouble last_update_time = 0.0;

/**
 * gwy_app_wait_start:
 * @window: A window.
 * @message: A message to show in the wait dialog.
 *
 * Starts waiting for a window @window, creating a dialog with a progress bar.
 *
 * Waiting is global, there can be only one at a time.
 *
 * Do not forget to call gwy_app_wait_finish() when the computation is finished (or cancelled).  You should also call
 * gwy_app_wait_set_fraction() or gwy_app_wait_set_message() regularly to leave the GUI responsive.
 **/
void
gwy_app_wait_start(GtkWindow *window,
                   const gchar *message)
{
    if (!wait_enabled)
        return;

    if (window && !GTK_IS_WINDOW(window))
        g_warning("Widget is not a window");

    if (dialog) {
        g_critical("Waiting is modal, cannot wait on more than one thing at once.");
        return;
    }

    last_update_time = -1e38;
    if (!timer)
        timer = g_timer_new();
    else
        g_timer_start(timer);

    cancelled = FALSE;
    gwy_app_wait_create_dialog(window, message);
}

static void
silent_kill_preview_widget(void)
{
    if (preview) {
        gtk_widget_destroy(preview);
        g_object_unref(preview);
        preview = NULL;
    }
}

/**
 * gwy_app_wait_finish:
 *
 * Finishes waiting, closing the dialog.
 *
 * No function like gwy_app_wait_set_message() should be call after that.
 *
 * This function must be called even if user cancelled the operation.
 **/
void
gwy_app_wait_finish(void)
{
    if (!wait_enabled)
        return;

    silent_kill_preview_widget();

    if (cancelled) {
        cancelled = FALSE;
        return;
    }

    g_return_if_fail(dialog != NULL);
    gtk_widget_destroy(dialog);
    g_free(message_prefix);

    dialog = NULL;
    progress = NULL;
    label = NULL;
    message_prefix = NULL;
}

static void
gwy_app_wait_create_dialog(GtkWindow *window,
                           const gchar *message)
{
    GtkBox *vbox;

    dialog = gtk_dialog_new_with_buttons(_("Please wait"), window,
                                         GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR | GTK_DIALOG_MODAL,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         NULL);
    if (!window) {
        gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
        gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    }

    vbox = GTK_BOX(GTK_DIALOG(dialog)->vbox);
    if (preview)
        gtk_box_pack_start(vbox, preview, FALSE, FALSE, 4);

    label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_label_set_markup(GTK_LABEL(label), message);
    gtk_box_pack_start(vbox, label, FALSE, FALSE, 4);

    progress = gtk_progress_bar_new();
    gtk_widget_set_size_request(progress, 280, -1);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), 0.0);
    gtk_box_pack_start(vbox, progress, FALSE, FALSE, 4);

    g_signal_connect(dialog, "response", G_CALLBACK(gwy_app_wait_cancelled), NULL);

    gtk_widget_show_all(dialog);
    gtk_window_present(GTK_WINDOW(dialog));
    while (gtk_events_pending())
        gtk_main_iteration();
}

/**
 * gwy_app_wait_set_message:
 * @message: A mesage to show in the progress dialog.
 *
 * Sets the message shown on the progress dialog.
 *
 * See also gwy_app_wait_set_message_prefix() which makes this function more usable directly as a callback.
 *
 * This function lets the Gtk+ main loop to run.
 *
 * It must not be called again once the operation is cancelled, i.e. after any of the progress reporting functions
 * return %FALSE.
 *
 * Returns: %TRUE if the operation can continue, %FALSE if user cancelled it meanwhile.  You must always check the
 *          return value and cancel the operation if it is %FALSE.
 **/
gboolean
gwy_app_wait_set_message(const gchar *message)
{
    if (!wait_enabled)
        return TRUE;

    g_return_val_if_fail(dialog, FALSE);

    while (gtk_events_pending())
        gtk_main_iteration();

    if (cancelled)
        return FALSE;

    g_return_val_if_fail(dialog, FALSE);
    if (message_prefix) {
        gchar *s = g_strconcat(message_prefix, message, NULL);
        gtk_label_set_markup(GTK_LABEL(label), s);
        g_free(s);
    }
    else
        gtk_label_set_markup(GTK_LABEL(label), message);

    while (gtk_events_pending())
        gtk_main_iteration();

    last_update_time = -1e38;
    return !cancelled;
}

/**
 * gwy_app_wait_set_message_prefix:
 * @prefix: The prefix for new messages.
 *
 * Sets prefix for the messages shown in the progress dialog.
 *
 * The prefix will take effect in the next gwy_app_wait_set_message() call.
 *
 * This function lets the Gtk+ main loop to run.
 *
 * It must not be called again once the operation is cancelled, i.e. after any of the progress reporting functions
 * return %FALSE.
 *
 * Returns: %TRUE if the operation can continue, %FALSE if user cancelled it meanwhile.  You must always check the
 *          return value and cancel the operation if it is %FALSE.
 **/
gboolean
gwy_app_wait_set_message_prefix(const gchar *prefix)
{
    if (!wait_enabled)
        return TRUE;

    g_return_val_if_fail(dialog, FALSE);

    if (cancelled)
        return FALSE;

    g_return_val_if_fail(dialog, FALSE);
    gwy_assign_string(&message_prefix, prefix);

    while (gtk_events_pending())
        gtk_main_iteration();

    return !cancelled;
}

/**
 * gwy_app_wait_set_fraction:
 * @fraction: The progress of the operation, as a number from 0 to 1.
 *
 * Sets the amount of progress the progress bar on the dialog displays.
 *
 * This function may let the Gtk+ main loop to run.  It used to let the main loop to run always.  Since version 2.46
 * it performs automated rate-limiting and only does so if sufficient time has passed since the last main loop
 * invocation.  Therefore, you can call it 10000 times per second without fearing that the program will spend all time
 * updating the GUI and no time in the calculation.
 *
 * It must not be called again once the operation is cancelled, i.e. after any of the progress reporting functions
 * return %FALSE.
 *
 * Returns: %TRUE if the operation can continue, %FALSE if user cancelled it meanwhile.  You must always check the
 *          return value and cancel the operation if it is %FALSE.
 **/
gboolean
gwy_app_wait_set_fraction(gdouble fraction)
{
    gchar buf[8];
    gdouble t;

    if (!wait_enabled)
        return TRUE;

    g_return_val_if_fail(dialog, FALSE);

    t = g_timer_elapsed(timer, NULL);
    if (t < last_update_time + 0.15)
        return TRUE;

    while (gtk_events_pending())
        gtk_main_iteration();

    if (cancelled)
        return FALSE;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), fraction);
    if (fraction < 0.0 || fraction > 1.0) {
        g_warning("Fraction outside [0, 1] range");
        fraction = CLAMP(fraction, 0.0, 1.0);
    }
    g_snprintf(buf, sizeof(buf), "%d %%", (gint)(100*fraction + 0.4));
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress), buf);

    while (gtk_events_pending())
        gtk_main_iteration();

    last_update_time = g_timer_elapsed(timer, NULL);
    return !cancelled;
}

static void
gwy_app_wait_cancelled(void)
{
    gwy_app_wait_finish();
    cancelled = TRUE;
}

/**
 * gwy_app_wait_cursor_start:
 * @window: A window.
 *
 * Changes the cursor for a window to indicate work.
 *
 * This function lets the Gtk+ main loop to run.
 *
 * Since: 2.3
 **/
void
gwy_app_wait_cursor_start(GtkWindow *window)
{
    GdkDisplay *display;
    GdkCursor *wait_cursor;
    GdkWindow *wait_window;
    GtkWidget *widget;

    if (!window && !wait_enabled)
        return;

    g_return_if_fail(GTK_IS_WINDOW(window));
    widget = GTK_WIDGET(window);

    if (!GTK_WIDGET_REALIZED(widget)) {
        g_warning("Window must be realized to change the cursor");
        return;
    }

    wait_window = widget->window;

    display = gtk_widget_get_display(widget);
    wait_cursor = gdk_cursor_new_for_display(display, GDK_WATCH);
    gdk_window_set_cursor(wait_window, wait_cursor);
    gdk_cursor_unref(wait_cursor);

    while (gtk_events_pending())
        gtk_main_iteration_do(FALSE);
}

/**
 * gwy_app_wait_cursor_finish:
 * @window: A window.
 *
 * Resets the cursor for a window.
 *
 * This function lets the Gtk+ main loop to run.
 *
 * If the window cursor was non-default before gwy_app_wait_cursor_start(), it is not restored and has to be set
 * manually.  This limitation is due to the nonexistence of a method to obtain the current cursor.
 *
 * Since: 2.3
 **/
void
gwy_app_wait_cursor_finish(GtkWindow *window)
{
    GdkWindow *wait_window;
    GtkWidget *widget;

    if (!window && !wait_enabled)
        return;

    g_return_if_fail(GTK_IS_WINDOW(window));
    widget = GTK_WIDGET(window);

    if (!GTK_WIDGET_REALIZED(widget)) {
        g_warning("Window must be realized to change the cursor");
        return;
    }

    wait_window = widget->window;

    gdk_window_set_cursor(wait_window, NULL);

    while (gtk_events_pending())
        gtk_main_iteration_do(FALSE);
}

/**
 * gwy_app_wait_get_enabled:
 *
 * Reports whether progress reporting is globally enabled.
 *
 * Returns: %TRUE if progress reporting is enabled, %FALSE if it is disabled.
 *
 * Since: 2.48
 **/
gboolean
gwy_app_wait_get_enabled(void)
{
    return wait_enabled;
}

/**
 * gwy_app_wait_set_enabled:
 * @setting: %TRUE to enable progress reporting, %FALSE to disable it.
 *
 * Globally enables or disables progress reporting.
 *
 * This function may not be used when a waiting dialog is currently being shown.
 *
 * By default, progress reporting is enabled.  Non-GUI applications that run module functions may wish to disable it
 * to avoid GTK+ calls or just showing the progress dialogs.
 *
 * If progress reporting is disabled then functions such as gwy_app_wait_set_message() and gwy_app_wait_set_fraction()
 * become no-op and always return %TRUE as nothing can be cancelled by the user.  Functions
 * gwy_app_wait_cursor_start() and gwy_app_wait_cursor_finish() still work but may be called with %NULL arguments.
 *
 * Since: 2.48
 **/
void
gwy_app_wait_set_enabled(gboolean setting)
{
    if (!wait_enabled == !setting)
        return;

    g_return_if_fail(!dialog);
    g_return_if_fail(!cancelled);
    wait_enabled = !!setting;
}

/**
 * gwy_app_wait_was_canceled:
 *
 * Checks if a progress dialog was cancelled.
 *
 * Calling this function is only meaningful between gwy_app_wait_start() and gwy_app_wait_finish().  It returns %TRUE
 * if the computation was cancelled by the user.  This may be occasionaly useful in complex multi-level calculations.
 * Usually, the return values of gwy_app_wait_set_fraction() and gwy_app_wait_set_message() are sufficient.
 *
 * Returns: %TRUE if the currently running calculation was cancelled.
 *
 * Since: 2.49
 **/
gboolean
gwy_app_wait_was_canceled(void)
{
    return cancelled;
}

/**
 * gwy_app_wait_set_preview_widget:
 * @widget: Preview widget, usually something like #GwyDataView.
 *
 * Sets the preview widget of a wait dialogue.
 *
 * This function needs to be used before gwy_app_wait_start() to have any effect.
 *
 * Since: 2.54
 **/
void
gwy_app_wait_set_preview_widget(GtkWidget *widget)
{
    g_return_if_fail(!widget || GTK_IS_WIDGET(widget));

    if (widget == preview)
        return;

    silent_kill_preview_widget();
    if (widget) {
        g_object_ref(widget);
        gtk_object_sink(GTK_OBJECT(widget));
        preview = widget;
    }
}

/************************** Documentation ****************************/

/**
 * SECTION:wait
 * @title: wait
 * @short_description: Informing the world we are busy
 *
 * The waiting functions implement a simple single-thread approach to performing a long computation while keeping the
 * GUI responsive.
 *
 * The typical basic usage is as follows:
 * |[
 * gboolean cancelled = FALSE;
 *
 * gwy_app_wait_start(window, _("Evaluating..."));
 * for (i = 0; i < n_iters; i++) {
 *     do_one_calculation_iteration();
 *     if (!gwy_app_wait_set_fraction((i + 1.0)/n_iters)) {
 *         cancelled = TRUE;
 *         break;
 *     }
 * }
 * gwy_app_wait_finish();
 *
 * if (cancelled)
 *     handle_cancelled_computation();
 * else
 *     do_something_with_result();
 * ]|
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
