/*
 *  @(%) $Id: plugin-proxy.c 20678 2017-12-18 18:26:55Z yeti-dn $
 *  Copyright (C) 2003,2004 David Necas (Yeti), Petr Klapetek.
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
 *
 **************************************************************************
 *
 *  As a special exception, you are allowed to freely study this one file
 *  (plugin-proxy.c) for the purpose of creation of Gwyddion plug-ins
 *  reading and/or writing the `dump' file format, and license them under
 *  any license.
 *
 *  This shall not be considered license infringement in the sense of
 *  creation of non-GPL derived work, as this file serves as the ultimate
 *  `dump' format documentation.
 *
 **************************************************************************
 */

/* XXX: ,safe` for Unix, but probably broken for Win32
 * It always creates the temporary file, keeps it open all the time during
 * plug-in runs, then unlinks it and closes at last.  It seems to work on
 * Win32 too, but one never knows...
 *
 * XXX: the `dump' should probably be `dumb'...
 *
 * XXX: Plug-ins cannot specify sens_flags.
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwymodule/gwymodule.h>
#include <libprocess/datafield.h>
#include <app/gwyapp.h>

typedef struct {
    gchar *name;
    gchar *menu_path;
    gchar *tooltip;
    GwyRunType run;
    gchar *file;  /* The file to execute to run the plug-in */
} ProcPluginInfo;

typedef struct {
    gchar *name;
    gchar *description;
    GwyFileOperationType run;
    gchar *glob;
    GPatternSpec **pattern;
    glong *specificity;
    gchar *file;  /* The file to execute to run the plug-in */
} FilePluginInfo;

typedef GList* (*ProxyRegister)(GList *plugins,
                                const gchar *dir,
                                gchar *buffer);

/* top-level */
static gboolean        create_user_plugin_dirs   (void);
static gboolean        module_register           (void);
static GList*          register_plugins          (GList *plugins,
                                                  const gchar *dir,
                                                  ProxyRegister register_func);
static GSList*         find_plugin_executables   (const gchar *dir,
                                                  GSList *list,
                                                  gint level);
static gchar**         construct_rgi_names       (const gchar *pluginname);

/* process plug-in proxy */
static GList*          proc_register_plugins     (GList *plugins,
                                                  const gchar *dir,
                                                  gchar *buffer);
static void            proc_plugin_proxy_run     (GwyContainer *data,
                                                  GwyRunType run,
                                                  const gchar *name);
static ProcPluginInfo* proc_find_plugin          (const gchar *name,
                                                  GwyRunType run);

/* file plug-in-proxy */
static GList*          file_register_plugins     (GList *plugins,
                                                  const gchar *dir,
                                                  gchar *buffer);
static GwyContainer*   file_plugin_proxy_load    (const gchar *filename,
                                                  GwyRunType mode,
                                                  GError **error,
                                                  const gchar *name);
static gboolean        file_plugin_proxy_export  (GwyContainer *data,
                                                  const gchar *filename,
                                                  GwyRunType mode,
                                                  GError **error,
                                                  const gchar *name);
static gint        file_plugin_proxy_detect  (const GwyFileDetectInfo *fileinfo,
                                              gboolean only_name,
                                              const gchar *name);
static FilePluginInfo* file_find_plugin          (const gchar *name,
                                                  GwyFileOperationType run);
static GPatternSpec**  file_patternize_globs     (const gchar *glob);
static glong*          file_glob_specificities   (const gchar *glob);
static glong           file_pattern_specificity  (const gchar *pattern);

/* common helpers */
static FILE*           text_dump_export          (GwyContainer *data,
                                                  GQuark dquark,
                                                  GQuark mquark,
                                                  gchar **filename,
                                                  GError **error);
static void            dump_export_data_field    (GwyDataField *dfield,
                                                  const gchar *name,
                                                  FILE *fh);
static FILE*           open_temporary_file       (gchar **filename,
                                                  GError **error);
static GwyContainer*   text_dump_import          (gchar *buffer,
                                                  gsize size,
                                                  GError **error);
static gchar*        decode_glib_encoded_filename(const gchar *filename);

/* The module info. */
static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Plug-in proxy is a module capable of querying, registering, and "
       "running external programs (plug-ins) on data pretending they are "
       "data processing or file loading/saving modules."),
    "Yeti <yeti@gwyddion.net>",
    "3.9",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

/* This is the ONLY exported symbol.  The argument is the module info.
 * NO semicolon after. */
GWY_MODULE_QUERY(module_info)

/* XXX: static data */
static GList *proc_plugins = NULL;
static GList *file_plugins = NULL;

static const GwyEnum run_mode_names[] = {
    { "noninteractive", GWY_RUN_IMMEDIATE,   },
    { "modal",          GWY_RUN_INTERACTIVE, },
    { "interactive",    GWY_RUN_INTERACTIVE, },
    { "with_defaults",  GWY_RUN_IMMEDIATE,   },
    { "immediate",      GWY_RUN_IMMEDIATE,   },
    { NULL,             -1,                  },
};

/* For plug-ins, save always means export */
static const GwyEnum file_op_names[] = {
    { "load",   GWY_FILE_OPERATION_LOAD,   },
    { "save",   GWY_FILE_OPERATION_EXPORT, },
    { "export", GWY_FILE_OPERATION_EXPORT, },
    { NULL,     -1,                        },
};

static gboolean
module_register(void)
{
    gchar *plugin_path, *libpath;
    gchar *dir;

    dir = gwy_find_self_dir("modules");
    g_return_val_if_fail(dir, FALSE);
    libpath = g_path_get_dirname(dir);
    g_free(dir);
    gwy_debug("plug-in library path is: %s", libpath);
    g_setenv("GWYPLUGINLIB", libpath, TRUE);
    /* Don't g_free(libpath), some systems don't like it. */

    plugin_path = gwy_find_self_dir("plugins");
    g_return_val_if_fail(plugin_path, FALSE);
    gwy_debug("plug-in path is: %s", plugin_path);

    dir = g_build_filename(plugin_path, "process", NULL);
    proc_plugins = register_plugins(NULL, dir, proc_register_plugins);
    g_free(dir);

    dir = g_build_filename(plugin_path, "file", NULL);
    file_plugins = register_plugins(NULL, dir, file_register_plugins);
    g_free(dir);

    create_user_plugin_dirs();

    dir = g_build_filename(gwy_get_user_dir(), "plugins", "process", NULL);
    proc_plugins = register_plugins(proc_plugins, dir, proc_register_plugins);
    g_free(dir);

    dir = g_build_filename(gwy_get_user_dir(), "plugins", "file", NULL);
    file_plugins = register_plugins(file_plugins, dir, file_register_plugins);
    g_free(dir);

    g_free(plugin_path);

    return TRUE;
}

/**
 * create_user_plugin_dirs:
 *
 * Creates plug-in directory tree in user's home directory (or whereever is
 * it on Win32).
 *
 * Returns: Whether all the directories either exists or were successfully
 * created.
 **/
static gboolean
create_user_plugin_dirs(void)
{
    gchar *dir[3];
    gsize i;
    gboolean ok = TRUE;

    dir[0] = g_build_filename(gwy_get_user_dir(), "plugins", NULL);
    dir[1] = g_build_filename(gwy_get_user_dir(), "plugins", "process", NULL);
    dir[2] = g_build_filename(gwy_get_user_dir(), "plugins", "file", NULL);

    for (i = 0; i < G_N_ELEMENTS(dir); i++) {
        if (!g_file_test(dir[i], G_FILE_TEST_IS_DIR)) {
            gwy_debug("Trying to create user plugin directory %s", dir[i]);
            if (g_mkdir(dir[i], 0700)) {
                g_warning("Cannot create user plugin directory %s: %s",
                        dir[i], g_strerror(errno));
                ok = FALSE;
            }
        }
        g_free(dir[i]);
    }

    return ok;
}

/**
 * register_plugins:
 * @plugins: Existing plug-in list.
 * @dir: Plug-in directory to search them in.
 * @register_func: Particular registration function.
 *
 * Register all plug-ins in a directory @dir with @register_func and add
 * them to @plugins.
 *
 * Returns: The new plug-in list, with all registered plug-in prepended.
 **/
static GList*
register_plugins(GList *plugins,
                 const gchar *dir,
                 ProxyRegister register_func)
{
    gchar *args[] = { NULL, "register", NULL };
    gchar *buffer, *pluginname, *rginame;
    gchar **rginames;
    gint i, exit_status;
    GError *err = NULL;
    GSList *list, *l;
    gboolean ok;

    list = find_plugin_executables(dir, NULL, 1);
    for (l = list; l; l = g_slist_next(l)) {
        pluginname = (gchar*)l->data;
        /* try rgi files first */
        rginames = construct_rgi_names(pluginname);
        for (i = 0; (rginame = rginames[i]); i++) {
            if (g_file_get_contents(rginame, &buffer, NULL, NULL))
                break;
        }
        if (rginame) {
            gwy_debug("registering using %s data", rginame);
            plugins = register_func(plugins, pluginname, buffer);
            g_free(pluginname);
            g_free(buffer);
            g_strfreev(rginames);
            continue;
        }
        g_strfreev(rginames);

        args[0] = pluginname;
        buffer = NULL;
        gwy_debug("querying plug-in %s for registration info", pluginname);
        ok = g_spawn_sync(NULL, args, NULL, 0, NULL, NULL,
                          &buffer, NULL, &exit_status, &err);
        if (ok)
            plugins = register_func(plugins, pluginname, buffer);
        else {
            g_warning("Cannot register plug-in %s: %s",
                      pluginname, err->message);
            g_clear_error(&err);
        }
        g_free(pluginname);
        g_free(buffer);
    }
    g_slist_free(list);

    return plugins;
}

/**
 * construct_rgi_names:
 * @pluginname: A file name.
 *
 * Construct list of possible .rgi file names if @pluginname is a plug-in
 * executable file name.
 *
 * Returns: A %NULL-terminated list of strings, to be freed by caller.
 **/
static gchar**
construct_rgi_names(const gchar *pluginname)
{
    gchar **rginames;
    gchar *pos;
    gint len;

    pos = strrchr(pluginname, '.');
    rginames = g_new0(gchar*, pos ? 5 : 3);
    rginames[0] = g_strconcat(pluginname, ".rgi", NULL);
    rginames[1] = g_strconcat(pluginname, ".RGI", NULL);
    if (pos) {
        len = pos - pluginname;
        rginames[2] = g_new(gchar, len + 5);
        strncpy(rginames[2], pluginname, len + 1);
        strncpy(rginames[2] + len + 1, "rgi", 4);
        rginames[3] = g_new(gchar, len + 5);
        strncpy(rginames[3], pluginname, len + 1);
        strncpy(rginames[3] + len + 1, "RGI", 4);
    }

    return rginames;
}

/**
 * find_plugin_executables:
 * @dir: A directory to search in.
 * @list: #GList of plug-in filenames to add filenames to.
 * @level: Maximum search depth (0 means do not search at all).
 *
 * Scans a directory for executables, maybe recursively.
 *
 * A fixed depth must be specified to prevent loops.
 *
 * Returns: @list with newly found executables appended.
 **/
static GSList*
find_plugin_executables(const gchar *dir,
                        GSList *list,
                        gint level)
{
    const gchar *filename;
    gchar *pluginname;
    GError *err = NULL;
    GDir *gdir;

    if (level-- < 0)
        return list;

    gdir = g_dir_open(dir, 0, &err);
    if (err) {
        gwy_debug("Cannot open plug-in directory %s: %s", dir, err->message);
        g_clear_error(&err);
        return NULL;
    }
    gwy_debug("Scanning directory %s", dir);
    while ((filename = g_dir_read_name(gdir))) {
        if (gwy_filename_ignore(filename)) {
            gwy_debug("Ignoring %s (ignorable file)", filename);
            continue;
        }
        pluginname = g_build_filename(dir, filename, NULL);
        if (g_file_test(pluginname, G_FILE_TEST_IS_DIR)) {
            gwy_debug("%s is a directory, descending", filename);
            list = find_plugin_executables(pluginname, list, level);
            g_free(pluginname);
            continue;
        }
        if (g_str_has_suffix(filename, ".rgi")
            || g_str_has_suffix(filename, ".RGI")) {
            gwy_debug("Ignoring %s, it is a RGI file", filename);
            g_free(pluginname);
            continue;
        }
        if (!g_file_test(pluginname, G_FILE_TEST_IS_EXECUTABLE)) {
            gwy_debug("Ignoring %s, is not executable", filename);
            g_free(pluginname);
            continue;
        }
#ifdef G_OS_WIN32
        if (!g_str_has_suffix(filename, ".exe")
            && !g_str_has_suffix(filename, ".EXE")) {
            gwy_debug("[Win32] Ignoring %s, is not .exe", filename);
            g_free(pluginname);
            continue;
        }
        if (g_str_has_prefix(filename, "unins")
            || g_str_has_prefix(filename, "UNINS")) {
            gwy_debug("[Win32] Ignoring %s, is uninstaller", filename);
            g_free(pluginname);
            continue;
        }
#endif
        gwy_debug("plug-in %s", filename);
        list = g_slist_prepend(list, pluginname);
    }

    g_dir_close(gdir);

    return list;
}


/***** Proc ****************************************************************/

/**
 * proc_register_plugins:
 * @plugins: Plug-in list to eventually add the plug-in to.
 * @file: Plug-in file (full path).
 * @buffer: The output from "plugin register".
 *
 * Parse output from "plugin register" and eventually add it to the
 * plugin-list.
 *
 * Returns: The new plug-in list, with the plug-in eventually prepended.
 **/
static GList*
proc_register_plugins(GList *plugins,
                      const gchar *file,
                      gchar *buffer)
{
    ProcPluginInfo *info;
    gchar *pname = NULL, *menu_path = NULL, *run_modes = NULL;
    GwyRunType run;

    gwy_debug("buffer: <<<%s>>>", buffer);
    while (buffer) {
        if ((pname = gwy_str_next_line(&buffer))
            && *pname
            && (menu_path = gwy_str_next_line(&buffer))
            && menu_path[0] == '/'
            && (run_modes = gwy_str_next_line(&buffer))
            && (run = gwy_string_to_flags(run_modes,
                                          run_mode_names, -1, NULL))) {
            info = g_new(ProcPluginInfo, 1);
            info->name = g_strdup(pname);
            info->menu_path = g_strconcat(_("/_Plug-Ins"), menu_path, NULL);
            info->tooltip = g_strdup_printf(_("Run plug-in %s"), menu_path+1);
            info->run = run;
            if (gwy_process_func_register(info->name,
                                          proc_plugin_proxy_run,
                                          info->menu_path,
                                          NULL,
                                          info->run,
                                          GWY_MENU_FLAG_DATA,
                                          info->tooltip)) {
                info->file = g_strdup(file);
                plugins = g_list_prepend(plugins, info);
            }
            else {
                g_free((gpointer)info->name);
                g_free((gpointer)info->menu_path);
                g_free((gpointer)info->tooltip);
                g_free(info);
            }
        }
        else if (pname && *pname) {
            g_warning("failed; "
                      "pname = %s, menu_path = %s, run_modes = %s",
                      pname, menu_path, run_modes);
        }
        while (buffer && *buffer)
            gwy_str_next_line(&buffer);
    }

    return plugins;
}

/**
 * proc_plugin_proxy_run:
 * @data: A data container.
 * @run: Run mode.
 * @name: Plug-in name (i.e. data processing function) to run.
 *
 * The plug-in proxy itself, runs plug-in @name on @data.
 *
 * Returns: Whether it succeeded running the plug-in.
 **/
static void
proc_plugin_proxy_run(GwyContainer *data,
                      GwyRunType run,
                      const gchar *name)
{
    ProcPluginInfo *info;
    GwyContainer *newdata;
    gchar *filename, *buffer = NULL;
    GError *err = NULL;
    gint exit_status, id, newid;
    gsize size = 0;
    FILE *fh;
    gchar *args[] = { NULL, "run", NULL, NULL, NULL };
    GQuark dquark, mquark, squark;
    gboolean ok;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_KEY, &dquark,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);

    gwy_debug("called as %s with run mode %d", name, run);
    if (!(info = proc_find_plugin(name, run)))
        return;

    fh = text_dump_export(data, dquark, mquark, &filename, NULL);
    g_return_if_fail(fh);
    args[0] = info->file;
    args[2] = g_strdup(gwy_enum_to_string(run, run_mode_names, -1));
    args[3] = decode_glib_encoded_filename(filename);
    gwy_debug("%s %s %s %s", args[0], args[1], args[2], args[3]);
    ok = g_spawn_sync(NULL, args, NULL, 0, NULL, NULL,
                      NULL, NULL, &exit_status, &err);
    if (!err)
        ok &= g_file_get_contents(filename, &buffer, &size, &err);
    g_unlink(filename);
    fclose(fh);
    gwy_debug("ok = %d, exit_status = %d, err = %p", ok, exit_status, err);
    ok &= !exit_status;
    if (ok && (newdata = text_dump_import(buffer, size, NULL))) {
        GwyDataField *dfield;

        /* Merge data */
        if (gwy_container_gis_object_by_name(newdata, "/0/data", &dfield))
            g_object_ref(dfield);
        else {
            dfield = gwy_container_get_object(data, dquark);
            dfield = gwy_data_field_duplicate(dfield);
        }
        newid = gwy_app_data_browser_add_data_field(dfield, data, TRUE);

        /* Merge mask */
        if (gwy_container_gis_object_by_name(newdata, "/0/mask", &dfield))
            g_object_ref(dfield);
        else if (gwy_container_gis_object(data, mquark, &dfield))
            dfield = gwy_data_field_duplicate(dfield);
        else
            dfield = NULL;

        if (dfield) {
            mquark = gwy_app_get_mask_key_for_id(newid);
            gwy_container_set_object(data, mquark, dfield);
            g_object_unref(dfield);
        }

        /* Merge presentation */
        if (gwy_container_gis_object_by_name(newdata, "/0/show", &dfield)) {
            squark = gwy_app_get_show_key_for_id(newid);
            gwy_container_set_object(data, squark, dfield);
        }

        /* Merge stuff.  XXX: This is brutal and incomplete. */
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_RANGE_TYPE,
                                GWY_DATA_ITEM_MASK_COLOR,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
        gwy_app_sync_data_items(newdata, data, 0, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_RANGE_TYPE,
                                0);

        g_object_unref(newdata);
    }
    else {
        g_warning("Cannot run plug-in %s: %s",
                  info->file,
                  err ? err->message : "it returned garbage.");
    }
    g_free(args[3]);
    g_free(args[2]);
    g_clear_error(&err);
    g_free(buffer);
    g_free(filename);
}

/**
 * proc_find_plugin:
 * @name: Plug-in name.
 * @run: Run modes it is supposed to support.
 *
 * Finds a data processing plugin of name @name supporting at least one of the
 * modes in @run.
 *
 * Returns: The plug-in info.
 **/
static ProcPluginInfo*
proc_find_plugin(const gchar *name,
                 GwyRunType run)
{
    ProcPluginInfo *info;
    GList *l;

    for (l = proc_plugins; l; l = g_list_next(l)) {
        info = (ProcPluginInfo*)l->data;
        if (gwy_strequal(info->name, name))
            break;
    }
    if (!l) {
        g_critical("Don't know anything about plug-in `%s'.", name);
        return NULL;
    }
    if (!(info->run & run)) {
        g_critical("Plug-in `%s' doesn't support this run mode.", name);
        return NULL;
    }

    return info;
}

/***** File ****************************************************************/

/**
 * file_register_plugins:
 * @plugins: Plug-in list to eventually add the plug-in to.
 * @file: Plug-in file (full path).
 * @buffer: The output from "plugin register".
 *
 * Parse output from "plugin register" and eventually add it to the
 * plugin-list.
 *
 * Returns: The new plug-in list, with the plug-in eventually prepended.
 **/
static GList*
file_register_plugins(GList *plugins,
                      const gchar *file,
                      gchar *buffer)
{
    FilePluginInfo *info;
    gchar *pname = NULL, *file_desc = NULL, *run_modes = NULL, *glob = NULL;
    GwyFileOperationType run;

    gwy_debug("buffer: <<<%s>>>", buffer);
    while (buffer) {
        if ((pname = gwy_str_next_line(&buffer))
            && *pname
            && (file_desc = gwy_str_next_line(&buffer))
            && *file_desc
            && (glob = gwy_str_next_line(&buffer))
            && *glob
            && (run_modes = gwy_str_next_line(&buffer))
            && (run = gwy_string_to_flags(run_modes,
                                          file_op_names, -1, NULL))) {
            info = g_new0(FilePluginInfo, 1);
            info->name = g_strdup(pname);
            info->description = g_strdup(file_desc);
            if (gwy_file_func_register(info->name, info->description,
                                       &file_plugin_proxy_detect,
                                       (run & GWY_FILE_OPERATION_LOAD)
                                       ? file_plugin_proxy_load : NULL,
                                       NULL,
                                       (run & GWY_FILE_OPERATION_EXPORT)
                                       ? file_plugin_proxy_export : NULL)) {
                info->file = g_strdup(file);
                info->run = run;
                info->glob = g_strdup(glob);
                info->pattern = file_patternize_globs(glob);
                info->specificity = file_glob_specificities(glob);
                plugins = g_list_prepend(plugins, info);
            }
            else {
                g_free((gpointer)info->name);
                g_free((gpointer)info->description);
                g_free(info);
            }
        }
        else if (pname && *pname) {
            g_warning("failed; "
                      "pname = %s, file_desc = %s, run_modes = %s, glob = %s",
                      pname, file_desc, run_modes, glob);
        }
        while (buffer && *buffer)
            gwy_str_next_line(&buffer);
    }

    return plugins;
}

/**
 * file_plugin_proxy_load:
 * @filename. A file name to load.
 * @mode: Run mode.
 * @error: Return location for a #GError (or %NULL).
 * @name: Plug-in name (i.e. file-loading function) to run.
 *
 * The plug-in proxy itself, runs file-loading plug-in @name to load @filename.
 *
 * Returns: A newly created data container with the contents of @filename,
 *          or %NULL if it fails.
 **/
static GwyContainer*
file_plugin_proxy_load(const gchar *filename,
                       GwyRunType mode,
                       GError **error,
                       const gchar *name)
{
    FilePluginInfo *info;
    GwyContainer *data = NULL;
    GObject *dfield;
    gchar *tmpname = NULL, *buffer = NULL;
    GError *err = NULL;
    gint exit_status;
    gsize size = 0;
    FILE *fh;
    gchar *args[] = { NULL, NULL, NULL, NULL, NULL };
    gboolean ok;

    gwy_debug("called as %s with file `%s'", name, filename);
    if (mode != GWY_RUN_INTERACTIVE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_INTERACTIVE,
                    _("Plugin-proxy must be run as interactive."));
        return NULL;
    }
    if (!(info = file_find_plugin(name, GWY_FILE_OPERATION_LOAD))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_UNIMPLEMENTED,
                    _("Plug-in `%s' does not implement file loading."), name);
        return NULL;
    }
    if (!(fh = open_temporary_file(&tmpname, error)))
        return NULL;

    args[0] = info->file;
    args[1] = g_strdup(gwy_enum_to_string(GWY_FILE_OPERATION_LOAD,
                                          file_op_names, -1));
    args[2] = tmpname;
    args[3] = decode_glib_encoded_filename(filename);
    gwy_debug("%s %s %s %s", args[0], args[1], args[2], args[3]);
    ok = g_spawn_sync(NULL, args, NULL, 0, NULL, NULL,
                      NULL, NULL, &exit_status, &err);
    if (ok) {
        ok = g_file_get_contents(tmpname, &buffer, &size, &err);
        if (!ok) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                        _("Cannot read temporary file: %s."), err->message);
            g_clear_error(&err);
        }
    }
    else {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("Cannot execute plug-in `%s': %s."), name, err->message);
        g_clear_error(&err);
    }
    g_unlink(tmpname);
    fclose(fh);
    gwy_debug("ok = %d, exit_status = %d, err = %p", ok, exit_status, err);
    if (ok && exit_status) {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("Plug-in `%s' returned non-zero exit status: %d."),
                    name, exit_status);
        ok = FALSE;
    }
    if (ok) {
        data = text_dump_import(buffer, size, error);
        if (!data)
            ok = FALSE;
    }
    if (ok
        && (!gwy_container_gis_object_by_name(data, "/0/data", &dfield)
            || !GWY_IS_DATA_FIELD(dfield))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Plug-in `%s' did not return any meaningful data."),
                    name);
        gwy_object_unref(data);
    }
    g_free(args[1]);
    g_free(args[3]);
    g_free(buffer);
    g_free(tmpname);

    return data;
}

/**
 * file_plugin_proxy_export:
 * @data: A data container to save.
 * @filename: A file name to save @data to.
 * @mode: Run mode.
 * @error: Return location for a #GError (or %NULL).
 * @name: Plug-in name (i.e. file-saving function) to run.
 *
 * The plug-in proxy itself, runs file-saving plug-in @name to save @filename.
 *
 * Returns: Whether it succeeded saving the data.
 **/
static gboolean
file_plugin_proxy_export(GwyContainer *data,
                         const gchar *filename,
                         GwyRunType mode,
                         GError **error,
                         const gchar *name)
{
    FilePluginInfo *info;
    gchar *tmpname = NULL;
    GError *err = NULL;
    gint exit_status;
    FILE *fh;
    gchar *args[] = { NULL, NULL, NULL, NULL, NULL };
    GQuark dquark, mquark;
    gboolean ok;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_KEY, &dquark,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     0);

    gwy_debug("called as %s with file `%s'", name, filename);
    if (mode != GWY_RUN_INTERACTIVE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_INTERACTIVE,
                    _("Plugin-proxy must be run as interactive."));
        return FALSE;
    }
    if (!(info = file_find_plugin(name, GWY_FILE_OPERATION_EXPORT))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_UNIMPLEMENTED,
                    _("Plug-in `%s' does not implement file saving."), name);
        return FALSE;
    }

    fh = text_dump_export(data, dquark, mquark, &tmpname, error);
    if (!fh)
        return FALSE;

    args[0] = info->file;
    args[1] = g_strdup(gwy_enum_to_string(GWY_FILE_OPERATION_EXPORT,
                                          file_op_names, -1));
    args[2] = tmpname;
    args[3] = decode_glib_encoded_filename(filename);
    gwy_debug("%s %s %s %s", args[0], args[1], args[2], args[3]);
    ok = g_spawn_sync(NULL, args, NULL, 0, NULL, NULL,
                      NULL, NULL, &exit_status, &err);
    if (!ok) {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("Cannot execute plug-in `%s': %s."), name, err->message);
        g_clear_error(&err);
    }
    g_unlink(tmpname);
    fclose(fh);
    gwy_debug("ok = %d, exit_status = %d, err = %p", ok, exit_status, err);
    if (ok && exit_status) {
        g_set_error(error, GWY_MODULE_FILE_ERROR,
                    GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("Plug-in `%s' returned non-zero exit status: %d."),
                    name, exit_status);
        ok = FALSE;
    }
    g_free(args[1]);
    g_free(args[3]);
    g_free(tmpname);

    return ok;
}

/**
 * file_plugin_proxy_detect:
 * @filename: File information.
 * @only_name: Whether only name should be used for detection (otherwise
 *             trying to open the file is allowed).  Note this parameter is
 *             formal, as the proxy always decides only on filename basis.
 * @name: Plug-in name (i.e. file-detection function) to run.
 *
 * The plug-in proxy itself.  Emulates filetype detection based on file
 * name glob given by the plug-in during registration.
 *
 * Returns: The score (as defined in gwyddion filetype module interface).
 **/
static gint
file_plugin_proxy_detect(const GwyFileDetectInfo *fileinfo,
                         G_GNUC_UNUSED gboolean only_name,
                         const gchar *name)
{
    FilePluginInfo *info;
    gint i, max;

    gwy_debug("called as %s with file `%s'", name, fileinfo->name);
    if (!(info = file_find_plugin(name, GWY_FILE_OPERATION_MASK)))
        return 0;

    max = G_MININT;
    for (i = 0; info->pattern[i]; i++) {
        if (info->specificity[i] > max
            && g_pattern_match_string(info->pattern[i], fileinfo->name))
            max = info->specificity[i];
    }
    if (max == G_MININT)
        return 0;

    return CLAMP(max, 1, 40);
}

/**
 * file_find_plugin:
 * @name: Plug-in name.
 * @run: File operations it is supposed to support.
 *
 * Finds a filetype plugin of name @name supporting at least one of the
 * file operations in @run.
 *
 * Returns: The plug-in info.
 **/
static FilePluginInfo*
file_find_plugin(const gchar *name,
                 GwyFileOperationType run)
{
    FilePluginInfo *info;
    GList *l;

    for (l = file_plugins; l; l = g_list_next(l)) {
        info = (FilePluginInfo*)l->data;
        if (gwy_strequal(info->name, name))
            break;
    }
    if (!l) {
        g_critical("Don't know anything about plug-in `%s'.", name);
        return NULL;
    }
    if (!(info->run & run)) {
        g_critical("Plug-in `%s' doesn't support this operation.", name);
        return NULL;
    }

    return info;
}

static GPatternSpec**
file_patternize_globs(const gchar *glob)
{
    GPatternSpec **specs;
    gchar **globs;
    gchar *s;
    gint i, n;

    globs = g_strsplit(glob, " ", 0);
    if (!globs) {
        specs = g_new(GPatternSpec*, 1);
        *specs = NULL;
        return specs;
    }

    for (n = 0; globs[n]; n++)
        ;
    specs = g_new(GPatternSpec*, n+1);
    for (i = 0; i < n; i++) {
        s = g_strstrip(globs[i]);
        specs[i] = g_pattern_spec_new(s);
    }
    specs[n] = NULL;
    g_strfreev(globs);

    return specs;
}

static glong*
file_glob_specificities(const gchar *glob)
{
    glong *specs;
    gchar **globs;
    gchar *s;
    gint i, n;

    globs = g_strsplit(glob, " ", 0);
    if (!globs) {
        specs = g_new(glong, 1);
        *specs = 0;
        return specs;
    }

    for (n = 0; globs[n]; n++)
        ;
    specs = g_new(glong, n+1);
    for (i = 0; i < n; i++) {
        s = g_strstrip(globs[i]);
        specs[i] = file_pattern_specificity(s);
    }
    specs[n] = 0;
    g_strfreev(globs);

    return specs;
}

/**
 * file_pattern_specificity:
 * @pattern: A fileglob-like pattern, as supported by #GPatternSpec.
 *
 * Computes a number approximately representing pattern specificity.
 *
 * The pattern specificity increases with the number of non-wildcards in
 * the pattern and decreases with the number of wildcards (*) in the pattern.
 *
 * Returns: The pattern specificity. Normally a small integer, may be even
 *          negative (e.g. for "*").
 **/
static glong
file_pattern_specificity(const gchar *pattern)
{
    glong psp = 0;
    gboolean changed;
    gchar *pat, *end, *p;

    g_return_val_if_fail(pattern && *pattern, 0);

    pat = g_strdup(pattern);
    end = pat + strlen(pat) - 1;
    /* change all '?' next to a '*' to '*' */
    do {
        changed = FALSE;
        for (p = pat; p < end; p++) {
            if (*p == '*' && *(p+1) == '?') {
                *(p+1) = '*';
                changed = TRUE;
            }
        }
        for (p = end; p > pat; p--) {
            if (*p == '*' && *(p-1) == '?') {
                *(p-1) = '*';
                changed = TRUE;
            }
        }
    } while (changed);

    end = p = pat;
    while (*p) {
        *end = *p;
        if (*p == '*') {
            while (*p == '*')
                p++;
        }
        else
            p++;
        end++;
    }
    *end = '\0';

    for (p = pat; *p; p++) {
        switch (*p) {
            case '*':
            psp -= 4;
            break;

            case '?':
            psp += 1;
            break;

            default:
            psp += 6;
            break;
        }
    }
    g_free(pat);

    return psp;
}

/***** Sub *****************************************************************/

/**
 * text_dump_export:
 * @data: A %GwyContainer to dump.
 * @filename: File name to dump the container to.
 * @error: Return location for a #GError (or %NULL).
 *
 * Dumps data container to a file @filename.
 *
 * In fact, it only dumps data, mask %DataField's, palette, and everything
 * under "/meta" as strings.
 *
 * Returns: A filehandle of the dump file open in "wb" mode.
 **/
static FILE*
text_dump_export(GwyContainer *data,
                 GQuark dquark,
                 GQuark mquark,
                 gchar **filename,
                 GError **error)
{
    GwyDataField *dfield;
    FILE *fh;

    if (!(fh = open_temporary_file(filename, error)))
        return NULL;

    dfield = GWY_DATA_FIELD(gwy_container_get_object(data, dquark));
    dump_export_data_field(dfield, "/0/data", fh);
    if (gwy_container_gis_object(data, mquark, &dfield)) {
        dump_export_data_field(dfield, "/0/mask", fh);
    }
    fflush(fh);

    return fh;
}

/**
 * dump_export_data_field:
 * @dfield: A #GwyDataField.
 * @name: The name of @dfield.
 * @fh: A filehandle open for writing.
 *
 * Dumps a one #GwyDataField to @fh.
 **/
static void
dump_export_data_field(GwyDataField *dfield, const gchar *name, FILE *fh)
{
    const gdouble *data;
    gchar *unit;
    gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
    gint xres, yres;

    gwy_debug("Exporting %s", name);
    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    fprintf(fh, "%s/xres=%d\n", name, xres);
    fprintf(fh, "%s/yres=%d\n", name, yres);
    fprintf(fh, "%s/xreal=%s\n", name,
            g_ascii_dtostr(buf, sizeof(buf), gwy_data_field_get_xreal(dfield)));
    fprintf(fh, "%s/yreal=%s\n", name,
            g_ascii_dtostr(buf, sizeof(buf), gwy_data_field_get_yreal(dfield)));
    unit = gwy_si_unit_get_string(gwy_data_field_get_si_unit_xy(dfield),
                                  GWY_SI_UNIT_FORMAT_PLAIN);
    fprintf(fh, "%s/unit-xy=%s\n", name, unit);
    g_free(unit);
    unit = gwy_si_unit_get_string(gwy_data_field_get_si_unit_z(dfield),
                                  GWY_SI_UNIT_FORMAT_PLAIN);
    fprintf(fh, "%s/unit-z=%s\n", name, unit);
    g_free(unit);
    fprintf(fh, "%s=[\n[", name);
    fflush(fh);
    data = gwy_data_field_get_data_const(dfield);
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
    fwrite(data, sizeof(gdouble), xres*yres, fh);
#else
    {
        guint8 *d;

        d = g_new(guint8, sizeof(gdouble)*xres*yres);
        gwy_memcpy_byte_swap((const guint8*)data, d,
                             sizeof(gdouble), xres*yres, sizeof(gdouble) - 1);
        fwrite(data, sizeof(gdouble), xres*yres, fh);
        g_free(d);
    }
#endif
    fwrite("]]\n", 1, 3, fh);
    fflush(fh);
}

/**
 * open_temporary_file:
 * @filename: Where the filename is to be stored.
 * @error: Return location for a #GError (or %NULL).
 *
 * Open a temporary file in "wb" mode, return the stream handle.
 *
 * On *nix, it tries to open the file in a safe manner.  On MS systems,
 * it just opens a file.  Who cares...
 *
 * Returns: The filehandle of the open file.
 **/
static FILE*
open_temporary_file(gchar **filename,
                    GError **error)
{
    FILE *fh;
#ifdef G_OS_WIN32
    gchar buf[9];
    gsize i;

    /* FIXME: this is bogus. like the OS it's needed for. */
    for (i = 0; i < sizeof(buf)-1; i++)
        buf[i] = 'a' + (rand()/283)%26;
    buf[sizeof(buf)-1] = '\0';
    *filename = g_build_filename(g_get_tmp_dir(), buf, NULL);

    fh = g_fopen(*filename, "wb");
    if (!fh)
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("Cannot create a temporary file: %s."),
                    g_strerror(errno));
#else
    GError *err = NULL;
    int fd;

    fd = g_file_open_tmp("gwydXXXXXXXX", filename, &err);
    if (fd < 0) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("Cannot create a temporary file: %s."),
                    err->message);
        g_clear_error(&err);
        return NULL;
    }
    fh = fdopen(fd, "wb");
    if (!fh)
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("Cannot fdopen() already open file: %s."),
                    g_strerror(errno));
#endif

    return fh;
}

static GwyContainer*
text_dump_import(gchar *buffer,
                 gsize size,
                 GError **error)
{
    gchar *val, *key, *pos, *line, *title;
    GwyContainer *data;
    GwyDataField *dfield;
    gdouble xreal, yreal;
    gint xres, yres;
    GwySIUnit *uxy, *uz;
    const guchar *s;
    gdouble *d;
    gsize n;

    data = gwy_container_new();

    pos = buffer;
    while ((line = gwy_str_next_line(&pos)) && *line) {
        val = strchr(line, '=');
        if (!val || *line != '/') {
            g_warning("Garbage key: %s", line);
            continue;
        }
        if ((gsize)(val - buffer) + 1 > size) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("End of file reached when value was expected."));
            goto fail;
        }
        *val = '\0';
        val++;
        if (!gwy_strequal(val, "[") || !pos || *pos != '[') {
            gwy_debug("<%s>=<%s>", line, val);
            if (*val)
                gwy_container_set_string_by_name(data, line, g_strdup(val));
            else
                gwy_container_remove_by_name(data, line);
            continue;
        }

        g_assert(pos && *pos == '[');
        pos++;
        dfield = NULL;
        gwy_container_gis_object_by_name(data, line, &dfield);

        /* get datafield parameters from already read values, failing back
         * to values of original data field */
        key = g_strconcat(line, "/xres", NULL);
        if (gwy_container_gis_string_by_name(data, key, &s))
            xres = atoi(s);
        else if (dfield)
            xres = gwy_data_field_get_xres(dfield);
        else {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Missing data field width."));
            goto fail;
        }
        g_free(key);

        key = g_strconcat(line, "/yres", NULL);
        if (gwy_container_gis_string_by_name(data, key, &s))
            yres = atoi(s);
        else if (dfield)
            yres = gwy_data_field_get_yres(dfield);
        else {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Missing data field height."));
            goto fail;
        }
        g_free(key);

        key = g_strconcat(line, "/xreal", NULL);
        if (gwy_container_gis_string_by_name(data, key, &s))
            xreal = g_ascii_strtod(s, NULL);
        else if (dfield)
            xreal = gwy_data_field_get_xreal(dfield);
        else {
            g_warning("Missing real data field width.");
            xreal = 1.0;   /* 0 could cause troubles */
        }
        g_free(key);

        key = g_strconcat(line, "/yreal", NULL);
        if (gwy_container_gis_string_by_name(data, key, &s))
            yreal = g_ascii_strtod(s, NULL);
        else if (dfield)
            yreal = gwy_data_field_get_yreal(dfield);
        else {
            g_warning("Missing real data field height.");
            yreal = 1.0;   /* 0 could cause troubles */
        }
        g_free(key);

        if (!(xres > 0 && yres > 0 && xreal > 0 && yreal > 0)) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Data field dimensions are not positive numbers."));
            goto fail;
        }

        key = g_strconcat(line, "/unit-xy", NULL);
        if (gwy_container_gis_string_by_name(data, key, &s))
            uxy = gwy_si_unit_new((const gchar*)s);
        else if (dfield) {
            uxy = gwy_data_field_get_si_unit_xy(dfield);
            uxy = gwy_si_unit_duplicate(uxy);
        }
        else {
            g_warning("Missing lateral units.");
            uxy = gwy_si_unit_new("m");
        }
        g_free(key);

        key = g_strconcat(line, "/unit-z", NULL);
        if (gwy_container_gis_string_by_name(data, key, &s))
            uz = gwy_si_unit_new((const gchar*)s);
        else if (dfield) {
            uz = gwy_data_field_get_si_unit_z(dfield);
            uz = gwy_si_unit_duplicate(uz);
        }
        else {
            g_warning("Missing value units.");
            uz = gwy_si_unit_new("m");
        }
        g_free(key);

        key = g_strconcat(line, "/title", NULL);
        title = NULL;
        gwy_container_gis_string_by_name(data, key, (const guchar**)&title);
        /* We got the contained string but that would disappear. */
        title = g_strdup(title);
        g_free(key);

        n = xres*yres*sizeof(gdouble);
        if ((gsize)(pos - buffer) + n + 3 > size) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("End of file reached inside a data field."));
            goto fail;
        }
        dfield = GWY_DATA_FIELD(gwy_data_field_new(xres, yres, xreal, yreal,
                                                   FALSE));
        gwy_data_field_set_si_unit_xy(dfield, GWY_SI_UNIT(uxy));
        gwy_object_unref(uxy);
        gwy_data_field_set_si_unit_z(dfield, GWY_SI_UNIT(uz));
        gwy_object_unref(uz);
        d = gwy_data_field_get_data(dfield);
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
        memcpy(d, pos, n);
#else
        gwy_memcpy_byte_swap(pos, (guint8*)d,
                             sizeof(gdouble), xres*yres, sizeof(gdouble)-1);
#endif
        pos += n;
        val = gwy_str_next_line(&pos);
        if (!gwy_strequal(val, "]]")) {
            g_set_error(error, GWY_MODULE_FILE_ERROR,
                        GWY_MODULE_FILE_ERROR_DATA,
                        _("Missing end of data field marker."));
            gwy_object_unref(dfield);
            goto fail;
        }
        gwy_container_remove_by_prefix(data, line);
        gwy_container_set_object_by_name(data, line, dfield);
        g_object_unref(dfield);

        if (title) {
            key = g_strconcat(line, "/title", NULL);
            gwy_container_set_string_by_name(data, key, title);
            g_free(key);
        }
    }
    return data;

fail:
    gwy_container_remove_by_prefix(data, NULL);
    g_object_unref(data);
    return NULL;
}

/* Convert GLib-encoded file name to on-disk file name to be used with
 * NON-widechar libc functions.  In fact, we just don't know what methods
 * plug-ins may use to open files.  Why file names can't be just sequences
 * of bytes... */
static gchar*
decode_glib_encoded_filename(const gchar *filename)
{
#ifdef G_OS_WIN32
    return g_locale_from_utf8(filename, -1, NULL, NULL, NULL);
#else
    return g_strdup(filename);
#endif
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
