# $Id: Makefile.am 22564 2019-10-10 15:48:53Z yeti-dn $

# Put sources that are for some reason disabled yet present in svn checkouts
# here.  This makes them distributed and translations of their messages are
# kept.  If we remove them completely, various bits can get lost.
disabled_sources =

xyzdir = $(pkglibdir)/modules/xyz

# Only bundle modules without own dependencies.  If something goes wrong with
# any required libraries at run-time, it will at most break these individual
# unbundled modules, not the bundle.
bundlable_modules = \
	xyzops.la \
	xyz_drift.la \
	xyz_level.la \
	xyz_raster.la \
	xyz_split.la

# xyz_modules is either the bundle or all the individual modules.
if MODULE_BUNDLING
xyz_modules = xyz.la
BUILT_SOURCES = xyz.c
xyz_la_CFLAGS = $(AM_CFLAGS) -DGWY_MODULE_BUNDLING
else
xyz_modules = $(bundlable_modules)
endif

xyz_LTLIBRARIES = \
	$(xyz_modules)

EXTRA_DIST = \
	$(disabled_sources)

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

# Bundlable modules, must be listed in xyz_la_SOURCES below.
xyzops_la_SOURCES     = xyzops.c
xyz_drift_la_SOURCES  = xyz_drift.c
xyz_level_la_SOURCES  = xyz_level.c
xyz_raster_la_SOURCES = xyz_raster.c
xyz_split_la_SOURCES  = xyz_split.c 

xyz_la_SOURCES = \
	$(xyzops_la_SOURCES) \
	$(xyz_drift_la_SOURCES) \
	$(xyz_level_la_SOURCES) \
	$(xyz_raster_la_SOURCES) \
	$(xyz_split_la_SOURCES) \
	xyz.c

xyz.c: $(srcdir)/Makefile.am $(top_srcdir)/utils/gen-module-bundle.sh
	$(SHELL) $(top_srcdir)/utils/gen-module-bundle.sh xyz.c $(bundlable_modules)

CLEAN_FILES = xyz.c

clean-local:
	rm -f core.* *~

