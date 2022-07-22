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

GwyModuleInfo* _gwy_module_query__corrlen(void);
GwyModuleInfo* _gwy_module_query__cprofile(void);
GwyModuleInfo* _gwy_module_query__crop(void);
GwyModuleInfo* _gwy_module_query__distance(void);
GwyModuleInfo* _gwy_module_query__filter(void);
GwyModuleInfo* _gwy_module_query__grainmeasure(void);
GwyModuleInfo* _gwy_module_query__grainremover(void);
GwyModuleInfo* _gwy_module_query__icolorange(void);
GwyModuleInfo* _gwy_module_query__level3(void);
GwyModuleInfo* _gwy_module_query__linestats(void);
GwyModuleInfo* _gwy_module_query__maskedit(void);
GwyModuleInfo* _gwy_module_query__pathlevel(void);
GwyModuleInfo* _gwy_module_query__profile(void);
GwyModuleInfo* _gwy_module_query__readvalue(void);
GwyModuleInfo* _gwy_module_query__roughness(void);
GwyModuleInfo* _gwy_module_query__rprofile(void);
GwyModuleInfo* _gwy_module_query__selectionmanager(void);
GwyModuleInfo* _gwy_module_query__sfunctions(void);
GwyModuleInfo* _gwy_module_query__spotremove(void);
GwyModuleInfo* _gwy_module_query__spectro(void);
GwyModuleInfo* _gwy_module_query__stats(void);

static const GwyModuleRecord modules[] = {
  { _gwy_module_query__corrlen, "corrlen", },
  { _gwy_module_query__cprofile, "cprofile", },
  { _gwy_module_query__crop, "crop", },
  { _gwy_module_query__distance, "distance", },
  { _gwy_module_query__filter, "filter", },
  { _gwy_module_query__grainmeasure, "grainmeasure", },
  { _gwy_module_query__grainremover, "grainremover", },
  { _gwy_module_query__icolorange, "icolorange", },
  { _gwy_module_query__level3, "level3", },
  { _gwy_module_query__linestats, "linestats", },
  { _gwy_module_query__maskedit, "maskedit", },
  { _gwy_module_query__pathlevel, "pathlevel", },
  { _gwy_module_query__profile, "profile", },
  { _gwy_module_query__readvalue, "readvalue", },
  { _gwy_module_query__roughness, "roughness", },
  { _gwy_module_query__rprofile, "rprofile", },
  { _gwy_module_query__selectionmanager, "selectionmanager", },
  { _gwy_module_query__sfunctions, "sfunctions", },
  { _gwy_module_query__spotremove, "spotremove", },
  { _gwy_module_query__spectro, "spectro", },
  { _gwy_module_query__stats, "stats", },
  { NULL, NULL, },
};

static const GwyModuleRecord*
register_bundle(void)
{
    return modules;
}
