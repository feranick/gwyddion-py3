/*
 *  $Id: graph_export_ascii.c 24405 2021-10-22 13:56:42Z yeti-dn $
 *  Copyright (C) 2006-2021 David Necas (Yeti), Petr Klapetek.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

enum {
    PARAM_STYLE,
    PARAM_UNITS,
    PARAM_LABELS,
    PARAM_METADATA,
    PARAM_POSIX,
    PARAM_MERGED_X,
};

typedef struct {
    GwyParams *params;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             export              (GwyGraph *graph);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             execute             (GwyGraph *graph,
                                             ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Exports graph data to text files."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, graph_export_ascii)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_export_ascii",
                            (GwyGraphFunc)&export,
                            N_("/_Export/_Text..."),
                            GWY_STOCK_GRAPH_EXPORT_ASCII,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Export graph data to a text file"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum styles[] = {
        { N_("Plain text"),             GWY_GRAPH_MODEL_EXPORT_ASCII_PLAIN,   },
        { N_("Gnuplot friendly"),       GWY_GRAPH_MODEL_EXPORT_ASCII_GNUPLOT, },
        { N_("Comma separated values"), GWY_GRAPH_MODEL_EXPORT_ASCII_CSV,     },
        { N_("Origin friendly"),        GWY_GRAPH_MODEL_EXPORT_ASCII_ORIGIN,  },
        { N_("Igor Pro text wave"),     GWY_GRAPH_MODEL_EXPORT_ASCII_IGORPRO, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_graph_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_STYLE, "style", _("Style"),
                              styles, G_N_ELEMENTS(styles), GWY_GRAPH_MODEL_EXPORT_ASCII_PLAIN);
    gwy_param_def_add_boolean(paramdef, PARAM_UNITS, "units", _("Export _units"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_LABELS, "labels", _("Export _labels"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_METADATA, "metadata", _("Export _metadata"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_POSIX, "posix", _("POSIX _number format"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_MERGED_X, "merged_x", _("Single _merged abscissa"), FALSE);
    return paramdef;
}

static void
export(GwyGraph *graph)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;

    gwy_clear(&args, 1);
    args.params = gwy_params_new_from_settings(define_module_params());
    outcome = run_gui(&args);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    execute(graph, &args);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gui.args = args;
    gui.dialog = gwy_dialog_new(_("Export Text"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_STYLE);
    gwy_param_table_append_checkbox(table, PARAM_POSIX);
    gwy_param_table_append_checkbox(table, PARAM_MERGED_X);
    gwy_param_table_append_checkbox(table, PARAM_LABELS);
    gwy_param_table_append_checkbox(table, PARAM_UNITS);
    gwy_param_table_append_checkbox(table, PARAM_METADATA);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);

    return gwy_dialog_run(dialog);
}

static void
execute(GwyGraph *graph, ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyGraphModelExportStyle style = gwy_params_get_enum(params, PARAM_STYLE);
    gboolean posix = gwy_params_get_boolean(params, PARAM_POSIX);
    gboolean merged_x = gwy_params_get_boolean(params, PARAM_MERGED_X);
    GString *str;

    if (posix)
        style |= GWY_GRAPH_MODEL_EXPORT_ASCII_POSIX;
    if (merged_x)
        style |= GWY_GRAPH_MODEL_EXPORT_ASCII_MERGED;

    str = gwy_graph_model_export_ascii(gwy_graph_get_model(graph),
                                       gwy_params_get_boolean(params, PARAM_UNITS),
                                       gwy_params_get_boolean(params, PARAM_LABELS),
                                       gwy_params_get_boolean(params, PARAM_METADATA),
                                       style,
                                       NULL);
    gwy_save_auxiliary_data(_("Export to Text File"), NULL, str->len, str->str);
    g_string_free(str, TRUE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
