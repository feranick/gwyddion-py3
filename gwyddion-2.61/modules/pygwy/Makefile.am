# $Id: Makefile.am 24502 2021-11-09 15:10:45Z yeti-dn $

moduledir = $(pkglibdir)/modules
pygwyutildir = $(pkgdatadir)/pygwy
langdir = $(datadir)/gtksourceview-2.0/language-specs

PYGWY_H_FILES = \
	$(top_srcdir)/libgwyddion/gwycontainer.h \
	$(top_srcdir)/libgwyddion/gwyddionenums.h \
	$(top_srcdir)/libgwyddion/gwyddion.h \
	$(top_srcdir)/libgwyddion/gwyddiontypes.h \
	$(top_srcdir)/libgwyddion/gwyentities.h \
	$(top_srcdir)/libgwyddion/gwyenum.h \
	$(top_srcdir)/libgwyddion/gwyexpr.h \
	$(top_srcdir)/libgwyddion/gwyfdcurvepreset.h \
	$(top_srcdir)/libgwyddion/gwyinventory.h \
	$(top_srcdir)/libgwyddion/gwymacros.h \
	$(top_srcdir)/libgwyddion/gwymath.h \
	$(top_srcdir)/libgwyddion/gwymd5.h \
	$(top_srcdir)/libgwyddion/gwynlfit.h \
	$(top_srcdir)/libgwyddion/gwynlfitpreset.h \
	$(top_srcdir)/libgwyddion/gwyresource.h \
	$(top_srcdir)/libgwyddion/gwyrandgenset.h \
	$(top_srcdir)/libgwyddion/gwyserializable.h \
	$(top_srcdir)/libgwyddion/gwysiunit.h \
	$(top_srcdir)/libgwyddion/gwysivalueformat.h \
	$(top_srcdir)/libgwyddion/gwystringlist.h \
	$(top_srcdir)/libgwyddion/gwythreads.h \
	$(top_srcdir)/libgwyddion/gwyutils.h \
	$(top_srcdir)/libgwyddion/gwyversion.h \
	$(top_srcdir)/libprocess/arithmetic.h \
	$(top_srcdir)/libprocess/brick.h \
	$(top_srcdir)/libprocess/cdline.h \
	$(top_srcdir)/libprocess/correct.h \
	$(top_srcdir)/libprocess/correlation.h \
	$(top_srcdir)/libprocess/cwt.h \
	$(top_srcdir)/libprocess/datafield.h \
	$(top_srcdir)/libprocess/dataline.h \
	$(top_srcdir)/libprocess/dwt.h \
	$(top_srcdir)/libprocess/elliptic.h \
	$(top_srcdir)/libprocess/filters.h \
	$(top_srcdir)/libprocess/fractals.h \
	$(top_srcdir)/libprocess/grains.h \
	$(top_srcdir)/libprocess/gwyprocessenums.h \
	$(top_srcdir)/libprocess/gwyprocess.h \
	$(top_srcdir)/libprocess/gwyprocesstypes.h \
	$(top_srcdir)/libprocess/gwyshapefitpreset.h \
	$(top_srcdir)/libprocess/hough.h \
	$(top_srcdir)/libprocess/interpolation.h \
	$(top_srcdir)/libprocess/inttrans.h \
	$(top_srcdir)/libprocess/level.h \
	$(top_srcdir)/libprocess/linestats.h \
	$(top_srcdir)/libprocess/mfm.h \
	$(top_srcdir)/libprocess/lawn.h \
	$(top_srcdir)/libprocess/peaks.h \
	$(top_srcdir)/libprocess/simplefft.h \
	$(top_srcdir)/libprocess/spectra.h \
	$(top_srcdir)/libprocess/spline.h \
	$(top_srcdir)/libprocess/stats.h \
	$(top_srcdir)/libprocess/surface.h \
	$(top_srcdir)/libprocess/synth.h \
	$(top_srcdir)/libprocess/tip.h \
	$(top_srcdir)/libdraw/gwydraw.h \
	$(top_srcdir)/libdraw/gwydrawenums.h \
	$(top_srcdir)/libdraw/gwydrawtypes.h \
	$(top_srcdir)/libdraw/gwyglmaterial.h \
	$(top_srcdir)/libdraw/gwygradient.h \
	$(top_srcdir)/libdraw/gwypixfield.h \
	$(top_srcdir)/libdraw/gwyrgba.h \
	$(top_srcdir)/libdraw/gwyselection.h \
	$(top_srcdir)/libgwydgets/gwy3dlabel.h \
	$(top_srcdir)/libgwydgets/gwy3dsetup.h \
	$(top_srcdir)/libgwydgets/gwy3dview.h \
	$(top_srcdir)/libgwydgets/gwy3dwindow.h \
	$(top_srcdir)/libgwydgets/gwyaxis.h \
	$(top_srcdir)/libgwydgets/gwycheckboxes.h \
	$(top_srcdir)/libgwydgets/gwycoloraxis.h \
	$(top_srcdir)/libgwydgets/gwycolorbutton.h \
	$(top_srcdir)/libgwydgets/gwycombobox.h \
	$(top_srcdir)/libgwydgets/gwycurve.h \
	$(top_srcdir)/libgwydgets/gwydataview.h \
	$(top_srcdir)/libgwydgets/gwydataviewlayer.h \
	$(top_srcdir)/libgwydgets/gwydatawindow.h \
	$(top_srcdir)/libgwydgets/gwydgetenums.h \
	$(top_srcdir)/libgwydgets/gwydgetmarshals.h \
	$(top_srcdir)/libgwydgets/gwydgets.h \
	$(top_srcdir)/libgwydgets/gwydgettypes.h \
	$(top_srcdir)/libgwydgets/gwydgetutils.h \
	$(top_srcdir)/libgwydgets/gwygrapharea.h \
	$(top_srcdir)/libgwydgets/gwygraphbasics.h \
	$(top_srcdir)/libgwydgets/gwygraphcorner.h \
	$(top_srcdir)/libgwydgets/gwygraphcurvemodel.h \
	$(top_srcdir)/libgwydgets/gwygraphcurves.h \
	$(top_srcdir)/libgwydgets/gwygraphdata.h \
	$(top_srcdir)/libgwydgets/gwygraph.h \
	$(top_srcdir)/libgwydgets/gwygraphlabel.h \
	$(top_srcdir)/libgwydgets/gwygraphmodel.h \
	$(top_srcdir)/libgwydgets/gwygraphselections.h \
	$(top_srcdir)/libgwydgets/gwygraphwindow.h \
	$(top_srcdir)/libgwydgets/gwyhmarkerbox.h \
	$(top_srcdir)/libgwydgets/gwyhruler.h \
	$(top_srcdir)/libgwydgets/gwyinventorystore.h \
	$(top_srcdir)/libgwydgets/gwylayer-basic.h \
	$(top_srcdir)/libgwydgets/gwylayer-mask.h \
	$(top_srcdir)/libgwydgets/gwymarkerbox.h \
	$(top_srcdir)/libgwydgets/gwynullstore.h \
	$(top_srcdir)/libgwydgets/gwyoptionmenus.h \
	$(top_srcdir)/libgwydgets/gwypixmaplayer.h \
	$(top_srcdir)/libgwydgets/gwyradiobuttons.h \
	$(top_srcdir)/libgwydgets/gwyruler.h \
	$(top_srcdir)/libgwydgets/gwyscitext.h \
	$(top_srcdir)/libgwydgets/gwyselectiongraph1darea.h \
	$(top_srcdir)/libgwydgets/gwyselectiongrapharea.h \
	$(top_srcdir)/libgwydgets/gwyselectiongraphline.h \
	$(top_srcdir)/libgwydgets/gwyselectiongraphpoint.h \
	$(top_srcdir)/libgwydgets/gwyselectiongraphzoom.h \
	$(top_srcdir)/libgwydgets/gwysensitivitygroup.h \
	$(top_srcdir)/libgwydgets/gwyshader.h \
	$(top_srcdir)/libgwydgets/gwystatusbar.h \
	$(top_srcdir)/libgwydgets/gwystock.h \
	$(top_srcdir)/libgwydgets/gwyvectorlayer.h \
	$(top_srcdir)/libgwydgets/gwyvruler.h \
	$(top_srcdir)/libgwymodule/gwymodulebrowser.h \
	$(top_srcdir)/libgwymodule/gwymoduleenums.h \
	$(top_srcdir)/libgwymodule/gwymodule-file.h \
	$(top_srcdir)/libgwymodule/gwymodule-graph.h \
	$(top_srcdir)/libgwymodule/gwymodule.h \
	$(top_srcdir)/libgwymodule/gwymodule-layer.h \
	$(top_srcdir)/libgwymodule/gwymoduleloader.h \
	$(top_srcdir)/libgwymodule/gwymodule-process.h \
	$(top_srcdir)/libgwymodule/gwymodule-tool.h \
	$(top_srcdir)/libgwymodule/gwymoduletypes.h \
	$(top_srcdir)/libgwymodule/gwymodule-volume.h \
	$(top_srcdir)/libgwymodule/gwymodule-xyz.h \
	$(top_srcdir)/libgwymodule/gwymodule-cmap.h \
	$(top_srcdir)/app/app.h \
	$(top_srcdir)/app/data-browser.h \
	$(top_srcdir)/app/datachooser.h \
	$(top_srcdir)/app/file.h \
	$(top_srcdir)/app/filelist.h \
	$(top_srcdir)/app/funcuse.h \
	$(top_srcdir)/app/glmaterial-editor.h \
	$(top_srcdir)/app/gradient-editor.h \
	$(top_srcdir)/app/gwyapp.h \
	$(top_srcdir)/app/gwyapptypes.h \
	$(top_srcdir)/app/gwymoduleutils-file.h \
	$(top_srcdir)/app/gwymoduleutils.h \
	$(top_srcdir)/app/gwyplaintool.h \
	$(top_srcdir)/app/help.h \
	$(top_srcdir)/app/log.h \
	$(top_srcdir)/app/logging.h \
	$(top_srcdir)/app/menu.h \
	$(top_srcdir)/app/meta.h \
	$(top_srcdir)/app/resource-editor.h \
	$(top_srcdir)/app/settings.h \
	$(top_srcdir)/app/undo.h \
	$(top_srcdir)/app/validate.h \
	$(top_srcdir)/app/wait.h \
	wrap_calls.h \
	macro-prototypes.h

disabled_sources =

# Configure should ensure at most one of the following is enabled.
if HAVE_LIBZIP
zip_libs = @LIBZIP_LIBS@
endif

if HAVE_MINIZIP1
zip_libs = @MINIZIP1_LIBS@
endif

if HAVE_MINIZIP2
zip_libs = @MINIZIP2_LIBS@
endif

if ENABLE_PYGWY
pygwy_module = pygwy.la
gwy_pymodule = gwy.la
pygwy_sources =
gwy_sources =

pygwyutil_DATA = gwyutils.py
lang_DATA = pygwy.lang

else
pygwyutil_DATA =
lang_DATA =
pygwy_module =
gwy_pymodule =
disabled_sources += pygwy.c gwy.c pygwy-console.c wrap_calls.c
endif

module_LTLIBRARIES = \
	$(pygwy_module)

pyexec_LTLIBRARIES = \
	$(gwy_pymodule)

noinst_HEADERS = \
	pygwy-console.h \
	pygwy.h \
	macro-prototypes.h \
	wrap_calls.h

EXTRA_DIST = \
	$(disabled_sources) \
	$(pygwyutil_DATA) \
	extra.defs \
	makelang.py \
	README.pygwy \
	pygwy.override \
	override.defs \
	pygwy-codegen.py \
	pygwy-generate-doc.py \
	pygwy-fix-defs-1.py \
	pygwy-fix-defs-2.py \
	gwyutils.py \
	pygtk-embed/include/pygtk-2.0 \
	pygtk-embed/include/pygtk-2.0/pygtk/pygtk.h \
	pygtk-embed/include/pygtk-2.0/pygobject.h \
	pygtk-embed/codegen/argtypes.py \
	pygtk-embed/codegen/codegen.py \
	pygtk-embed/codegen/definitions.py \
	pygtk-embed/codegen/defsparser.py \
	pygtk-embed/codegen/h2def.py \
	pygtk-embed/codegen/__init__.py \
	pygtk-embed/codegen/override.py \
	pygtk-embed/codegen/reversewrapper.py \
	pygtk-embed/codegen/scmexpr.py

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


if ENABLE_PYGWY
BUILT_SOURCES = pygwywrap.c
pygwy_la_SOURCES = pygwy.c wrap_calls.c pygwy-console.c
pygwy_la_CFLAGS = @COMMON_CFLAGS@ @PYTHON_EXTRA_CFLAGS@ @GTKSOURCEVIEW_CFLAGS@ $(PYTHON_INCLUDES) $(PYGTK_CFLAGS)
pygwy_la_LIBADD = $(module_libadd) $(PYTHON_LDFLAGS) $(PYGTK_LIBS) @GTKSOURCEVIEW_LIBS@

# Do not use pipes between Python scripts, seems broken with Fedora's Wine 3.20
pygwy.defs: $(PYGWY_H_FILES) Makefile.am pygwy-fix-defs-1.py pygwy-fix-defs-2.py extra.defs
	$(AM_V_GEN)$(PYTHON) $(PYGTK_CODEGENDIR)/h2def.py $(PYGWY_H_FILES) >pygwy.defs.tmp2
	$(PYTHON) $(srcdir)/pygwy-fix-defs-1.py pygwy.defs.tmp2 >pygwy.defs.tmp
	$(PYTHON) $(srcdir)/pygwy-fix-defs-2.py --codegendir=$(PYGTK_CODEGENDIR) >pygwy.defs
	rm pygwy.defs.tmp pygwy.defs.tmp2

pygwy.lang: pygwy.defs override.defs
	$(AM_V_GEN)cat pygwy.defs override.defs | $(PYTHON) $(srcdir)/makelang.py >pygwy.lang

# The sed fixes pygwy_add_constants() and pygwy_register_classes() to static
# functions, and standardized `generated' line for automated exclusions of
# generated files.
# Keep the `GENERATED' string quoted to prevent match here
pygwywrap.c: $(srcdir)/pygwy-codegen.py pygwy.defs $(srcdir)/pygwy.override wrap_calls.c
	PYGTK_CODEGENDIR=$(PYGTK_CODEGENDIR) $(PYTHON) $(srcdir)/pygwy-codegen.py \
	  --override $(srcdir)/pygwy.override --prefix pygwy pygwy.defs \
	  | sed -e 's/^void$$/static void/' -e 's/pygwy\.c/pygwywrap\.c/' \
	        -e '1s#.*#/* This is a 'GENERATED' file */#' >pygwywrap.c

gwy.py: pygwy.defs override.defs pygwywrap.c pygwy-generate-doc.py
	$(PYTHON) ./pygwy-generate-doc.py --codegendir $(PYGTK_CODEGENDIR) >gwy.py

pygwy-doc: gwy.py gwyutils.py
	$(EPYDOC) -v -o pygwy-doc gwy.py gwyutils.py

gwy_la_SOURCES = gwy.c wrap_calls.c
gwy_la_CFLAGS = @COMMON_CFLAGS@ @PYTHON_EXTRA_CFLAGS@ $(PYTHON_INCLUDES) $(PYGTK_CFLAGS)
# Link with all libraries modules are linked with.  This is excessive but done
# to prevent some nested dlopening ‘can't find DLL’ problems in Win32.
gwy_la_LIBADD = \
	$(top_builddir)/app/libgwyapp2.la \
	$(top_builddir)/libdraw/libgwydraw2.la \
	$(top_builddir)/libgwyddion/libgwyddion2.la \
	$(top_builddir)/libgwydgets/libgwydgets2.la \
	$(top_builddir)/libgwymodule/libgwymodule2.la \
	$(top_builddir)/libprocess/libgwyprocess2.la \
	$(PYGTK_LIBS) @GTK_LIBS@ \
	@PNG_LIBS@ @ZLIB@ @BZIP2@ @EXR_LIBS@ @XML2_LIBS@ $(zip_libs) \
	@CFITSIO_LIBS@ @WEBP_LIBS@ @HDF5_LDFLAGS@ @HDF5_LIBS@

gwy_la_LDFLAGS = -avoid-version -module $(no_undefined)
if MODULE_DEPENDENCIES
gwy_la_LDFLAGS += $(PYTHON_LDFLAGS)
endif
if OS_DARWIN
gwy_la_LDFLAGS += -undefined dynamic_lookup
endif

if OS_MSWIN
install-exec-hook:
	cp "$(DESTDIR)$(pyexecdir)/gwy.dll" "$(DESTDIR)$(bindir)/gwy.pyd"

uninstall-hook:
	rm -f "$(DESTDIR)$(bindir)/gwy.pyd"
endif
endif

CLEANFILES = gwy.py gwy.pyc gwy.pyo pygwy.lang boxed.override

DISTCLEANFILES = pygwy.defs pygwywrap.c

clean-local:
	rm -f core.* *~
	rm -rf pygwy-doc/
