/* This is a GENERATED file. */
/* This file is generated by glib-mkenums, do not modify it. This code is licensed under the same license as the containing project. Note that it links to GLib, so must comply with the LGPL linking clauses. */

#include "gwyddiontypes.h"
#include "./gwyddionenums.h"
GType
gwy_si_unit_format_style_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_SI_UNIT_FORMAT_NONE, "GWY_SI_UNIT_FORMAT_NONE", "none" },
            { GWY_SI_UNIT_FORMAT_PLAIN, "GWY_SI_UNIT_FORMAT_PLAIN", "plain" },
            { GWY_SI_UNIT_FORMAT_MARKUP, "GWY_SI_UNIT_FORMAT_MARKUP", "markup" },
            { GWY_SI_UNIT_FORMAT_VFMARKUP, "GWY_SI_UNIT_FORMAT_VFMARKUP", "vfmarkup" },
            { GWY_SI_UNIT_FORMAT_TEX, "GWY_SI_UNIT_FORMAT_TEX", "tex" },
            { GWY_SI_UNIT_FORMAT_VFTEX, "GWY_SI_UNIT_FORMAT_VFTEX", "vftex" },
            { GWY_SI_UNIT_FORMAT_UNICODE, "GWY_SI_UNIT_FORMAT_UNICODE", "unicode" },
            { GWY_SI_UNIT_FORMAT_VFUNICODE, "GWY_SI_UNIT_FORMAT_VFUNICODE", "vfunicode" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwySIUnitFormatStyle", values);
    }
    return etype;
}
GType
gwy_nl_fit_param_flags_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GFlagsValue values[] = {
            { GWY_NLFIT_PARAM_ANGLE, "GWY_NLFIT_PARAM_ANGLE", "angle" },
            { GWY_NLFIT_PARAM_ABSVAL, "GWY_NLFIT_PARAM_ABSVAL", "absval" },
            { 0, NULL, NULL }
        };
        etype = g_flags_register_static("GwyNLFitParamFlags", values);
    }
    return etype;
}
GType
gwy_percentile_interpolation_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_PERCENTILE_INTERPOLATION_LINEAR, "GWY_PERCENTILE_INTERPOLATION_LINEAR", "linear" },
            { GWY_PERCENTILE_INTERPOLATION_LOWER, "GWY_PERCENTILE_INTERPOLATION_LOWER", "lower" },
            { GWY_PERCENTILE_INTERPOLATION_HIGHER, "GWY_PERCENTILE_INTERPOLATION_HIGHER", "higher" },
            { GWY_PERCENTILE_INTERPOLATION_NEAREST, "GWY_PERCENTILE_INTERPOLATION_NEAREST", "nearest" },
            { GWY_PERCENTILE_INTERPOLATION_MIDPOINT, "GWY_PERCENTILE_INTERPOLATION_MIDPOINT", "midpoint" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyPercentileInterpolationType", values);
    }
    return etype;
}
#include "./gwyexpr.h"
GType
gwy_expr_error_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_EXPR_ERROR_CLOSING_PARENTHESIS, "GWY_EXPR_ERROR_CLOSING_PARENTHESIS", "closing-parenthesis" },
            { GWY_EXPR_ERROR_EMPTY, "GWY_EXPR_ERROR_EMPTY", "empty" },
            { GWY_EXPR_ERROR_EMPTY_PARENTHESES, "GWY_EXPR_ERROR_EMPTY_PARENTHESES", "empty-parentheses" },
            { GWY_EXPR_ERROR_GARBAGE, "GWY_EXPR_ERROR_GARBAGE", "garbage" },
            { GWY_EXPR_ERROR_INVALID_ARGUMENT, "GWY_EXPR_ERROR_INVALID_ARGUMENT", "invalid-argument" },
            { GWY_EXPR_ERROR_INVALID_TOKEN, "GWY_EXPR_ERROR_INVALID_TOKEN", "invalid-token" },
            { GWY_EXPR_ERROR_MISSING_ARGUMENT, "GWY_EXPR_ERROR_MISSING_ARGUMENT", "missing-argument" },
            { GWY_EXPR_ERROR_NOT_EXECUTABLE, "GWY_EXPR_ERROR_NOT_EXECUTABLE", "not-executable" },
            { GWY_EXPR_ERROR_OPENING_PARENTHESIS, "GWY_EXPR_ERROR_OPENING_PARENTHESIS", "opening-parenthesis" },
            { GWY_EXPR_ERROR_STRAY_COMMA, "GWY_EXPR_ERROR_STRAY_COMMA", "stray-comma" },
            { GWY_EXPR_ERROR_UNRESOLVED_IDENTIFIERS, "GWY_EXPR_ERROR_UNRESOLVED_IDENTIFIERS", "unresolved-identifiers" },
            { GWY_EXPR_ERROR_CONSTANT_NAME, "GWY_EXPR_ERROR_CONSTANT_NAME", "constant-name" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyExprError", values);
    }
    return etype;
}
#include "./gwyresults.h"
GType
gwy_results_value_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_RESULTS_VALUE_FLOAT, "GWY_RESULTS_VALUE_FLOAT", "float" },
            { GWY_RESULTS_VALUE_STRING, "GWY_RESULTS_VALUE_STRING", "string" },
            { GWY_RESULTS_VALUE_INT, "GWY_RESULTS_VALUE_INT", "int" },
            { GWY_RESULTS_VALUE_YESNO, "GWY_RESULTS_VALUE_YESNO", "yesno" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyResultsValueType", values);
    }
    return etype;
}
GType
gwy_results_report_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GFlagsValue values[] = {
            { GWY_RESULTS_REPORT_COLON, "GWY_RESULTS_REPORT_COLON", "colon" },
            { GWY_RESULTS_REPORT_TABSEP, "GWY_RESULTS_REPORT_TABSEP", "tabsep" },
            { GWY_RESULTS_REPORT_CSV, "GWY_RESULTS_REPORT_CSV", "csv" },
            { GWY_RESULTS_REPORT_MACHINE, "GWY_RESULTS_REPORT_MACHINE", "machine" },
            { 0, NULL, NULL }
        };
        etype = g_flags_register_static("GwyResultsReportType", values);
    }
    return etype;
}
#include "./gwyutils.h"
GType
gwy_raw_data_type_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_RAW_DATA_SINT8, "GWY_RAW_DATA_SINT8", "sint8" },
            { GWY_RAW_DATA_UINT8, "GWY_RAW_DATA_UINT8", "uint8" },
            { GWY_RAW_DATA_SINT16, "GWY_RAW_DATA_SINT16", "sint16" },
            { GWY_RAW_DATA_UINT16, "GWY_RAW_DATA_UINT16", "uint16" },
            { GWY_RAW_DATA_SINT32, "GWY_RAW_DATA_SINT32", "sint32" },
            { GWY_RAW_DATA_UINT32, "GWY_RAW_DATA_UINT32", "uint32" },
            { GWY_RAW_DATA_SINT64, "GWY_RAW_DATA_SINT64", "sint64" },
            { GWY_RAW_DATA_UINT64, "GWY_RAW_DATA_UINT64", "uint64" },
            { GWY_RAW_DATA_HALF, "GWY_RAW_DATA_HALF", "half" },
            { GWY_RAW_DATA_FLOAT, "GWY_RAW_DATA_FLOAT", "float" },
            { GWY_RAW_DATA_REAL, "GWY_RAW_DATA_REAL", "real" },
            { GWY_RAW_DATA_DOUBLE, "GWY_RAW_DATA_DOUBLE", "double" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyRawDataType", values);
    }
    return etype;
}
GType
gwy_byte_order_get_type(void)
{
    static GType etype = 0;

    if (etype == 0) {
        static const GEnumValue values[] = {
            { GWY_BYTE_ORDER_NATIVE, "GWY_BYTE_ORDER_NATIVE", "native" },
            { GWY_BYTE_ORDER_LITTLE_ENDIAN, "GWY_BYTE_ORDER_LITTLE_ENDIAN", "little-endian" },
            { GWY_BYTE_ORDER_BIG_ENDIAN, "GWY_BYTE_ORDER_BIG_ENDIAN", "big-endian" },
            { GWY_BYTE_ORDER_IMPLICIT, "GWY_BYTE_ORDER_IMPLICIT", "implicit" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static("GwyByteOrder", values);
    }
    return etype;
}

/* Generated data ends here */

