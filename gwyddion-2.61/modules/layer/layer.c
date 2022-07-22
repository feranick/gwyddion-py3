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

GwyModuleInfo* _gwy_module_query__axis(void);
GwyModuleInfo* _gwy_module_query__cross(void);
GwyModuleInfo* _gwy_module_query__ellipse(void);
GwyModuleInfo* _gwy_module_query__lattice(void);
GwyModuleInfo* _gwy_module_query__line(void);
GwyModuleInfo* _gwy_module_query__path(void);
GwyModuleInfo* _gwy_module_query__point(void);
GwyModuleInfo* _gwy_module_query__projective(void);
GwyModuleInfo* _gwy_module_query__rectangle(void);

static const GwyModuleRecord modules[] = {
  { _gwy_module_query__axis, "axis", },
  { _gwy_module_query__cross, "cross", },
  { _gwy_module_query__ellipse, "ellipse", },
  { _gwy_module_query__lattice, "lattice", },
  { _gwy_module_query__line, "line", },
  { _gwy_module_query__path, "path", },
  { _gwy_module_query__point, "point", },
  { _gwy_module_query__projective, "projective", },
  { _gwy_module_query__rectangle, "rectangle", },
  { NULL, NULL, },
};

static const GwyModuleRecord*
register_bundle(void)
{
    return modules;
}
