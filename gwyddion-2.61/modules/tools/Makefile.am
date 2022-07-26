# $Id: Makefile.am 22785 2020-04-29 22:03:59Z yeti-dn $

toolsdir = $(pkglibdir)/modules/tool

# Only bundle modules without own dependencies.  If something goes wrong with
# any required libraries at run-time, it will at most break these individual
# unbundled modules, not the bundle.
bundlable_modules = \
	corrlen.la \
	cprofile.la \
	crop.la \
	distance.la \
	filter.la \
	grainmeasure.la \
	grainremover.la \
	icolorange.la \
	level3.la \
	linestats.la \
	maskedit.la \
	pathlevel.la \
	profile.la \
	readvalue.la \
	roughness.la \
	rprofile.la \
	selectionmanager.la \
	sfunctions.la \
	spotremove.la \
	spectro.la \
	stats.la

# tools_modules is either the bundle or all the individual modules.
if MODULE_BUNDLING
tools_modules = tools.la
BUILT_SOURCES = tools.c
tools_la_CFLAGS = $(AM_CFLAGS) -DGWY_MODULE_BUNDLING
else
tools_modules = $(bundlable_modules)
endif

tools_LTLIBRARIES = \
	$(tools_modules)

if OS_MSWIN
no_undefined = -no-undefined
endif

if MODULE_DEPENDENCIES
module_libadd = \
	$(top_builddir)/app/libgwyapp2.la \
	$(top_builddir)/libdraw/libgwydraw2.la \
	$(top_builddir)/libgwyddion/libgwyddion2.la \
	$(top_builddir)/libgwydgets/libgwydgets2.la \
	$(top_builddir)/libgwymodule/libgwymodule2.la \
	$(top_builddir)/libprocess/libgwyprocess2.la \
	@GTK_LIBS@
endif

AM_CPPFLAGS = -I$(top_srcdir) -DG_LOG_DOMAIN=\"Module\"
AM_CFLAGS = @COMMON_CFLAGS@ @OPENMP_CFLAGS@
AM_LDFLAGS = -avoid-version -module $(no_undefined) $(module_libadd) @OPENMP_CFLAGS@

# Bundlable modules, must be listed in tools_la_SOURCES below.
corrlen_la_SOURCES          = corrlen.c
cprofile_la_SOURCES         = cprofile.c
crop_la_SOURCES             = crop.c
distance_la_SOURCES         = distance.c
filter_la_SOURCES           = filter.c
grainmeasure_la_SOURCES     = grainmeasure.c
grainremover_la_SOURCES     = grainremover.c
icolorange_la_SOURCES       = icolorange.c
level3_la_SOURCES           = level3.c
linestats_la_SOURCES        = linestats.c
maskedit_la_SOURCES         = maskedit.c
pathlevel_la_SOURCES        = pathlevel.c
profile_la_SOURCES          = profile.c
readvalue_la_SOURCES        = readvalue.c
roughness_la_SOURCES        = roughness.c
rprofile_la_SOURCES         = rprofile.c
stats_la_SOURCES            = stats.c
selectionmanager_la_SOURCES = selectionmanager.c
sfunctions_la_SOURCES       = sfunctions.c
spectro_la_SOURCES          = spectro.c
spotremove_la_SOURCES       = spotremove.c

tools_la_SOURCES = \
	$(corrlen_la_SOURCES) \
	$(cprofile_la_SOURCES) \
	$(crop_la_SOURCES) \
	$(distance_la_SOURCES) \
	$(filter_la_SOURCES) \
	$(grainmeasure_la_SOURCES) \
	$(grainremover_la_SOURCES) \
	$(icolorange_la_SOURCES) \
	$(level3_la_SOURCES) \
	$(linestats_la_SOURCES) \
	$(maskedit_la_SOURCES) \
	$(pathlevel_la_SOURCES) \
	$(profile_la_SOURCES) \
	$(readvalue_la_SOURCES) \
	$(roughness_la_SOURCES) \
	$(rprofile_la_SOURCES) \
	$(stats_la_SOURCES) \
	$(selectionmanager_la_SOURCES) \
	$(sfunctions_la_SOURCES) \
	$(spectro_la_SOURCES) \
	$(spotremove_la_SOURCES) \
	tools.c

tools.c: $(srcdir)/Makefile.am $(top_srcdir)/utils/gen-module-bundle.sh
	$(SHELL) $(top_srcdir)/utils/gen-module-bundle.sh tools.c $(bundlable_modules)

CLEAN_FILES = tools.c

clean-local:
	rm -f core.* *~
