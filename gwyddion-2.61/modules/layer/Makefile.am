# $Id: Makefile.am 23051 2021-01-11 15:31:18Z yeti-dn $

layerdir = $(pkglibdir)/modules/layer

# Only bundle modules without own dependencies.  If something goes wrong with
# any required libraries at run-time, it will at most break these individual
# unbundled modules, not the bundle.
bundlable_modules = \
	axis.la \
	cross.la \
	ellipse.la \
	lattice.la \
	line.la \
	path.la \
	point.la \
	projective.la \
	rectangle.la

# layer_modules is either the bundle or all the individual modules.
if MODULE_BUNDLING
layer_modules = layer.la
BUILT_SOURCES = layer.c
layer_la_CFLAGS = $(AM_CFLAGS) -DGWY_MODULE_BUNDLING
else
layer_modules = $(bundlable_modules)
endif

layer_LTLIBRARIES = \
	$(layer_modules)

noinst_HEADERS = \
	layer.h

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
AM_CFLAGS = @COMMON_CFLAGS@
AM_LDFLAGS = -avoid-version -module $(no_undefined) $(module_libadd)

# Bundlable modules, must be listed in layer_la_SOURCES below.
axis_la_SOURCES       = axis.c
cross_la_SOURCES      = cross.c
ellipse_la_SOURCES    = ellipse.c
lattice_la_SOURCES    = lattice.c
line_la_SOURCES       = line.c
path_la_SOURCES       = path.c
point_la_SOURCES      = point.c
projective_la_SOURCES = projective.c
rectangle_la_SOURCES  = rectangle.c

layer_la_SOURCES = \
	$(axis_la_SOURCES) \
	$(cross_la_SOURCES) \
	$(ellipse_la_SOURCES) \
	$(lattice_la_SOURCES) \
	$(line_la_SOURCES) \
	$(path_la_SOURCES) \
	$(point_la_SOURCES) \
	$(projective_la_SOURCES) \
	$(rectangle_la_SOURCES) \
	layer.h \
	layer.c

layer.c: $(srcdir)/Makefile.am $(top_srcdir)/utils/gen-module-bundle.sh
	$(SHELL) $(top_srcdir)/utils/gen-module-bundle.sh layer.c $(bundlable_modules)

CLEAN_FILES = layer.c

clean-local:
	rm -f core.* *~
