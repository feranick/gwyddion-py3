/*
 *  $Id: logging.c 22396 2019-08-18 06:38:30Z yeti-dn $
 *  Copyright (C) 2016-2018 David Necas (Yeti).
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
#include <stdio.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <app/gwyapp.h>
#include "gwyappinternal.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef _WIN32
#include <process.h>
#endif

#ifdef _MSC_VER
#define getpid _getpid
#endif

/* Our handler should not get G_LOG_LEVEL_ERROR errors but it does not hurt. */
enum {
    ALERT_LEVELS = (G_LOG_LEVEL_ERROR
                    | G_LOG_LEVEL_CRITICAL
                    | G_LOG_LEVEL_WARNING)
};

typedef struct {
    FILE *file;
    GString *message;
    GString *last;
    GQuark last_domain;
    guint last_count;
    GLogLevelFlags last_level;
    GwyAppLoggingFlags flags;
    GArray *message_history;
    GtkTextBuffer *textbuf;
    gboolean to_file;
    gboolean to_console;
    guint capturing_from;
} LoggingSetup;

static void     logger                (const gchar *log_domain_str,
                                       GLogLevelFlags log_level,
                                       const gchar *message,
                                       gpointer user_data);
static void     black_hole            (const gchar *log_domain_str,
                                       GLogLevelFlags log_level,
                                       const gchar *message,
                                       gpointer user_data);
static void     flush_last_message    (LoggingSetup *setup);
static void     emit_message_to_file  (FILE *fh,
                                       GLogLevelFlags log_level,
                                       const gchar *message,
                                       gboolean add_eol);
static void     format_log_message    (GString *str,
                                       GQuark log_domain,
                                       GLogLevelFlags log_level,
                                       const gchar *message);
static void     append_escaped_message(GString *str,
                                       const gchar *message);
static void     emit_log_message      (LoggingSetup *setup,
                                       GQuark log_domain,
                                       GLogLevelFlags log_level);
static void     append_level_prefix   (GString *str,
                                       GLogLevelFlags log_level);
static FILE*    get_console_stream    (GLogLevelFlags log_level);
static gboolean skip_annoying_message (const gchar *message);

static LoggingSetup log_setup;

/**
 * gwy_app_setup_logging:
 * @flags: Flags controlling how messages are handled.
 *
 * Sets up Gwyddion GLib log handler.
 *
 * The log handler sends the messages to a log file or console, as Gwyddion
 * usually does.  This function may not be useful in Gwyddion-based programs
 * unless they try to emulate Gwyddion behaviour closely.
 *
 * Since: 2.45
 **/
void
gwy_app_setup_logging(GwyAppLoggingFlags flags)
{
    static const gchar *our_domains[] = {
        "Gwyddion", "GwyProcess", "GwyDraw", "Gwydgets", "GwyModule", "GwyApp",
        "Module", "Gwyddion-Program", NULL
    };
    static const gchar *other_domains[] = {
        "GLib", "GLib-GObject", "GLib-GIO", "GModule", "GThread",
        "GdkPixbuf", "Gdk", "Gtk",
        "GdkGLExt", "GtkGLExt",
        "GtkSourceView",
        "Pango", "Unique",
        NULL,
    };

    guint i;

    if (log_setup.message_history) {
        g_warning("Logging has been already set up.");
        return;
    }

    log_setup.flags = flags;

    log_setup.to_console = (flags & GWY_APP_LOGGING_TO_CONSOLE);
    if (flags & GWY_APP_LOGGING_TO_FILE) {
        const gchar *log_filename = gwy_app_settings_get_log_filename();
        log_setup.file = gwy_fopen(log_filename, "w");
        log_setup.to_file = !!log_setup.file;
    }

    log_setup.message = g_string_new(NULL);
    log_setup.last = g_string_new(NULL);
    log_setup.last_domain = 0;
    log_setup.last_count = G_MAXUINT;
    log_setup.message_history = g_array_new(FALSE, FALSE,
                                            sizeof(GwyAppLogMessage));
    log_setup.capturing_from = G_MAXUINT;
    /* NB: We must not initialise the text buffer here because Gtk+ may not be
     * initialised yet.  Only do that on demand. */

    for (i = 0; i < G_N_ELEMENTS(our_domains); i++) {
        g_log_set_handler(our_domains[i],
                          G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_MESSAGE
                          | G_LOG_LEVEL_INFO | G_LOG_LEVEL_WARNING
                          | G_LOG_LEVEL_CRITICAL,
                          logger, &log_setup);
    }
    for (i = 0; i < G_N_ELEMENTS(other_domains); i++) {
        g_log_set_handler(other_domains[i],
                          G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL,
                          logger, &log_setup);
    }
    /* Send messages at levels lower than WARNING to a black hole because
     * the developers of the libraries think printing DEBUG messages in
     * production code is a good idea and hence they cannot be trusted.  We
     * could make an effort to pass some specifically to the default handler,
     * but I do not care. */
    for (i = 0; i < G_N_ELEMENTS(other_domains); i++) {
        g_log_set_handler(other_domains[i],
                          G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_MESSAGE
                          | G_LOG_LEVEL_INFO,
                          black_hole, NULL);
    }
}

void
_gwy_app_log_start_message_capture(void)
{
    /* No message_history indicates no logging setup.  Be no-op then. */
    if (!log_setup.message_history)
        return;

    g_return_if_fail(log_setup.capturing_from == G_MAXUINT);
    flush_last_message(&log_setup);
    log_setup.capturing_from = log_setup.message_history->len;
}

GwyAppLogMessage*
_gwy_app_log_get_captured_messages(guint *nmesg)
{
    GwyAppLogMessage *messages;
    guint capturing_from = log_setup.capturing_from, i, n;
    GArray *message_history = log_setup.message_history;

    /* No message_history indicates no logging setup.  Be no-op then. */
    if (!log_setup.message_history)
        return NULL;

    g_return_val_if_fail(log_setup.capturing_from != G_MAXUINT, NULL);
    log_setup.capturing_from = G_MAXUINT;

    if (message_history->len == capturing_from) {
        *nmesg = 0;
        return NULL;
    }

    flush_last_message(&log_setup);
    n = message_history->len - capturing_from;
    messages = g_new(GwyAppLogMessage, n);
    for (i = 0; i < n; i++) {
        messages[i] = g_array_index(message_history, GwyAppLogMessage,
                                    capturing_from + i);
        messages[i].message = g_strdup(messages[i].message);
    }
    *nmesg = n;
    return messages;
}

void
_gwy_app_log_discard_captured_messages(void)
{
    /* No message_history indicates no logging setup.  Be no-op then. */
    if (!log_setup.message_history)
        return;

    g_return_if_fail(log_setup.capturing_from != G_MAXUINT);
    log_setup.capturing_from = G_MAXUINT;
}

static void
flush_last_message(LoggingSetup *setup)
{
    GLogLevelFlags just_log_level;

    if (!setup->last_count || setup->last_count == G_MAXUINT) {
        setup->last_count = G_MAXUINT;
        return;
    }

    just_log_level = (setup->last_level & G_LOG_LEVEL_MASK);
    g_string_printf(setup->last, "Last message repeated %u times",
                    setup->last_count);
    format_log_message(setup->message, setup->last_domain, just_log_level,
                       setup->last->str);
    emit_log_message(setup, setup->last_domain, just_log_level);
    setup->last_count = G_MAXUINT;
}

static void
black_hole(G_GNUC_UNUSED const gchar *log_domain_str,
           G_GNUC_UNUSED GLogLevelFlags log_level,
           G_GNUC_UNUSED const gchar *message,
           G_GNUC_UNUSED gpointer user_data)
{
}

static void
logger(const gchar *log_domain_str,
       GLogLevelFlags log_level,
       const gchar *message,
       gpointer user_data)
{
    LoggingSetup *setup = (LoggingSetup*)user_data;
    GLogLevelFlags just_log_level = (log_level & G_LOG_LEVEL_MASK);
    GQuark log_domain = (log_domain_str
                         ? g_quark_from_string(log_domain_str)
                         : 0);

    /* Can't use gwy_debug() for debugging here... */
    /*
    fprintf(stderr, "Received(%s,0x%04x) <%s>\n",
            log_domain_str ? log_domain_str : "*", log_level, message);
            */
    if (setup->last_count != G_MAXUINT
        && log_level == setup->last_level
        && log_domain == setup->last_domain
        && gwy_strequal(message, setup->last->str)) {
        setup->last_count++;
        return;
    }

    flush_last_message(setup);
    g_string_assign(setup->last, message);
    setup->last_domain = log_domain;
    setup->last_level = log_level;
    setup->last_count = 0;

    format_log_message(setup->message, log_domain, just_log_level, message);
    emit_log_message(setup, log_domain, just_log_level);
}

static void
emit_log_message(LoggingSetup *setup,
                 GQuark log_domain, GLogLevelFlags log_level)
{
    GwyAppLogMessage logmessage;
    gchar *message = setup->message->str;

    if (setup->to_console) {
        if (!skip_annoying_message(message)) {
            emit_message_to_file(get_console_stream(log_level),
                                 log_level, message, TRUE);
        }
    }

    if (setup->to_file)
        emit_message_to_file(setup->file, log_level, message, FALSE);

    /* This should not have any extras because the just clutter the GUI. */
    logmessage.message = g_strdup(message);
    logmessage.log_domain = log_domain;
    logmessage.log_level = log_level;
    g_array_append_val(setup->message_history, logmessage);

    if (setup->textbuf)
        _gwy_app_log_add_message_to_textbuf(setup->textbuf, message, log_level);
}

static void
emit_message_to_file(FILE *fh,
                     GLogLevelFlags log_level, const gchar *message,
                     gboolean add_eol)
{
    const gchar *prg_name = g_get_prgname();
    gulong pid = getpid();

    if (add_eol && (log_level & ALERT_LEVELS))
        fputs("\n", fh);

    if (!prg_name)
        gwy_fprintf(fh, "(process:%lu): ", pid);
    else
        gwy_fprintf(fh, "(%s:%lu): ", prg_name, pid);

    fputs(message, fh);
    fflush(fh);
}

static void
format_log_message(GString *str,
                   GQuark log_domain,
                   GLogLevelFlags log_level,
                   const gchar *message)
{
    g_string_truncate(str, 0);

    if (log_domain) {
        g_string_append(str, g_quark_to_string(log_domain));
        g_string_append_c(str, '-');
    }

    append_level_prefix(str, log_level);
    g_string_append(str, ": ");
    append_escaped_message(str, message);
    g_string_append_c(str, '\n');
}

static void
append_escaped_message(GString *str, const gchar *message)
{
    guint pos, good_from, utf8_valid_only_to;
    gboolean escape_this;
    const gchar *p;
    guchar c;

    if (!message) {
        g_string_append(str, "(NULL) message");
        return;
    }

    g_utf8_validate(message, -1, &p);
    utf8_valid_only_to = p - message;
    for (pos = good_from = 0; (c = (guchar)message[pos]); pos++) {
        /* First find out if this character breaks the sequence of characters
         * we just copy as-is. */
        escape_this = FALSE;
        if (pos == utf8_valid_only_to) {
            escape_this = TRUE;
            g_utf8_validate(message + pos + 1, -1, &p);
            utf8_valid_only_to = p - message;
        }
        else if (g_ascii_iscntrl(c) && !g_ascii_isspace(c))
            escape_this = TRUE;

        /* If it does not just increment the counter.  If we have to escape
         * this one then first copy as-is the entire good segment, and only
         * after that add the bad character. */
        if (escape_this) {
            if (pos > good_from)
                g_string_append_len(str, message + good_from, pos - good_from);
            g_string_append_printf(str, "\\x%02x", c);
            good_from = pos+1;
        }
    }
    if (pos > good_from)
        g_string_append_len(str, message + good_from, pos - good_from);
}

/* Similar to GLib's function but we do not handle recursive and fatal errors
 * so we do not have to handle the difficult cases and can use GLib functions.
 */
static void
append_level_prefix(GString *str,
                    GLogLevelFlags log_level)
{
    if (log_level == G_LOG_LEVEL_ERROR)
        g_string_append(str, "ERROR");
    else if (log_level == G_LOG_LEVEL_CRITICAL)
        g_string_append(str, "CRITICAL");
    else if (log_level == G_LOG_LEVEL_WARNING)
        g_string_append(str, "WARNING");
    else if (log_level == G_LOG_LEVEL_MESSAGE)
        g_string_append(str, "Message");
    else if (log_level == G_LOG_LEVEL_INFO)
        g_string_append(str, "INFO");
    else if (log_level == G_LOG_LEVEL_DEBUG)
        g_string_append(str, "DEBUG");
    else if (log_level)
        g_string_append_printf(str, "LOG-%u", log_level);
    else
        g_string_append(str, "LOG");
}

static FILE*
get_console_stream(GLogLevelFlags log_level)
{
    if (log_level == G_LOG_LEVEL_ERROR
        || log_level == G_LOG_LEVEL_CRITICAL
        || log_level == G_LOG_LEVEL_WARNING
        || log_level == G_LOG_LEVEL_MESSAGE)
        return stderr;
    return stdout;
}

/* Annoying messages we keep in the log but do not print to console. */
static gboolean
skip_annoying_message(const gchar *message)
{
    static const gchar *skip_message_prefixes[] = {
        "GtkSourceView-WARNING: Cannot create a regex for all the transitions, "
            "the syntax highlighting process will be slower than usual.",
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS(skip_message_prefixes); i++) {
        if (g_str_has_prefix(message, skip_message_prefixes[i]))
            return TRUE;
    }
    return FALSE;
}

/**
 * gwy_app_get_log_text_buffer:
 *
 * Obtains a text buffer with program log messages.
 *
 * This functions may only be called after gwy_app_setup_logging() and,
 * obviously, after GTK+ was intialised.
 *
 * The text buffer is owned by the library and must not be modified nor
 * destroyed.  It will be already filled with messages occurring between
 * gwy_app_setup_logging() and this function call.  New messages will be
 * appended to the buffer as they arrive.
 *
 * Returns: Text buffer with the program log messages.
 *
 * Since: 2.45
 **/
GtkTextBuffer*
gwy_app_get_log_text_buffer(void)
{
    if (!log_setup.message_history) {
        g_warning("Obtaining program log text buffer requires "
                  "gwy_app_setup_logging() being called first.");
        return _gwy_app_log_create_textbuf();
    }

    if (!log_setup.textbuf) {
        GArray *message_history = log_setup.message_history;
        guint i;

        log_setup.textbuf = _gwy_app_log_create_textbuf();
        for (i = 0; i < message_history->len; i++) {
            GwyAppLogMessage *message = &g_array_index(message_history,
                                                       GwyAppLogMessage, i);
            _gwy_app_log_add_message_to_textbuf(log_setup.textbuf,
                                                message->message,
                                                message->log_level);
        }
    }

    return log_setup.textbuf;
}

GtkTextBuffer*
_gwy_app_log_create_textbuf(void)
{
    GtkTextBuffer *textbuf = gtk_text_buffer_new(NULL);

    gtk_text_buffer_create_tag(textbuf, "ERROR",
                               "foreground", "#ffffff",
                               "foreground-set", TRUE,
                               "background", "#e00000",
                               "background-set", TRUE,
                               NULL);
    gtk_text_buffer_create_tag(textbuf, "CRITICAL",
                               "foreground", "#e00000",
                               "foreground-set", TRUE,
                               NULL);
    gtk_text_buffer_create_tag(textbuf, "WARNING",
                               "foreground", "#b05000",
                               "foreground-set", TRUE,
                               NULL);
    gtk_text_buffer_create_tag(textbuf, "Message",
                               "foreground", "#3030f0",
                               "foreground-set", TRUE,
                               NULL);
    gtk_text_buffer_create_tag(textbuf, "INFO",
                               "foreground", "#000000",
                               "foreground-set", TRUE,
                               NULL);
    gtk_text_buffer_create_tag(textbuf, "DEBUG",
                               "foreground", "#a0a0a0",
                               "foreground-set", TRUE,
                               NULL);

    return textbuf;
}

void
_gwy_app_log_add_message_to_textbuf(GtkTextBuffer *textbuf,
                                    const gchar *message,
                                    GLogLevelFlags log_level)
{
    const gchar *tagname = NULL;
    GtkTextIter iter;

    if (log_level & G_LOG_LEVEL_ERROR)
        tagname = "ERROR";
    else if (log_level & G_LOG_LEVEL_CRITICAL)
        tagname = "CRITICAL";
    else if (log_level & G_LOG_LEVEL_WARNING)
        tagname = "WARNING";
    else if (log_level & G_LOG_LEVEL_MESSAGE)
        tagname = "Message";
    else if (log_level & G_LOG_LEVEL_INFO)
        tagname = "INFO";
    else if (log_level & G_LOG_LEVEL_DEBUG)
        tagname = "DEBUG";

    gtk_text_buffer_get_end_iter(textbuf, &iter);
    if (tagname) {
        gtk_text_buffer_insert_with_tags_by_name(textbuf, &iter, message, -1,
                                                 tagname, NULL);
    }
    else
        gtk_text_buffer_insert(textbuf, &iter, message, -1);
}

/************************** Documentation ****************************/

/**
 * SECTION:logging
 * @title: logging
 * @short_description: Program message log
 **/

/**
 * GwyAppLoggingFlags:
 * @GWY_APP_LOGGING_TO_FILE: Messages go to a log file, either gwyddion.log or
 *                           given by environment variable GWYDDION_LOGFILE.
 * @GWY_APP_LOGGING_TO_CONSOLE: Messages go to standard output and standard
 *                              error depending on message type (emulating
 *                              where GLib sends them).
 *
 * Flags controlling where program messages are written.
 *
 * Since: 2.45
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
