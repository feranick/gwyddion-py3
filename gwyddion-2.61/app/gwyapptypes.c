/* This is a GENERATED file. */
/* This file is generated by glib-mkenums, do not modify it. This code is licensed under the same license as the containing project. Note that it links to GLib, so must comply with the LGPL linking clauses. */

#include "gwyapptypes.h"
#include "./data-browser.h"
GType
gwy_app_what_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_APP_CONTAINER, "GWY_APP_CONTAINER", "container" },
            { GWY_APP_DATA_VIEW, "GWY_APP_DATA_VIEW", "data-view" },
            { GWY_APP_GRAPH, "GWY_APP_GRAPH", "graph" },
            { GWY_APP_DATA_FIELD, "GWY_APP_DATA_FIELD", "data-field" },
            { GWY_APP_DATA_FIELD_KEY, "GWY_APP_DATA_FIELD_KEY", "data-field-key" },
            { GWY_APP_DATA_FIELD_ID, "GWY_APP_DATA_FIELD_ID", "data-field-id" },
            { GWY_APP_MASK_FIELD, "GWY_APP_MASK_FIELD", "mask-field" },
            { GWY_APP_MASK_FIELD_KEY, "GWY_APP_MASK_FIELD_KEY", "mask-field-key" },
            { GWY_APP_SHOW_FIELD, "GWY_APP_SHOW_FIELD", "show-field" },
            { GWY_APP_SHOW_FIELD_KEY, "GWY_APP_SHOW_FIELD_KEY", "show-field-key" },
            { GWY_APP_GRAPH_MODEL, "GWY_APP_GRAPH_MODEL", "graph-model" },
            { GWY_APP_GRAPH_MODEL_KEY, "GWY_APP_GRAPH_MODEL_KEY", "graph-model-key" },
            { GWY_APP_GRAPH_MODEL_ID, "GWY_APP_GRAPH_MODEL_ID", "graph-model-id" },
            { GWY_APP_SPECTRA, "GWY_APP_SPECTRA", "spectra" },
            { GWY_APP_SPECTRA_KEY, "GWY_APP_SPECTRA_KEY", "spectra-key" },
            { GWY_APP_SPECTRA_ID, "GWY_APP_SPECTRA_ID", "spectra-id" },
            { GWY_APP_VOLUME_VIEW, "GWY_APP_VOLUME_VIEW", "volume-view" },
            { GWY_APP_BRICK, "GWY_APP_BRICK", "brick" },
            { GWY_APP_BRICK_KEY, "GWY_APP_BRICK_KEY", "brick-key" },
            { GWY_APP_BRICK_ID, "GWY_APP_BRICK_ID", "brick-id" },
            { GWY_APP_CONTAINER_ID, "GWY_APP_CONTAINER_ID", "container-id" },
            { GWY_APP_XYZ_VIEW, "GWY_APP_XYZ_VIEW", "xyz-view" },
            { GWY_APP_SURFACE, "GWY_APP_SURFACE", "surface" },
            { GWY_APP_SURFACE_KEY, "GWY_APP_SURFACE_KEY", "surface-key" },
            { GWY_APP_SURFACE_ID, "GWY_APP_SURFACE_ID", "surface-id" },
            { GWY_APP_PAGE, "GWY_APP_PAGE", "page" },
            { GWY_APP_LAWN, "GWY_APP_LAWN", "lawn" },
            { GWY_APP_LAWN_KEY, "GWY_APP_LAWN_KEY", "lawn-key" },
            { GWY_APP_LAWN_ID, "GWY_APP_LAWN_ID", "lawn-id" },
            { GWY_APP_CURVE_MAP_VIEW, "GWY_APP_CURVE_MAP_VIEW", "curve-map-view" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyAppWhat", values);
    }
    return etype;
}
GType
gwy_app_page_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_PAGE_NOPAGE, "GWY_PAGE_NOPAGE", "nopage" },
            { GWY_PAGE_CHANNELS, "GWY_PAGE_CHANNELS", "channels" },
            { GWY_PAGE_GRAPHS, "GWY_PAGE_GRAPHS", "graphs" },
            { GWY_PAGE_SPECTRA, "GWY_PAGE_SPECTRA", "spectra" },
            { GWY_PAGE_VOLUMES, "GWY_PAGE_VOLUMES", "volumes" },
            { GWY_PAGE_XYZS, "GWY_PAGE_XYZS", "xyzs" },
            { GWY_PAGE_CURVE_MAPS, "GWY_PAGE_CURVE_MAPS", "curve-maps" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyAppPage", values);
    }
    return etype;
}
GType
gwy_data_item_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_DATA_ITEM_GRADIENT, "GWY_DATA_ITEM_GRADIENT", "gradient" },
            { GWY_DATA_ITEM_PALETTE, "GWY_DATA_ITEM_PALETTE", "palette" },
            { GWY_DATA_ITEM_MASK_COLOR, "GWY_DATA_ITEM_MASK_COLOR", "mask-color" },
            { GWY_DATA_ITEM_TITLE, "GWY_DATA_ITEM_TITLE", "title" },
            { GWY_DATA_ITEM_RANGE, "GWY_DATA_ITEM_RANGE", "range" },
            { GWY_DATA_ITEM_RANGE_TYPE, "GWY_DATA_ITEM_RANGE_TYPE", "range-type" },
            { GWY_DATA_ITEM_REAL_SQUARE, "GWY_DATA_ITEM_REAL_SQUARE", "real-square" },
            { GWY_DATA_ITEM_SELECTIONS, "GWY_DATA_ITEM_SELECTIONS", "selections" },
            { GWY_DATA_ITEM_META, "GWY_DATA_ITEM_META", "meta" },
            { GWY_DATA_ITEM_CALDATA, "GWY_DATA_ITEM_CALDATA", "caldata" },
            { GWY_DATA_ITEM_PREVIEW, "GWY_DATA_ITEM_PREVIEW", "preview" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyDataItem", values);
    }
    return etype;
}
GType
gwy_visibility_reset_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_VISIBILITY_RESET_DEFAULT, "GWY_VISIBILITY_RESET_DEFAULT", "default" },
            { GWY_VISIBILITY_RESET_RESTORE, "GWY_VISIBILITY_RESET_RESTORE", "restore" },
            { GWY_VISIBILITY_RESET_SHOW_ALL, "GWY_VISIBILITY_RESET_SHOW_ALL", "show-all" },
            { GWY_VISIBILITY_RESET_HIDE_ALL, "GWY_VISIBILITY_RESET_HIDE_ALL", "hide-all" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyVisibilityResetType", values);
    }
    return etype;
}
GType
gwy_data_watch_event_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_DATA_WATCH_EVENT_ADDED, "GWY_DATA_WATCH_EVENT_ADDED", "added" },
            { GWY_DATA_WATCH_EVENT_CHANGED, "GWY_DATA_WATCH_EVENT_CHANGED", "changed" },
            { GWY_DATA_WATCH_EVENT_REMOVED, "GWY_DATA_WATCH_EVENT_REMOVED", "removed" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyDataWatchEventType", values);
    }
    return etype;
}
#include "./dialog.h"
GType
gwy_dialog_outcome_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_DIALOG_CANCEL, "GWY_DIALOG_CANCEL", "cancel" },
            { GWY_DIALOG_PROCEED, "GWY_DIALOG_PROCEED", "proceed" },
            { GWY_DIALOG_HAVE_RESULT, "GWY_DIALOG_HAVE_RESULT", "have-result" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyDialogOutcome", values);
    }
    return etype;
}
GType
gwy_preview_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_PREVIEW_NONE, "GWY_PREVIEW_NONE", "none" },
            { GWY_PREVIEW_IMMEDIATE, "GWY_PREVIEW_IMMEDIATE", "immediate" },
            { GWY_PREVIEW_UPON_REQUEST, "GWY_PREVIEW_UPON_REQUEST", "upon-request" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyPreviewType", values);
    }
    return etype;
}
GType
gwy_response_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_RESPONSE_RESET, "GWY_RESPONSE_RESET", "reset" },
            { GWY_RESPONSE_UPDATE, "GWY_RESPONSE_UPDATE", "update" },
            { GWY_RESPONSE_CLEAR, "GWY_RESPONSE_CLEAR", "clear" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyResponseType", values);
    }
    return etype;
}
#include "./gwymoduleutils-file.h"
GType
gwy_text_header_error_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_TEXT_HEADER_ERROR_SECTION_NAME, "GWY_TEXT_HEADER_ERROR_SECTION_NAME", "section-name" },
            { GWY_TEXT_HEADER_ERROR_SECTION_END, "GWY_TEXT_HEADER_ERROR_SECTION_END", "section-end" },
            { GWY_TEXT_HEADER_ERROR_SECTION_START, "GWY_TEXT_HEADER_ERROR_SECTION_START", "section-start" },
            { GWY_TEXT_HEADER_ERROR_PREFIX, "GWY_TEXT_HEADER_ERROR_PREFIX", "prefix" },
            { GWY_TEXT_HEADER_ERROR_GARBAGE, "GWY_TEXT_HEADER_ERROR_GARBAGE", "garbage" },
            { GWY_TEXT_HEADER_ERROR_KEY, "GWY_TEXT_HEADER_ERROR_KEY", "key" },
            { GWY_TEXT_HEADER_ERROR_VALUE, "GWY_TEXT_HEADER_ERROR_VALUE", "value" },
            { GWY_TEXT_HEADER_ERROR_TERMINATOR, "GWY_TEXT_HEADER_ERROR_TERMINATOR", "terminator" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyTextHeaderError", values);
    }
    return etype;
}
#include "./gwymoduleutils-synth.h"
GType
gwy_synth_response_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_RESPONSE_SYNTH_TAKE_DIMS, "GWY_RESPONSE_SYNTH_TAKE_DIMS", "take-dims" },
            { GWY_RESPONSE_SYNTH_INIT_Z, "GWY_RESPONSE_SYNTH_INIT_Z", "init-z" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwySynthResponseType", values);
    }
    return etype;
}
GType
gwy_synth_dims_param_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_DIMS_PARAM_XRES, "GWY_DIMS_PARAM_XRES", "param-xres" },
            { GWY_DIMS_PARAM_YRES, "GWY_DIMS_PARAM_YRES", "param-yres" },
            { GWY_DIMS_PARAM_SQUARE_IMAGE, "GWY_DIMS_PARAM_SQUARE_IMAGE", "param-square-image" },
            { GWY_DIMS_PARAM_XREAL, "GWY_DIMS_PARAM_XREAL", "param-xreal" },
            { GWY_DIMS_PARAM_YREAL, "GWY_DIMS_PARAM_YREAL", "param-yreal" },
            { GWY_DIMS_PARAM_SQUARE_PIXELS, "GWY_DIMS_PARAM_SQUARE_PIXELS", "param-square-pixels" },
            { GWY_DIMS_PARAM_XYUNIT, "GWY_DIMS_PARAM_XYUNIT", "param-xyunit" },
            { GWY_DIMS_PARAM_ZUNIT, "GWY_DIMS_PARAM_ZUNIT", "param-zunit" },
            { GWY_DIMS_PARAM_REPLACE, "GWY_DIMS_PARAM_REPLACE", "param-replace" },
            { GWY_DIMS_PARAM_INITIALIZE, "GWY_DIMS_PARAM_INITIALIZE", "param-initialize" },
            { GWY_DIMS_BUTTON_TAKE, "GWY_DIMS_BUTTON_TAKE", "button-take" },
            { GWY_DIMS_HEADER_PIXEL, "GWY_DIMS_HEADER_PIXEL", "header-pixel" },
            { GWY_DIMS_HEADER_PHYSICAL, "GWY_DIMS_HEADER_PHYSICAL", "header-physical" },
            { GWY_DIMS_HEADER_UNITS, "GWY_DIMS_HEADER_UNITS", "header-units" },
            { GWY_DIMS_HEADER_CURRENT_IMAGE, "GWY_DIMS_HEADER_CURRENT_IMAGE", "header-current-image" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwySynthDimsParam", values);
    }
    return etype;
}
GType
gwy_synth_dims_flags_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GFlagsValue values[] = {
            { GWY_SYNTH_FIXED_XYUNIT, "GWY_SYNTH_FIXED_XYUNIT", "xyunit" },
            { GWY_SYNTH_FIXED_ZUNIT, "GWY_SYNTH_FIXED_ZUNIT", "zunit" },
            { GWY_SYNTH_FIXED_UNITS, "GWY_SYNTH_FIXED_UNITS", "units" },
            { 0, NULL, NULL }
        };
        etype = g_flags_register_static("GwySynthDimsFlags", values);
    }
    return etype;
}
GType
gwy_synth_update_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_SYNTH_UPDATE_CANCELLED, "GWY_SYNTH_UPDATE_CANCELLED", "cancelled" },
            { GWY_SYNTH_UPDATE_NOTHING, "GWY_SYNTH_UPDATE_NOTHING", "nothing" },
            { GWY_SYNTH_UPDATE_DO_PREVIEW, "GWY_SYNTH_UPDATE_DO_PREVIEW", "do-preview" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwySynthUpdateType", values);
    }
    return etype;
}
#include "./gwymoduleutils.h"
GType
gwy_preview_surface_flags_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GFlagsValue values[] = {
            { GWY_PREVIEW_SURFACE_DENSITY, "GWY_PREVIEW_SURFACE_DENSITY", "density" },
            { GWY_PREVIEW_SURFACE_FILL, "GWY_PREVIEW_SURFACE_FILL", "fill" },
            { 0, NULL, NULL }
        };
        etype = g_flags_register_static("GwyPreviewSurfaceFlags", values);
    }
    return etype;
}
#include "./gwyplaintool.h"
GType
gwy_plain_tool_changed_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GFlagsValue values[] = {
            { GWY_PLAIN_TOOL_CHANGED_DATA, "GWY_PLAIN_TOOL_CHANGED_DATA", "changed-data" },
            { GWY_PLAIN_TOOL_CHANGED_MASK, "GWY_PLAIN_TOOL_CHANGED_MASK", "changed-mask" },
            { GWY_PLAIN_TOOL_CHANGED_SHOW, "GWY_PLAIN_TOOL_CHANGED_SHOW", "changed-show" },
            { GWY_PLAIN_TOOL_CHANGED_SELECTION, "GWY_PLAIN_TOOL_CHANGED_SELECTION", "changed-selection" },
            { GWY_PLAIN_TOOL_FINISHED_SELECTION, "GWY_PLAIN_TOOL_FINISHED_SELECTION", "finished-selection" },
            { 0, NULL, NULL }
        };
        etype = g_flags_register_static("GwyPlainToolChanged", values);
    }
    return etype;
}
#include "./gwyresultsexport.h"
GType
gwy_results_export_style_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_RESULTS_EXPORT_PARAMETERS, "GWY_RESULTS_EXPORT_PARAMETERS", "parameters" },
            { GWY_RESULTS_EXPORT_TABULAR_DATA, "GWY_RESULTS_EXPORT_TABULAR_DATA", "tabular-data" },
            { GWY_RESULTS_EXPORT_FIXED_FORMAT, "GWY_RESULTS_EXPORT_FIXED_FORMAT", "fixed-format" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyResultsExportStyle", values);
    }
    return etype;
}
#include "./gwytool.h"
GType
gwy_tool_response_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_TOOL_RESPONSE_CLEAR, "GWY_TOOL_RESPONSE_CLEAR", "clear" },
            { GWY_TOOL_RESPONSE_UPDATE, "GWY_TOOL_RESPONSE_UPDATE", "update" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyToolResponseType", values);
    }
    return etype;
}
#include "./help.h"
GType
gwy_help_flags_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GFlagsValue values[] = {
            { GWY_HELP_DEFAULT, "GWY_HELP_DEFAULT", "default" },
            { GWY_HELP_NO_BUTTON, "GWY_HELP_NO_BUTTON", "no-button" },
            { 0, NULL, NULL }
        };
        etype = g_flags_register_static("GwyHelpFlags", values);
    }
    return etype;
}
#include "./logging.h"
GType
gwy_app_logging_flags_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GFlagsValue values[] = {
            { GWY_APP_LOGGING_TO_FILE, "GWY_APP_LOGGING_TO_FILE", "file" },
            { GWY_APP_LOGGING_TO_CONSOLE, "GWY_APP_LOGGING_TO_CONSOLE", "console" },
            { 0, NULL, NULL }
        };
        etype = g_flags_register_static("GwyAppLoggingFlags", values);
    }
    return etype;
}
#include "./menu.h"
GType
gwy_menu_sens_flags_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GFlagsValue values[] = {
            { GWY_MENU_FLAG_DATA, "GWY_MENU_FLAG_DATA", "data" },
            { GWY_MENU_FLAG_UNDO, "GWY_MENU_FLAG_UNDO", "undo" },
            { GWY_MENU_FLAG_REDO, "GWY_MENU_FLAG_REDO", "redo" },
            { GWY_MENU_FLAG_GRAPH, "GWY_MENU_FLAG_GRAPH", "graph" },
            { GWY_MENU_FLAG_LAST_PROC, "GWY_MENU_FLAG_LAST_PROC", "last-proc" },
            { GWY_MENU_FLAG_LAST_GRAPH, "GWY_MENU_FLAG_LAST_GRAPH", "last-graph" },
            { GWY_MENU_FLAG_DATA_MASK, "GWY_MENU_FLAG_DATA_MASK", "data-mask" },
            { GWY_MENU_FLAG_DATA_SHOW, "GWY_MENU_FLAG_DATA_SHOW", "data-show" },
            { GWY_MENU_FLAG_3D, "GWY_MENU_FLAG_3D", "3d" },
            { GWY_MENU_FLAG_FILE, "GWY_MENU_FLAG_FILE", "file" },
            { GWY_MENU_FLAG_VOLUME, "GWY_MENU_FLAG_VOLUME", "volume" },
            { GWY_MENU_FLAG_XYZ, "GWY_MENU_FLAG_XYZ", "xyz" },
            { GWY_MENU_FLAG_CURVE_MAP, "GWY_MENU_FLAG_CURVE_MAP", "curve-map" },
            { GWY_MENU_FLAG_GRAPH_CURVE, "GWY_MENU_FLAG_GRAPH_CURVE", "graph-curve" },
            { GWY_MENU_FLAG_MASK, "GWY_MENU_FLAG_MASK", "mask" },
            { 0, NULL, NULL }
        };
        etype = g_flags_register_static("GwyMenuSensFlags", values);
    }
    return etype;
}
#include "./settings.h"
GType
gwy_app_settings_error_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_APP_SETTINGS_ERROR_FILE, "GWY_APP_SETTINGS_ERROR_FILE", "file" },
            { GWY_APP_SETTINGS_ERROR_CORRUPT, "GWY_APP_SETTINGS_ERROR_CORRUPT", "corrupt" },
            { GWY_APP_SETTINGS_ERROR_CFGDIR, "GWY_APP_SETTINGS_ERROR_CFGDIR", "cfgdir" },
            { GWY_APP_SETTINGS_ERROR_EMPTY, "GWY_APP_SETTINGS_ERROR_EMPTY", "empty" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyAppSettingsError", values);
    }
    return etype;
}
#include "./validate.h"
GType
gwy_data_error_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_DATA_ERROR_KEY_FORMAT, "GWY_DATA_ERROR_KEY_FORMAT", "key-format" },
            { GWY_DATA_ERROR_KEY_CHARACTERS, "GWY_DATA_ERROR_KEY_CHARACTERS", "key-characters" },
            { GWY_DATA_ERROR_KEY_UNKNOWN, "GWY_DATA_ERROR_KEY_UNKNOWN", "key-unknown" },
            { GWY_DATA_ERROR_KEY_ID, "GWY_DATA_ERROR_KEY_ID", "key-id" },
            { GWY_DATA_ERROR_ITEM_TYPE, "GWY_DATA_ERROR_ITEM_TYPE", "item-type" },
            { GWY_DATA_ERROR_NON_UTF8_STRING, "GWY_DATA_ERROR_NON_UTF8_STRING", "non-utf8-string" },
            { GWY_DATA_ERROR_REF_COUNT, "GWY_DATA_ERROR_REF_COUNT", "ref-count" },
            { GWY_DATA_ERROR_STRAY_SECONDARY_DATA, "GWY_DATA_ERROR_STRAY_SECONDARY_DATA", "stray-secondary-data" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyDataError", values);
    }
    return etype;
}
GType
gwy_data_validate_flags_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GFlagsValue values[] = {
            { GWY_DATA_VALIDATE_UNKNOWN, "GWY_DATA_VALIDATE_UNKNOWN", "unknown" },
            { GWY_DATA_VALIDATE_REF_COUNT, "GWY_DATA_VALIDATE_REF_COUNT", "ref-count" },
            { GWY_DATA_VALIDATE_ALL, "GWY_DATA_VALIDATE_ALL", "all" },
            { GWY_DATA_VALIDATE_CORRECT, "GWY_DATA_VALIDATE_CORRECT", "correct" },
            { GWY_DATA_VALIDATE_NO_REPORT, "GWY_DATA_VALIDATE_NO_REPORT", "no-report" },
            { 0, NULL, NULL }
        };
        etype = g_flags_register_static("GwyDataValidateFlags", values);
    }
    return etype;
}

/* Generated data ends here */

