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

GwyModuleInfo* _gwy_module_query__graph_align(void);
GwyModuleInfo* _gwy_module_query__graph_cd(void);
GwyModuleInfo* _gwy_module_query__graph_cut(void);
GwyModuleInfo* _gwy_module_query__graph_dos_spectrum(void);
GwyModuleInfo* _gwy_module_query__graph_export_ascii(void);
GwyModuleInfo* _gwy_module_query__graph_export_bitmap(void);
GwyModuleInfo* _gwy_module_query__graph_export_vector(void);
GwyModuleInfo* _gwy_module_query__graph_fdfit(void);
GwyModuleInfo* _gwy_module_query__graph_filter(void);
GwyModuleInfo* _gwy_module_query__graph_fit(void);
GwyModuleInfo* _gwy_module_query__graph_flip(void);
GwyModuleInfo* _gwy_module_query__graph_fztofd(void);
GwyModuleInfo* _gwy_module_query__graph_invert(void);
GwyModuleInfo* _gwy_module_query__graph_level(void);
GwyModuleInfo* _gwy_module_query__graph_logscale(void);
GwyModuleInfo* _gwy_module_query__graph_peaks(void);
GwyModuleInfo* _gwy_module_query__graph_polylevel(void);
GwyModuleInfo* _gwy_module_query__graph_sfuncs(void);
GwyModuleInfo* _gwy_module_query__graph_simplemech(void);
GwyModuleInfo* _gwy_module_query__graph_sinebg(void);
GwyModuleInfo* _gwy_module_query__graph_stats(void);
GwyModuleInfo* _gwy_module_query__graph_terraces(void);

static const GwyModuleRecord modules[] = {
  { _gwy_module_query__graph_align, "graph_align", },
  { _gwy_module_query__graph_cd, "graph_cd", },
  { _gwy_module_query__graph_cut, "graph_cut", },
  { _gwy_module_query__graph_dos_spectrum, "graph_dos_spectrum", },
  { _gwy_module_query__graph_export_ascii, "graph_export_ascii", },
  { _gwy_module_query__graph_export_bitmap, "graph_export_bitmap", },
  { _gwy_module_query__graph_export_vector, "graph_export_vector", },
  { _gwy_module_query__graph_fdfit, "graph_fdfit", },
  { _gwy_module_query__graph_filter, "graph_filter", },
  { _gwy_module_query__graph_fit, "graph_fit", },
  { _gwy_module_query__graph_flip, "graph_flip", },
  { _gwy_module_query__graph_fztofd, "graph_fztofd", },
  { _gwy_module_query__graph_invert, "graph_invert", },
  { _gwy_module_query__graph_level, "graph_level", },
  { _gwy_module_query__graph_logscale, "graph_logscale", },
  { _gwy_module_query__graph_peaks, "graph_peaks", },
  { _gwy_module_query__graph_polylevel, "graph_polylevel", },
  { _gwy_module_query__graph_sfuncs, "graph_sfuncs", },
  { _gwy_module_query__graph_simplemech, "graph_simplemech", },
  { _gwy_module_query__graph_sinebg, "graph_sinebg", },
  { _gwy_module_query__graph_stats, "graph_stats", },
  { _gwy_module_query__graph_terraces, "graph_terraces", },
  { NULL, NULL, },
};

static const GwyModuleRecord*
register_bundle(void)
{
    return modules;
}
