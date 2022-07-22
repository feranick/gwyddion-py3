/*
 *  $Id: gwyappfilechooser.c 24709 2022-03-21 17:31:45Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
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
#include <string.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/level.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwyapp.h>
#include "gwyappinternal.h"

/* Do not try to full-preview files larger than this.  100 MB is an arbitrary limit but most < 100 MB files seem to be
 * readable fairly quickly. */
#define MAX_FILE_SIZE_FOR_PREVIEW (96UL*1024UL*1024UL)

enum {
    BLOODY_ICON_VIEW_PADDING = 2*6,
    PADDED_THUMBNAIL_SIZE = TMS_NORMAL_THUMB_SIZE + BLOODY_ICON_VIEW_PADDING
};

enum {
    COLUMN_FILETYPE,
    COLUMN_LABEL
};

enum {
    COLUMN_FILEINFO,
    COLUMN_PIXBUF
};

typedef struct {
    GSList *list;
    GwyFileOperationType fileop;
    gboolean only_nondetectable;
} TypeListData;

static void     gwy_app_file_chooser_finalize       (GObject *object);
static void     gwy_app_file_chooser_destroy        (GtkObject *object);
static void     gwy_app_file_chooser_hide           (GtkWidget *widget);
static void     gwy_app_file_chooser_save_position  (GwyAppFileChooser *chooser);
static void     gwy_app_file_chooser_add_type       (const gchar *name,
                                                     TypeListData *data);
static gint     gwy_app_file_chooser_type_compare   (gconstpointer a,
                                                     gconstpointer b);
static void     gwy_app_file_chooser_add_types      (GtkListStore *store,
                                                     GwyFileOperationType fileop,
                                                     gboolean only_nondetectable);
static void     gwy_app_file_chooser_add_type_list  (GwyAppFileChooser *chooser);
static void     gwy_app_file_chooser_update_expander(GwyAppFileChooser *chooser);
static void     gwy_app_file_chooser_type_changed   (GwyAppFileChooser *chooser,
                                                     GtkTreeSelection *selection);
static void     loadable_filter_toggled             (GwyAppFileChooser *chooser,
                                                     GtkToggleButton *check);
static void     enforce_refilter                    (GwyAppFileChooser *chooser);
static void     gwy_app_file_chooser_expanded       (GwyAppFileChooser *chooser,
                                                     GParamSpec *pspec,
                                                     GtkExpander *expander);
static void     construct_loadable_filter           (GwyAppFileChooser *chooser,
                                                     GtkBox *vbox);
static void     construct_glob_filter               (GwyAppFileChooser *chooser,
                                                     GtkBox *vbox);
static void     glob_entry_clear                    (GtkWidget *button,
                                                     GwyAppFileChooser *chooser);
static void     glob_entry_updated                  (GtkEntry *entry,
                                                     GwyAppFileChooser *chooser);
static void     glob_case_changed                   (GtkToggleButton *check,
                                                     GwyAppFileChooser *chooser);
static gboolean gwy_app_file_chooser_open_filter    (const GtkFileFilterInfo *filter_info,
                                                     gpointer user_data);
static void     gwy_app_file_chooser_add_preview    (GwyAppFileChooser *chooser);
static void     plane_level_changed                 (GwyAppFileChooser *chooser,
                                                     GtkToggleButton *button);
static void     row_level_changed                   (GwyAppFileChooser *chooser,
                                                     GtkToggleButton *button);
static void     gwy_app_file_chooser_update_preview (GwyAppFileChooser *chooser);
static gboolean gwy_app_file_chooser_do_full_preview(gpointer user_data);
static void     modify_channel_for_preview          (GwyContainer *data,
                                                     gint id,
                                                     gboolean plane_level,
                                                     gboolean row_level);
static void     gwy_app_file_chooser_free_preview   (GwyAppFileChooser *chooser);
static void     ensure_gtk_recently_used            (void);

/* Enforce a get-type function that beings with underscore. */
G_DEFINE_TYPE(GwyAppFileChooser, _gwy_app_file_chooser,
              GTK_TYPE_FILE_CHOOSER_DIALOG)

static GtkWidget *instance_open = NULL;
static GtkWidget *instance_save = NULL;

static void
_gwy_app_file_chooser_class_init(GwyAppFileChooserClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GtkObjectClass *object_class = GTK_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gobject_class->finalize = gwy_app_file_chooser_finalize;

    object_class->destroy = gwy_app_file_chooser_destroy;

    widget_class->hide = gwy_app_file_chooser_hide;
}

static void
_gwy_app_file_chooser_init(G_GNUC_UNUSED GwyAppFileChooser *chooser)
{
}

static void
gwy_app_file_chooser_finalize(GObject *object)
{
    GwyAppFileChooser *chooser = GWY_APP_FILE_CHOOSER(object);

    GWY_OBJECT_UNREF(chooser->filter);
    GWY_OBJECT_UNREF(chooser->no_filter);
    if (chooser->pattern)
        g_pattern_spec_free(chooser->pattern);
    if (chooser->glob)
        g_string_free(chooser->glob, TRUE);

    G_OBJECT_CLASS(_gwy_app_file_chooser_parent_class)->finalize(object);
}

static void
gwy_app_file_chooser_destroy(GtkObject *object)
{
    GwyAppFileChooser *chooser = GWY_APP_FILE_CHOOSER(object);

    gwy_app_file_chooser_free_preview(chooser);

    GTK_OBJECT_CLASS(_gwy_app_file_chooser_parent_class)->destroy(object);
}

static void
gwy_app_file_chooser_hide(GtkWidget *widget)
{
    GwyAppFileChooser *chooser = GWY_APP_FILE_CHOOSER(widget);

    gwy_app_file_chooser_free_preview(chooser);

    GTK_WIDGET_CLASS(_gwy_app_file_chooser_parent_class)->hide(widget);
}

static void
gwy_app_file_chooser_add_type(const gchar *name,
                              TypeListData *data)
{
    if (!(gwy_file_func_get_operations(name) & data->fileop))
        return;

    if (data->only_nondetectable && gwy_file_func_get_is_detectable(name))
        return;

    data->list = g_slist_prepend(data->list, (gpointer)name);
}

static gint
gwy_app_file_chooser_type_compare(gconstpointer a,
                                  gconstpointer b)
{
    return g_utf8_collate(_(gwy_file_func_get_description((const gchar*)a)),
                          _(gwy_file_func_get_description((const gchar*)b)));
}

static void
gwy_app_file_chooser_add_types(GtkListStore *store,
                               GwyFileOperationType fileop,
                               gboolean only_nondetectable)
{
    TypeListData tldata;
    GtkTreeIter iter;
    GSList *l;

    tldata.list = NULL;
    tldata.fileop = fileop;
    tldata.only_nondetectable = only_nondetectable;
    gwy_file_func_foreach((GFunc)gwy_app_file_chooser_add_type, &tldata);
    tldata.list = g_slist_sort(tldata.list, gwy_app_file_chooser_type_compare);

    for (l = tldata.list; l; l = g_slist_next(l)) {
        gtk_list_store_insert_with_values(store, &iter, G_MAXINT,
                                          COLUMN_FILETYPE, l->data,
                                          COLUMN_LABEL, gettext(gwy_file_func_get_description(l->data)),
                                          -1);
    }

    g_slist_free(tldata.list);
}

GtkWidget*
_gwy_app_file_chooser_get(GtkFileChooserAction action)
{
    GtkDialog *dialog;
    GwyAppFileChooser *chooser;
    GtkWidget **instance;
    const gchar *title;

    switch (action) {
        case GTK_FILE_CHOOSER_ACTION_OPEN:
        instance = &instance_open;
        title = _("Open File");
        break;

        case GTK_FILE_CHOOSER_ACTION_SAVE:
        instance = &instance_save;
        title = _("Save File");
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }

    if (*instance)
        return *instance;

    ensure_gtk_recently_used();

    chooser = g_object_new(GWY_TYPE_APP_FILE_CHOOSER, "title", title, "action", action, NULL);
    *instance = GTK_WIDGET(chooser);
    dialog = GTK_DIALOG(chooser);
    g_object_add_weak_pointer(G_OBJECT(chooser), (gpointer*)instance);

    gtk_dialog_add_button(dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    switch (action) {
        case GTK_FILE_CHOOSER_ACTION_OPEN:
        chooser->prefix = "/app/file/load";
        gtk_dialog_add_button(dialog, GTK_STOCK_OPEN, GTK_RESPONSE_OK);
        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), TRUE);
        break;

        case GTK_FILE_CHOOSER_ACTION_SAVE:
        chooser->prefix = "/app/file/save";
        gtk_dialog_add_button(dialog, GTK_STOCK_SAVE, GTK_RESPONSE_OK);
        break;

        default:
        g_assert_not_reached();
        break;
    }

    gwy_help_add_to_window(GTK_WINDOW(dialog), "managing-files", NULL, GWY_HELP_DEFAULT);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog), TRUE);

    gwy_app_file_chooser_add_type_list(chooser);
    gwy_app_file_chooser_add_preview(chooser);

    g_signal_connect(chooser, "response", G_CALLBACK(gwy_app_file_chooser_save_position), NULL);
    gwy_app_restore_window_position(GTK_WINDOW(chooser), chooser->prefix, TRUE);

    /* Does not filter when initially shown without this. */
    if (action == GTK_FILE_CHOOSER_ACTION_OPEN)
        enforce_refilter(chooser);

    return *instance;
}

static void
gwy_app_file_chooser_save_position(GwyAppFileChooser *chooser)
{
    gwy_app_save_window_position(GTK_WINDOW(chooser), chooser->prefix, FALSE, TRUE);
}

/**
 * gwy_app_file_chooser_select_type:
 * @selector: File type selection widget.
 *
 * Selects the same file type as the last time.
 *
 * If no information about last selection is available or the type is not
 * present any more, the list item is selected.
 **/
static void
gwy_app_file_chooser_select_type(GwyAppFileChooser *chooser)
{
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreePath *path;
    GtkTreeIter iter, first;
    const guchar *name;
    gboolean ok;
    gchar *s;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(chooser->type_list));
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(chooser->type_list));

    if (!gtk_tree_model_get_iter_first(model, &first))
        return;

    ok = gwy_container_gis_string(gwy_app_settings_get(), chooser->type_key, &name);
    if (!ok) {
        gtk_tree_selection_select_iter(selection, &first);
        return;
    }

    iter = first;
    do {
        gtk_tree_model_get(model, &iter, COLUMN_FILETYPE, &s, -1);
        ok = gwy_strequal(name, s);
        g_free(s);
        if (ok) {
            gtk_tree_selection_select_iter(selection, &iter);
            path = gtk_tree_model_get_path(model, &iter);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(chooser->type_list), path, NULL, TRUE, 0.5, 0.0);
            gtk_tree_path_free(path);
            return;
        }
    } while (gtk_tree_model_iter_next(model, &iter));

    gtk_tree_selection_select_iter(selection, &first);
}

gchar*
_gwy_app_file_chooser_get_selected_type(GwyAppFileChooser *chooser)
{
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    gchar *s;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(chooser->type_list));

    if (!(gtk_tree_selection_get_selected(selection, &model, &iter)))
        return NULL;
    gtk_tree_model_get(model, &iter, COLUMN_FILETYPE, &s, -1);
    if (!*s) {
        g_free(s);
        gwy_container_remove(gwy_app_settings_get(), chooser->type_key);
        s = NULL;
    }
    else
        gwy_container_set_string(gwy_app_settings_get(), chooser->type_key, g_strdup(s));

    return s;
}

static void
gwy_app_file_chooser_update_expander(GwyAppFileChooser *chooser)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *name;
    GString *label;
    GtkFileChooserAction action;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(chooser->type_list));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        name = g_strdup("???");
    else
        gtk_tree_model_get(model, &iter, COLUMN_LABEL, &name, -1);

    label = g_string_new(NULL);
    g_string_printf(label, _("File _type: %s"), name);
    g_free(name);

    g_object_get(chooser, "action", &action, NULL);
    if (action == GTK_FILE_CHOOSER_ACTION_OPEN) {
        if (chooser->only_loadable) {
            g_string_append(label, ", ");
            g_string_append(label, _("Only loadable shown"));
        }
        if (chooser->glob->len) {
            g_string_append(label, ", ");
            g_string_append_printf(label, _("Filter: %s"), chooser->glob->str);
        }
    }

    gtk_expander_set_label(GTK_EXPANDER(chooser->expander), label->str);
    g_string_free(label, TRUE);
}

static void
gwy_app_file_chooser_type_changed(GwyAppFileChooser *chooser,
                                  GtkTreeSelection *selection)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;
    g_free(chooser->filetype);
    gtk_tree_model_get(model, &iter, COLUMN_FILETYPE, &chooser->filetype, -1);

    gwy_app_file_chooser_update_expander(chooser);
}

static void
gwy_app_file_chooser_expanded(GwyAppFileChooser *chooser,
                              G_GNUC_UNUSED GParamSpec *pspec,
                              GtkExpander *expander)
{
    gchar *key;

    key = g_strconcat(chooser->prefix, "/expanded", NULL);
    gwy_container_set_boolean_by_name(gwy_app_settings_get(), key, gtk_expander_get_expanded(expander));
    g_free(key);
}

static void
gwy_app_file_chooser_add_type_list(GwyAppFileChooser *chooser)
{
    GtkWidget *vbox, *scwin;
    GtkTreeView *treeview;
    GtkFileChooserAction action;
    GtkRequisition req;
    GtkTreeSelection *selection;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkListStore *store;
    GtkTreeIter iter;
    gboolean expanded = FALSE;
    gchar *key;
    gint extraheight;

    g_object_get(chooser, "action", &action, NULL);

    key = g_strconcat(chooser->prefix, "/type", NULL);
    chooser->type_key = g_quark_from_string(key);
    g_free(key);

    store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    gtk_list_store_append(store, &iter);
    switch (action) {
        case GTK_FILE_CHOOSER_ACTION_SAVE:
        gtk_list_store_set(store, &iter,
                           COLUMN_FILETYPE, "",
                           COLUMN_LABEL, _("Automatic by extension"),
                           -1);
        gwy_app_file_chooser_add_types(store, GWY_FILE_OPERATION_SAVE, FALSE);
        gwy_app_file_chooser_add_types(store, GWY_FILE_OPERATION_EXPORT, FALSE);
        break;

        case GTK_FILE_CHOOSER_ACTION_OPEN:
        gtk_list_store_set(store, &iter,
                           COLUMN_FILETYPE, "",
                           COLUMN_LABEL, _("Automatically detected"),
                           -1);
        gwy_app_file_chooser_add_types(store, GWY_FILE_OPERATION_LOAD, TRUE);
        break;

        default:
        g_assert_not_reached();
        break;
    }

    chooser->expander = gtk_expander_new(NULL);
    gtk_expander_set_use_underline(GTK_EXPANDER(chooser->expander), TRUE);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(chooser), chooser->expander);
    key = g_strconcat(chooser->prefix, "/expanded", NULL);
    gwy_container_gis_boolean_by_name(gwy_app_settings_get(), key, &expanded);
    g_free(key);
    gtk_expander_set_expanded(GTK_EXPANDER(chooser->expander), expanded);
    g_signal_connect_swapped(chooser->expander, "notify::expanded",
                             G_CALLBACK(gwy_app_file_chooser_expanded), chooser);

    vbox = gtk_vbox_new(FALSE, 4);
    gtk_container_add(GTK_CONTAINER(chooser->expander), vbox);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    chooser->type_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    treeview = GTK_TREE_VIEW(chooser->type_list);
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(treeview, FALSE);
    gtk_container_add(GTK_CONTAINER(scwin), chooser->type_list);

    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(treeview, column);
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(column), renderer, "text", COLUMN_LABEL);

    selection = gtk_tree_view_get_selection(treeview);
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    g_signal_connect_swapped(selection, "changed", G_CALLBACK(gwy_app_file_chooser_type_changed), chooser);

    if (action == GTK_FILE_CHOOSER_ACTION_OPEN) {
        /* Gtk+ file chooser filter model is completely antagonistic to any parametric filter mechanism.  Also we
         * cannot set the filter to NULL because it assumes we always choose a filter from some list.  So (1) we need
         * an explicit no_filter object that does nothing (2) we cannot combine filters, we need a single
         * monster-filter function that does everything (3) despite this, there is no ‘refilter’ function, so we must
         * set filter to no_filter and then back to filter to refilter. */
        chooser->filter = gtk_file_filter_new();
        g_object_ref_sink(chooser->filter);

        chooser->no_filter = gtk_file_filter_new();
        gtk_file_filter_add_pattern(chooser->no_filter, "*");
        g_object_ref_sink(chooser->no_filter);

        construct_glob_filter(chooser, GTK_BOX(vbox));
        construct_loadable_filter(chooser, GTK_BOX(vbox));

        gtk_file_filter_add_custom(chooser->filter, GTK_FILE_FILTER_FILENAME,
                                   gwy_app_file_chooser_open_filter, chooser, NULL);
    }

    /* Give it some reasonable size. FIXME: hack. */
    gtk_widget_show_all(vbox);
    gtk_widget_size_request(scwin, &req);
    extraheight = 40;
    if (action == GTK_FILE_CHOOSER_ACTION_SAVE)
        extraheight = 5*extraheight/3;
    gtk_widget_set_size_request(scwin, -1, req.height + extraheight);

    /* Ignore the file from settings (i.e. between sessions).  Preserving it can be useful when importing lots of raw
     * data, but it confuses people no end when they suddently cannot open files because some kind of raw data import
     * is selected. */
    if (action != GTK_FILE_CHOOSER_ACTION_OPEN)
        gwy_app_file_chooser_select_type(chooser);
    gwy_app_file_chooser_type_changed(chooser, selection);
}

/***** Filters *************************************************************/

static void
construct_loadable_filter(GwyAppFileChooser *chooser, GtkBox *vbox)
{
    GwyContainer *settings = gwy_app_settings_get();
    GtkToggleButton *toggle;
    gchar *key;

    chooser->only_loadable = FALSE;
    chooser->loadable_check = gtk_check_button_new_with_mnemonic(_("Show only loadable files"));
    toggle = GTK_TOGGLE_BUTTON(chooser->loadable_check);
    key = g_strconcat(chooser->prefix, "/filter", NULL);
    gwy_container_gis_boolean_by_name(settings, key, &chooser->only_loadable);
    g_free(key);
    gtk_toggle_button_set_active(toggle, chooser->only_loadable);
    gtk_box_pack_start(vbox, chooser->loadable_check, FALSE, FALSE, 0);
    g_signal_connect_swapped(chooser->loadable_check, "toggled", G_CALLBACK(loadable_filter_toggled), chooser);
}

static void
construct_glob_filter(GwyAppFileChooser *chooser, GtkBox *vbox)
{
    GwyContainer *settings = gwy_app_settings_get();
    GtkWidget *hbox, *label, *entry, *check, *button, *image;
    const guchar *glob;
    gchar *key;

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(vbox, hbox, FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic(_("_Filter:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 4);

    if (!chooser->glob)
        chooser->glob = g_string_new(NULL);

    key = g_strconcat(chooser->prefix, "/glob/pattern", NULL);
    if (gwy_container_gis_string_by_name(settings, key, &glob))
        g_string_assign(chooser->glob, glob);
    g_free(key);

    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), chooser->glob->str);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    chooser->glob_entry = entry;
    g_signal_connect(entry, "activate", G_CALLBACK(glob_entry_updated), chooser);

    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(glob_entry_clear), chooser);

    image = gtk_image_new_from_stock(GTK_STOCK_CLEAR, GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_button_set_image(GTK_BUTTON(button), image);

#ifdef G_OS_WIN32
    chooser->glob_casesens = FALSE;
#else
    chooser->glob_casesens = TRUE;
#endif
    key = g_strconcat(chooser->prefix, "/glob/case-sensitive", NULL);
    gwy_container_gis_boolean_by_name(settings, key, &chooser->glob_casesens);

    check = gtk_check_button_new_with_mnemonic(_("Case _sensitive"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), chooser->glob_casesens);
    gtk_box_pack_start(GTK_BOX(hbox), check, FALSE, FALSE, 4);
    g_signal_connect(check, "toggled", G_CALLBACK(glob_case_changed), chooser);
    chooser->glob_case_check = check;

    if (chooser->glob->len)
        glob_entry_updated(GTK_ENTRY(entry), chooser);
}

static void
glob_entry_clear(G_GNUC_UNUSED GtkWidget *button, GwyAppFileChooser *chooser)
{
    gtk_entry_set_text(GTK_ENTRY(chooser->glob_entry), "");
    gtk_widget_activate(chooser->glob_entry);
}

static void
glob_entry_updated(GtkEntry *entry, GwyAppFileChooser *chooser)
{
    GwyContainer *settings = gwy_app_settings_get();
    GPatternSpec *oldpattern;
    gchar *key, *s, *t;

    g_string_assign(chooser->glob, gtk_entry_get_text(entry));
    key = g_strconcat(chooser->prefix, "/glob/pattern", NULL);
    gwy_container_set_const_string_by_name(settings, key, chooser->glob->str);
    g_free(key);

    oldpattern = chooser->pattern;

    if (chooser->glob_casesens) {
        if (!strchr(chooser->glob->str, '*') && !strchr(chooser->glob->str, '?'))
            s = g_strconcat("*", chooser->glob->str, "*", NULL);
        else
            s = g_strdup(chooser->glob->str);
    }
    else {
        /* FIXME: This is crude. */
        s = g_utf8_strdown(chooser->glob->str, chooser->glob->len);
        if (!strchr(s, '*') && !strchr(s, '?')) {
            t = s;
            s = g_strconcat("*", t, "*", NULL);
            g_free(t);
        }
    }
    chooser->pattern = g_pattern_spec_new(s);
    g_free(s);

    if (oldpattern)
        g_pattern_spec_free(oldpattern);

    gwy_app_file_chooser_update_expander(chooser);
    enforce_refilter(chooser);
}

static void
glob_case_changed(GtkToggleButton *check, GwyAppFileChooser *chooser)
{
    GwyContainer *settings = gwy_app_settings_get();
    gchar *key;

    chooser->glob_casesens = gtk_toggle_button_get_active(check);
    key = g_strconcat(chooser->prefix, "/glob/case-sensitive", NULL);
    gwy_container_set_boolean_by_name(settings, key, chooser->glob_casesens);
    g_free(key);

    gwy_app_file_chooser_update_expander(chooser);
    enforce_refilter(chooser);
}

static gboolean
gwy_app_file_chooser_open_filter(const GtkFileFilterInfo *filter_info,
                                 gpointer user_data)
{
    GwyAppFileChooser *chooser = (GwyAppFileChooser*)user_data;
    const gchar *filename = filter_info->filename;
    gboolean ok = TRUE;

    if (ok && chooser->glob->len) {
        gchar *basename = g_path_get_basename(filename);
        gchar *filename_utf8 = g_filename_to_utf8(basename, -1, NULL, NULL, NULL);

        g_free(basename);
        if (filename_utf8) {
            if (chooser->glob_casesens)
                ok = g_pattern_match_string(chooser->pattern, filename_utf8);
            else {
                gchar *filename_lc = g_utf8_strdown(filename_utf8, -1);
                ok = g_pattern_match_string(chooser->pattern, filename_lc);
                g_free(filename_lc);
            }
            g_free(filename_utf8);
        }
    }

    if (ok && chooser->only_loadable) {
        const gchar *name;
        gint score = 0;

        name = gwy_file_detect_with_score(filter_info->filename, FALSE, GWY_FILE_OPERATION_LOAD, &score);
        /* To filter out `fallback' importers like rawfile */
        ok = (name != NULL && score >= 5);
    }

    return ok;
}

static void
loadable_filter_toggled(GwyAppFileChooser *chooser,
                        GtkToggleButton *check)
{
    GwyContainer *settings = gwy_app_settings_get();
    gboolean active;
    gchar *key;

    active = gtk_toggle_button_get_active(check);
    key = g_strconcat(chooser->prefix, "/filter", NULL);
    gwy_container_set_boolean_by_name(settings, key, active);
    g_free(key);
    chooser->only_loadable = active;

    gwy_app_file_chooser_update_expander(chooser);
    enforce_refilter(chooser);
}

static void
enforce_refilter(GwyAppFileChooser *chooser)
{
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser), chooser->no_filter);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser), chooser->filter);
}

/***** Preview *************************************************************/

static void
gwy_app_file_chooser_add_preview(GwyAppFileChooser *chooser)
{
    GwyContainer *settings;
    GtkListStore *store;
    GtkIconView *preview;
    GtkCellLayout *layout;
    GtkCellRenderer *renderer;
    GtkWidget *scwin, *vbox, *button, *toolbar;
    gboolean setting;
    gint w;

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    store = gtk_list_store_new(2, G_TYPE_STRING, GDK_TYPE_PIXBUF);
    chooser->preview = gtk_icon_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    preview = GTK_ICON_VIEW(chooser->preview);
    layout = GTK_CELL_LAYOUT(preview);
    gtk_icon_view_set_columns(preview, 1);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(layout, renderer, FALSE);
    gtk_cell_layout_add_attribute(layout, renderer, "pixbuf", COLUMN_PIXBUF);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
                 "wrap-mode", PANGO_WRAP_WORD_CHAR,
                 "ellipsize", PANGO_ELLIPSIZE_END,
                 "ellipsize-set", TRUE,
                 NULL);
    gtk_cell_layout_pack_start(layout, renderer, FALSE);
    gtk_cell_layout_add_attribute(layout, renderer, "markup", COLUMN_FILEINFO);
    chooser->renderer_fileinfo = G_OBJECT(renderer);

    gtk_icon_view_set_selection_mode(preview, GTK_SELECTION_NONE);
    /* In Gtk+ 2.14 and older, things work.  2.16 adds some padding that breaks everything.  And this padding together
     * with the usual margin means too much white space so we have to get rid of the margin in 2.16+.
     */
    if (gtk_major_version == 2 && gtk_minor_version <= 14) {
        gtk_icon_view_set_item_width(preview, TMS_NORMAL_THUMB_SIZE);
        w = TMS_NORMAL_THUMB_SIZE + 2*gtk_icon_view_get_margin(preview);
    }
    else {
        gtk_icon_view_set_margin(preview, 0);
        gtk_icon_view_set_item_width(preview, PADDED_THUMBNAIL_SIZE);
        w = PADDED_THUMBNAIL_SIZE;
    }
    gtk_widget_set_size_request(chooser->preview, w, -1);
    gtk_container_add(GTK_CONTAINER(scwin), chooser->preview);

    vbox = gtk_vbox_new(FALSE, 2);

    chooser->preview_filename = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(chooser->preview_filename), 0.0, 0.5);
    gtk_label_set_single_line_mode(GTK_LABEL(chooser->preview_filename), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(chooser->preview_filename), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(vbox), chooser->preview_filename, FALSE, FALSE, 0);

    chooser->preview_type = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(chooser->preview_type), 0.0, 0.5);
    gtk_label_set_single_line_mode(GTK_LABEL(chooser->preview_type), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(chooser->preview_type), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(vbox), chooser->preview_type, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    toolbar = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    settings = gwy_app_settings_get();

    setting = FALSE;
    gwy_container_gis_boolean_by_name(settings, "/app/file/preview/plane-level",
                                      &setting);
    button = gtk_toggle_button_new();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), setting);
    GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(button, _("Plane-level previewed data"));
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_stock(GWY_STOCK_LEVEL, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "toggled", G_CALLBACK(plane_level_changed), chooser);

    setting = FALSE;
    gwy_container_gis_boolean_by_name(settings, "/app/file/preview/row-level", &setting);
    button = gtk_toggle_button_new();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), setting);
    GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(button, _("Row-level previewed data"));
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_stock(GWY_STOCK_LINE_LEVEL, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "toggled", G_CALLBACK(row_level_changed), chooser);

    gtk_widget_show_all(vbox);

    gtk_file_chooser_set_preview_widget(GTK_FILE_CHOOSER(chooser), vbox);
    gtk_file_chooser_set_use_preview_label(GTK_FILE_CHOOSER(chooser), FALSE);
    g_signal_connect(chooser, "update-preview", G_CALLBACK(gwy_app_file_chooser_update_preview), NULL);

    toolbar = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
}

static void
plane_level_changed(GwyAppFileChooser *chooser,
                    GtkToggleButton *button)
{
    GwyContainer *settings = gwy_app_settings_get();
    gwy_container_set_boolean_by_name(settings, "/app/file/preview/plane-level", gtk_toggle_button_get_active(button));
    if (chooser->full_preview_id) {
        g_source_remove(chooser->full_preview_id);
        chooser->full_preview_id = 0;
    }
    gwy_app_file_chooser_do_full_preview(chooser);
}

static void
row_level_changed(GwyAppFileChooser *chooser,
                  GtkToggleButton *button)
{
    GwyContainer *settings = gwy_app_settings_get();
    gwy_container_set_boolean_by_name(settings, "/app/file/preview/row-level", gtk_toggle_button_get_active(button));
    if (chooser->full_preview_id) {
        g_source_remove(chooser->full_preview_id);
        chooser->full_preview_id = 0;
    }
    gwy_app_file_chooser_do_full_preview(chooser);
}

static void
gwy_app_file_chooser_update_preview(GwyAppFileChooser *chooser)
{
    GtkFileChooser *fchooser;
    GtkTreeModel *model;
    GdkPixbuf *pixbuf;
    GtkTreeIter iter;
    gchar *filename_sys, *basename_sys, *filename_utf8;
    gboolean file_too_large = TRUE;
    GStatBuf st;

    gwy_app_file_chooser_free_preview(chooser);

    model = gtk_icon_view_get_model(GTK_ICON_VIEW(chooser->preview));
    gtk_list_store_clear(GTK_LIST_STORE(model));

    fchooser = GTK_FILE_CHOOSER(chooser);
    filename_sys = gtk_file_chooser_get_preview_filename(fchooser);
    /* It should be UTF-8, but don't convert it just for gwy_debug() */
    gwy_debug("%s", filename_sys);

    /* Never set the preview inactive.  Gtk+ can do all kinds of silly things if you do. */
    if (!filename_sys) {
        gtk_label_set_text(GTK_LABEL(chooser->preview_filename), "");
        gtk_label_set_text(GTK_LABEL(chooser->preview_type), "");
        return;
    }

    /* Preview file name */
    basename_sys = g_path_get_basename(filename_sys);
    filename_utf8 = g_filename_to_utf8(basename_sys, -1, NULL, NULL, NULL);
    g_free(basename_sys);
    if (!filename_utf8)
        filename_utf8 = g_strdup("???");
    gtk_label_set_text(GTK_LABEL(chooser->preview_filename), filename_utf8);
    g_free(filename_utf8);

    /* Let directories fail gracefully */
    if (g_file_test(filename_sys, G_FILE_TEST_IS_DIR)) {
        gtk_label_set_markup(GTK_LABEL(chooser->preview_type), "<small>directory</small>");
        g_free(filename_sys);
        return;
    }
    gtk_label_set_text(GTK_LABEL(chooser->preview_type), "");

    if (g_stat(filename_sys, &st) == 0)
        file_too_large = (st.st_size > MAX_FILE_SIZE_FOR_PREVIEW);

    pixbuf = _gwy_app_recent_file_try_thumbnail(filename_sys);
    g_free(filename_sys);

    if (!pixbuf) {
        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
        gdk_pixbuf_fill(pixbuf, 0x00000000);
        chooser->make_thumbnail = TRUE;
    }
    else
        chooser->make_thumbnail = FALSE;

    g_object_set(chooser->renderer_fileinfo,
                 "ellipsize", PANGO_ELLIPSIZE_NONE,
                 "wrap-width", TMS_NORMAL_THUMB_SIZE,
                 NULL);
    gtk_list_store_insert_with_values(GTK_LIST_STORE(model), &iter, -1,
                                      COLUMN_PIXBUF, pixbuf,
                                      COLUMN_FILEINFO, (file_too_large ? _("File too large for preview") : _("…")),
                                      -1);
    g_object_unref(pixbuf);

    if (!file_too_large) {
        chooser->full_preview_id = g_timeout_add_full(G_PRIORITY_LOW, 250,
                                                      gwy_app_file_chooser_do_full_preview, chooser, NULL);
    }
}

static guint
count_ids(const gint *ids)
{
    guint n = 0;

    while (ids[n] != -1)
        n++;

    return n;
}

/* NB: Consumes the pixbuf. */
static void
insert_thumbnail_row(GwyAppFileChooser *chooser,
                     GwyContainer *data, GwyAppPage pageno, gint id,
                     GdkPixbuf *pixbuf, const gchar *description)
{
    GtkTreeModel *model;
    GtkListStore *store;
    GtkTreeIter iter;

    model = gtk_icon_view_get_model(GTK_ICON_VIEW(chooser->preview));
    store = GTK_LIST_STORE(model);

    if (chooser->make_thumbnail) {
        _gwy_app_recent_file_write_thumbnail(chooser->preview_name_sys, data, pageno, id, pixbuf);
        chooser->make_thumbnail = FALSE;
    }

    gtk_list_store_insert_with_values(store, &iter, -1,
                                      COLUMN_PIXBUF, pixbuf,
                                      COLUMN_FILEINFO, description,
                                      -1);
    g_object_unref(pixbuf);
}

static void
describe_channel(GwyContainer *container, gint id, GString *str)
{
    GwyDataField *dfield;
    GwySIUnit *siunit;
    GwySIValueFormat *vf = NULL;
    GQuark quark;
    gint xres, yres;
    gdouble xreal, yreal;
    gchar *s;

    g_string_truncate(str, 0);

    quark = gwy_app_get_data_key_for_id(id);
    dfield = GWY_DATA_FIELD(gwy_container_get_object(container, quark));
    g_return_if_fail(GWY_IS_DATA_FIELD(dfield));

    s = gwy_app_get_data_field_title(container, id);
    g_string_append(str, s);
    g_free(s);

    siunit = gwy_data_field_get_si_unit_z(dfield);
    s = gwy_si_unit_get_string(siunit, GWY_SI_UNIT_FORMAT_MARKUP);
    g_string_append_printf(str, " [%s]\n", s);
    g_free(s);

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    g_string_append_printf(str, "%d×%d %s\n", xres, yres, _("px"));

    xreal = gwy_data_field_get_xreal(dfield);
    yreal = gwy_data_field_get_yreal(dfield);
    siunit = gwy_data_field_get_si_unit_xy(dfield);
    vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, sqrt(xreal*yreal), vf);
    g_string_append_printf(str, "%.*f×%.*f%s%s",
                           vf->precision, xreal/vf->magnitude,
                           vf->precision, yreal/vf->magnitude,
                           (vf->units && *vf->units) ? " " : "", vf->units);
    gwy_si_unit_value_format_free(vf);
}

static void
add_channel_thumbnails(GwyAppFileChooser *chooser,
                       GwyContainer *data, gint *ids,
                       GString *str)
{
    gboolean row_level = FALSE, plane_level = FALSE;
    GwyContainer *settings;
    GdkPixbuf *pixbuf;
    guint i;
    gint id;

    settings = gwy_app_settings_get();
    gwy_container_gis_boolean_by_name(settings, "/app/file/preview/plane-level", &plane_level);
    gwy_container_gis_boolean_by_name(settings, "/app/file/preview/row-level", &row_level);

    for (i = 0; ids[i] != -1; i++) {
        id = ids[i];
        modify_channel_for_preview(data, id, plane_level, row_level);
        pixbuf = gwy_app_get_channel_thumbnail(data, id, TMS_NORMAL_THUMB_SIZE, TMS_NORMAL_THUMB_SIZE);
        if (!pixbuf) {
            g_warning("Cannot make a pixbuf of channel %d", id);
            continue;
        }
        describe_channel(data, id, str);
        insert_thumbnail_row(chooser, data, GWY_PAGE_CHANNELS, id, pixbuf, str->str);
    }
}

static void
describe_graph(GwyContainer *container, gint id, GString *str)
{
    GwyGraphModel *gmodel;
    GwySIUnit *xunit, *yunit;
    GQuark quark;
    gint n;
    gchar *s;

    g_string_truncate(str, 0);

    quark = gwy_app_get_graph_key_for_id(id);
    gmodel = GWY_GRAPH_MODEL(gwy_container_get_object(container, quark));
    g_return_if_fail(GWY_IS_GRAPH_MODEL(gmodel));

    g_object_get(gmodel,
                 "n-curves", &n,
                 "title", &s,
                 "si-unit-x", &xunit,
                 "si-unit-y", &yunit,
                 NULL);
    g_string_append_printf(str, "%s (%d)\n", s, n);
    g_free(s);

    s = gwy_si_unit_get_string(xunit, GWY_SI_UNIT_FORMAT_MARKUP);
    g_string_append_printf(str, "[%s] ", s);
    g_object_unref(xunit);
    g_free(s);

    s = gwy_si_unit_get_string(yunit, GWY_SI_UNIT_FORMAT_MARKUP);
    g_string_append_printf(str, "[%s]\n", s);
    g_object_unref(yunit);
    g_free(s);
}

static void
add_graph_thumbnails(GwyAppFileChooser *chooser,
                     GwyContainer *data, gint *ids,
                     GString *str)
{
    GdkPixbuf *pixbuf;
    guint i;
    gint id;

    for (i = 0; ids[i] != -1; i++) {
        id = ids[i];
        pixbuf = gwy_app_get_graph_thumbnail(data, id, TMS_NORMAL_THUMB_SIZE, 3*TMS_NORMAL_THUMB_SIZE/4);
        if (!pixbuf) {
            g_warning("Cannot make a pixbuf of graph %d", id);
            continue;
        }
        describe_graph(data, id, str);
        insert_thumbnail_row(chooser, data, GWY_PAGE_GRAPHS, id, pixbuf, str->str);
    }
}

static void
describe_volume(GwyContainer *container, gint id, GString *str)
{
    GwyBrick *brick;
    GwySIUnit *siunit;
    GwySIValueFormat *vf = NULL;
    GQuark quark;
    gint xres, yres, zres;
    gdouble real;
    gchar *s;

    g_string_truncate(str, 0);

    quark = gwy_app_get_brick_key_for_id(id);
    brick = GWY_BRICK(gwy_container_get_object(container, quark));
    g_return_if_fail(GWY_IS_BRICK(brick));

    s = gwy_app_get_brick_title(container, id);
    g_string_append(str, s);
    g_free(s);

    siunit = gwy_brick_get_si_unit_w(brick);
    s = gwy_si_unit_get_string(siunit, GWY_SI_UNIT_FORMAT_MARKUP);
    g_string_append_printf(str, " [%s]\n", s);
    g_free(s);

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);
    g_string_append_printf(str, "%d×%dx%d %s\n", xres, yres, zres, _("px"));

    real = gwy_brick_get_xreal(brick);
    siunit = gwy_brick_get_si_unit_x(brick);
    vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, real, vf);
    g_string_append_printf(str, "%.*f%s%s",
                           vf->precision, real/vf->magnitude,
                           (vf->units && *vf->units) ? " " : "", vf->units);

    real = gwy_brick_get_yreal(brick);
    siunit = gwy_brick_get_si_unit_y(brick);
    vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, real, vf);
    g_string_append_printf(str, "×%.*f%s%s",
                           vf->precision, real/vf->magnitude,
                           (vf->units && *vf->units) ? " " : "", vf->units);

    real = gwy_brick_get_zreal(brick);
    siunit = gwy_brick_get_si_unit_z(brick);
    vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, real, vf);
    g_string_append_printf(str, "×%.*f%s%s",
                           vf->precision, real/vf->magnitude,
                           (vf->units && *vf->units) ? " " : "", vf->units);

    gwy_si_unit_value_format_free(vf);
}

static void
ensure_brick_preview(GwyContainer *container, gint id)
{
    GwyBrick *brick;
    GwyDataField *preview;
    GQuark pquark, bquark;

    pquark = gwy_app_get_brick_preview_key_for_id(id);
    if (gwy_container_gis_object(container, pquark, (GObject**)&preview) && GWY_IS_DATA_FIELD(preview))
        return;

    bquark = gwy_app_get_brick_key_for_id(id);
    brick = gwy_container_get_object(container, bquark);
    if (!GWY_IS_BRICK(brick))
        return;

    preview = _gwy_app_create_brick_preview_field(brick);
    gwy_container_set_object(container, pquark, preview);
    g_object_unref(preview);
}

static void
add_volume_thumbnails(GwyAppFileChooser *chooser,
                      GwyContainer *data, gint *ids,
                      GString *str)
{
    GdkPixbuf *pixbuf;
    guint i;
    gint id;

    for (i = 0; ids[i] != -1; i++) {
        id = ids[i];
        ensure_brick_preview(data, id);
        pixbuf = gwy_app_get_volume_thumbnail(data, id, TMS_NORMAL_THUMB_SIZE, TMS_NORMAL_THUMB_SIZE);
        if (!pixbuf) {
            g_warning("Cannot make a pixbuf of volume data %d", id);
            continue;
        }
        describe_volume(data, id, str);
        insert_thumbnail_row(chooser, data, GWY_PAGE_VOLUMES, id, pixbuf, str->str);
    }
}

static void
describe_xyz(GwyContainer *container, gint id, GString *str)
{
    GwySurface *surface;
    GwySIUnit *siunit;
    GwySIValueFormat *vf = NULL;
    GQuark quark;
    gdouble xmin, xmax, ymin, ymax;
    gchar *s;

    g_string_truncate(str, 0);

    quark = gwy_app_get_surface_key_for_id(id);
    surface = GWY_SURFACE(gwy_container_get_object(container, quark));
    g_return_if_fail(GWY_IS_SURFACE(surface));

    s = gwy_app_get_surface_title(container, id);
    g_string_append(str, s);
    g_free(s);

    siunit = gwy_surface_get_si_unit_z(surface);
    s = gwy_si_unit_get_string(siunit, GWY_SI_UNIT_FORMAT_MARKUP);
    g_string_append_printf(str, " [%s]\n", s);
    g_free(s);

    gwy_surface_get_xrange(surface, &xmin, &xmax);
    gwy_surface_get_yrange(surface, &ymin, &ymax);
    xmax -= xmin;
    ymax -= ymin;
    siunit = gwy_surface_get_si_unit_xy(surface);
    vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, sqrt(xmax*ymax), vf);
    g_string_append_printf(str, "%.*f×%.*f%s%s",
                           vf->precision, xmax/vf->magnitude,
                           vf->precision, ymax/vf->magnitude,
                           (vf->units && *vf->units) ? " " : "", vf->units);

    gwy_si_unit_value_format_free(vf);
}

static void
add_xyz_thumbnails(GwyAppFileChooser *chooser,
                   GwyContainer *data, gint *ids,
                   GString *str)
{
    GdkPixbuf *pixbuf;
    guint i;
    gint id;

    for (i = 0; ids[i] != -1; i++) {
        id = ids[i];
        pixbuf = gwy_app_get_xyz_thumbnail(data, id, TMS_NORMAL_THUMB_SIZE, TMS_NORMAL_THUMB_SIZE);
        if (!pixbuf) {
            g_warning("Cannot make a pixbuf of xyz data %d", id);
            continue;
        }
        describe_xyz(data, id, str);
        insert_thumbnail_row(chooser, data, GWY_PAGE_XYZS, id, pixbuf, str->str);
    }
}

static void
describe_cmap(GwyContainer *container, gint id, GString *str)
{
    GwyLawn *lawn;
    GwySIUnit *siunit;
    GwySIValueFormat *vf = NULL;
    GQuark quark;
    gint xres, yres, ncurves, i;
    gdouble real;
    const gchar *name;
    gchar *s;

    g_string_truncate(str, 0);

    quark = gwy_app_get_lawn_key_for_id(id);
    lawn = GWY_LAWN(gwy_container_get_object(container, quark));
    g_return_if_fail(GWY_IS_LAWN(lawn));

    s = gwy_app_get_lawn_title(container, id);
    g_string_append(str, s);
    g_free(s);

    g_string_append(str, " (");
    ncurves = gwy_lawn_get_n_curves(lawn);
    for (i = 0; i < ncurves; i++) {
        if (i)
            g_string_append(str, ", ");
        name = gwy_lawn_get_curve_label(lawn, i);
        g_string_append(str, name ? name : _("Untitled"));
    }
    g_string_append(str, ") ");

    xres = gwy_lawn_get_xres(lawn);
    yres = gwy_lawn_get_yres(lawn);
    g_string_append_printf(str, "%d×%d %s\n", xres, yres, _("px"));

    real = gwy_lawn_get_xreal(lawn);
    siunit = gwy_lawn_get_si_unit_xy(lawn);
    vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, real, vf);
    g_string_append_printf(str, "%.*f%s%s",
                           vf->precision, real/vf->magnitude,
                           (vf->units && *vf->units) ? " " : "", vf->units);

    real = gwy_lawn_get_yreal(lawn);
    vf = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, real, vf);
    g_string_append_printf(str, "×%.*f%s%s",
                           vf->precision, real/vf->magnitude,
                           (vf->units && *vf->units) ? " " : "", vf->units);

    gwy_si_unit_value_format_free(vf);
}

static void
ensure_lawn_preview(GwyContainer *container, gint id)
{
    GwyLawn *lawn;
    GwyDataField *preview;
    GQuark pquark, bquark;

    pquark = gwy_app_get_lawn_preview_key_for_id(id);
    if (gwy_container_gis_object(container, pquark, (GObject**)&preview) && GWY_IS_DATA_FIELD(preview))
        return;

    bquark = gwy_app_get_lawn_key_for_id(id);
    lawn = gwy_container_get_object(container, bquark);
    if (!GWY_IS_LAWN(lawn))
        return;

    preview = _gwy_app_create_lawn_preview_field(lawn);
    gwy_container_set_object(container, pquark, preview);
    g_object_unref(preview);
}

static void
add_cmap_thumbnails(GwyAppFileChooser *chooser,
                    GwyContainer *data, gint *ids,
                    GString *str)
{
    GdkPixbuf *pixbuf;
    guint i;
    gint id;

    for (i = 0; ids[i] != -1; i++) {
        id = ids[i];
        ensure_lawn_preview(data, id);
        pixbuf = gwy_app_get_curve_map_thumbnail(data, id, TMS_NORMAL_THUMB_SIZE, TMS_NORMAL_THUMB_SIZE);
        if (!pixbuf) {
            g_warning("Cannot make a pixbuf of curve map %d", id);
            continue;
        }
        describe_cmap(data, id, str);
        insert_thumbnail_row(chooser, data, GWY_PAGE_CURVE_MAPS, id, pixbuf, str->str);
    }
}

static gboolean
gwy_app_file_chooser_do_full_preview(gpointer user_data)
{
    GtkFileChooser *fchooser;
    GtkTreeModel *model;
    GtkListStore *store;
    GwyAppFileChooser *chooser;
    gint *channel_ids, *graph_ids, *sps_ids, *volume_ids, *xyz_ids, *cmap_ids;
    GwyContainer *data;
    const gchar *name;
    GtkTreeIter iter;
    GString *str;
    guint n;

    chooser = GWY_APP_FILE_CHOOSER(user_data);
    chooser->full_preview_id = 0;

    gwy_app_file_chooser_free_preview(chooser);

    fchooser = GTK_FILE_CHOOSER(chooser);
    chooser->preview_name_sys = gtk_file_chooser_get_preview_filename(fchooser);
    /* We should not be called when gtk_file_chooser_get_preview_filename()
     * returns NULL preview file name */
    if (!chooser->preview_name_sys) {
        g_warning("Full preview invoked with NULL preview file name");
        return FALSE;
    }

    model = gtk_icon_view_get_model(GTK_ICON_VIEW(chooser->preview));
    store = GTK_LIST_STORE(model);
    gtk_list_store_clear(store);

    data = gwy_file_load(chooser->preview_name_sys, GWY_RUN_NONINTERACTIVE, NULL);
    if (!data) {
        gwy_app_file_chooser_free_preview(chooser);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COLUMN_FILEINFO, _("Cannot preview"), -1);

        return FALSE;
    }

    gwy_data_validate(data, GWY_DATA_VALIDATE_CORRECT | GWY_DATA_VALIDATE_NO_REPORT);

    /* Since 2.45 data browser can provide the lists of unmanaged data. */
    channel_ids = gwy_app_data_browser_get_data_ids(data);
    graph_ids = gwy_app_data_browser_get_graph_ids(data);
    sps_ids = gwy_app_data_browser_get_spectra_ids(data);
    volume_ids = gwy_app_data_browser_get_volume_ids(data);
    xyz_ids = gwy_app_data_browser_get_xyz_ids(data);
    cmap_ids = gwy_app_data_browser_get_curve_map_ids(data);

    str = g_string_new(NULL);
    if (gwy_file_get_data_info(data, &name, NULL)) {
        /* FIXME: Make this translatable */
        g_string_printf(str, "<small>%s", name);
        if ((n = count_ids(volume_ids)))
            g_string_append_printf(str, ", %d vol", n);
        if ((n = count_ids(xyz_ids)))
            g_string_append_printf(str, ", %d xyz", n);
        if ((n = count_ids(cmap_ids)))
            g_string_append_printf(str, ", %d cm", n);
        if ((n = count_ids(channel_ids)))
            g_string_append_printf(str, ", %d img", n);
        if ((n = count_ids(graph_ids)))
            g_string_append_printf(str, ", %d gr", n);
        if ((n = count_ids(sps_ids)))
            g_string_append_printf(str, ", %d sp", n);
        g_string_append(str, "</small>");
        gtk_label_set_markup(GTK_LABEL(chooser->preview_type), str->str);
    }

    g_object_set(chooser->renderer_fileinfo,
                 "ellipsize", PANGO_ELLIPSIZE_END,
                 "wrap-width", -1,
                 NULL);

    add_cmap_thumbnails(chooser, data, cmap_ids, str);
    add_xyz_thumbnails(chooser, data, xyz_ids, str);
    add_volume_thumbnails(chooser, data, volume_ids, str);
    add_channel_thumbnails(chooser, data, channel_ids, str);
    add_graph_thumbnails(chooser, data, graph_ids, str);

    g_free(channel_ids);
    g_free(graph_ids);
    g_free(sps_ids);
    g_free(volume_ids);
    g_free(xyz_ids);
    g_free(cmap_ids);
    g_string_free(str, TRUE);
    g_object_unref(data);

    return FALSE;
}

static void
modify_channel_for_preview(GwyContainer *data,
                           gint id,
                           gboolean plane_level, gboolean row_level)
{
    GwyDataField *field;
    gdouble a, bx, by;

    if (!plane_level && !row_level)
        return;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &field) || !GWY_IS_DATA_FIELD(field))
        return;

    if (plane_level) {
        gwy_data_field_fit_plane(field, &a, &bx, &by);
        gwy_data_field_plane_level(field, a, bx, by);
    }

    if (row_level) {
        guint xres = gwy_data_field_get_xres(field);
        guint yres = gwy_data_field_get_yres(field);
        gdouble *row = gwy_data_field_get_data(field);
        gdouble *diffs = g_new(gdouble, xres);
        guint i, j;

        for (i = 1; i < yres; i++) {
            gdouble *prev = row;
            gdouble median;

            row += xres;
            for (j = 0; j < xres; j++)
                diffs[j] = prev[j] - row[j];
            median = gwy_math_median(xres, diffs);
            for (j = 0; j < xres; j++)
                row[j] += median;
        }

        g_free(diffs);
    }
}

static void
gwy_app_file_chooser_free_preview(GwyAppFileChooser *chooser)
{
    if (chooser->full_preview_id) {
        g_source_remove(chooser->full_preview_id);
        chooser->full_preview_id = 0;
    }

    if (chooser->preview_name_sys) {
        gwy_debug("freeing preview of <%s>", chooser->preview_name_sys);
    }
    g_free(chooser->preview_name_sys);
    chooser->preview_name_sys = NULL;
}

/* Work around crashes in the file open dialog in some Gtk+ versions if no .recently-used.xbel is present. */
static void
ensure_gtk_recently_used(void)
{
    static gboolean ensured = FALSE;
    gchar *filename = NULL;

    if (ensured)
        return;

    filename = g_build_filename(g_get_user_data_dir(), ".recently-used.xbel", NULL);
    if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
        GBookmarkFile *bookmarkfile = g_bookmark_file_new();
        GError *error = NULL;

        if (!g_bookmark_file_to_file(bookmarkfile, filename, &error)) {
            g_warning("Failed to create %s: %s", filename, error->message);
            g_clear_error(&error);
        }
        g_bookmark_file_free(bookmarkfile);
    }
    g_free(filename);
    ensured = TRUE;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
