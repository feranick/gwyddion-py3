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

GwyModuleInfo* _gwy_module_query__volume_arithmetic(void);
GwyModuleInfo* _gwy_module_query__volume_asciiexport(void);
GwyModuleInfo* _gwy_module_query__volume_calibrate(void);
GwyModuleInfo* _gwy_module_query__volume_equiplane(void);
GwyModuleInfo* _gwy_module_query__volume_extract(void);
GwyModuleInfo* _gwy_module_query__volume_fdfit(void);
GwyModuleInfo* _gwy_module_query__volume_invert(void);
GwyModuleInfo* _gwy_module_query__volume_kmeans(void);
GwyModuleInfo* _gwy_module_query__volume_kmedians(void);
GwyModuleInfo* _gwy_module_query__volume_lawnize(void);
GwyModuleInfo* _gwy_module_query__volume_linestat(void);
GwyModuleInfo* _gwy_module_query__volume_mfmrecalc(void);
GwyModuleInfo* _gwy_module_query__volume_outliers(void);
GwyModuleInfo* _gwy_module_query__volume_planelevel(void);
GwyModuleInfo* _gwy_module_query__volume_planestat(void);
GwyModuleInfo* _gwy_module_query__volume_psf(void);
GwyModuleInfo* _gwy_module_query__volume_rephase(void);
GwyModuleInfo* _gwy_module_query__volume_slice(void);
GwyModuleInfo* _gwy_module_query__volume_strayfield(void);
GwyModuleInfo* _gwy_module_query__volume_swaxes(void);
GwyModuleInfo* _gwy_module_query__volume_zcal(void);
GwyModuleInfo* _gwy_module_query__volume_zposlevel(void);
GwyModuleInfo* _gwy_module_query__volumeops(void);

static const GwyModuleRecord modules[] = {
  { _gwy_module_query__volume_arithmetic, "volume_arithmetic", },
  { _gwy_module_query__volume_asciiexport, "volume_asciiexport", },
  { _gwy_module_query__volume_calibrate, "volume_calibrate", },
  { _gwy_module_query__volume_equiplane, "volume_equiplane", },
  { _gwy_module_query__volume_extract, "volume_extract", },
  { _gwy_module_query__volume_fdfit, "volume_fdfit", },
  { _gwy_module_query__volume_invert, "volume_invert", },
  { _gwy_module_query__volume_kmeans, "volume_kmeans", },
  { _gwy_module_query__volume_kmedians, "volume_kmedians", },
  { _gwy_module_query__volume_lawnize, "volume_lawnize", },
  { _gwy_module_query__volume_linestat, "volume_linestat", },
  { _gwy_module_query__volume_mfmrecalc, "volume_mfmrecalc", },
  { _gwy_module_query__volume_outliers, "volume_outliers", },
  { _gwy_module_query__volume_planelevel, "volume_planelevel", },
  { _gwy_module_query__volume_planestat, "volume_planestat", },
  { _gwy_module_query__volume_psf, "volume_psf", },
  { _gwy_module_query__volume_rephase, "volume_rephase", },
  { _gwy_module_query__volume_slice, "volume_slice", },
  { _gwy_module_query__volume_strayfield, "volume_strayfield", },
  { _gwy_module_query__volume_swaxes, "volume_swaxes", },
  { _gwy_module_query__volume_zcal, "volume_zcal", },
  { _gwy_module_query__volume_zposlevel, "volume_zposlevel", },
  { _gwy_module_query__volumeops, "volumeops", },
  { NULL, NULL, },
};

static const GwyModuleRecord*
register_bundle(void)
{
    return modules;
}
