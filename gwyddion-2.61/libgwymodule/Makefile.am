# $Id: Makefile.am 24505 2021-11-10 16:19:25Z yeti-dn $

libgwyddion = $(top_builddir)/libgwyddion/libgwyddion2.la
libgwyprocess = $(top_builddir)/libprocess/libgwyprocess2.la
libgwydraw = $(top_builddir)/libdraw/libgwydraw2.la
libgwydgets = $(top_builddir)/libgwydgets/libgwydgets2.la

libgwymodule2includedir = $(pkgincludedir)/libgwymodule

if MODULE_DEPENDENCIES
libgwymodule_libadd = \
	$(libgwydgets) \
	$(libgwydraw) \
	$(libgwyprocess) \
	$(libgwyddion)
endif

libgwymodule2include_HEADERS = \
	gwymodule.h \
	gwymodule-cmap.h \
	gwymodule-file.h \
	gwymodule-graph.h \
	gwymodule-layer.h \
	gwymodule-process.h \
	gwymodule-tool.h \
	gwymodule-volume.h \
	gwymodule-xyz.h \
	gwymodulebrowser.h \
	gwymoduleenums.h \
	gwymoduleloader.h \
	gwymoduletypes.h

noinst_HEADERS = \
	gwymoduleinternal.h

lib_LTLIBRARIES = libgwymodule2.la

if OS_MSWIN
no_undefined = -no-undefined
export_symbols = -export-symbols libgwymodule2.def
libgwymodule2_la_DEPENDENCIES = libgwymodule2.def
endif

# Must init
EXTRA_DIST =
CLEANFILES = libgwymodule2.def
MAINTAINERCLEANFILES =
BUILT_SOURCES =

MKENUM_NAME = gwymoduletypes
MKENUM_ID = GWY_MODULE_TYPES
MKENUM_HFILES = \
	$(srcdir)/gwymoduleenums.h \
	$(srcdir)/gwymoduleloader.h \
	$(srcdir)/gwymodule-file.h
include $(top_srcdir)/utils/mkenum.mk

libgwymodule2.def: $(libgwymodule2_la_OBJECTS)
	$(AM_V_GEN)echo EXPORTS >libgwymodule2.def
	$(AM_V_at)for lo in $(libgwymodule2_la_OBJECTS); do \
		o=`sed "s/^pic_object='\\(.*\\)'/\\1/;t;d" "$$lo"`; \
		$(NM) --defined-only --demangle --no-sort "$$o" \
		| awk -F' ' '($$2=="T" && $$3 ~ /^gwy_/){print "     ",$$3}'; \
	done | sort >>libgwymodule2.def

AM_CFLAGS = @COMMON_CFLAGS@
AM_CPPFLAGS = -I$(top_srcdir) -DG_LOG_DOMAIN=\"GwyModule\"

# In following order do:
# Any code change     C:   R++: A
# Any iface change    C++: 0:   A
# Adding ifaces       C:   R:   A++
# Changing ifaces     C:   R:   0
libversion = -version-info 12:0:12
#libversion = -release @LIBRARY_RELEASE@
libgwymodule2_la_LDFLAGS = @GMODULE_LIBS@ @GTK_LIBS@ @BASIC_LIBS@  -export-dynamic $(no_undefined) $(export_symbols) $(libversion)
libgwymodule2_la_LIBADD = $(libgwymodule_libadd)

libgwymodule2_la_SOURCES = \
	gwymodule-cmap.c \
	gwymodule-file.c \
	gwymodule-graph.c \
	gwymodule-layer.c \
	gwymodule-process.c \
	gwymodule-tool.c \
	gwymodule-volume.c \
	gwymodule-xyz.c \
	gwymodulebrowser.c \
	gwymoduleloader.c \
	gwymoduletypes.c

clean-local:
	rm -f core.* *~
