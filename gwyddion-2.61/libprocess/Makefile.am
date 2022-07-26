# $Id: Makefile.am 24802 2022-04-29 08:55:16Z yeti-dn $

libgwyddion = $(top_builddir)/libgwyddion/libgwyddion2.la

libgwyprocess2includedir = $(pkgincludedir)/libprocess

if MODULE_DEPENDENCIES
libgwyprocess_libadd = \
	$(libgwyddion)
endif

libgwyprocess2include_HEADERS = \
	arithmetic.h \
	brick.h \
	cdline.h \
	correct.h \
	correlation.h \
	cwt.h \
	datafield.h \
	dataline.h \
	dwt.h \
	elliptic.h \
	filters.h \
	fractals.h \
	grains.h \
	gwycaldata.h \
	gwycalibration.h \
	gwygrainvalue.h \
	gwyprocess.h \
	gwyprocessenums.h \
	gwyprocesstypes.h \
	gwyshapefitpreset.h \
	hough.h \
	interpolation.h \
	inttrans.h \
	lawn.h \
	level.h \
	linestats.h \
	mfm.h \
	peaks.h \
	simplefft.h \
	spectra.h \
	spline.h \
	stats.h \
	stats_uncertainty.h \
	surface.h \
	synth.h \
	tip.h \
	triangulation.h

noinst_HEADERS = \
	gwyfftw.h \
	gwyprocessinternal.h \
	morph_lib.h \
	natural.h

lib_LTLIBRARIES = libgwyprocess2.la

if OS_MSWIN
no_undefined = -no-undefined
export_symbols = -export-symbols libgwyprocess2.def
libgwyprocess2_la_DEPENDENCIES = libgwyprocess2.def
endif

# Must init
EXTRA_DIST =
CLEANFILES = libgwyprocess2.def
MAINTAINERCLEANFILES =
BUILT_SOURCES =

MKENUM_NAME = gwyprocesstypes
MKENUM_ID = GWY_PROCESS_TYPES
MKENUM_HFILES = \
	$(srcdir)/gwyprocessenums.h \
	$(srcdir)/peaks.h \
	$(srcdir)/mfm.h \
	$(srcdir)/gwygrainvalue.h
include $(top_srcdir)/utils/mkenum.mk

libgwyprocess2.def: $(libgwyprocess2_la_OBJECTS)
	$(AM_V_GEN)echo EXPORTS >libgwyprocess2.def
	$(AM_V_at)for lo in $(libgwyprocess2_la_OBJECTS); do \
		o=`sed "s/^pic_object='\\(.*\\)'/\\1/;t;d" "$$lo"`; \
		$(NM) --defined-only --demangle --no-sort "$$o" \
		| awk -F' ' '($$2=="T" && $$3 ~ /^gwy_/){print "     ",$$3}'; \
	done | sort >>libgwyprocess2.def

AM_CFLAGS = @COMMON_CFLAGS@ @FFTW3_CFLAGS@ @OPENMP_CFLAGS@
AM_CPPFLAGS = -I$(top_srcdir) -DG_LOG_DOMAIN=\"GwyProcess\" @OPENMP_CFLAGS@

# In following order do:
# Any code change     C:   R++: A
# Any iface change    C++: 0:   A
# Adding ifaces       C:   R:   A++
# Changing ifaces     C:   R:   0
libversion = -version-info 44:0:44
#libversion = -release @LIBRARY_RELEASE@
libgwyprocess2_la_LDFLAGS = @BASIC_LIBS@ @FFTW3_LIBS@ @OPENMP_CFLAGS@ -export-dynamic $(no_undefined) $(export_symbols) $(libversion)
libgwyprocess2_la_LIBADD = $(libgwyprocess_libadd)

libgwyprocess2_la_SOURCES = \
	arithmetic.c \
	brick.c \
	natural.c \
	cdline.c \
	correct.c \
	correct-laplace.c \
	correlation.c \
	cwt.c \
	datafield.c \
	dataline.c \
	dwt.c \
	elliptic.c \
	filters.c \
	filters-convdeconv.c \
	filters-minmax.c \
	fractals.c \
	grains.c \
	grains-disttrans.c \
	grains-values.c \
	grains-wshed.c \
	gwycaldata.c \
	gwycalibration.c \
	gwygrainvalue.c \
	gwyprocess.c \
	gwyprocessenums.c \
	gwyprocesstypes.c \
	gwyshapefitpreset.c \
	hough.c \
	interpolation.c \
	inttrans.c \
	lawn.c \
	level.c \
	linestats.c \
	mfm.c \
	morph_lib.c \
	peaks.c \
	simplefft.c \
	spectra.c \
	spline.c \
	stats.c \
	stats-acfpsdf.c \
	stats-entropy.c \
	stats-sum.c \
	stats_uncertainty.c \
	surface.c \
	synth.c \
	tip.c \
	triangulation.c

clean-local:
	rm -f core.* *~
