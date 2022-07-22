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

GwyModuleInfo* _gwy_module_query__cmap_align(void);
GwyModuleInfo* _gwy_module_query__cmap_basicops(void);
GwyModuleInfo* _gwy_module_query__cmap_crop(void);
GwyModuleInfo* _gwy_module_query__cmap_cutter(void);
GwyModuleInfo* _gwy_module_query__cmap_extractcurve(void);
GwyModuleInfo* _gwy_module_query__cmap_fdfit(void);
GwyModuleInfo* _gwy_module_query__cmap_fztofd(void);
GwyModuleInfo* _gwy_module_query__cmap_linestat(void);
GwyModuleInfo* _gwy_module_query__cmap_polylevel(void);
GwyModuleInfo* _gwy_module_query__cmap_simplemech(void);
GwyModuleInfo* _gwy_module_query__cmap_sinebg(void);

static const GwyModuleRecord modules[] = {
  { _gwy_module_query__cmap_align, "cmap_align", },
  { _gwy_module_query__cmap_basicops, "cmap_basicops", },
  { _gwy_module_query__cmap_crop, "cmap_crop", },
  { _gwy_module_query__cmap_cutter, "cmap_cutter", },
  { _gwy_module_query__cmap_extractcurve, "cmap_extractcurve", },
  { _gwy_module_query__cmap_fdfit, "cmap_fdfit", },
  { _gwy_module_query__cmap_fztofd, "cmap_fztofd", },
  { _gwy_module_query__cmap_linestat, "cmap_linestat", },
  { _gwy_module_query__cmap_polylevel, "cmap_polylevel", },
  { _gwy_module_query__cmap_simplemech, "cmap_simplemech", },
  { _gwy_module_query__cmap_sinebg, "cmap_sinebg", },
  { NULL, NULL, },
};

static const GwyModuleRecord*
register_bundle(void)
{
    return modules;
}
