# $Id: Makefile.am 22136 2019-06-03 08:29:36Z yeti-dn $

# Some combinations of the tools do not give these us automatically.
abs_top_srcdir = @abs_top_srcdir@
abs_srcdir = @abs_srcdir@

SUBDIRS = \
	glmaterials \
	gradients

pkgconfigdatadir = $(libdir)/pkgconfig

if HOME_INSTALLATION
gwyddionappdir = $(XDG_DATA_HOME_DIR)/applications
gwyddionmimedir = $(XDG_DATA_HOME_DIR)/mime/packages
thumbnailerdir = $(XDG_DATA_HOME_DIR)/thumbnailers
# No place to put appdata files in homedir installation.  Probably.  So keep
# metainfofiles empty.
appdatadir = $(XDG_DATA_HOME_DIR)/metainfo
metainfofiles =
else
gwyddionappdir = $(datadir)/applications
gwyddionmimedir = $(datadir)/mime/packages
thumbnailerdir = $(datadir)/thumbnailers
appdatadir = $(datadir)/metainfo
metainfofiles = net.gwyddion.Gwyddion.appdata.xml
endif

docsrc = $(top_builddir)/devel-docs
vim_syn_gen = $(abs_top_srcdir)/utils/vim-syn-gen.py
gwyddion_vim_cfg = $(abs_srcdir)/gwyddion-cfg.py

pkgconfigdata_DATA = gwyddion.pc
gwyddionapp_DATA = gwyddion.desktop
gwyddionmime_DATA = gwyddion.xml
thumbnailer_DATA = gwyddion.thumbnailer
appdata_DATA = $(metainfofiles)
man1_MANS = gwyddion.1

EXTRA_DIST = \
	gwyddion.desktop.in \
	gwyddion.nsit.in \
	gwyddion.pc.in \
	gwyddion.spec.in \
	gwyddion.thumbnailer.in \
	gwyddion-cfg.py \
	gwyddion.vim \
	gwyddion.1 \
	mingw-gwyddion-libs.spec.in \
	net.gwyddion.Gwyddion.appdata.xml \
	$(gwyddionmime_DATA)

# gwyddion.vim generation
gwyddion_vim_sources = \
	$(docsrc)/libgwyapp/libgwyapp-decl.txt \
	$(docsrc)/libgwyddion/libgwyddion-decl.txt \
	$(docsrc)/libgwydgets/libgwydgets-decl.txt \
	$(docsrc)/libgwydraw/libgwydraw-decl.txt \
	$(docsrc)/libgwymodule/libgwymodule-decl.txt \
	$(docsrc)/libgwyprocess/libgwyprocess-decl.txt

file_magic_sources = \
	$(top_srcdir)/modules/file/*.c \
	$(top_srcdir)/modules/file/*.cc

CLEANFILES = \
	gwyddion.thumbnailer

MAINTAINERCLEANFILES = \
	gwyddion.vim \
	gwyddion.xml

# FIXME: this breaks VPATH build
# Maintainer mode and VPATH are exclusive!
if MAINTAINER_MODE
gwyddion.vim: $(gwyddion_vim_sources) $(gwyddion_vim_cfg) $(vim_syn_gen)
	$(AM_V_GEN)cd $(top_builddir); \
	$(PYTHON) $(vim_syn_gen) $(gwyddion_vim_cfg) >data/gwyddion.vim

# Freedesktop MIME associations
gwyddion.xml: $(top_srcdir)/utils/extract-file-magic.py $(file_magic_sources)
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/utils/extract-file-magic.py \
	  FREEDESKTOP $(file_magic_sources) >gwyddion.xml

# File format table for user guide
file-format-table.xml: $(top_srcdir)/utils/extract-file-magic.py $(file_magic_sources)
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/utils/extract-file-magic.py \
	  USERGUIDE $(file_magic_sources) >file-format-table.xml

# File format table for file(1) magic
magic: $(top_srcdir)/utils/extract-file-magic.py $(file_magic_sources)
	$(AM_V_GEN)$(PYTHON) $(top_srcdir)/utils/extract-file-magic.py \
	  FILEMAGIC $(file_magic_sources) >magic
endif

# Generate gwyddion.desktop from gwyddion.xml to keep the file format list in
# sync.
# NB: We must use sed, not autoconf @-replacements, because that would leave
# unexpanded ${prefix} in the desktop file.
# Depend on Makefile, this may run this rule perhaps too often but ensures
# the paths are updated every time they should.
gwyddion.desktop: gwyddion.desktop.in $(srcdir)/gwyddion.xml Makefile
	$(AM_V_GEN)x=`$(SED) -e '/^<mime-type/ s/.*<mime-type type="\(.*\)">.*/\1;/' -e t -e d <$(srcdir)/gwyddion.xml | tr -d '\n'` \
	&& $(SED) -e "s#@MIME_TYPE_LIST@#$$x#" \
	  -e 's#[@]datadir[@]#$(datadir)#' \
	  <gwyddion.desktop.in >gwyddion.desktop

# Generate gwyddion.thumbnailer from gwyddion.xml to keep the file format list
# in sync.
gwyddion.thumbnailer: gwyddion.thumbnailer.in $(srcdir)/gwyddion.xml
	$(AM_V_GEN)x=`$(SED) -e '/^<mime-type/ s/.*<mime-type type="\(.*\)">.*/\1;/' -e t -e d <$(srcdir)/gwyddion.xml | tr -d '\n'` \
	&& $(SED) \
	     -e "s#@MIME_TYPE_LIST@#$$x#" \
	     -e "s#[@]EXEEXT[@]#$(EXEEXT)#" \
	     -e "s#[@]bindir[@]#$(bindir)#" \
	     <gwyddion.thumbnailer.in >gwyddion.thumbnailer

# Desktop file installation
# FIXME: Maybe not according to current standards
install-data-hook:
if DESKTOP_FILE_UPDATE
	if test -z "$(DESTDIR)"; then \
		test -d $(gwyddionappdir) || $(MKDIR_P) $(gwyddionappdir); \
		$(UPDATE_DESKTOP_DATABASE) $(gwyddionappdir); \
		test -d $(gwyddionmimedir) || $(MKDIR_P) $(gwyddionmimedir); \
		$(UPDATE_MIME_DATABASE) `dirname $(gwyddionmimedir)`; \
	fi
endif

uninstall-hook:
if DESKTOP_FILE_UPDATE
	if test -z "$(DESTDIR)"; then \
		$(UPDATE_DESKTOP_DATABASE) $(gwyddionappdir); \
		$(UPDATE_MIME_DATABASE) `dirname $(gwyddionmimedir)`; \
	fi
endif
