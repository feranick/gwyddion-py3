# $Id: Makefile.am 23988 2021-08-16 13:58:44Z yeti-dn $

# Keep this on one line to make it easier for gwyddion-night-build to patch
SUBDIRS = cmap file graph layer process tools volume xyz pygwy

moduledir = $(pkglibdir)/modules

if ENABLE_PLUGIN_PROXY
plugin_proxy_module = plugin-proxy.la
plugin_proxy_sources =
else
plugin_proxy_module =
plugin_proxy_sources = plugin-proxy.c
endif

module_LTLIBRARIES = \
	$(plugin_proxy_module)

EXTRA_DIST = \
	$(plugin_proxy_sources)

if OS_MSWIN
no_undefined = -no-undefined
endif

AM_CPPFLAGS = -I$(top_srcdir) -DG_LOG_DOMAIN=\"Module\"
AM_CFLAGS = @COMMON_CFLAGS@
AM_LDFLAGS = -avoid-version -module $(no_undefined)

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


if ENABLE_PLUGIN_PROXY
plugin_proxy_la_SOURCES = plugin-proxy.c
plugin_proxy_la_LIBADD = $(module_libadd)
endif

clean-local:
	rm -f core.* *~
