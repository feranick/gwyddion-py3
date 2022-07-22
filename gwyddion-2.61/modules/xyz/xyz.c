/* This is a GENERATED file. */
#include <glib.h>
#include <libgwymodule/gwymodule.h>

static const GwyModuleRecord* register_bundle(void);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION | GWY_MODULE_BUNDLE_FLAG,
    (GwyModuleRegisterFunc)&register_bundle,
    NULL, NULL, NULL, NULL, NULL,
};

GWY_MODULE_QUERY(module_info)

GwyModuleInfo* _gwy_module_query__xyzops(void);
GwyModuleInfo* _gwy_module_query__xyz_drift(void);
GwyModuleInfo* _gwy_module_query__xyz_level(void);
GwyModuleInfo* _gwy_module_query__xyz_raster(void);
GwyModuleInfo* _gwy_module_query__xyz_split(void);

static const GwyModuleRecord modules[] = {
  { _gwy_module_query__xyzops, "xyzops", },
  { _gwy_module_query__xyz_drift, "xyz_drift", },
  { _gwy_module_query__xyz_level, "xyz_level", },
  { _gwy_module_query__xyz_raster, "xyz_raster", },
  { _gwy_module_query__xyz_split, "xyz_split", },
  { NULL, NULL, },
};

static const GwyModuleRecord*
register_bundle(void)
{
    return modules;
}
