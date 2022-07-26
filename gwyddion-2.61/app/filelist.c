/*
 *  $Id: filelist.c 24709 2022-03-21 17:31:45Z yeti-dn $
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

/*
 * This file implements -- among other things -- Thumbnail Managing Standard
 * http://triq.net/~jens/thumbnail-spec/index.html
 *
 * The implementation is quite minimal: we namely ignore large thumbnails altogether (as they would be usually larger
 * than SPM data). We try not to break other TMS aware applications though.
 */

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef _WIN32
#include <process.h>
#endif

#ifdef _MSC_VER
#define getpid _getpid
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libgwyddion/gwymd5.h>
#include <libgwymodule/gwymodule-file.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>
#include "gwyappinternal.h"

/* PNG (additional in TMS) */
#define KEY_DESCRIPTION "tEXt::Description"
#define KEY_SOFTWARE    "tEXt::Software"
/* TMS, required */
#define KEY_THUMB_URI   "tEXt::Thumb::URI"
#define KEY_THUMB_MTIME "tEXt::Thumb::MTime"
/* TMS, additional */
#define KEY_THUMB_FILESIZE "tEXt::Thumb::Size"
#define KEY_THUMB_MIMETYPE "tEXt::Thumb::Mimetype"
/* TMS, format specific
 * XXX: we use Image::Width, Image::Height, even tough the data are not images but they are very image-like.  There is
 * no place to store the third dimension for volume data because there is no generic key for image stacks, only
 * specific keys for multipage documents and movies. */
#define KEY_THUMB_IMAGE_WIDTH "tEXt::Thumb::Image::Width"
#define KEY_THUMB_IMAGE_HEIGHT "tEXt::Thumb::Image::Height"
/* Gwyddion specific */
#define KEY_THUMB_GWY_REAL_SIZE "tEXt::Thumb::X-Gwyddion::RealSize"

typedef enum {
    FILE_STATE_UNKNOWN = 0,
    FILE_STATE_OLD,
    FILE_STATE_OK,
    FILE_STATE_FAILED
} FileState;

enum {
    FILELIST_RAW,
    FILELIST_THUMB,
    FILELIST_FILENAME,
    FILELIST_LAST
};

typedef struct {
    FileState file_state;
    gchar *file_utf8;
    gchar *file_utf8_lc;
    gchar *file_sys;
    gchar *file_uri;
    gulong file_mtime;
    gulong file_size;

    gint image_width;
    gint image_height;
    gchar *image_real_size;

    FileState thumb_state;
    gchar *thumb_sys;    /* doesn't matter, names are ASCII */
    gulong thumb_mtime;
    GdkPixbuf *pixbuf;
} GwyRecentFile;

typedef struct {
    GtkListStore *store;
    GString *glob;
    gboolean casesens;
    GPatternSpec *pattern;

    GtkTreeModel *filter;
    GList *recent_file_list;
    GtkWidget *window;
    GtkWidget *list;
    GtkWidget *open;
    GtkWidget *prune;
    GtkWidget *filter_glob;
    GtkWidget *filter_case;
} Controls;

static GtkWidget*     gwy_app_recent_file_list_construct          (Controls *controls);
static void           gwy_app_recent_file_list_unmapped           (GtkWindow *window);
static void           cell_renderer_desc                          (GtkTreeViewColumn *column,
                                                                   GtkCellRenderer *cell,
                                                                   GtkTreeModel *model,
                                                                   GtkTreeIter *piter,
                                                                   gpointer userdata);
static void           cell_renderer_thumb                         (GtkTreeViewColumn *column,
                                                                   GtkCellRenderer *cell,
                                                                   GtkTreeModel *model,
                                                                   GtkTreeIter *iter,
                                                                   gpointer userdata);
static void           gwy_app_recent_file_list_update_sensitivity (Controls *controls);
static void           gwy_app_recent_file_list_row_activated      (GtkTreeView *treeview,
                                                                   GtkTreePath *path,
                                                                   GtkTreeViewColumn *column,
                                                                   gpointer user_data);
static void           gwy_app_recent_file_list_destroyed          (Controls *controls);
static void           gwy_app_recent_file_list_prune              (Controls *controls);
static void           gwy_app_recent_file_list_open               (GtkWidget *list);
static GtkWidget*     gwy_app_recent_file_list_filter_construct   (Controls *controls);
static void           gwy_app_recent_file_list_filter_clear       (GtkWidget *button,
                                                                   Controls *controls);
static void           gwy_app_recent_file_list_filter_apply       (GtkEntry *entry,
                                                                   Controls *controls);
static void           gwy_app_recent_file_list_filter_case_changed(GtkToggleButton *check,
                                                                   Controls *controls);
static gboolean       gwy_app_recent_file_list_filter             (GtkTreeModel *model,
                                                                   GtkTreeIter *iter,
                                                                   gpointer data);
static gboolean       gwy_app_recent_file_find                    (const gchar *filename_utf8,
                                                                   GtkTreeIter *piter,
                                                                   GwyRecentFile **prf);
static void           gwy_app_recent_file_list_update_menu        (Controls *controls);
static void           gwy_app_recent_file_create_dirs             (void);
static GwyRecentFile* gwy_app_recent_file_new                     (gchar *filename_utf8,
                                                                   gchar *filename_sys);
static gboolean       gwy_app_recent_file_try_load_thumbnail      (GwyRecentFile *rf);
static void           gwy_recent_file_update_thumbnail            (GwyRecentFile *rf,
                                                                   GwyContainer *data,
                                                                   GwyAppPage pageno,
                                                                   gint hint,
                                                                   GdkPixbuf *use_this_pixbuf);
static void           gwy_app_recent_file_free                    (GwyRecentFile *rf);
static gchar*         gwy_recent_file_thumbnail_name              (const gchar *uri);
static const gchar*   gwy_recent_file_thumbnail_dir               (void);

static guint remember_recent_files = 1024;

/* Note we assert initialization to zeroes */
static Controls gcontrols;

static GdkPixbuf*
gwy_app_recent_file_list_get_failed_pixbuf(void)
{
    static GdkPixbuf *failed_pixbuf = NULL;

    if (!failed_pixbuf) {
        failed_pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, THUMB_SIZE, THUMB_SIZE);
        gdk_pixbuf_fill(failed_pixbuf, 0);
    }

    return failed_pixbuf;
}

/**
 * gwy_app_recent_file_list_new:
 *
 * Creates document history browser.
 *
 * There should be at most one document history browser, so this function fails if it already exists.
 *
 * Returns: The newly created document history browser window.
 **/
GtkWidget*
gwy_app_recent_file_list_new(void)
{
    GtkWidget *vbox, *filterbox, *buttonbox, *list, *scroll, *button;
    GtkTreeModelFilter *filter;
    GtkTreeSelection *selection;

    g_return_val_if_fail(gcontrols.store, gcontrols.window);
    g_return_val_if_fail(gcontrols.window == NULL, gcontrols.window);

    gcontrols.filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(gcontrols.store), NULL);
    filter = GTK_TREE_MODEL_FILTER(gcontrols.filter);
    gtk_tree_model_filter_set_visible_func(filter, gwy_app_recent_file_list_filter, &gcontrols, NULL);

    gcontrols.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(gcontrols.window), _("Document History"));
    gtk_window_set_default_size(GTK_WINDOW(gcontrols.window), 400, 3*gdk_screen_height()/4);
    gwy_app_restore_window_position(GTK_WINDOW(gcontrols.window), "/app/document-history", FALSE);
    gwy_help_add_to_window(GTK_WINDOW(gcontrols.window), "managing-files", "document-history", GWY_HELP_DEFAULT);
    g_signal_connect(gcontrols.window, "unmap", G_CALLBACK(gwy_app_recent_file_list_unmapped), NULL);

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(gcontrols.window), vbox);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    list = gwy_app_recent_file_list_construct(&gcontrols);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    g_object_unref(gcontrols.filter);

    filterbox = gwy_app_recent_file_list_filter_construct(&gcontrols);
    gtk_box_pack_start(GTK_BOX(vbox), filterbox, FALSE, FALSE, 0);

    buttonbox = gtk_hbox_new(TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(buttonbox), 2);
    gtk_box_pack_start(GTK_BOX(vbox), buttonbox, FALSE, FALSE, 0);

    gcontrols.prune = gwy_stock_like_button_new(_("Clean U_p"), GTK_STOCK_FIND);
    gtk_box_pack_start(GTK_BOX(buttonbox), gcontrols.prune, TRUE, TRUE, 0);
    gtk_widget_set_tooltip_text(gcontrols.prune,
                                _("Remove entries of files that no longer exist"));
    g_signal_connect_swapped(gcontrols.prune, "clicked", G_CALLBACK(gwy_app_recent_file_list_prune), &gcontrols);

    button = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
    gtk_box_pack_start(GTK_BOX(buttonbox), button, TRUE, TRUE, 0);
    gtk_widget_set_tooltip_text(button, _("Close file list"));
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_widget_destroy), gcontrols.window);

    gcontrols.open = gtk_button_new_from_stock(GTK_STOCK_OPEN);
    gtk_box_pack_start(GTK_BOX(buttonbox), gcontrols.open, TRUE, TRUE, 0);
    gtk_widget_set_tooltip_text(gcontrols.open, _("Open selected file"));
    g_signal_connect_swapped(gcontrols.open, "clicked", G_CALLBACK(gwy_app_recent_file_list_open), list);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
    gtk_widget_set_sensitive(gcontrols.open, gtk_tree_selection_get_selected(selection, NULL, NULL));

    g_signal_connect_swapped(gcontrols.window, "destroy", G_CALLBACK(gwy_app_recent_file_list_destroyed), &gcontrols);

    gwy_app_recent_file_list_filter_apply(GTK_ENTRY(gcontrols.filter_glob), &gcontrols);
    gtk_widget_show_all(vbox);

    return gcontrols.window;
}

static void
gwy_app_recent_file_list_unmapped(GtkWindow *window)
{
    gwy_app_save_window_position(window, "/app/document-history", FALSE, TRUE);
}


static GtkWidget*
gwy_app_recent_file_list_construct(Controls *controls)
{
    static const struct {
        const gchar *title;
        const guint id;
    }
    columns[] = {
        { "Preview",    FILELIST_THUMB },
        { "File Path",  FILELIST_FILENAME },
    };

    GtkWidget *list;
    GtkTreeSelection *selection;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    g_return_val_if_fail(controls->store, NULL);

    list = gtk_tree_view_new_with_model(controls->filter);
    controls->list = list;
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);

    /* thumbnail name column */
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_renderer_set_fixed_size(renderer, THUMB_SIZE, THUMB_SIZE);
    column = gtk_tree_view_column_new_with_attributes(_(columns[0].title), renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            cell_renderer_thumb, GUINT_TO_POINTER(columns[0].id), NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

    renderer = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_fixed_size(renderer, -1, THUMB_SIZE);
    column = gtk_tree_view_column_new_with_attributes(_(columns[1].title), renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                            cell_renderer_desc, GUINT_TO_POINTER(columns[1].id), NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

    /* selection */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

    g_signal_connect_swapped(selection, "changed", G_CALLBACK(gwy_app_recent_file_list_update_sensitivity), controls);
    g_signal_connect_swapped(controls->store, "row-deleted",
                             G_CALLBACK(gwy_app_recent_file_list_update_sensitivity), controls);
    g_signal_connect_swapped(controls->store, "row-inserted",
                             G_CALLBACK(gwy_app_recent_file_list_update_sensitivity), controls);
    g_signal_connect(controls->list, "row-activated", G_CALLBACK(gwy_app_recent_file_list_row_activated), controls);

    return list;
}

static void
gwy_app_recent_file_list_update_sensitivity(Controls *controls)
{
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    gboolean has_rows;

    if (!controls->window)
        return;

    /* Prune sensitivity depends on absolute row availability */
    has_rows = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(controls->store), &iter);
    gtk_widget_set_sensitive(controls->prune, has_rows);

    /* Open sensitivity depends on visible row availability */
    has_rows = gtk_tree_model_get_iter_first(controls->filter, &iter);
    if (has_rows) {
        selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(controls->list));
        gtk_widget_set_sensitive(controls->open, gtk_tree_selection_get_selected(selection, NULL, NULL));
    }
    else
        gtk_widget_set_sensitive(controls->open, has_rows);
}

static void
gwy_app_recent_file_list_destroyed(Controls *controls)
{
    if (controls->pattern)
        g_pattern_spec_free(controls->pattern);

    controls->pattern = NULL;
    controls->window = NULL;
    controls->open = NULL;
    controls->prune = NULL;
    controls->list = NULL;
    controls->filter = NULL;
    controls->filter_glob = NULL;
    controls->filter_case = NULL;
}

static void
gwy_app_recent_file_list_prune(Controls *controls)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    GwyRecentFile *rf;
    gboolean ok;

    g_return_if_fail(controls->store);

    model = GTK_TREE_MODEL(controls->store);
    if (!gtk_tree_model_get_iter_first(model, &iter))
        return;

    gwy_app_wait_cursor_start(GTK_WINDOW(controls->window));
    g_object_ref(controls->filter);
    gtk_tree_view_set_model(GTK_TREE_VIEW(controls->list), NULL);

    do {
        gtk_tree_model_get(model, &iter, FILELIST_RAW, &rf, -1);
        gwy_debug("<%s>", rf->file_utf8);
        if (!g_file_test(rf->file_utf8, G_FILE_TEST_IS_REGULAR)) {
            if (rf->thumb_sys && rf->thumb_state != FILE_STATE_FAILED)
                g_unlink(rf->thumb_sys);
            gwy_app_recent_file_free(rf);
            ok = gtk_list_store_remove(controls->store, &iter);
        }
        else
            ok = gtk_tree_model_iter_next(model, &iter);
    } while (ok);

    gtk_tree_view_set_model(GTK_TREE_VIEW(controls->list), controls->filter);
    g_object_unref(controls->filter);

    gwy_app_recent_file_list_update_menu(controls);
    gwy_app_recent_file_list_update_sensitivity(controls);
    gwy_app_wait_cursor_finish(GTK_WINDOW(controls->window));
}

static void
gwy_app_recent_file_list_row_activated(GtkTreeView *treeview,
                                       GtkTreePath *path,
                                       G_GNUC_UNUSED GtkTreeViewColumn *column,
                                       G_GNUC_UNUSED gpointer user_data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    GwyRecentFile *rf;

    model = gtk_tree_view_get_model(treeview);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, FILELIST_RAW, &rf, -1);
    gwy_app_file_load(rf->file_utf8, rf->file_sys, NULL);
}

static void
gwy_app_recent_file_list_open(GtkWidget *list)
{
    GtkTreeSelection *selection;
    GtkTreeModel *store;
    GtkTreeIter iter;
    GwyRecentFile *rf;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
    if (!gtk_tree_selection_get_selected(selection, &store, &iter))
        return;
    gtk_tree_model_get(store, &iter, FILELIST_RAW, &rf, -1);
    gwy_app_file_load(rf->file_utf8, rf->file_sys, NULL);
}

static void
cell_renderer_desc(G_GNUC_UNUSED GtkTreeViewColumn *column,
                   GtkCellRenderer *cell,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   gpointer userdata)
{
    static GString *s = NULL;    /* XXX: never freed */
    gchar *escaped;
    guint id;
    GwyRecentFile *rf;

    id = GPOINTER_TO_UINT(userdata);
    gtk_tree_model_get(model, iter, FILELIST_RAW, &rf, -1);
    switch (id) {
        case FILELIST_FILENAME:
        escaped = g_markup_escape_text(rf->file_utf8, -1);
        if (!s)
            s = g_string_new(escaped);
        else
            g_string_assign(s, escaped);
        g_free(escaped);

        if (rf->image_width && rf->image_height)
            g_string_append_printf(s, "\n%d×%d %s", rf->image_width, rf->image_height, _("px"));
        if (rf->image_real_size) {
            g_string_append_c(s, '\n');
            g_string_append(s, rf->image_real_size);
        }
        g_object_set(cell, "markup", s->str, NULL);
        break;

        default:
        g_return_if_reached();
        break;
    }
}

static void
cell_renderer_thumb(G_GNUC_UNUSED GtkTreeViewColumn *column,
                    GtkCellRenderer *cell,
                    GtkTreeModel *model,
                    GtkTreeIter *iter,
                    gpointer userdata)
{
    GwyRecentFile *rf;
    guint id;

    if (!GTK_WIDGET_REALIZED(gcontrols.list))
        return;
    id = GPOINTER_TO_UINT(userdata);
    g_return_if_fail(id == FILELIST_THUMB);
    gtk_tree_model_get(model, iter, FILELIST_RAW, &rf, -1);
    gwy_debug("<%s>", rf->file_utf8);
    switch (rf->thumb_state) {
        case FILE_STATE_UNKNOWN:
        if (!gwy_app_recent_file_try_load_thumbnail(rf))
            return;
        case FILE_STATE_FAILED:
        case FILE_STATE_OK:
        case FILE_STATE_OLD:
        g_object_set(cell, "pixbuf", rf->pixbuf, NULL);
        break;

        default:
        g_assert_not_reached();
        break;
    }
}

static GtkWidget*
gwy_app_recent_file_list_filter_construct(Controls *controls)
{
    GwyContainer *settings;
    GtkWidget *hbox, *label, *entry, *check, *button, *image;
    const guchar *glob;

    settings = gwy_app_settings_get();

    hbox = gtk_hbox_new(FALSE, 0);

    label = gtk_label_new_with_mnemonic(_("_Filter:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 4);

    if (!controls->glob)
        controls->glob = g_string_new(NULL);
    if (gwy_container_gis_string_by_name(settings, "/app/file/recent/glob", &glob))
        g_string_assign(controls->glob, glob);

    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), controls->glob->str);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    controls->filter_glob = entry;
    g_signal_connect(entry, "activate", G_CALLBACK(gwy_app_recent_file_list_filter_apply), controls);

    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(gwy_app_recent_file_list_filter_clear), controls);

    image = gtk_image_new_from_stock(GTK_STOCK_CLEAR, GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_button_set_image(GTK_BUTTON(button), image);

#ifdef G_OS_WIN32
    controls->casesens = FALSE;
#else
    controls->casesens = TRUE;
#endif
    gwy_container_gis_boolean_by_name(settings, "/app/file/recent/case-sensitive", &controls->casesens);

    check = gtk_check_button_new_with_mnemonic(_("Case _sensitive"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), controls->casesens);
    gtk_box_pack_start(GTK_BOX(hbox), check, FALSE, FALSE, 4);
    controls->filter_case = check;
    g_signal_connect(check, "toggled", G_CALLBACK(gwy_app_recent_file_list_filter_case_changed), controls);

    return hbox;
}

static void
gwy_app_recent_file_list_filter_clear(G_GNUC_UNUSED GtkWidget *button,
                                      Controls *controls)
{
    gtk_entry_set_text(GTK_ENTRY(controls->filter_glob), "");
    gtk_widget_activate(controls->filter_glob);
}

static void
gwy_app_recent_file_list_filter_apply(GtkEntry *entry,
                                      Controls *controls)
{
    GPatternSpec *oldpattern;
    GwyContainer *settings;
    gchar *s, *t;

    settings = gwy_app_settings_get();
    g_string_assign(controls->glob, gtk_entry_get_text(entry));
    gwy_container_set_string_by_name(settings, "/app/file/recent/glob", g_strdup(controls->glob->str));

    oldpattern = controls->pattern;

    if (controls->casesens) {
        if (!strchr(controls->glob->str, '*') && !strchr(controls->glob->str, '?'))
            s = g_strconcat("*", controls->glob->str, "*", NULL);
        else
            s = g_strdup(controls->glob->str);
    }
    else {
        /* FIXME: This is crude. */
        s = g_utf8_strdown(controls->glob->str, controls->glob->len);
        if (!strchr(s, '*') && !strchr(s, '?')) {
            t = s;
            s = g_strconcat("*", t, "*", NULL);
            g_free(t);
        }
    }
    controls->pattern = g_pattern_spec_new(s);
    g_free(s);

    if (oldpattern)
        g_pattern_spec_free(oldpattern);

    if (GTK_WIDGET_REALIZED(controls->window))
        gwy_app_wait_cursor_start(GTK_WINDOW(controls->window));
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(controls->filter));
    if (GTK_WIDGET_REALIZED(controls->window))
        gwy_app_wait_cursor_finish(GTK_WINDOW(controls->window));
}

static void
gwy_app_recent_file_list_filter_case_changed(GtkToggleButton *check,
                                             Controls *controls)
{
    GwyContainer *settings;

    settings = gwy_app_settings_get();
    controls->casesens = gtk_toggle_button_get_active(check);
    gwy_container_set_boolean_by_name(settings, "/app/file/recent/case-sensitive", controls->casesens);

    gwy_app_recent_file_list_filter_apply(GTK_ENTRY(controls->filter_glob), controls);
}

static gboolean
gwy_app_recent_file_list_filter(GtkTreeModel *model,
                                GtkTreeIter *iter,
                                gpointer data)
{
    Controls *controls = (Controls*)data;
    GwyRecentFile *rf;

    if (!controls->pattern)
        return TRUE;

    gtk_tree_model_get(model, iter, 0, &rf, -1);
    /* This can happen when the row has been just created and rf is not set yet. */
    if (!rf)
        return FALSE;

    if (controls->casesens)
        return g_pattern_match_string(controls->pattern, rf->file_utf8);
    else {
        if (!rf->file_utf8_lc)
            rf->file_utf8_lc = g_utf8_strdown(rf->file_utf8, -1);

        return g_pattern_match_string(controls->pattern, rf->file_utf8_lc);
    }
}

/**
 * gwy_app_recent_file_list_load:
 * @filename: Name of file containing list of recently open files.
 *
 * Loads list of recently open files from @filename.
 *
 * Cannot be called more than once (at least not without doing gwy_app_recent_file_list_free() first).  Must be called
 * before any other document history function can be used, even if on a nonexistent file: use %NULL as @filename in
 * that case.
 *
 * Returns: %TRUE if the file was read successfully, %FALSE otherwise.
 **/
gboolean
gwy_app_recent_file_list_load(const gchar *filename)
{
    GtkTreeIter iter;
    GError *err = NULL;
    gchar *buffer = NULL;
    gsize size = 0;
    gchar **files;
    guint n, nrecent;

    gwy_app_recent_file_create_dirs();

    g_return_val_if_fail(gcontrols.store == NULL, FALSE);
    gcontrols.store = gtk_list_store_new(1, G_TYPE_POINTER);

    if (!filename)
        return TRUE;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        g_clear_error(&err);
        return FALSE;
    }

#ifdef G_OS_WIN32
    gwy_strkill(buffer, "\r");
#endif
    files = g_strsplit(buffer, "\n", 0);
    g_free(buffer);
    if (!files)
        return TRUE;

    nrecent = _gwy_app_get_n_recent_files();
    for (n = 0; files[n]; n++) {
        if (*files[n] && g_utf8_validate(files[n], -1, NULL)) {
            GwyRecentFile *rf;

            gwy_debug("%s", files[n]);
            rf = gwy_app_recent_file_new(gwy_canonicalize_path(files[n]), NULL);
            gtk_list_store_insert_with_values(gcontrols.store, &iter, G_MAXINT, FILELIST_RAW, rf, -1);
            if (n < nrecent) {
                gcontrols.recent_file_list = g_list_append(gcontrols.recent_file_list, rf->file_utf8);
            }
        }
        g_free(files[n]);
    }
    g_free(files);

    return TRUE;
}


/**
 * gwy_app_recent_file_list_save:
 * @filename: Name of file to save the list of recently open files to.
 *
 * Saves list of recently open files to @filename.
 *
 * Returns: %TRUE if the file was written successfully, %FALSE otherwise.
 **/
gboolean
gwy_app_recent_file_list_save(const gchar *filename)
{
    GtkTreeIter iter;
    GwyRecentFile *rf;
    guint i;
    FILE *fh;

    g_return_val_if_fail(gcontrols.store, FALSE);
    fh = gwy_fopen(filename, "w");
    if (!fh)
        return FALSE;

    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(gcontrols.store), &iter)) {
        i = 0;
        do {
            gtk_tree_model_get(GTK_TREE_MODEL(gcontrols.store), &iter, FILELIST_RAW, &rf, -1);
            fputs(rf->file_utf8, fh);
            fputc('\n', fh);
            i++;
        } while (i < remember_recent_files && gtk_tree_model_iter_next(GTK_TREE_MODEL(gcontrols.store), &iter));
    }
    fclose(fh);

    return TRUE;
}

/**
 * gwy_app_recent_file_list_free:
 *
 * Frees all memory taken by recent file list.
 *
 * Should not be called while the recent file menu still exists.
 **/
void
gwy_app_recent_file_list_free(void)
{
    GtkTreeIter iter;
    GwyRecentFile *rf;

    if (!gcontrols.store)
        return;

    if (gcontrols.window)
        gtk_widget_destroy(gcontrols.window);

    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(gcontrols.store), &iter)) {
        do {
            gtk_tree_model_get(GTK_TREE_MODEL(gcontrols.store), &iter, FILELIST_RAW, &rf, -1);
            gwy_app_recent_file_free(rf);
        } while (gtk_list_store_remove(gcontrols.store, &iter));
    }
    GWY_OBJECT_UNREF(gcontrols.store);

    if (gcontrols.glob) {
        g_string_free(gcontrols.glob, TRUE);
        gcontrols.glob = NULL;
    }

    g_list_free(gcontrols.recent_file_list);
    gcontrols.recent_file_list = NULL;
    gwy_app_recent_file_list_update_menu(&gcontrols);
}

/**
 * gwy_app_recent_file_list_update:
 * @data: A data container corresponding to the file.
 * @filename_utf8: A recent file to insert or move to the first position in document history, in UTF-8.
 * @filename_sys: A recent file to insert or move to the first position in document history, in GLib encoding.
 * @hint: Preferred channel id to use for thumbnail, pass 0 if no channel is specificaly preferred.
 *
 * Moves @filename_utf8 to the first position in document history, possibly adding it if not present yet.
 *
 * At least one of @filename_utf8, @filename_sys should be set.
 **/
void
gwy_app_recent_file_list_update(GwyContainer *data,
                                const gchar *filename_utf8,
                                const gchar *filename_sys,
                                gint hint)
{
    gboolean free_utf8 = FALSE, free_sys = FALSE;

    g_return_if_fail(!data || GWY_IS_CONTAINER(data));

    if (!gcontrols.store)
        return;

    if (!filename_utf8 && filename_sys) {
        filename_utf8 = g_filename_to_utf8(filename_sys, -1, NULL, NULL, NULL);
        free_utf8 = TRUE;
    }

    if (!filename_sys && filename_utf8) {
        filename_sys = g_filename_from_utf8(filename_utf8, -1, NULL, NULL, NULL);
        free_sys = TRUE;
    }

    if (filename_utf8) {
        GtkTreeIter iter;
        GwyRecentFile *rf;

        if (gwy_app_recent_file_find(filename_utf8, &iter, &rf))
            gtk_list_store_move_after(gcontrols.store, &iter, NULL);
        else {
            rf = gwy_app_recent_file_new(gwy_canonicalize_path(filename_utf8), gwy_canonicalize_path(filename_sys));
            gtk_list_store_prepend(gcontrols.store, &iter);
            gtk_list_store_set(gcontrols.store, &iter, FILELIST_RAW, rf, -1);
        }

        if (data) {
            gwy_recent_file_update_thumbnail(rf, data, GWY_PAGE_NOPAGE, hint, NULL);
        }
    }

    if (free_utf8)
        g_free((gpointer)filename_utf8);
    if (free_sys)
        g_free((gpointer)filename_sys);

    gwy_app_recent_file_list_update_menu(&gcontrols);
}

static gboolean
gwy_app_recent_file_find(const gchar *filename_utf8,
                         GtkTreeIter *piter,
                         GwyRecentFile **prf)
{
    GtkTreeIter iter;
    GwyRecentFile *rf;
    gchar *filename_canon = gwy_canonicalize_path(filename_utf8);

    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(gcontrols.store), &iter)) {
        do {
            gtk_tree_model_get(GTK_TREE_MODEL(gcontrols.store), &iter, FILELIST_RAW, &rf, -1);
            if (gwy_strequal(filename_canon, rf->file_utf8)) {
                if (piter)
                    *piter = iter;
                if (prf)
                    *prf = rf;

                g_free(filename_canon);
                return TRUE;
            }
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(gcontrols.store), &iter));
    }
    g_free(filename_canon);

    return FALSE;
}

static void
gwy_app_recent_file_list_update_menu(Controls *controls)
{
    GtkTreeIter iter;
    GList *l;
    guint i, nrecent;

    if (!controls->store) {
        g_return_if_fail(controls->recent_file_list == NULL);
        gwy_app_menu_recent_files_update(controls->recent_file_list);
        return;
    }

    l = controls->recent_file_list;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(controls->store), &iter)) {
        nrecent = _gwy_app_get_n_recent_files();
        i = 0;
        do {
            GwyRecentFile *rf;

            gtk_tree_model_get(GTK_TREE_MODEL(controls->store), &iter, FILELIST_RAW, &rf, -1);
            if (l) {
                l->data = rf->file_utf8;
                l = g_list_next(l);
            }
            else {
                controls->recent_file_list = g_list_append(controls->recent_file_list, rf->file_utf8);
            }
            i++;
        } while (i < nrecent
                 && gtk_tree_model_iter_next(GTK_TREE_MODEL(controls->store), &iter));
    }
    /* This should not happen here as we added a file */
    if (l) {
        if (!l->prev)
            controls->recent_file_list = NULL;
        else {
            l->prev->next = NULL;
            l->prev = NULL;
        }
        g_list_free(l);
    }
    gwy_app_menu_recent_files_update(controls->recent_file_list);
}

/**
 * gwy_app_recent_file_get_thumbnail:
 * @filename_utf8: Name of a recent file, in UTF-8 encoding.
 *
 * Gets thumbnail of a recently open file.
 *
 * Returns: The thumbnail as a new pixbuf or a pixbuf with a new reference.
 *          The caller must unreference it but not modify it.  If not
 *          thumbnail can not be obtained, a fully transparent pixbuf is
 *          returned.
 **/
GdkPixbuf*
gwy_app_recent_file_get_thumbnail(const gchar *filename_utf8)
{
    GdkPixbuf *pixbuf;
    GwyRecentFile *rf;

    if (gcontrols.store && gwy_app_recent_file_find(filename_utf8, NULL, &rf)) {
        if (!rf->pixbuf)
            gwy_app_recent_file_try_load_thumbnail(rf);
        pixbuf = (GdkPixbuf*)g_object_ref(rf->pixbuf);
    }
    else {
        rf = gwy_app_recent_file_new(gwy_canonicalize_path(filename_utf8), NULL);
        gwy_app_recent_file_try_load_thumbnail(rf);
        pixbuf = (GdkPixbuf*)g_object_ref(rf->pixbuf);
        gwy_app_recent_file_free(rf);
    }

    return pixbuf;
}

/* Get raw, unscaled thumbnail, also get NULL when there's none. */
GdkPixbuf*
_gwy_app_recent_file_try_thumbnail(const gchar *filename_sys)
{
    GdkPixbuf *pixbuf;
    gchar *uri, *thumb;

    if (!(uri = g_filename_to_uri(filename_sys, NULL, NULL)))
        return NULL;

    thumb = gwy_recent_file_thumbnail_name(uri);
    g_free(uri);

    pixbuf = gdk_pixbuf_new_from_file(thumb, NULL);
    g_free(thumb);

    return pixbuf;
}

void
_gwy_app_recent_file_write_thumbnail(const gchar *filename_sys,
                                     GwyContainer *data,
                                     GwyAppPage pageno,
                                     gint id,
                                     GdkPixbuf *pixbuf)
{
    GwyRecentFile *rf;

    rf = gwy_app_recent_file_new(NULL, gwy_canonicalize_path(filename_sys));
    gwy_recent_file_update_thumbnail(rf, data, pageno, id, pixbuf);
    gwy_app_recent_file_free(rf);
}

static void
gwy_app_recent_file_create_dirs(void)
{
    const gchar *base;
    gchar *dir;

    base = gwy_recent_file_thumbnail_dir();
    if (!g_file_test(base, G_FILE_TEST_IS_DIR)) {
        gwy_debug("Creating base thumbnail directory <%s>", base);
        g_mkdir(base, 0700);
    }

    dir = g_build_filename(base, "normal", NULL);
    if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        gwy_debug("Creating normal thumbnail directory <%s>", dir);
        g_mkdir(dir, 0700);
    }
    g_free(dir);
}

/* XXX: eats arguments! */
static GwyRecentFile*
gwy_app_recent_file_new(gchar *filename_utf8,
                        gchar *filename_sys)
{
    GError *err = NULL;
    GwyRecentFile *rf;

    g_return_val_if_fail(filename_utf8 || filename_sys, NULL);

    if (!filename_utf8)
        filename_utf8 = g_filename_to_utf8(filename_sys, -1, NULL, NULL, NULL);
    if (!filename_sys)
        filename_sys = g_filename_from_utf8(filename_utf8, -1, NULL, NULL, NULL);

    rf = g_new0(GwyRecentFile, 1);
    rf->file_utf8 = filename_utf8;
    rf->file_sys = filename_sys;
    if (!(rf->file_uri = g_filename_to_uri(filename_sys, NULL, &err))) {
        /* TODO: recovery ??? */
        rf->thumb_state = FILE_STATE_FAILED;
        g_clear_error(&err);
        return rf;
    }
    rf->thumb_sys = gwy_recent_file_thumbnail_name(rf->file_uri);

    return rf;
}

static void
gwy_app_recent_file_free(GwyRecentFile *rf)
{
    GWY_OBJECT_UNREF(rf->pixbuf);
    g_free(rf->file_utf8_lc);
    g_free(rf->file_utf8);
    g_free(rf->file_sys);
    g_free(rf->file_uri);
    g_free(rf->thumb_sys);
    g_free(rf->image_real_size);
    g_free(rf);
}

static gboolean
gwy_app_recent_file_try_load_thumbnail(GwyRecentFile *rf)
{
    GdkPixbuf *pixbuf;
    gint width, height;
    const gchar *option;
    GStatBuf st;
    gdouble scale;

    gwy_debug("<%s>", rf->thumb_sys);
    rf->thumb_state = FILE_STATE_FAILED;
    GWY_OBJECT_UNREF(rf->pixbuf);

    if (!rf->thumb_sys) {
        rf->pixbuf = gwy_app_recent_file_list_get_failed_pixbuf();
        g_object_ref(rf->pixbuf);
        return FALSE;
    }

    pixbuf = gdk_pixbuf_new_from_file(rf->thumb_sys, NULL);
    if (!pixbuf) {
        rf->pixbuf = gwy_app_recent_file_list_get_failed_pixbuf();
        g_object_ref(rf->pixbuf);
        return FALSE;
    }

    width = gdk_pixbuf_get_width(pixbuf);
    height = gdk_pixbuf_get_height(pixbuf);
    scale = (gdouble)THUMB_SIZE/MAX(width, height);
    width = CLAMP((gint)(scale*width), 1, THUMB_SIZE);
    height = CLAMP((gint)(scale*height), 1, THUMB_SIZE);
    rf->pixbuf = gdk_pixbuf_scale_simple(pixbuf, width, height, GDK_INTERP_TILES);

    option = gdk_pixbuf_get_option(pixbuf, KEY_THUMB_URI);
    gwy_debug("uri = <%s>", rf->file_uri);
    if (!option || strcmp(option, rf->file_uri)) {
        g_warning("URI <%s> from thumb doesn't match <%s>. If this isn't an MD5 collision, it's an implementation bug",
                  option, rf->file_uri);
    }

    if ((option = gdk_pixbuf_get_option(pixbuf, KEY_THUMB_MTIME)))
        rf->thumb_mtime = atol(option);

    if ((option = gdk_pixbuf_get_option(pixbuf, KEY_THUMB_FILESIZE)))
        rf->file_size = atol(option);

    if ((option = gdk_pixbuf_get_option(pixbuf, KEY_THUMB_IMAGE_WIDTH)))
        rf->image_width = atoi(option);

    if ((option = gdk_pixbuf_get_option(pixbuf, KEY_THUMB_IMAGE_HEIGHT)))
        rf->image_height = atoi(option);

    if ((option = gdk_pixbuf_get_option(pixbuf, KEY_THUMB_GWY_REAL_SIZE)))
        rf->image_real_size = g_strdup(option);

    if (g_stat(rf->file_sys, &st) == 0) {
        rf->file_state = FILE_STATE_OK;
        rf->file_mtime = st.st_mtime;
        if (rf->thumb_mtime != rf->file_mtime)
            rf->thumb_state = FILE_STATE_OLD;
        else
            rf->thumb_state = FILE_STATE_OK;
    }
    else {
        rf->thumb_state = FILE_STATE_OLD;
        rf->file_state = FILE_STATE_FAILED;
    }

    g_object_unref(pixbuf);
    gwy_debug("<%s> thumbnail loaded OK", rf->file_utf8);

    return TRUE;
}

/* Consumes ids. */
static gint
find_lowest_id(gint *ids, gint hint)
{
    gint i, id;

    for (i = 0, id = G_MAXINT; ids[i] != -1; i++) {
        if (ids[i] >= hint && ids[i] < id)
            id = ids[i];
    }
    /* On failure simply find the data item with the lowest id. */
    if (id == G_MAXINT) {
        for (i = 0, id = G_MAXINT; ids[i] != -1; i++) {
            if (ids[i] < id)
                id = ids[i];
        }
    }
    g_free(ids);

    return (id == G_MAXINT) ? -1 : id;
}

static gint
gwy_recent_file_find_some_channel(GwyContainer *data,
                                  gint hint)
{
    return find_lowest_id(gwy_app_data_browser_get_data_ids(data), hint);
}

static gint
gwy_recent_file_find_some_graph(GwyContainer *data)
{
    return find_lowest_id(gwy_app_data_browser_get_graph_ids(data), 0);
}

static gint
gwy_recent_file_find_some_volume(GwyContainer *data)
{
    return find_lowest_id(gwy_app_data_browser_get_volume_ids(data), 0);
}

static gint
gwy_recent_file_find_some_xyz(GwyContainer *data)
{
    return find_lowest_id(gwy_app_data_browser_get_xyz_ids(data), 0);
}

static gint
gwy_recent_file_find_some_cmap(GwyContainer *data)
{
    return find_lowest_id(gwy_app_data_browser_get_curve_map_ids(data), 0);
}

static void
gwy_recent_file_update_thumbnail(GwyRecentFile *rf,
                                 GwyContainer *data,
                                 GwyAppPage pageno,
                                 gint hint,
                                 GdkPixbuf *use_this_pixbuf)
{
    /* Prioritise volume and XYZ data over images because if both images and some strange data are in the same file
     * most likely the strange data are the primary data.  */
    static const GwyAppPage pages_priority[] = {
        GWY_PAGE_CURVE_MAPS, GWY_PAGE_XYZS, GWY_PAGE_VOLUMES, GWY_PAGE_CHANNELS, GWY_PAGE_GRAPHS,
    };

    GwyDataField *dfield = NULL;
    GwyBrick *brick = NULL;
    GwySurface *surface = NULL;
    GwyLawn *lawn = NULL;
    GwyGraphModel *gmodel = NULL;
    GdkPixbuf *pixbuf;
    GStatBuf st;
    gchar *fnm;
    GwySIUnit *siunit;
    GwySIValueFormat *vf, *vf2;
    gdouble xreal, yreal, zreal, xmin, ymin;
    gchar str_mtime[22];
    gchar str_size[22];
    gchar str_width[22];
    gchar str_height[22];
    GError *err = NULL;
    GQuark quark;
    gint ids[GWY_NPAGES];
    GPtrArray *option_keys, *option_values;
    guint i;

    g_return_if_fail(GWY_CONTAINER(data));

    for (i = 0; i < GWY_NPAGES; i++)
        ids[i] = G_MAXINT;

    if (use_this_pixbuf) {
        /* If we are given a pixbuf, hint must be the ultimate channel id.
         * We also ignore the thnumbnail state then. */
        g_return_if_fail(GDK_IS_PIXBUF(use_this_pixbuf));
        g_return_if_fail((gint)pageno >= 0 && (gint)pageno < GWY_NPAGES);
        ids[pageno] = hint;
        pixbuf = g_object_ref(use_this_pixbuf);
    }
    else {
        pixbuf = NULL;
        /* Find channel with the lowest id not smaller than hint */
        ids[GWY_PAGE_CHANNELS] = gwy_recent_file_find_some_channel(data, hint);
        ids[GWY_PAGE_GRAPHS] = gwy_recent_file_find_some_graph(data);
        ids[GWY_PAGE_VOLUMES] = gwy_recent_file_find_some_volume(data);
        ids[GWY_PAGE_XYZS] = gwy_recent_file_find_some_xyz(data);
        ids[GWY_PAGE_CURVE_MAPS] = gwy_recent_file_find_some_cmap(data);
        if (pageno == GWY_PAGE_NOPAGE || ids[pageno] == -1) {
            for (i = 0; i < G_N_ELEMENTS(pages_priority); i++) {
                if (ids[pages_priority[i]] != -1) {
                    pageno = pages_priority[i];
                    break;
                }
            }
        }

        if (rf->file_state == FILE_STATE_UNKNOWN)
            gwy_app_recent_file_try_load_thumbnail(rf);
    }

    if (pageno == GWY_PAGE_NOPAGE) {
        gwy_debug("There is no previewable data in the file, cannot make thumbnail.");
        return;
    }

    if (g_stat(rf->file_sys, &st) != 0) {
        g_warning("File <%s> was just loaded or saved, but it doesn't seem to exist any more: %s",
                  rf->file_utf8, g_strerror(errno));
        return;
    }

    if ((gulong)rf->file_mtime == (gulong)st.st_mtime)
        return;

    rf->image_width = rf->image_height = 0;
    rf->file_mtime = st.st_mtime;
    rf->file_size = st.st_size;
    g_free(rf->image_real_size);
    rf->image_real_size = NULL;

    if (pageno == GWY_PAGE_CHANNELS) {
        quark = gwy_app_get_data_key_for_id(ids[pageno]);
        dfield = GWY_DATA_FIELD(gwy_container_get_object(data, quark));
        g_return_if_fail(GWY_IS_DATA_FIELD(dfield));
        rf->image_width = gwy_data_field_get_xres(dfield);
        rf->image_height = gwy_data_field_get_yres(dfield);
        xreal = gwy_data_field_get_xreal(dfield);
        yreal = gwy_data_field_get_yreal(dfield);
        siunit = gwy_data_field_get_si_unit_xy(dfield);
        vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, sqrt(xreal*yreal), NULL);
        rf->image_real_size = g_strdup_printf("%.*f×%.*f%s%s",
                                              vf->precision, xreal/vf->magnitude,
                                              vf->precision, yreal/vf->magnitude,
                                              (vf->units && *vf->units) ? " " : "", vf->units);
        gwy_si_unit_value_format_free(vf);
    }
    else if (pageno == GWY_PAGE_GRAPHS) {
        quark = gwy_app_get_graph_key_for_id(ids[pageno]);
        gmodel = GWY_GRAPH_MODEL(gwy_container_get_object(data, quark));
        g_return_if_fail(GWY_IS_GRAPH_MODEL(gmodel));
        /* XXX: There is not much we can do with graphs. */
    }
    else if (pageno == GWY_PAGE_VOLUMES) {
        quark = gwy_app_get_brick_key_for_id(ids[pageno]);
        brick = GWY_BRICK(gwy_container_get_object(data, quark));
        g_return_if_fail(GWY_IS_BRICK(brick));
        rf->image_width = gwy_brick_get_xres(brick);
        rf->image_height = gwy_brick_get_yres(brick);
        xreal = gwy_brick_get_xreal(brick);
        yreal = gwy_brick_get_yreal(brick);
        zreal = gwy_brick_get_zreal(brick);
        siunit = gwy_brick_get_si_unit_x(brick);
        vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, sqrt(xreal*yreal), NULL);
        vf2 = gwy_brick_get_value_format_z(brick, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
        rf->image_real_size = g_strdup_printf("%.*f×%.*f%s%s × %.*f%s%s",
                                              vf->precision, xreal/vf->magnitude,
                                              vf->precision, yreal/vf->magnitude,
                                              (vf->units && *vf->units) ? " " : "",
                                              vf->units,
                                              vf2->precision, zreal/vf->magnitude,
                                              (vf2->units && *vf2->units) ? " " : "",
                                              vf2->units);
        gwy_si_unit_value_format_free(vf2);
        gwy_si_unit_value_format_free(vf);
    }
    else if (pageno == GWY_PAGE_XYZS) {
        quark = gwy_app_get_surface_key_for_id(ids[pageno]);
        surface = GWY_SURFACE(gwy_container_get_object(data, quark));
        g_return_if_fail(GWY_IS_SURFACE(surface));
        gwy_surface_get_xrange(surface, &xmin, &xreal);
        gwy_surface_get_yrange(surface, &ymin, &yreal);
        xreal -= xmin;
        yreal -= ymin;
        siunit = gwy_surface_get_si_unit_xy(surface);
        vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, sqrt(xreal*yreal), NULL);
        rf->image_real_size = g_strdup_printf("%.*f×%.*f%s%s",
                                              vf->precision, xreal/vf->magnitude,
                                              vf->precision, yreal/vf->magnitude,
                                              (vf->units && *vf->units) ? " " : "", vf->units);
        gwy_si_unit_value_format_free(vf);
    }
    else if (pageno == GWY_PAGE_CURVE_MAPS) {
        quark = gwy_app_get_lawn_key_for_id(ids[pageno]);
        lawn = GWY_LAWN(gwy_container_get_object(data, quark));
        g_return_if_fail(GWY_IS_LAWN(lawn));
        rf->image_width = gwy_lawn_get_xres(lawn);
        rf->image_height = gwy_lawn_get_yres(lawn);
        xreal = gwy_lawn_get_xreal(lawn);
        yreal = gwy_lawn_get_yreal(lawn);
        siunit = gwy_lawn_get_si_unit_xy(lawn);
        vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, sqrt(xreal*yreal), NULL);
        rf->image_real_size = g_strdup_printf("%.*f×%.*f%s%s",
                                              vf->precision, xreal/vf->magnitude,
                                              vf->precision, yreal/vf->magnitude,
                                              (vf->units && *vf->units) ? " " : "", vf->units);
        gwy_si_unit_value_format_free(vf);
    }
    else {
        g_return_if_reached();
    }

    rf->file_state = FILE_STATE_OK;

    if (g_str_has_prefix(rf->file_sys, gwy_recent_file_thumbnail_dir())) {
        gchar c;

        c = rf->file_sys[strlen(gwy_recent_file_thumbnail_dir())];
        if (!c || G_IS_DIR_SEPARATOR(c))
            return;
    }

    if (!pixbuf) {
        if (pageno == GWY_PAGE_CURVE_MAPS)
            pixbuf = gwy_app_get_curve_map_thumbnail(data, ids[pageno], TMS_NORMAL_THUMB_SIZE, TMS_NORMAL_THUMB_SIZE);
        if (pageno == GWY_PAGE_XYZS)
            pixbuf = gwy_app_get_xyz_thumbnail(data, ids[pageno], TMS_NORMAL_THUMB_SIZE, TMS_NORMAL_THUMB_SIZE);
        else if (pageno == GWY_PAGE_VOLUMES)
            pixbuf = gwy_app_get_volume_thumbnail(data, ids[pageno], TMS_NORMAL_THUMB_SIZE, TMS_NORMAL_THUMB_SIZE);
        else if (pageno == GWY_PAGE_GRAPHS) {
            /* This can return NULL if GUI is not running. */
            pixbuf = gwy_app_get_graph_thumbnail(data, ids[pageno], TMS_NORMAL_THUMB_SIZE, TMS_NORMAL_THUMB_SIZE);
        }
        else if (pageno == GWY_PAGE_CHANNELS)
            pixbuf = gwy_app_get_channel_thumbnail(data, ids[pageno], TMS_NORMAL_THUMB_SIZE, TMS_NORMAL_THUMB_SIZE);
    }

    option_keys = g_ptr_array_new();
    option_values = g_ptr_array_new();

    g_ptr_array_add(option_keys, (gpointer)KEY_SOFTWARE);
    g_ptr_array_add(option_values, (gpointer)PACKAGE_NAME);

    g_ptr_array_add(option_keys, (gpointer)KEY_THUMB_URI);
    g_ptr_array_add(option_values, rf->file_uri);

    g_snprintf(str_mtime, sizeof(str_mtime), "%lu", rf->file_mtime);
    g_ptr_array_add(option_keys, (gpointer)KEY_THUMB_MTIME);
    g_ptr_array_add(option_values, str_mtime);

    g_snprintf(str_size, sizeof(str_size), "%lu", rf->file_size);
    g_ptr_array_add(option_keys, (gpointer)KEY_THUMB_FILESIZE);
    g_ptr_array_add(option_values, str_size);

    if (rf->image_width) {
        g_snprintf(str_width, sizeof(str_width), "%d", rf->image_width);
        g_ptr_array_add(option_keys, (gpointer)KEY_THUMB_IMAGE_WIDTH);
        g_ptr_array_add(option_values, str_width);
    }

    if (rf->image_height) {
        g_snprintf(str_height, sizeof(str_height), "%d", rf->image_height);
        g_ptr_array_add(option_keys, (gpointer)KEY_THUMB_IMAGE_HEIGHT);
        g_ptr_array_add(option_values, str_height);
    }

    if (rf->image_real_size) {
        g_ptr_array_add(option_keys, (gpointer)KEY_THUMB_GWY_REAL_SIZE);
        g_ptr_array_add(option_values, rf->image_real_size);
    }

    g_ptr_array_add(option_keys, NULL);
    g_ptr_array_add(option_values, NULL);

    /* invent an unique temporary name for atomic save
     * FIXME: rough, but works on Win32 */
    fnm = g_strdup_printf("%s.%u", rf->thumb_sys, getpid());
    if (!pixbuf)
        rf->thumb_state = FILE_STATE_FAILED;
    else if (!gdk_pixbuf_savev(pixbuf, fnm, "png", (char**)option_keys->pdata, (char**)option_values->pdata, &err)) {
        g_clear_error(&err);
        rf->thumb_state = FILE_STATE_FAILED;
    }
#ifndef G_OS_WIN32
    chmod(fnm, 0600);
#endif
    g_unlink(rf->thumb_sys);
    if (g_rename(fnm, rf->thumb_sys) != 0) {
        g_unlink(fnm);
        rf->thumb_state = FILE_STATE_FAILED;
        rf->thumb_mtime = 0;
    }
    else {
        rf->thumb_state = FILE_STATE_UNKNOWN;  /* force reload */
        rf->thumb_mtime = rf->file_mtime;
    }
    g_free(fnm);

    GWY_OBJECT_UNREF(rf->pixbuf);
    GWY_OBJECT_UNREF(pixbuf);
    g_ptr_array_free(option_values, TRUE);
    g_ptr_array_free(option_keys, TRUE);
}

static gchar*
gwy_recent_file_thumbnail_name(const gchar *uri)
{
    static const gchar *hex2digit = "0123456789abcdef";
    guchar md5sum[16];
    gchar buffer[37], *p;
    gsize i;

    gwy_md5_get_digest(uri, -1, md5sum);
    p = buffer;
    for (i = 0; i < 16; i++) {
        *p++ = hex2digit[(guint)md5sum[i] >> 4];
        *p++ = hex2digit[(guint)md5sum[i] & 0x0f];
    }
    *p++ = '.';
    *p++ = 'p';
    *p++ = 'n';
    *p++ = 'g';
    *p = '\0';

    return g_build_filename(gwy_recent_file_thumbnail_dir(), "normal", buffer, NULL);
}

static const gchar*
gwy_recent_file_thumbnail_dir(void)
{
    const gchar *thumbdir =
#ifdef G_OS_WIN32
        "thumbnails";
#else
        ".thumbnails";
#endif
    static gchar *thumbnail_dir = NULL;

    if (thumbnail_dir)
        return thumbnail_dir;

    thumbnail_dir = g_build_filename(gwy_get_home_dir(), thumbdir, NULL);
    return thumbnail_dir;
}

/************************** Documentation ****************************/

/**
 * SECTION:filelist
 * @title: filelist
 * @short_description: Document history
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
