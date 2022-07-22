/*
 *  $Id: gwyddion.c 24555 2021-12-02 15:31:19Z yeti-dn $
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
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwyddion.h>
#include <libprocess/gwygrainvalue.h>
#include <libprocess/gwycalibration.h>
#include <libgwymodule/gwymoduleloader.h>
#include <libgwymodule/gwymodule-file.h>
#include <libgwydgets/gwydgets.h>
#include <app/gwyapp.h>
#include "gwyddion.h"
#include "mac_integration.h"
#include "release.h"

#ifdef G_OS_WIN32
#define LOG_TO_FILE_DEFAULT TRUE
#define LOG_TO_CONSOLE_DEFAULT FALSE
#include <windows.h>
#include <winreg.h>
#define gwyddion_key "Software\\Gwyddion\\2.0"
#else
#define LOG_TO_FILE_DEFAULT TRUE
#define LOG_TO_CONSOLE_DEFAULT TRUE
#endif

typedef enum {
    MODE_NORMAL,
    MODE_CHECK,
    MODE_IDENTIFY,
    MODE_CONVERT_TO_GWY,
} GwyAppMode;

typedef struct {
    gboolean no_splash;
    gboolean startup_time;
    gboolean disable_gl;
    gboolean log_to_file;
    gboolean log_to_console;
    GwyAppMode mode;
    GwyAppRemoteType remote;
    gchar *disabled_modules;
    gchar *convert_outfilename;
} GwyAppOptions;

static gboolean open_command_line_files         (gint n,
                                                 gchar **args);
static gboolean open_directory_at_startup       (gpointer user_data);
static void     block_modules                   (const gchar *modules_to_block);
static gboolean show_tip_at_startup             (gpointer user_data);
static gint     identify_command_line_files     (gint n,
                                                 gchar **args);
static gint     check_command_line_files        (gint n,
                                                 gchar **args);
static gint     convert_command_line_files      (gint n,
                                                 gchar **args,
                                                 const gchar *outname);
static void     print_help_and_exit             (void);
static void     print_version_and_exit          (void);
static void     extend_disabled_modules_arg     (gchar **modules_to_block,
                                                 const gchar *arg);
static void     process_preinit_options         (int *argc,
                                                 char ***argv,
                                                 GwyAppOptions *options);
static void     debug_time                      (GTimer *timer,
                                                 const gchar *task);
static void     setup_locale_from_win32_registry(void);
static void     warn_broken_settings_file       (GtkWidget *parent,
                                                 const gchar *settings_file,
                                                 const gchar *reason);
static void     check_broken_modules            (GtkWidget *parent);
static void     gwy_app_init                    (int *argc,
                                                 char ***argv,
                                                 gboolean is_gui);
static void     gwy_app_set_window_icon         (void);
static void     gwy_app_check_version           (void);

static GwyAppOptions app_options = {
    FALSE, FALSE, FALSE,
    LOG_TO_FILE_DEFAULT, LOG_TO_CONSOLE_DEFAULT,
    MODE_NORMAL, GWY_APP_REMOTE_DEFAULT,
    NULL, NULL,
};

# ifdef __MINGW64__
int _dowildcard = -1;  /* Enable wildcard expansion for Win64. */
# endif

int
main(int argc, char *argv[])
{
    GtkWidget *toolbox;
    gchar **module_dirs;
    gchar *settings_file, *recent_file_file = NULL, *accel_file = NULL;
    gboolean has_settings, settings_ok = FALSE;
    gboolean opening_files = FALSE, show_tips = FALSE;
    GwyContainer *settings;
    GError *settings_err = NULL;
    GTimer *timer;

    g_unsetenv("UBUNTU_MENUPROXY");
    gwy_threads_set_enabled(TRUE);
    timer = g_timer_new();

    process_preinit_options(&argc, &argv, &app_options);
    gwy_app_setup_logging((app_options.log_to_file ? GWY_APP_LOGGING_TO_FILE : 0)
                          | (app_options.log_to_console ? GWY_APP_LOGGING_TO_CONSOLE : 0));
    gwy_app_check_version();

    gwy_osx_init_handler(&argc);
    gwy_osx_set_locale();

    /* If we are given some files to open *and* we are not run from a terminal, we are almost surely run from some
     * kind of file association.  So behave as if --remote-new was the default because various ‘Open this with that’
     * selectors often allow selecting a program, but passing options is much more difficult.  Otherwise behave
     * normally, i.e. as if --new-instance was the default.  */
#ifdef HAVE_UNISTD_H
    if (app_options.remote == GWY_APP_REMOTE_DEFAULT) {
        if (isatty(fileno(stdin)) || isatty(fileno(stdout)) || isatty(fileno(stderr)))
            app_options.remote = GWY_APP_REMOTE_NONE;
    }
#endif
    if (app_options.remote == GWY_APP_REMOTE_DEFAULT) {
        if (argc < 2)
            app_options.remote = GWY_APP_REMOTE_NONE;
        else
            app_options.remote = GWY_APP_REMOTE_NEW;
    }

    /* TODO: handle failure */
    gwy_app_settings_create_config_dir(NULL);
    debug_time(timer, "init");
    setup_locale_from_win32_registry();
    if (app_options.mode == MODE_NORMAL) {
        gtk_init(&argc, &argv);
        debug_time(timer, "gtk_init()");
        gwy_remote_do(app_options.remote, argc - 1, argv + 1);
    }
    gwy_app_init(&argc, &argv, app_options.mode == MODE_NORMAL);
    debug_time(timer, "gwy_app_init()");

    settings_file = gwy_app_settings_get_settings_filename();
    has_settings = g_file_test(settings_file, G_FILE_TEST_IS_REGULAR);
    gwy_debug("Text settings file is `%s'. Do we have it: %s", settings_file, has_settings ? "TRUE" : "FALSE");

    gwy_app_splash_start(!app_options.no_splash && app_options.mode == MODE_NORMAL);
    debug_time(timer, "create splash");

    if (app_options.mode == MODE_NORMAL) {
        accel_file = g_build_filename(gwy_get_user_dir(), "ui", "accel_map", NULL);
        gtk_accel_map_load(accel_file);
        debug_time(timer, "load accel map");

        gwy_app_splash_set_message(_("Loading document history"));
        recent_file_file = gwy_app_settings_get_recent_file_list_filename();
        gwy_app_recent_file_list_load(recent_file_file);
        debug_time(timer, "load document history");

        gwy_app_splash_set_message_prefix(_("Registering "));
        gwy_app_splash_set_message(_("stock items"));
        gwy_stock_register_stock_items();
        debug_time(timer, "register stock items");
    }

    gwy_app_splash_set_message(_("color gradients"));
    gwy_resource_class_load(g_type_class_peek(GWY_TYPE_GRADIENT));
    gwy_app_splash_set_message(_("GL materials"));
    gwy_resource_class_load(g_type_class_peek(GWY_TYPE_GL_MATERIAL));
    gwy_app_splash_set_message(_("grain quantities"));
    gwy_resource_class_load(g_type_class_peek(GWY_TYPE_GRAIN_VALUE));
    gwy_app_splash_set_message(_("calibrations"));
    gwy_resource_class_load(g_type_class_peek(GWY_TYPE_CALIBRATION));
    gwy_app_splash_set_message_prefix(NULL);
    debug_time(timer, "load resources");

    gwy_app_splash_set_message(_("Loading settings"));
    if (has_settings)
        settings_ok = gwy_app_settings_load(settings_file, &settings_err);
    gwy_debug("Loading settings was: %s", settings_ok ? "OK" : "Not OK");
    settings = gwy_app_settings_get();
    debug_time(timer, "load settings");

    /* Modules load pretty fast with bundling.  Most time is taken by:
     * 1) pygwy, but only if it registers some Python modules; when it is no-op it is fast, so you only pay the price
     *    when you get the benefits
     * 2) a few external libraries that are noticeably slow to init, in particular cfitsio (we do not call any its
     *    function during registration, so it is its internal init)
     * Running gwyddion --disable-modules=pygwy,fitsfile can take module registration down to ~6 ms. */
    gwy_app_splash_set_message(_("Registering modules"));
    if (app_options.mode != MODE_NORMAL)
        extend_disabled_modules_arg(&app_options.disabled_modules, "rawfile");
    block_modules(app_options.disabled_modules);
    GWY_FREE(app_options.disabled_modules);

    module_dirs = gwy_app_settings_get_module_dirs();
    gwy_module_register_modules((const gchar**)module_dirs);
    /* The Python initialisation overrides SIGINT and Gwyddion can no longer be terminated with Ctrl-C.  Fix it. */
    signal(SIGINT, SIG_DFL);
    /* TODO: The Python initialisation also overrides where the warnings go. Restore the handlers. */
    debug_time(timer, "register modules");

    /* Destroy splash before creating UI.  Opposite order of actions can apparently lead to strange errors. */
    gwy_app_splash_finish();
    debug_time(timer, "destroy splash");

    if (app_options.mode == MODE_IDENTIFY) {
        gint nfailures = identify_command_line_files(argc - 1, argv + 1);
        debug_time(timer, "identify files");
        return !!nfailures;
    }
    if (app_options.mode == MODE_CHECK) {
        gint nfailures = check_command_line_files(argc - 1, argv + 1);
        debug_time(timer, "check files");
        return !!nfailures;
    }
    if (app_options.mode == MODE_CONVERT_TO_GWY) {
        gint nfailures = convert_command_line_files(argc - 1, argv + 1, app_options.convert_outfilename);
        debug_time(timer, "convert files");
        return !!nfailures;
    }

    /* Toolbox creation is one of the most time-consuming parts of startup.
     *
     * Most of the time, about 2/3, is taken by show-all on the toolbox widget (this is likely also theme-dependent,
     * etc.).  There may not be much we can do to speed it up -- it is the price of having a GUI at all. */
    toolbox = gwy_app_toolbox_window_create();
    debug_time(timer, "create toolbox");
    gwy_app_data_browser_restore();
    debug_time(timer, "init data-browser");
    /* A dirty trick, it constructs the recent files menu as a side effect. */
    gwy_app_recent_file_list_update(NULL, NULL, NULL, 0);
    debug_time(timer, "create recent files menu");

    /* Win32 does not give programs a reasonable physical cwd.  So try to set something reasonable here.  Do it before
     * opening files from arguments because that can set the directory. */
#ifdef G_OS_WIN32
    {
        const gchar *cwd;

        if ((cwd = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS)) && g_file_test(cwd, G_FILE_TEST_IS_DIR))
            gwy_app_set_current_directory(cwd);
        else if ((cwd = g_get_home_dir()) && g_file_test(cwd, G_FILE_TEST_IS_DIR))
            gwy_app_set_current_directory(cwd);
        else if (g_file_test("c:\\", G_FILE_TEST_IS_DIR))
            gwy_app_set_current_directory("c:\\");
    }
#endif

    if (has_settings && !settings_ok) {
        if (!(settings_err->domain == GWY_APP_SETTINGS_ERROR && settings_err->code == GWY_APP_SETTINGS_ERROR_EMPTY))
            warn_broken_settings_file(toolbox, settings_file, settings_err->message);
        g_clear_error(&settings_err);
    }

    check_broken_modules(toolbox);

    /* Move focus to toolbox */
    gtk_window_present(GTK_WINDOW(toolbox));
    debug_time(timer, "show toolbox");

    opening_files |= open_command_line_files(argc - 1, argv + 1);
    opening_files |= gwy_osx_open_files();
    debug_time(timer, "open commandline files");

    g_timer_destroy(timer);
    debug_time(NULL, "STARTUP");

    gwy_container_gis_boolean_by_name(settings, "/app/tips/show-at-startup", &show_tips);
    if (show_tips && !opening_files)
        g_idle_add(show_tip_at_startup, NULL);

    gtk_main();

    gwy_osx_remove_handler();

    timer = g_timer_new();
    /* TODO: handle failure */
    if (settings_ok || !has_settings)
        gwy_app_settings_save(settings_file, NULL);
    gtk_accel_map_save(accel_file);
    debug_time(timer, "save settings");
    gwy_app_recent_file_list_save(recent_file_file);
    debug_time(timer, "save document history");
    gwy_app_process_func_save_use();
    debug_time(timer, "save funcuse");
    gwy_app_settings_free();
    /*gwy_resource_classes_finalize();*/
    gwy_app_recent_file_list_free();
    /* EXIT-CLEAN-UP */
    g_free(recent_file_file);
    g_free(settings_file);
    g_free(accel_file);
    g_strfreev(module_dirs);
    debug_time(timer, "destroy resources");
    g_timer_destroy(timer);
    debug_time(NULL, "SHUTDOWN");

    return 0;
}

/* There are things we want to set up before GTK+ init. */
static void
process_preinit_options(int *argc, char ***argv, GwyAppOptions *options)
{
    gboolean ignore, handled;
    const gchar *arg;
    int i, j;

    ignore = FALSE;
    for (i = j = 1; i < *argc; i++) {
        if (gwy_strequal((*argv)[i], "--"))
            ignore = TRUE;

        (*argv)[j] = (*argv)[i];
        if (ignore) {
            j++;
            continue;
        }
        arg = (*argv)[i];
        handled = TRUE;
        if (gwy_stramong(arg, "--help", "-h", NULL))
            print_help_and_exit();
        if (gwy_stramong(arg, "--version", "-v", NULL))
            print_version_and_exit();
        else if (gwy_strequal(arg, "--no-splash"))
            options->no_splash = TRUE;
        else if (gwy_strequal(arg, "--remote-existing")) {
            options->mode = MODE_NORMAL;
            options->remote = GWY_APP_REMOTE_EXISTING;
        }
        else if (gwy_strequal(arg, "--remote-new")) {
            options->mode = MODE_NORMAL;
            options->remote = GWY_APP_REMOTE_NEW;
        }
        else if (gwy_strequal(arg, "--remote-query")) {
            options->mode = MODE_NORMAL;
            options->remote = GWY_APP_REMOTE_QUERY;
        }
        else if (gwy_strequal(arg, "--new-instance")) {
            options->mode = MODE_NORMAL;
            options->remote = GWY_APP_REMOTE_NONE;
        }
        else if (gwy_strequal(arg, "--debug-objects")) {
            /* Silently ignore it. */
        }
        else if (gwy_strequal(arg, "--startup-time"))
            options->startup_time = TRUE;
        else if (gwy_strequal(arg, "--log-to-file"))
            options->log_to_file = TRUE;
        else if (gwy_strequal(arg, "--no-log-to-file"))
            options->log_to_file = FALSE;
        else if (gwy_strequal(arg, "--log-to-console"))
            options->log_to_console = TRUE;
        else if (gwy_strequal(arg, "--no-log-to-console"))
            options->log_to_console = FALSE;
        else if (gwy_strequal(arg, "--disable-gl"))
            options->disable_gl = TRUE;
        else if (gwy_strequal(arg, "--check"))
            options->mode = MODE_CHECK;
        else if (gwy_strequal(arg, "--identify"))
            options->mode = MODE_IDENTIFY;
        else if (g_str_has_prefix(arg, "--convert-to-gwy=")) {
            options->mode = MODE_CONVERT_TO_GWY;
            gwy_assign_string(&options->convert_outfilename, strchr(arg, '=') + 1);
        }
        else if (g_str_has_prefix(arg, "--disable-modules="))
            extend_disabled_modules_arg(&options->disabled_modules, strchr(arg, '=') + 1);
        else
            handled = FALSE;

        if (!handled)
            j++;
    }
    (*argv)[j] = NULL;
    *argc = j;
}

static void
print_help_and_exit(void)
{
    puts("Usage: gwyddion [OPTIONS...] FILES...\n"
         "An SPM data visualization and analysis tool, written with Gtk+.\n");
    puts("Interaction with other instances:\n"
         "     --remote-query         Check if a Gwyddion instance is already running.\n"
         "     --remote-new           Load FILES to a running instance or run a new one.\n"
         "     --remote-existing      Load FILES to a running instance or fail.\n"
         "     --new-instance         Run a new instance, ignoring any already running.\n"
         "Any of these options also implicitly selects the normal GUI mode.\n");
    puts("Non-GUI operations:\n"
         "     --identify             Identify and print the type of SPM data FILES.\n"
         "     --check                Check FILES, print problems and terminate.\n"
         "     --convert-to-gwy=OUTFILE.gwy\n"
         "                            Read FILES, merge them and write a GWY file.\n"
         " -h, --help                 Print this help and terminate.\n"
         " -v, --version              Print version info and terminate.\n");
    puts("Logging:\n"
         "     --log-to-file          Write messages to file set in GWYDDION_LOGFILE.\n"
         "     --no-log-to-file       Do not write messages to any file.\n"
         "     --log-to-console       Print messages to console.\n"
         "     --no-log-to-console    Do not print messages to console.\n");
    puts("Miscellaneous options:\n"
         "     --no-splash            Don't show splash screen.\n"
         "     --disable-gl           Disable OpenGL, including any availability checks.\n"
         "     --disable-modules=MODNAME1,MODNAME2,...\n"
         "                            Prevent registration of given modules.\n"
         "     --startup-time         Measure time of startup tasks.\n");
    puts("Gtk+ and Gdk options:\n"
         "     --display=DISPLAY      Set X display to use.\n"
         "     --screen=SCREEN        Set X screen to use.\n"
         "     --sync                 Make X calls synchronous.\n"
         "     --name=NAME            Set program name as used by the window manager.\n"
         "     --class=CLASS          Set program class as used by the window manager.\n"
         "     --gtk-module=MODULE    Load an additional Gtk module MODULE.\n"
         "They may be other Gtk+, Gdk, and GtkGLExt options, depending on platform, on\n"
         "how it was compiled, and on loaded modules.  Please see Gtk+ documentation.\n");
    puts("Please report bugs to <" PACKAGE_BUGREPORT ">.");
    exit(0);
}

static void
print_version_and_exit(void)
{
    const gchar *verextra = "";
    gchar *s;

    s = gwy_version_date_info();
    if (!RELEASEDATE && strlen(GWY_VERSION_STRING) < 9)
        verextra = "+SVN";

    printf("%s %s%s (%s)\n", PACKAGE_NAME, GWY_VERSION_STRING, verextra, s);
    g_free(s);
    exit(0);
}

static void
extend_disabled_modules_arg(gchar **modules_to_block, const gchar *arg)
{
    if (!strlen(arg))
        return;

    if (*modules_to_block) {
        gchar *s = g_strconcat(*modules_to_block, ",", arg, NULL);
        g_free(*modules_to_block);
        *modules_to_block = s;
    }
    else
        *modules_to_block = g_strdup(arg);
}

static void
debug_time(GTimer *timer, const gchar *task)
{
    static gdouble total = 0.0;

    gdouble t;

    if (!app_options.startup_time || app_options.remote > GWY_APP_REMOTE_NONE)
        return;

    if (timer) {
        total += t = g_timer_elapsed(timer, NULL);
        printf("%24s: %5.1f ms\n", task, 1000.0*t);
        g_timer_start(timer);
    }
    else {
        printf("%24s: %5.1f ms\n", task, 1000.0*total);
        total = 0.0;
    }
}

static void
warn_broken_settings_file(GtkWidget *parent,
                          const gchar *settings_file,
                          const gchar *reason)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                                    _("Could not read settings."));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             _("Settings file `%s' cannot be read: %s\n\n"
                                               "To prevent loss of saved settings no attempt to update it will "
                                               "be made until it is repaired or removed."),
                                             settings_file, reason);
    /* parent is usually in a screen corner, centering on it looks ugly */
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
count_failures(gpointer mod_fail_info, gpointer user_data)
{
    GwyModuleFailureInfo *finfo = (GwyModuleFailureInfo*)mod_fail_info;
    guint *n = (guint*)user_data;

    /* Ignore user's modules. */
    if (!g_str_has_prefix(finfo->filename, gwy_get_user_dir()))
        (*n)++;
}

static void
check_broken_modules(GtkWidget *parent)
{
    GtkWidget *dialog;
    gchar *moduledir;
    guint n = 0;

    gwy_module_failure_foreach(count_failures, &n);
    /* Usually, the number should either be less than 3 or huge. */
    if (n < 8)
        return;

    dialog = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                                    _("Many modules (%u) failed to register."), n);
    moduledir = gwy_find_self_dir("modules");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             _("Most likely Gwyddion was not upgraded correctly.  "
                                               "Instead, one installation was just overwritten with another, "
                                               "and now it is a mess.\n\n"
                                               "Please remove completely the module directory\n\n"
                                               "%s\n\n"
                                               "and reinstall Gwyddion.\n\n"
                                               "See Info → Module Browser for specific errors."),
                                             moduledir);
    g_free(moduledir);
    /* parent is usually in a screen corner, centering on it looks ugly */
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    gtk_window_present(GTK_WINDOW(dialog));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
setup_locale_from_win32_registry(void)
{
#ifdef G_OS_WIN32
    gchar locale[65];
    DWORD size = sizeof(locale)-1;
    HKEY reg_key;

    gwy_clear(locale, sizeof(locale));
    if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT(gwyddion_key), 0, KEY_READ, &reg_key) == ERROR_SUCCESS) {
        if (RegQueryValueEx(reg_key, TEXT("Locale"), NULL, NULL, locale, &size) == ERROR_SUCCESS) {
            g_setenv("LANG", locale, TRUE);
            RegCloseKey(reg_key);
            return;
        }
        RegCloseKey(reg_key);
    }
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT(gwyddion_key), 0, KEY_READ, &reg_key) == ERROR_SUCCESS) {
        if (RegQueryValueEx(reg_key, TEXT("Locale"), NULL, NULL, locale, &size) == ERROR_SUCCESS)
            g_setenv("LANG", locale, TRUE);
        RegCloseKey(reg_key);
    }
#endif
}

static gchar*
fix_win32_commandline_arg(gchar *p)
{
#ifdef G_OS_WIN32
    int buflen, n = strlen(p);
    gchar *q;
    wchar_t *w;

    if (!(buflen = MultiByteToWideChar(CP_ACP, 0, p, n, NULL, 0)))
        return g_strdup(p);
    w = g_new(wchar_t, buflen+1);
    if (!MultiByteToWideChar(CP_ACP, 0, p, n, w, buflen+1)) {
        /* XXX: This should not really happen. */
        g_free(w);
        return g_strdup(p);
    }
    w[buflen] = 0;
    if (!(q = g_utf16_to_utf8((const gunichar2*)w, buflen, NULL, NULL, NULL))) {
        g_free(w);
        return g_strdup(p);
    }
    g_free(w);
    return q;
#else
    return g_strdup(p);
#endif
}

static gboolean
open_command_line_files(gint n, gchar **args)
{
    gchar *dir_to_open = NULL;
    gchar **p;
    gchar *cwd, *filename, *q;
    gboolean opening_anything = FALSE;

    cwd = g_get_current_dir();
#ifdef DEBUG
    gwy_debug("current dir: <%s>", g_strescape(cwd, ""));
#endif
    for (p = args; n; p++, n--) {
        opening_anything = TRUE;

        q = fix_win32_commandline_arg(*p);
#ifdef DEBUG
        gwy_debug("argv: <%s>", g_strescape(*p, ""));
        gwy_debug("converted: <%s>", g_strescape(q, ""));
#endif
        if (g_path_is_absolute(q))
            filename = g_strdup(q);
        else
            filename = g_build_filename(cwd, q, NULL);

        if (g_file_test(filename, G_FILE_TEST_IS_DIR)) {
            /* Show the file open dialogue for the last directory given. */
            if (dir_to_open)
                g_free(dir_to_open);
            dir_to_open = g_strdup(filename);
        }
        else {
#ifdef DEBUG
            gwy_debug("calling gwy_app_file_load() with <%s>", g_strescape(filename, ""));
#endif
            gwy_app_file_load(NULL, filename, NULL);
        }
        g_free(q);
        g_free(filename);
    }
    g_free(cwd);

    if (dir_to_open)
        g_idle_add(open_directory_at_startup, dir_to_open);

    return opening_anything;
}

static gboolean
open_directory_at_startup(gpointer user_data)
{
    gchar *dir_to_open = (gchar*)user_data;

    gwy_app_set_current_directory(dir_to_open);
    gwy_app_file_open();
    g_free(dir_to_open);

    return FALSE;
}

static gint
identify_command_line_files(gint n, gchar **args)
{
    gint i, nfailures;

    for (i = nfailures = 0; i < n; i++) {
        const gchar *filename = args[i], *name;
        gint score;

        name = gwy_file_detect_with_score(filename, FALSE, GWY_FILE_OPERATION_LOAD, &score);
        if (name)
            g_print("%s: %s [%s, %d]\n", filename, gwy_file_func_get_description(name), name, score);
        else {
            g_print("%s: %s\n", filename, _("Unknown"));
            nfailures++;
        }
    }

    return nfailures;
}

static gint
check_command_line_files(gint n, gchar **args)
{
    gint i, nfailures;

    for (i = nfailures = 0; i < n; i++) {
        const gchar *filename = args[i];
        const gchar *name = NULL;
        GwyContainer *data;
        GError *error = NULL;
        GSList *failures, *f;

        if (!(data = gwy_file_load(filename, GWY_RUN_NONINTERACTIVE, &error))) {
            if (!error)
                g_printerr("%s: Loader failed to report error properly!\n", filename);
            else {
                g_printerr("%s: %s\n", filename, error->message);
                g_clear_error(&error);
            }
            continue;
        }

        failures = gwy_data_validate(data, GWY_DATA_VALIDATE_ALL);
        gwy_file_get_data_info(data, &name, NULL);
        g_assert(name);
        for (f = failures; f; f = g_slist_next(f)) {
            GwyDataValidationFailure *failure = (GwyDataValidationFailure*)f->data;
            g_printerr("%s: %s, %s: %s",
                       filename, name, g_quark_to_string(failure->key), gwy_data_error_desrcibe(failure->error));
            if (failure->details)
                g_printerr(" (%s)", failure->details);
            g_printerr("\n");

            nfailures++;
        }
        gwy_data_validation_failure_list_free(failures);
    }

    return nfailures;
}

static gboolean
select_one(gint* (*get_ids)(GwyContainer*),
           void (*select_object)(GwyContainer*, gint),
           GwyContainer *data)
{
    gint *ids;
    gint id;

    ids = get_ids(data);
    id = ids[0];
    g_free(ids);
    if (id < 0)
        return FALSE;

    select_object(data, id);
    return TRUE;
}

static void
select_anything(GwyContainer *data)
{
    if (!select_one(gwy_app_data_browser_get_data_ids, gwy_app_data_browser_select_data_field, data)
        && !select_one(gwy_app_data_browser_get_graph_ids, gwy_app_data_browser_select_graph_model, data)
        && !select_one(gwy_app_data_browser_get_volume_ids, gwy_app_data_browser_select_brick, data)
        && !select_one(gwy_app_data_browser_get_xyz_ids, gwy_app_data_browser_select_surface, data)
        && !select_one(gwy_app_data_browser_get_curve_map_ids, gwy_app_data_browser_select_lawn, data)) {
        g_warning("Cannot find any data object to select in a file.");
    }
}

static gint
convert_command_line_files(gint n, gchar **args, const gchar *outname)
{
    GwyContainer *maindata = NULL;
    GError *error = NULL;
    gint i, nfailures;

    gwy_app_data_browser_set_gui_enabled(FALSE);

    for (i = nfailures = 0; i < n; i++) {
        GwyContainer *data;
        const gchar *filename = args[i];

        if (!(data = gwy_file_load(filename, GWY_RUN_NONINTERACTIVE, &error))) {
            if (!error)
                g_printerr("%s: Loader failed to report error properly!\n", filename);
            else {
                g_printerr("%s: %s\n", filename, error->message);
                g_clear_error(&error);
            }
            nfailures++;
            continue;
        }

        if (maindata) {
            gwy_debug("merge %s (i=%d, nfailures=%d)", filename, i, nfailures);
            if (i - nfailures == 1)
                select_anything(maindata);
            gwy_app_data_browser_merge(data);
            g_object_unref(data);
        }
        else {
            gwy_debug("add %s (i=%d, nfailures=%d)", filename, i, nfailures);
            maindata = data;
            gwy_app_data_browser_add(maindata);
        }

    }

    if (!maindata) {
        g_printerr("Cannot write %s: No data.\n", outname);
        return nfailures ? nfailures : 1;
    }
    if (!gwy_file_func_run_save("gwyfile", maindata, outname, GWY_RUN_NONINTERACTIVE, &error)) {
        g_printerr("Cannot write %s: %s\n", outname, error->message);
        g_clear_error(&error);
        return 1;
    }
    g_object_unref(maindata);

    return 0;
}

static void
block_modules(const gchar *modules_to_block)
{
    gchar **modlist;
    guint i;

    if (!modules_to_block)
        return;

    modlist = g_strsplit(modules_to_block, ",", 0);
    for (i = 0; modlist[i]; i++)
        gwy_module_disable_registration(modlist[i]);
    g_strfreev(modlist);
}

static gboolean
show_tip_at_startup(G_GNUC_UNUSED gpointer user_data)
{
    gwy_app_tip_of_the_day();
    return FALSE;
}

/**
 * gwy_app_init:
 * @argc: Address of the argc parameter of main(). Passed to gwy_app_gl_init().
 * @argv: Address of the argv parameter of main(). Passed to gwy_app_gl_init().
 *
 * Initializes all Gwyddion data types, i.e. types that may appear in serialized data. GObject has to know about them
 * when g_type_from_name() is called.
 *
 * It registeres stock items, initializes tooltip class resources, sets application icon, sets Gwyddion specific
 * widget resources.
 *
 * If NLS is compiled in, it sets it up and binds text domains.
 *
 * If OpenGL is compiled in, it checks whether it's really available (calling gtk_gl_init_check() and
 * gwy_widgets_gl_init()).
 **/
static void
gwy_app_init(int *argc, char ***argv, gboolean is_gui)
{
    gwy_widgets_type_init();
    /* Dump core on Critical errors in development versions. */
    if (RELEASEDATE == 0 || sizeof(GWY_VERSION_STRING) > 9)
        g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);

    if (!is_gui)
        return;

    g_set_application_name(PACKAGE_NAME);
    if (!gwy_app_gl_disabled())
        gwy_app_gl_init(argc, argv);
    /* XXX: These references are never released. */
    gwy_data_window_class_set_tooltips(gwy_app_get_tooltips());
    gwy_3d_window_class_set_tooltips(gwy_app_get_tooltips());
    gwy_graph_window_class_set_tooltips(gwy_app_get_tooltips());

    gwy_app_set_window_icon();
    gwy_app_init_widget_styles();
    gwy_app_init_i18n();
}

static void
gwy_app_set_window_icon(void)
{
    gchar *filename, *p;
    GError *err = NULL;

    p = gwy_find_self_dir("pixmaps");
    filename = g_build_filename(p, "gwyddion.ico", NULL);
    gtk_window_set_default_icon_from_file(filename, &err);
    if (err) {
        g_warning("Cannot load window icon: %s", err->message);
        g_clear_error(&err);
    }
    g_free(filename);
    g_free(p);
}

static void
gwy_app_check_version(void)
{
    if (!gwy_strequal(GWY_VERSION_STRING, gwy_version_string()))
        g_warning("Application and library versions do not match: %s vs. %s", GWY_VERSION_STRING, gwy_version_string());
}

gboolean
gwy_app_gl_disabled(void)
{
    return app_options.disable_gl;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
